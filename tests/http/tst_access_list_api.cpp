#include "domain/SessionContext.h"
#include "infrastructure/http/AccessListApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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

QJsonObject record(qint64 id,
                   const QString &listType = QStringLiteral("WHITE"),
                   const QString &plate = QStringLiteral("京A12345"),
                   const QString &remark = QStringLiteral("教学车辆"))
{
    return QJsonObject {{QStringLiteral("id"), id},
                        {QStringLiteral("listType"), listType},
                        {QStringLiteral("plateNumber"), plate},
                        {QStringLiteral("remark"), remark},
                        {QStringLiteral("createdBy"), QStringLiteral("测试操作员")},
                        {QStringLiteral("createdAt"),
                         QStringLiteral("2026-08-24T10:00:00.001+08:00")}};
}

QByteArray envelope(const QJsonValue &data,
                    bool success = true,
                    const QString &code = QStringLiteral("OK"),
                    const QString &message = {})
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), success},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"), message},
                             {QStringLiteral("requestId"), QStringLiteral("access-request")},
                             {QStringLiteral("data"), data},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray pageEnvelope(int page,
                        const QJsonArray &items,
                        qint64 total = 1)
{
    return envelope(QJsonObject {{QStringLiteral("items"), items},
                                 {QStringLiteral("page"), page},
                                 {QStringLiteral("pageSize"), 100},
                                 {QStringLiteral("total"), total}});
}

QByteArray failureEnvelope(const QString &code)
{
    return envelope(QJsonValue::Null, false, code, QStringLiteral("名单请求失败"));
}

MockHttpResponse jsonResponse(int status, QByteArray body, int delayMs = 0)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = status >= 400 ? QByteArrayLiteral("Error")
                                           : QByteArrayLiteral("OK");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(std::move(body));
    response.initialDelayMs = delayMs;
    return response;
}

AccessListApiError errorFrom(const QSignalSpy &spy)
{
    return *static_cast<const AccessListApiError *>(spy.first().first().constData());
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

class AccessListApiTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void encodesAllRoutesAndContexts();
    void emptyKeywordUsesExplicitEqualsDelimiter();
    void rejectsUncorrelatedSuccessResponses();
    void mapsNotFoundStableConflictsAndUnauthorized();
    void unauthorizedSynchronousApiDeletionIsSafe();
    void mapsProtocolAndTransportFailuresWithoutRetry();
    void appliesPoliciesCancellationAndNoRetry();
};

void AccessListApiTest::initTestCase()
{
    qRegisterMetaType<AccessListApiError>();
}

void AccessListApiTest::encodesAllRoutesAndContexts()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        200, pageEnvelope(2, QJsonArray {record(1, QStringLiteral("BLACK"))})));
    server.enqueueResponse(jsonResponse(200, envelope(record(2))));
    server.enqueueResponse(jsonResponse(200, envelope(record(3))));
    server.enqueueResponse(jsonResponse(200, envelope(QJsonValue::Null)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(QStringLiteral("/gateway/")),
                            &transport,
                            &sessions);
    const quint64 generation = sessions.currentGeneration();

    auto *page = api.getPage(AccessListType::Black,
                             QStringLiteral("京A/%_"), 2, generation, 11);
    QSignalSpy pageSuccess(page, &AccessListPageRequestHandle::succeeded);
    QVERIFY(pageSuccess.wait());
    QCOMPARE(page->context().filterSnapshot().value(QStringLiteral("pageSize")).toInt(),
             100);
    QCOMPARE(page->page()->items().first().listType(), AccessListType::Black);

    auto *lookup = api.lookup(QStringLiteral("京A12345"), generation, 12);
    QSignalSpy lookupSuccess(lookup, &AccessListLookupRequestHandle::succeeded);
    QVERIFY(lookupSuccess.wait());
    QCOMPARE(lookup->record()->id(), qint64(2));

    auto *create = api.create(AccessListType::White,
                              QStringLiteral("京A12345"),
                              QStringLiteral("教学车辆"),
                              generation,
                              13);
    QSignalSpy createSuccess(create, &AccessListCreateRequestHandle::succeeded);
    QVERIFY(createSuccess.wait());
    QCOMPARE(create->record()->id(), qint64(3));

    auto *remove = api.remove(3,
                              AccessListType::White,
                              QStringLiteral("京A12345"),
                              generation,
                              14);
    QSignalSpy deleteSuccess(remove, &AccessListDeleteRequestHandle::succeeded);
    QVERIFY(deleteSuccess.wait());

    QTRY_COMPARE(server.requests().size(), 4);
    QCOMPARE(server.requests().at(0).method, QByteArrayLiteral("GET"));
    const QUrl pageUrl(QStringLiteral("http://mock.test")
                       + QString::fromLatin1(server.requests().at(0).target));
    QCOMPARE(pageUrl.path(), QStringLiteral("/gateway/api/v1/access-lists"));
    const QUrlQuery pageQuery(pageUrl);
    QCOMPARE(pageQuery.queryItemValue(QStringLiteral("listType")), QStringLiteral("BLACK"));
    QCOMPARE(pageQuery.queryItemValue(QStringLiteral("keyword"), QUrl::FullyDecoded),
             QStringLiteral("京A/%_"));
    QCOMPARE(pageQuery.queryItemValue(QStringLiteral("page")), QStringLiteral("2"));
    QCOMPARE(pageQuery.queryItemValue(QStringLiteral("pageSize")), QStringLiteral("100"));
    QCOMPARE(server.requests().at(0).header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));

    const QUrl lookupUrl(QStringLiteral("http://mock.test")
                         + QString::fromLatin1(server.requests().at(1).target));
    QCOMPARE(lookupUrl.path(), QStringLiteral("/gateway/api/v1/access-lists/lookup"));
    QCOMPARE(QUrlQuery(lookupUrl).queryItemValue(QStringLiteral("plateNumber"),
                                                 QUrl::FullyDecoded),
             QStringLiteral("京A12345"));
    QCOMPARE(server.requests().at(2).method, QByteArrayLiteral("POST"));
    QCOMPARE(QJsonDocument::fromJson(server.requests().at(2).body).object(),
             QJsonObject({{QStringLiteral("listType"), QStringLiteral("WHITE")},
                          {QStringLiteral("plateNumber"), QStringLiteral("京A12345")},
                          {QStringLiteral("remark"), QStringLiteral("教学车辆")}}));
    QCOMPARE(server.requests().at(3).method, QByteArrayLiteral("DELETE"));
    QCOMPARE(server.requests().at(3).target,
             QByteArrayLiteral("/gateway/api/v1/access-lists/3"));
    QVERIFY(server.requests().at(3).body.isEmpty());
}

void AccessListApiTest::emptyKeywordUsesExplicitEqualsDelimiter()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1, {}, 0)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);

    auto *request = api.getPage(AccessListType::White,
                                {}, 1, sessions.currentGeneration(), 1);
    QSignalSpy succeeded(request, &AccessListPageRequestHandle::succeeded);
    QVERIFY(succeeded.wait());
    QTRY_COMPARE(server.requests().size(), 1);

    const QByteArray target = server.requests().constFirst().target;
    QCOMPARE(target,
             QByteArrayLiteral("/api/v1/access-lists?listType=WHITE&keyword=&page=1&pageSize=100"));
    QVERIFY(target.contains(QByteArrayLiteral("&keyword=&")));
    QVERIFY(!target.contains(QByteArrayLiteral("&keyword&page=")));
}

void AccessListApiTest::rejectsUncorrelatedSuccessResponses()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        200, pageEnvelope(2, QJsonArray {record(1)})));
    server.enqueueResponse(jsonResponse(
        200, pageEnvelope(1, QJsonArray {record(1, QStringLiteral("BLACK"))})));
    server.enqueueResponse(jsonResponse(
        200, envelope(record(2, QStringLiteral("WHITE"), QStringLiteral("OTHER")))));
    server.enqueueResponse(jsonResponse(
        200, envelope(record(3, QStringLiteral("BLACK")))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    const quint64 generation = sessions.currentGeneration();

    auto *wrongPage = api.getPage(AccessListType::White, {}, 1, generation, 1);
    QSignalSpy wrongPageFailure(wrongPage, &AccessListPageRequestHandle::failed);
    QVERIFY(wrongPageFailure.wait());
    QCOMPARE(errorFrom(wrongPageFailure).code(), AccessListApiErrorCode::ProtocolError);

    auto *wrongType = api.getPage(AccessListType::White, {}, 1, generation, 2);
    QSignalSpy wrongTypeFailure(wrongType, &AccessListPageRequestHandle::failed);
    QVERIFY(wrongTypeFailure.wait());
    QCOMPARE(errorFrom(wrongTypeFailure).code(), AccessListApiErrorCode::ProtocolError);

    auto *wrongPlate = api.lookup(QStringLiteral("京A12345"), generation, 3);
    QSignalSpy wrongPlateFailure(wrongPlate, &AccessListLookupRequestHandle::failed);
    QVERIFY(wrongPlateFailure.wait());
    QCOMPARE(errorFrom(wrongPlateFailure).code(), AccessListApiErrorCode::ProtocolError);

    auto *wrongCreate = api.create(AccessListType::White,
                                   QStringLiteral("京A12345"),
                                   QStringLiteral("教学车辆"), generation, 4);
    QSignalSpy wrongCreateFailure(wrongCreate, &AccessListCreateRequestHandle::failed);
    QVERIFY(wrongCreateFailure.wait());
    QCOMPARE(errorFrom(wrongCreateFailure).code(), AccessListApiErrorCode::ProtocolError);
    QCOMPARE(server.requests().size(), 4);
}

void AccessListApiTest::mapsNotFoundStableConflictsAndUnauthorized()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        404, failureEnvelope(QStringLiteral("ACCESS_LIST_NOT_FOUND"))));
    server.enqueueResponse(jsonResponse(
        409, failureEnvelope(QStringLiteral("ACCESS_LIST_CONFLICT_WHITE"))));
    server.enqueueResponse(jsonResponse(
        409, failureEnvelope(QStringLiteral("ACCESS_LIST_CONFLICT_BLACK"))));
    server.enqueueResponse(jsonResponse(
        409, failureEnvelope(QStringLiteral("UNKNOWN_CONFLICT"))));
    server.enqueueResponse(jsonResponse(
        401, failureEnvelope(QStringLiteral("UNAUTHORIZED"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    const quint64 generation = sessions.currentGeneration();

    auto createRequest = [&](quint64 requestGeneration) {
        return api.create(AccessListType::White,
                          QStringLiteral("京A12345"),
                          QStringLiteral("教学车辆"),
                          generation,
                          requestGeneration);
    };
    auto *notFound = api.lookup(QStringLiteral("京A12345"), generation, 1);
    QSignalSpy notFoundFailure(notFound, &AccessListLookupRequestHandle::failed);
    QVERIFY(notFoundFailure.wait());
    QCOMPARE(errorFrom(notFoundFailure).code(), AccessListApiErrorCode::NotFound);

    auto *white = createRequest(2);
    QSignalSpy whiteFailure(white, &AccessListCreateRequestHandle::failed);
    QVERIFY(whiteFailure.wait());
    QCOMPARE(errorFrom(whiteFailure).code(), AccessListApiErrorCode::ConflictWhite);

    auto *black = createRequest(3);
    QSignalSpy blackFailure(black, &AccessListCreateRequestHandle::failed);
    QVERIFY(blackFailure.wait());
    QCOMPARE(errorFrom(blackFailure).code(), AccessListApiErrorCode::ConflictBlack);

    auto *unknown = createRequest(4);
    QSignalSpy unknownFailure(unknown, &AccessListCreateRequestHandle::failed);
    QVERIFY(unknownFailure.wait());
    QCOMPARE(errorFrom(unknownFailure).code(), AccessListApiErrorCode::ProtocolError);

    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);
    auto *unauthorized = api.getPage(AccessListType::White, {}, 1, generation, 5);
    QSignalSpy unauthorizedFailure(unauthorized, &AccessListPageRequestHandle::failed);
    QVERIFY(unauthorizedFailure.wait());
    QCOMPARE(errorFrom(unauthorizedFailure).code(), AccessListApiErrorCode::Unauthorized);
    QCOMPARE(invalidated.count(), 1);
    QVERIFY(!sessions.hasActiveSession());
}

void AccessListApiTest::unauthorizedSynchronousApiDeletionIsSafe()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        401, failureEnvelope(QStringLiteral("UNAUTHORIZED"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    QPointer<AccessListApiClient> api =
        new AccessListApiClient(server.url(), &transport, &sessions);
    const quint64 generation = sessions.currentGeneration();
    auto *rawHandle = api->getPage(AccessListType::White, {}, 1, generation, 1);
    QPointer<AccessListPageRequestHandle> handle(rawHandle);
    QObject observer;
    int failuresPublished = 0;
    connect(rawHandle, &AccessListPageRequestHandle::failed,
            &observer, [&failuresPublished] { ++failuresPublished; });
    connect(&sessions, &SessionService::sessionInvalidated,
            &observer, [api](SessionInvalidationReason, quint64) mutable {
                delete api.data();
            });

    QTRY_VERIFY(api.isNull());
    QVERIFY(handle.isNull());
    QVERIFY(!sessions.hasActiveSession());
    QCOMPARE(failuresPublished, 0);
    QCOMPARE(server.requests().size(), 1);
}

void AccessListApiTest::mapsProtocolAndTransportFailuresWithoutRetry()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        403, failureEnvelope(QStringLiteral("FORBIDDEN"))));
    server.enqueueResponse(jsonResponse(
        503, failureEnvelope(QStringLiteral("DATABASE_UNAVAILABLE"))));
    MockHttpResponse redirect;
    redirect.statusCode = 302;
    redirect.reasonPhrase = QByteArrayLiteral("Found");
    redirect.headers.append({QByteArrayLiteral("Location"),
                             server.url(QStringLiteral("/unexpected")).toEncoded()});
    server.enqueueResponse(redirect);
    server.enqueueResponse(jsonResponse(400, QByteArrayLiteral("{}")));
    MockHttpResponse oversized = jsonResponse(200, pageEnvelope(1, {}));
    oversized.declaredContentLength = HttpRequestPolicy::jsonMaximumBytes + 1;
    server.enqueueResponse(oversized);
    MockHttpResponse partial = jsonResponse(200, pageEnvelope(1, {}));
    partial.declaredContentLength = 100;
    partial.chunks = {QByteArrayLiteral("{")};
    partial.disconnectAfterChunks = 1;
    server.enqueueResponse(partial);

    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    const quint64 generation = sessions.currentGeneration();
    const QList<AccessListApiErrorCode> expected {
        AccessListApiErrorCode::Forbidden,
        AccessListApiErrorCode::ServerError,
        AccessListApiErrorCode::ProtocolError,
        AccessListApiErrorCode::ProtocolError,
        AccessListApiErrorCode::ResponseTooLarge,
        AccessListApiErrorCode::NetworkError,
    };

    for (qsizetype index = 0; index < expected.size(); ++index) {
        auto *request = api.getPage(AccessListType::White, {}, 1,
                                    generation, static_cast<quint64>(index + 1));
        QSignalSpy failure(request, &AccessListPageRequestHandle::failed);
        QVERIFY(failure.wait());
        QCOMPARE(errorFrom(failure).code(), expected.at(index));
    }
    QCOMPARE(server.requests().size(), expected.size());
    QVERIFY(sessions.hasActiveSession());

    TlsErrorNetworkManager tlsManager;
    HttpClient tlsTransport(&tlsManager,
                            [&sessions](const RequestContext &context) {
                                return sessions.isCurrent(context);
                            },
                            {},
                            nullptr);
    AccessListApiClient tlsApi(QUrl(QStringLiteral("https://example.test")),
                               &tlsTransport,
                               &sessions);
    auto *tls = tlsApi.getPage(AccessListType::White, {}, 1, generation, 20);
    QSignalSpy tlsFailure(tls, &AccessListPageRequestHandle::failed);
    QVERIFY(tlsFailure.wait());
    QCOMPARE(errorFrom(tlsFailure).code(), AccessListApiErrorCode::TlsError);
}

void AccessListApiTest::appliesPoliciesCancellationAndNoRetry()
{
    using namespace std::chrono_literals;
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Query), 10s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::AccessListWrite), 10s);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(record(1)), 100));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1, {}), 100));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    QList<HttpRequestKind> observedKinds;
    QNetworkAccessManager manager;
    HttpClient timeoutTransport(&manager,
                                [&sessions](const RequestContext &context) {
                                    return sessions.isCurrent(context);
                                },
                                [&observedKinds](HttpRequestKind kind) {
                                    observedKinds.append(kind);
                                    return std::chrono::milliseconds(15);
                                });
    AccessListApiClient timeoutApi(server.url(), &timeoutTransport, &sessions);

    auto *timeout = timeoutApi.create(AccessListType::White,
                                      QStringLiteral("京A12345"),
                                      QStringLiteral("教学车辆"),
                                      sessions.currentGeneration(), 1);
    QSignalSpy timeoutFailure(timeout, &AccessListCreateRequestHandle::failed);
    QVERIFY(timeoutFailure.wait());
    QCOMPARE(errorFrom(timeoutFailure).code(), AccessListApiErrorCode::Timeout);
    QCOMPARE(observedKinds.last(), HttpRequestKind::AccessListWrite);

    HttpClient cancelTransport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient cancelApi(server.url(), &cancelTransport, &sessions);
    auto *cancelled = cancelApi.getPage(AccessListType::White, {}, 1,
                                        sessions.currentGeneration(), 2);
    QSignalSpy cancelledFailure(cancelled, &AccessListPageRequestHandle::failed);
    QTRY_COMPARE(server.requests().size(), 2);
    cancelled->cancel();
    QTRY_COMPARE(cancelledFailure.count(), 1);
    QCOMPARE(errorFrom(cancelledFailure).code(), AccessListApiErrorCode::Cancelled);
    QTest::qWait(130);
    QCOMPARE(server.requests().size(), 2);

    auto *invalid = cancelApi.lookup(QStringLiteral(" bad plate "),
                                     sessions.currentGeneration(), 3);
    QSignalSpy invalidFailure(invalid, &AccessListLookupRequestHandle::failed);
    QVERIFY(invalidFailure.wait());
    QCOMPARE(errorFrom(invalidFailure).code(), AccessListApiErrorCode::InvalidRequest);
    QCOMPARE(server.requests().size(), 2);
}

QTEST_MAIN(AccessListApiTest)

#include "tst_access_list_api.moc"
