#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSignalSpy>
#include <QSslError>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>

#include <chrono>

namespace {
SessionContext session()
{
    return SessionContext(QStringLiteral("测试操作员"),
                          QStringLiteral("access-secret"),
                          QDateTime::currentDateTime().addSecs(3600),
                          QStringLiteral("broker.test"),
                          8883,
                          true,
                          QStringLiteral("mqtt-user"),
                          QStringLiteral("mqtt-secret"),
                          QStringLiteral("plate/events"));
}

QByteArray csvPrefix()
{
    return QByteArray::fromHex("EFBBBF")
           + QStringLiteral("记录ID,设备,车牌号,状态,错误码,错误信息,拍摄时间,开始时间,完成时间,耗时(ms)\r\n")
                 .toUtf8();
}

QByteArray csvBody()
{
    return csvPrefix()
           + QStringLiteral("rec-1,device-1,京A12345,SUCCEEDED,,,2026-08-24T09:00:00.001+08:00,2026-08-24T09:00:01.002+08:00,2026-08-24T09:00:02.003+08:00,1001\r\n")
                 .toUtf8();
}

QByteArray failureEnvelope(const QString &code)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), false},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"), QStringLiteral("导出失败")},
                             {QStringLiteral("requestId"), QStringLiteral("csv-error")},
                             {QStringLiteral("data"), QJsonValue::Null},
                         })
        .toJson(QJsonDocument::Compact);
}

MockHttpResponse csvResponse(QByteArray contentType,
                             QList<QByteArray> chunks = {csvBody()})
{
    MockHttpResponse response;
    if (!contentType.isNull()) {
        response.headers.append({QByteArrayLiteral("Content-Type"),
                                 std::move(contentType)});
    }
    response.chunks = std::move(chunks);
    response.chunkDelayMs = 5;
    return response;
}

MockHttpResponse errorResponse(int status, const QString &code)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = QByteArrayLiteral("Error");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(failureEnvelope(code));
    return response;
}

RecognitionApiError errorFrom(const QSignalSpy &spy)
{
    return *static_cast<const RecognitionApiError *>(spy.first().first().constData());
}

QDateTime time(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

class TlsErrorReply final : public QNetworkReply
{
public:
    explicit TlsErrorReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit sslErrors({QSslError(QSslError::SelfSignedCertificate)});
        });
    }

    void abort() override
    {
        if (finished_) {
            return;
        }
        finished_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *, qint64) override { return -1; }

private:
    bool finished_ = false;
};

class TlsErrorNetworkManager final : public QNetworkAccessManager
{
protected:
    QNetworkReply *createRequest(Operation,
                                 const QNetworkRequest &request,
                                 QIODevice *) override
    {
        return new TlsErrorReply(request, this);
    }
};
} // namespace

class RecognitionApiCsvTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsFixedStreamingRequestAndForwardsExactBytes();
    void acceptsOnlyCanonicalContentType();
    void rejectsInvalidBomHeaderAndLineEnding();
    void mapsStrictErrorsAndInvalidatesUnauthorizedSession();
    void mapsTransportFailuresAndNeverRetries();
    void firstChunkCallbackCanCancelOrDeleteHandle();
};

void RecognitionApiCsvTest::initTestCase()
{
    qRegisterMetaType<RecognitionApiError>();
    using namespace std::chrono_literals;
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Csv), 120s);
}

void RecognitionApiCsvTest::sendsFixedStreamingRequestAndForwardsExactBytes()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    const QByteArray body = csvBody() + QByteArray(1024 * 1024, 'x');
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {body.left(2), body.mid(2, 137), body.mid(139)}));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(QStringLiteral("/gateway/")),
                             &transport,
                             &sessions);

    auto *handle = api.exportCsv(time(QStringLiteral("2026-08-23T16:00:00.007Z")),
                                 time(QStringLiteral("2026-08-24T16:00:00.009Z")),
                                 QStringLiteral("  设备 A/1 +%  "),
                                 sessions.currentGeneration(),
                                 41);
    QByteArray received;
    connect(handle, &RecognitionCsvRequestHandle::dataChunkReceived,
            this, [&received](const QByteArray &chunk) { received.append(chunk); });
    QSignalSpy success(handle, &RecognitionCsvRequestHandle::succeeded);
    QSignalSpy failure(handle, &RecognitionCsvRequestHandle::failed);
    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QCOMPARE(received, body);
    QCOMPARE(handle->context().operation(), QStringLiteral("recognition.export"));
    QCOMPARE(handle->context().requestGeneration(), quint64(41));
    QCOMPARE(handle->context().filterSnapshot().size(), 3);

    QTRY_COMPARE(server.requests().size(), 1);
    const MockHttpRequest &request = server.requests().constFirst();
    QCOMPARE(request.method, QByteArrayLiteral("GET"));
    QCOMPARE(request.header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));
    QCOMPARE(request.header(QByteArrayLiteral("Accept")), QByteArrayLiteral("text/csv"));
    const QUrl url(QStringLiteral("http://mock.test") + QString::fromLatin1(request.target));
    QCOMPARE(url.path(), QStringLiteral("/gateway/api/v1/recognitions/export"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("startTime")),
             QStringLiteral("2026-08-24T00:00:00.007+08:00"));
    QCOMPARE(query.queryItemValue(QStringLiteral("endTime")),
             QStringLiteral("2026-08-25T00:00:00.009+08:00"));
    QCOMPARE(query.queryItemValue(QStringLiteral("deviceId"), QUrl::FullyDecoded),
             QStringLiteral("设备 A/1 +%"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("page")));
    QVERIFY(!query.hasQueryItem(QStringLiteral("pageSize")));
}

void RecognitionApiCsvTest::acceptsOnlyCanonicalContentType()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    const QList<QByteArray> values {
        QByteArrayLiteral("TEXT/CSV; CHARSET=UTF-8"),
        QByteArrayLiteral(" text/csv ; charset = utf-8 "),
        QByteArrayLiteral("text/csv"),
        QByteArrayLiteral("text/csv; charset=utf-8; version=1"),
        QByteArrayLiteral("text/csv; charset=utf-8; charset=utf-8"),
        QByteArrayLiteral("text/csv; charset=latin1"),
        QByteArrayLiteral("text/csv; boundary=x"),
    };
    for (const QByteArray &value : values) {
        server.enqueueResponse(csvResponse(value));
    }
    MockHttpResponse duplicate = csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"));
    duplicate.headers.append({QByteArrayLiteral("Content-Type"),
                              QByteArrayLiteral("text/csv; charset=utf-8")});
    server.enqueueResponse(std::move(duplicate));

    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));

    for (int index = 0; index < values.size() + 1; ++index) {
        auto *handle = api.exportCsv(start, start.addDays(1), {},
                                     sessions.currentGeneration(), index + 1);
        QSignalSpy success(handle, &RecognitionCsvRequestHandle::succeeded);
        QSignalSpy failure(handle, &RecognitionCsvRequestHandle::failed);
        if (index < 2) {
            QVERIFY(success.wait());
            QCOMPARE(failure.count(), 0);
        } else {
            QVERIFY(failure.wait());
            QCOMPARE(success.count(), 0);
            QCOMPARE(errorFrom(failure).code(), RecognitionApiErrorCode::InvalidContentType);
        }
    }
}

void RecognitionApiCsvTest::rejectsInvalidBomHeaderAndLineEnding()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    const QByteArray header = csvPrefix();
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {header.mid(3)}));
    QByteArray wrongHeader = header;
    wrongHeader[5] = 'X';
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {wrongHeader}));
    QByteArray lfOnly = header;
    lfOnly.replace("\r\n", "\n");
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {lfOnly}));
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {header.left(header.size() - 1)}));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));

    for (int index = 0; index < 4; ++index) {
        auto *handle = api.exportCsv(start, start.addDays(1), {},
                                     sessions.currentGeneration(), index + 1);
        QSignalSpy failure(handle, &RecognitionCsvRequestHandle::failed);
        QVERIFY(failure.wait());
        QCOMPARE(errorFrom(failure).code(), RecognitionApiErrorCode::ProtocolError);
    }
}

void RecognitionApiCsvTest::mapsStrictErrorsAndInvalidatesUnauthorizedSession()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(errorResponse(503, QStringLiteral("TEMPORARY")));
    server.enqueueResponse(errorResponse(401, QStringLiteral("UNAUTHORIZED")));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));

    auto *serverError = api.exportCsv(start, start.addDays(1), {},
                                      sessions.currentGeneration(), 1);
    QSignalSpy serverFailure(serverError, &RecognitionCsvRequestHandle::failed);
    QVERIFY(serverFailure.wait());
    QCOMPARE(errorFrom(serverFailure).code(), RecognitionApiErrorCode::ServerError);
    QCOMPARE(errorFrom(serverFailure).serverCode(), QStringLiteral("TEMPORARY"));

    auto *unauthorized = api.exportCsv(start, start.addDays(1), {},
                                       sessions.currentGeneration(), 2);
    QSignalSpy unauthorizedFailure(unauthorized, &RecognitionCsvRequestHandle::failed);
    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);
    QVERIFY(unauthorizedFailure.wait());
    QCOMPARE(errorFrom(unauthorizedFailure).code(), RecognitionApiErrorCode::Unauthorized);
    QCOMPARE(invalidated.count(), 1);
    QVERIFY(!sessions.hasActiveSession());
}

void RecognitionApiCsvTest::mapsTransportFailuresAndNeverRetries()
{
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));

    TlsErrorNetworkManager tlsManager;
    HttpClient tlsTransport(&tlsManager,
                            [&sessions](const RequestContext &context) {
                                return sessions.isCurrent(context);
                            },
                            {},
                            nullptr);
    RecognitionApiClient tlsApi(QUrl(QStringLiteral("https://example.test")),
                                &tlsTransport,
                                &sessions);
    auto *tls = tlsApi.exportCsv(start, start.addDays(1), {},
                                 sessions.currentGeneration(), 1);
    QSignalSpy tlsFailure(tls, &RecognitionCsvRequestHandle::failed);
    QVERIFY(tlsFailure.wait());
    QCOMPARE(errorFrom(tlsFailure).code(), RecognitionApiErrorCode::TlsError);

    MockHttpServer disconnectedServer;
    QVERIFY(disconnectedServer.listen());
    MockHttpResponse disconnected = csvResponse(
        QByteArrayLiteral("text/csv; charset=utf-8"),
        {csvPrefix().left(4)});
    disconnected.declaredContentLength = csvBody().size();
    disconnected.disconnectAfterChunks = 1;
    disconnectedServer.enqueueResponse(std::move(disconnected));
    HttpClient networkTransport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient networkApi(disconnectedServer.url(),
                                    &networkTransport,
                                    &sessions);
    auto *network = networkApi.exportCsv(start, start.addDays(1), {},
                                         sessions.currentGeneration(), 2);
    QSignalSpy networkFailure(network, &RecognitionCsvRequestHandle::failed);
    QVERIFY(networkFailure.wait());
    QCOMPARE(errorFrom(networkFailure).code(), RecognitionApiErrorCode::NetworkError);
    QCOMPARE(disconnectedServer.requests().size(), 1);

    MockHttpServer timeoutServer;
    QVERIFY(timeoutServer.listen());
    MockHttpResponse slow = csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"));
    slow.initialDelayMs = 200;
    timeoutServer.enqueueResponse(std::move(slow));
    QNetworkAccessManager timeoutManager;
    HttpRequestKind observedKind = HttpRequestKind::Login;
    HttpClient timeoutTransport(&timeoutManager,
                                [&sessions](const RequestContext &context) {
                                    return sessions.isCurrent(context);
                                },
                                [&observedKind](HttpRequestKind kind) {
                                    observedKind = kind;
                                    return std::chrono::milliseconds(20);
                                });
    RecognitionApiClient timeoutApi(timeoutServer.url(),
                                    &timeoutTransport,
                                    &sessions);
    auto *timeout = timeoutApi.exportCsv(start, start.addDays(1), {},
                                         sessions.currentGeneration(), 3);
    QSignalSpy timeoutFailure(timeout, &RecognitionCsvRequestHandle::failed);
    QVERIFY(timeoutFailure.wait());
    QCOMPARE(errorFrom(timeoutFailure).code(), RecognitionApiErrorCode::Timeout);
    QCOMPARE(observedKind, HttpRequestKind::Csv);
    QTest::qWait(220);
    QCOMPARE(timeoutServer.requests().size(), 1);

    MockHttpServer redirectServer;
    QVERIFY(redirectServer.listen());
    MockHttpResponse redirect;
    redirect.statusCode = 302;
    redirect.reasonPhrase = QByteArrayLiteral("Found");
    redirect.headers.append({QByteArrayLiteral("Location"),
                             redirectServer.url(QStringLiteral("/target")).toEncoded()});
    redirectServer.enqueueResponse(redirect);
    redirectServer.enqueueResponse(redirect);
    HttpClient redirectTransport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient redirectApi(redirectServer.url(),
                                     &redirectTransport,
                                     &sessions);
    auto *redirected = redirectApi.exportCsv(start, start.addDays(1), {},
                                              sessions.currentGeneration(), 4);
    QSignalSpy redirectFailure(redirected, &RecognitionCsvRequestHandle::failed);
    QVERIFY(redirectFailure.wait());
    QCOMPARE(errorFrom(redirectFailure).code(), RecognitionApiErrorCode::ProtocolError);
    QTest::qWait(30);
    QCOMPARE(redirectServer.requests().size(), 1);
}

void RecognitionApiCsvTest::firstChunkCallbackCanCancelOrDeleteHandle()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    const QByteArray firstChunk = csvPrefix() + QByteArray(512 * 1024, 'x');
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {firstChunk, QByteArrayLiteral("tail")}));
    server.enqueueResponse(csvResponse(QByteArrayLiteral("text/csv; charset=utf-8"),
                                       {firstChunk, QByteArrayLiteral("tail")}));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));

    auto *cancelled = api.exportCsv(start, start.addDays(1), {},
                                    sessions.currentGeneration(), 1);
    int cancelledChunks = 0;
    connect(cancelled, &RecognitionCsvRequestHandle::dataChunkReceived,
            this, [cancelled, &cancelledChunks](const QByteArray &) {
                ++cancelledChunks;
                cancelled->cancel();
            });
    QSignalSpy cancelledSuccess(cancelled, &RecognitionCsvRequestHandle::succeeded);
    QSignalSpy cancelledFailure(cancelled, &RecognitionCsvRequestHandle::failed);
    QVERIFY(cancelledFailure.wait());
    QCOMPARE(cancelledChunks, 1);
    QCOMPARE(cancelledSuccess.count(), 0);
    QCOMPARE(errorFrom(cancelledFailure).code(), RecognitionApiErrorCode::Cancelled);

    auto *deleted = api.exportCsv(start, start.addDays(1), {},
                                  sessions.currentGeneration(), 2);
    QPointer<RecognitionCsvRequestHandle> guarded(deleted);
    int deletedChunks = 0;
    int deletedSuccesses = 0;
    connect(deleted, &RecognitionCsvRequestHandle::dataChunkReceived,
            this, [deleted, &deletedChunks](const QByteArray &) {
                ++deletedChunks;
                delete deleted;
            });
    connect(deleted, &RecognitionCsvRequestHandle::succeeded,
            this, [&deletedSuccesses] { ++deletedSuccesses; });
    QTRY_VERIFY(guarded.isNull());
    QTest::qWait(30);
    QCOMPARE(deletedChunks, 1);
    QCOMPARE(deletedSuccesses, 0);
}

QTEST_MAIN(RecognitionApiCsvTest)

#include "tst_recognition_api_csv.moc"
