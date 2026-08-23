#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
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

QJsonObject snapshot(const QString &id,
                     qint64 revision = 1,
                     const QString &status = QStringLiteral("PROCESSING"))
{
    const bool completed = status != QStringLiteral("PROCESSING");
    return QJsonObject {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("recognitionId"), id},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
        {QStringLiteral("status"), status},
        {QStringLiteral("plateNumber"),
         status == QStringLiteral("SUCCEEDED") ? QJsonValue(QStringLiteral("京A12345"))
                                                : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("errorCode"),
         status == QStringLiteral("FAILED") ? QJsonValue(QStringLiteral("MODEL_ERROR"))
                                             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("errorMessage"),
         status == QStringLiteral("FAILED") ? QJsonValue(QStringLiteral("识别失败"))
                                             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-12T10:00:00+08:00")},
        {QStringLiteral("startedAt"), QStringLiteral("2026-08-12T10:00:01+08:00")},
        {QStringLiteral("completedAt"),
         completed ? QJsonValue(QStringLiteral("2026-08-12T10:00:02+08:00"))
                   : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("durationMs"), completed ? QJsonValue(1000)
                                                 : QJsonValue(QJsonValue::Null)},
    };
}

QByteArray envelope(const QJsonValue &data,
                    bool success = true,
                    const QString &code = QStringLiteral("OK"))
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), success},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"),
                              success ? QString() : QStringLiteral("请求失败")},
                             {QStringLiteral("requestId"), QStringLiteral("request-detail")},
                             {QStringLiteral("data"), data},
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
} // namespace

class RecognitionApiDetailTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsBearerGetWithSafeSinglePathSegment();
    void rejectsMalformedSuccessAndMismatchedId();
    void mapsStrictFailureAndInvalidatesOn401();
    void appliesQueryTimeoutWithoutRetry();
};

void RecognitionApiDetailTest::initTestCase()
{
    qRegisterMetaType<RecognitionApiError>();
}

void RecognitionApiDetailTest::sendsBearerGetWithSafeSinglePathSegment()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    const QString id = QStringLiteral("rec/../?x% y");
    server.enqueueResponse(jsonResponse(200, envelope(snapshot(id))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(QStringLiteral("/gateway/")),
                                &transport,
                                &sessions);

    RecognitionDetailRequestHandle *handle = client.getDetail(id, sessions.currentGeneration());
    QSignalSpy success(handle, &RecognitionDetailRequestHandle::succeeded);
    QSignalSpy failure(handle, &RecognitionDetailRequestHandle::failed);
    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QVERIFY(handle->snapshot().has_value());
    QCOMPARE(handle->snapshot()->recognitionId(), id);
    QTRY_COMPARE(server.requests().size(), 1);
    const MockHttpRequest &request = server.requests().constFirst();
    QCOMPARE(request.method, QByteArrayLiteral("GET"));
    QCOMPARE(request.target,
             QByteArrayLiteral("/gateway/api/v1/recognitions/rec%2F..%2F%3Fx%25%20y"));
    QCOMPARE(request.header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));
    QVERIFY(request.body.isEmpty());
    QCOMPARE(handle->context().operation(), QStringLiteral("recognition.detail"));
    QCOMPARE(handle->context().recognitionId(), std::optional<QString>(id));
}

void RecognitionApiDetailTest::rejectsMalformedSuccessAndMismatchedId()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    QJsonObject unknown = snapshot(QStringLiteral("rec-1"));
    unknown.insert(QStringLiteral("debug"), true);
    server.enqueueResponse(jsonResponse(200, envelope(unknown)));
    server.enqueueResponse(jsonResponse(200, envelope(snapshot(QStringLiteral("other")))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(), &transport, &sessions);

    auto *malformed = client.getDetail(QStringLiteral("rec-1"), sessions.currentGeneration());
    QSignalSpy malformedFailure(malformed, &RecognitionDetailRequestHandle::failed);
    QVERIFY(malformedFailure.wait());
    QCOMPARE(errorFrom(malformedFailure).code(), RecognitionApiErrorCode::ProtocolError);
    QVERIFY(errorFrom(malformedFailure).protocolError().has_value());
    QCOMPARE(errorFrom(malformedFailure).protocolError()->code(),
             ProtocolErrorCode::UnknownField);

    auto *mismatched = client.getDetail(QStringLiteral("rec-1"), sessions.currentGeneration());
    QSignalSpy mismatchFailure(mismatched, &RecognitionDetailRequestHandle::failed);
    QVERIFY(mismatchFailure.wait());
    QCOMPARE(errorFrom(mismatchFailure).code(), RecognitionApiErrorCode::ProtocolError);
    QCOMPARE(errorFrom(mismatchFailure).protocolError()->code(),
             ProtocolErrorCode::InvalidCombination);
    QCOMPARE(server.requests().size(), 2);
}

void RecognitionApiDetailTest::mapsStrictFailureAndInvalidatesOn401()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        503, envelope(QJsonValue::Null, false, QStringLiteral("TEMPORARY"))));
    server.enqueueResponse(jsonResponse(
        401, envelope(QJsonValue::Null, false, QStringLiteral("UNAUTHORIZED"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(), &transport, &sessions);

    auto *serverFailure = client.getDetail(QStringLiteral("rec-1"),
                                           sessions.currentGeneration());
    QSignalSpy serverSpy(serverFailure, &RecognitionDetailRequestHandle::failed);
    QVERIFY(serverSpy.wait());
    QCOMPARE(errorFrom(serverSpy).code(), RecognitionApiErrorCode::ServerError);
    QCOMPARE(errorFrom(serverSpy).serverCode(), QStringLiteral("TEMPORARY"));
    QVERIFY(sessions.hasActiveSession());

    const quint64 generation = sessions.currentGeneration();
    auto *unauthorized = client.getDetail(QStringLiteral("rec-1"), generation);
    QSignalSpy unauthorizedSpy(unauthorized, &RecognitionDetailRequestHandle::failed);
    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);
    QVERIFY(unauthorizedSpy.wait());
    QCOMPARE(errorFrom(unauthorizedSpy).code(), RecognitionApiErrorCode::Unauthorized);
    QCOMPARE(invalidated.count(), 1);
    QVERIFY(!sessions.hasActiveSession());
    QCOMPARE(server.requests().size(), 2);
}

void RecognitionApiDetailTest::appliesQueryTimeoutWithoutRetry()
{
    using namespace std::chrono_literals;
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        200, envelope(snapshot(QStringLiteral("rec-timeout"))), 100));
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
                             return 15ms;
                         });
    RecognitionApiClient client(server.url(), &transport, &sessions);

    auto *handle = client.getDetail(QStringLiteral("rec-timeout"),
                                    sessions.currentGeneration());
    QSignalSpy failure(handle, &RecognitionDetailRequestHandle::failed);
    QVERIFY(failure.wait());
    QCOMPARE(errorFrom(failure).code(), RecognitionApiErrorCode::Timeout);
    QCOMPARE(observedKind, HttpRequestKind::Query);
    QTest::qWait(150);
    QCOMPARE(server.requests().size(), 1);
}

QTEST_MAIN(RecognitionApiDetailTest)

#include "tst_recognition_api_detail.moc"
