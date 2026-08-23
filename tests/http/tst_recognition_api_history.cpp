#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
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

QJsonObject snapshot(const QString &id)
{
    return QJsonObject {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("recognitionId"), id},
        {QStringLiteral("revision"), 2},
        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
        {QStringLiteral("status"), QStringLiteral("SUCCEEDED")},
        {QStringLiteral("plateNumber"), QStringLiteral("京A12345")},
        {QStringLiteral("errorCode"), QJsonValue::Null},
        {QStringLiteral("errorMessage"), QJsonValue::Null},
        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-24T09:00:00.001+08:00")},
        {QStringLiteral("startedAt"), QStringLiteral("2026-08-24T09:00:01.002+08:00")},
        {QStringLiteral("completedAt"), QStringLiteral("2026-08-24T09:00:02.003+08:00")},
        {QStringLiteral("durationMs"), 1001},
    };
}

QByteArray pageEnvelope(int page,
                        QJsonArray items = QJsonArray {snapshot(QStringLiteral("rec-1"))},
                        qint64 total = 1)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("history-request")},
                             {QStringLiteral("data"),
                              QJsonObject {{QStringLiteral("items"), items},
                                           {QStringLiteral("page"), page},
                                           {QStringLiteral("pageSize"), 100},
                                           {QStringLiteral("total"), total}}},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray failureEnvelope(const QString &code)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), false},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"), QStringLiteral("查询失败")},
                             {QStringLiteral("requestId"), QStringLiteral("history-error")},
                             {QStringLiteral("data"), QJsonValue::Null},
                         })
        .toJson(QJsonDocument::Compact);
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

RecognitionApiError errorFrom(const QSignalSpy &spy)
{
    return *static_cast<const RecognitionApiError *>(spy.first().first().constData());
}

QDateTime time(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}
} // namespace

class RecognitionApiHistoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void encodesFixedQueryAndImmutableContext();
    void omitsBlankDeviceAndRejectsMismatchedOrMalformedPage();
    void mapsStrictErrorsAndInvalidatesOnlyOriginal401Session();
    void appliesTenSecondQueryPolicyCancellationAndNoRetry();
};

void RecognitionApiHistoryTest::initTestCase()
{
    qRegisterMetaType<RecognitionApiError>();
}

void RecognitionApiHistoryTest::encodesFixedQueryAndImmutableContext()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(3)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(QStringLiteral("/gateway/")),
                                &transport,
                                &sessions);

    auto *handle = client.getHistory(time(QStringLiteral("2026-08-23T16:00:00.007Z")),
                                     time(QStringLiteral("2026-08-24T16:00:00.009Z")),
                                     QStringLiteral("  设备 A/1 +%  "),
                                     3,
                                     sessions.currentGeneration(),
                                     41);
    QSignalSpy success(handle, &RecognitionHistoryRequestHandle::succeeded);
    QSignalSpy failure(handle, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QVERIFY(handle->page());
    QCOMPARE(handle->page()->page(), 3);
    QCOMPARE(handle->context().operation(), QStringLiteral("recognition.history"));
    QCOMPARE(handle->context().requestGeneration(), quint64(41));
    QCOMPARE(handle->context().filterSnapshot().value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("设备 A/1 +%"));
    QTRY_COMPARE(server.requests().size(), 1);
    const MockHttpRequest &request = server.requests().constFirst();
    QCOMPARE(request.method, QByteArrayLiteral("GET"));
    QCOMPARE(request.header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));
    QCOMPARE(request.header(QByteArrayLiteral("Accept")),
             QByteArrayLiteral("application/json"));
    const QUrl url(QStringLiteral("http://mock.test") + QString::fromLatin1(request.target));
    QCOMPARE(url.path(), QStringLiteral("/gateway/api/v1/recognitions"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("startTime")),
             QStringLiteral("2026-08-24T00:00:00.007+08:00"));
    QCOMPARE(query.queryItemValue(QStringLiteral("endTime")),
             QStringLiteral("2026-08-25T00:00:00.009+08:00"));
    QCOMPARE(query.queryItemValue(QStringLiteral("deviceId"), QUrl::FullyDecoded),
             QStringLiteral("设备 A/1 +%"));
    QCOMPARE(query.queryItemValue(QStringLiteral("page")), QStringLiteral("3"));
    QCOMPARE(query.queryItemValue(QStringLiteral("pageSize")), QStringLiteral("100"));
}

void RecognitionApiHistoryTest::omitsBlankDeviceAndRejectsMismatchedOrMalformedPage()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1, {}, 0)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(2)));
    QJsonObject unknownPage = QJsonDocument::fromJson(pageEnvelope(1)).object();
    QJsonObject data = unknownPage.value(QStringLiteral("data")).toObject();
    data.insert(QStringLiteral("debug"), true);
    unknownPage.insert(QStringLiteral("data"), data);
    server.enqueueResponse(jsonResponse(200,
                                        QJsonDocument(unknownPage)
                                            .toJson(QJsonDocument::Compact)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    const QDateTime end = time(QStringLiteral("2026-08-25T00:00:00.000+08:00"));

    auto *blank = client.getHistory(start, end, QStringLiteral(" \t "), 1,
                                    sessions.currentGeneration(), 1);
    QSignalSpy blankSuccess(blank, &RecognitionHistoryRequestHandle::succeeded);
    QVERIFY(blankSuccess.wait());
    QTRY_COMPARE(server.requests().size(), 1);
    QVERIFY(!server.requests().at(0).target.contains("deviceId"));

    auto *mismatch = client.getHistory(start, end, {}, 1,
                                       sessions.currentGeneration(), 2);
    QSignalSpy mismatchFailure(mismatch, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(mismatchFailure.wait());
    QCOMPARE(errorFrom(mismatchFailure).code(), RecognitionApiErrorCode::ProtocolError);
    QVERIFY(errorFrom(mismatchFailure).protocolError());
    QCOMPARE(errorFrom(mismatchFailure).protocolError()->fieldPath(),
             QStringLiteral("$.data.page"));

    auto *unknown = client.getHistory(start, end, {}, 1,
                                      sessions.currentGeneration(), 3);
    QSignalSpy unknownFailure(unknown, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(unknownFailure.wait());
    QCOMPARE(errorFrom(unknownFailure).protocolError()->code(),
             ProtocolErrorCode::UnknownField);
    QCOMPARE(server.requests().size(), 3);
}

void RecognitionApiHistoryTest::mapsStrictErrorsAndInvalidatesOnlyOriginal401Session()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(403, failureEnvelope(QStringLiteral("FORBIDDEN"))));
    server.enqueueResponse(jsonResponse(503, failureEnvelope(QStringLiteral("TEMPORARY"))));
    server.enqueueResponse(jsonResponse(401, failureEnvelope(QStringLiteral("UNAUTHORIZED"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    const QDateTime end = start.addDays(1);

    auto request = [&](quint64 requestGeneration) {
        return client.getHistory(start, end, {}, 1,
                                 sessions.currentGeneration(), requestGeneration);
    };
    auto *forbidden = request(1);
    QSignalSpy forbiddenFailure(forbidden, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(forbiddenFailure.wait());
    QCOMPARE(errorFrom(forbiddenFailure).code(), RecognitionApiErrorCode::Forbidden);
    QCOMPARE(errorFrom(forbiddenFailure).serverCode(), QStringLiteral("FORBIDDEN"));
    QVERIFY(sessions.hasActiveSession());

    auto *serverError = request(2);
    QSignalSpy serverFailure(serverError, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(serverFailure.wait());
    QCOMPARE(errorFrom(serverFailure).code(), RecognitionApiErrorCode::ServerError);
    QVERIFY(sessions.hasActiveSession());

    const quint64 originalGeneration = sessions.currentGeneration();
    auto *unauthorized = request(3);
    QSignalSpy unauthorizedFailure(unauthorized, &RecognitionHistoryRequestHandle::failed);
    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);
    QVERIFY(unauthorizedFailure.wait());
    QCOMPARE(errorFrom(unauthorizedFailure).code(), RecognitionApiErrorCode::Unauthorized);
    QCOMPARE(unauthorized->context().sessionGeneration(), originalGeneration);
    QCOMPARE(invalidated.count(), 1);
    QVERIFY(!sessions.hasActiveSession());
}

void RecognitionApiHistoryTest::appliesTenSecondQueryPolicyCancellationAndNoRetry()
{
    using namespace std::chrono_literals;
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Query), 10s);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1), 100));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1), 100));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpRequestKind observedKind = HttpRequestKind::Login;
    QNetworkAccessManager manager;
    HttpClient transport(&manager,
                         [&sessions](const RequestContext &context) {
                             return sessions.isCurrent(context);
                         },
                         [&observedKind](HttpRequestKind kind) {
                             observedKind = kind;
                             return std::chrono::milliseconds(15);
                         });
    RecognitionApiClient client(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    const QDateTime end = start.addDays(1);

    auto *timeout = client.getHistory(start, end, {}, 1,
                                      sessions.currentGeneration(), 1);
    QSignalSpy timeoutFailure(timeout, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(timeoutFailure.wait());
    QCOMPARE(errorFrom(timeoutFailure).code(), RecognitionApiErrorCode::Timeout);
    QCOMPARE(observedKind, HttpRequestKind::Query);

    HttpClient cancelTransport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient cancelClient(server.url(), &cancelTransport, &sessions);
    auto *cancelled = cancelClient.getHistory(start, end, {}, 1,
                                              sessions.currentGeneration(), 2);
    QSignalSpy cancelledFailure(cancelled, &RecognitionHistoryRequestHandle::failed);
    QTRY_COMPARE(server.requests().size(), 2);
    cancelled->cancel();
    QTRY_COMPARE(cancelledFailure.count(), 1);
    QCOMPARE(errorFrom(cancelledFailure).code(), RecognitionApiErrorCode::Cancelled);
    QTest::qWait(150);
    QCOMPARE(server.requests().size(), 2);

    auto *invalid = client.getHistory(end, start, {}, 0,
                                      sessions.currentGeneration(), 3);
    QSignalSpy invalidFailure(invalid, &RecognitionHistoryRequestHandle::failed);
    QVERIFY(invalidFailure.wait());
    QCOMPARE(errorFrom(invalidFailure).code(), RecognitionApiErrorCode::InvalidRequest);
    QCOMPARE(server.requests().size(), 2);
}

QTEST_MAIN(RecognitionApiHistoryTest)

#include "tst_recognition_api_history.moc"
