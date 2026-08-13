#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QtTest>

#include <chrono>

namespace {
QByteArray envelope(const QJsonValue &data,
                    bool success = true,
                    const QString &code = QStringLiteral("OK"),
                    const QString &message = {})
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), success},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"), message},
                             {QStringLiteral("requestId"), QStringLiteral("request-auth")},
                             {QStringLiteral("data"), data},
                         })
        .toJson(QJsonDocument::Compact);
}

QJsonObject sessionData(const QDateTime &expiresAt = QDateTime::currentDateTime().addSecs(3600))
{
    const QString expiry = expiresAt.toOffsetFromUtc(8 * 3600).toString(Qt::ISODate);
    return QJsonObject {
        {QStringLiteral("displayName"), QStringLiteral("操作员")},
        {QStringLiteral("accessToken"), QStringLiteral("access-secret")},
        {QStringLiteral("expiresAt"), expiry},
        {QStringLiteral("mqtt"),
         QJsonObject {{QStringLiteral("host"), QStringLiteral("broker.test")},
                      {QStringLiteral("port"), 8883},
                      {QStringLiteral("tls"), true},
                      {QStringLiteral("username"), QStringLiteral("mqtt-user")},
                      {QStringLiteral("password"), QStringLiteral("mqtt-secret")},
                      {QStringLiteral("expiresAt"), expiry},
                      {QStringLiteral("eventTopic"), QStringLiteral("plate/events")}}},
    };
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

AuthError authError(const QSignalSpy &spy)
{
    return *static_cast<const AuthError *>(spy.first().first().constData());
}
} // namespace

class AuthApiClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsLoginDtoAndReturnsSession();
    void preservesBasePathForLoginAndLogout();
    void maps401403And5xx_data();
    void maps401403And5xx();
    void rejectsMalformedErrorExpiredAndMismatchedSuccess();
    void appliesLoginAndLogoutTimeoutPolicies();
};

void AuthApiClientTest::initTestCase()
{
    qRegisterMetaType<AuthError>();
}

void AuthApiClientTest::sendsLoginDtoAndReturnsSession()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(sessionData())));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    const QUuid clientId(QStringLiteral("11111111-2222-3333-4444-555555555555"));
    AuthApiClient client(server.url(QStringLiteral("/root")), clientId, &transport, &sessions);

    LoginRequestHandle *handle = client.login(QStringLiteral("operator"),
                                              QStringLiteral("login-secret"));
    QSignalSpy success(handle, &LoginRequestHandle::succeeded);
    QSignalSpy failure(handle, &LoginRequestHandle::failed);
    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QTRY_COMPARE(server.requests().size(), 1);
    const MockHttpRequest &request = server.requests().first();
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.target, QByteArrayLiteral("/root/api/v1/auth/login"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("username")).toString(), QStringLiteral("operator"));
    QCOMPARE(body.value(QStringLiteral("password")).toString(), QStringLiteral("login-secret"));
    QCOMPARE(body.value(QStringLiteral("clientId")).toString(),
             QStringLiteral("11111111-2222-3333-4444-555555555555"));
    QVERIFY(handle->summary().has_value());
    QCOMPARE(handle->summary()->displayName(), QStringLiteral("操作员"));
    QVERIFY(handle->summary()->generation() > 0);
    QString token;
    QVERIFY(sessions.withAccessToken(handle->summary()->generation(),
                                     [&token](const QString &value) { token = value; }));
    QCOMPARE(token, QStringLiteral("access-secret"));
}

void AuthApiClientTest::preservesBasePathForLoginAndLogout()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(QJsonValue::Null)));
    HttpClient transport;
    SessionService sessions;
    AuthApiClient client(server.url(QStringLiteral("/gateway/")), QUuid::createUuid(), &transport,
                         &sessions);
    LogoutRequestHandle *handle = client.logout(QStringLiteral("bearer-secret"), 7);
    QSignalSpy success(handle, &LogoutRequestHandle::succeeded);
    QVERIFY(success.wait());
    QTRY_COMPARE(server.requests().size(), 1);
    const MockHttpRequest &request = server.requests().first();
    QCOMPARE(request.target, QByteArrayLiteral("/gateway/api/v1/auth/logout"));
    QCOMPARE(request.header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer bearer-secret"));
    QVERIFY(request.body.isEmpty());
}

void AuthApiClientTest::maps401403And5xx_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<AuthErrorCode>("expected");
    QTest::newRow("unauthorized") << 401 << AuthErrorCode::Unauthorized;
    QTest::newRow("forbidden") << 403 << AuthErrorCode::Forbidden;
    QTest::newRow("server") << 503 << AuthErrorCode::ServerError;
}

void AuthApiClientTest::maps401403And5xx()
{
    QFETCH(int, status);
    QFETCH(AuthErrorCode, expected);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        status,
        envelope(QJsonValue::Null, false, QStringLiteral("AUTH_ERROR"), QStringLiteral("拒绝"))));
    HttpClient transport;
    SessionService sessions;
    AuthApiClient client(server.url(), QUuid::createUuid(), &transport, &sessions);
    LoginRequestHandle *handle = client.login(QStringLiteral("operator"), QStringLiteral("secret"));
    QSignalSpy failure(handle, &LoginRequestHandle::failed);
    QVERIFY(failure.wait());
    QCOMPARE(authError(failure).code(), expected);
    QCOMPARE(authError(failure).serviceCode(), QStringLiteral("AUTH_ERROR"));
    QCOMPARE(authError(failure).requestId(), QStringLiteral("request-auth"));
}

void AuthApiClientTest::rejectsMalformedErrorExpiredAndMismatchedSuccess()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(401, QByteArrayLiteral("{\"error\":true}")));
    server.enqueueResponse(jsonResponse(
        200, envelope(sessionData(QDateTime::currentDateTime().addSecs(-60)))));
    QJsonObject mismatched = sessionData();
    QJsonObject mqtt = mismatched.value(QStringLiteral("mqtt")).toObject();
    mqtt.insert(QStringLiteral("expiresAt"),
                QDateTime::currentDateTime().addSecs(7200).toOffsetFromUtc(8 * 3600)
                    .toString(Qt::ISODate));
    mismatched.insert(QStringLiteral("mqtt"), mqtt);
    server.enqueueResponse(jsonResponse(200, envelope(mismatched)));
    HttpClient transport;
    SessionService sessions;
    AuthApiClient client(server.url(), QUuid::createUuid(), &transport, &sessions);

    LoginRequestHandle *badError = client.login(QStringLiteral("operator"), QStringLiteral("secret"));
    QSignalSpy badErrorSpy(badError, &LoginRequestHandle::failed);
    QVERIFY(badErrorSpy.wait());
    QCOMPARE(authError(badErrorSpy).code(), AuthErrorCode::ProtocolError);

    LoginRequestHandle *expired = client.login(QStringLiteral("operator"), QStringLiteral("secret"));
    QSignalSpy expiredSpy(expired, &LoginRequestHandle::failed);
    QVERIFY(expiredSpy.wait());
    QCOMPARE(authError(expiredSpy).code(), AuthErrorCode::ProtocolError);
    QVERIFY(!expired->summary().has_value());

    LoginRequestHandle *mismatch = client.login(QStringLiteral("operator"), QStringLiteral("secret"));
    QSignalSpy mismatchSpy(mismatch, &LoginRequestHandle::failed);
    QVERIFY(mismatchSpy.wait());
    QCOMPARE(authError(mismatchSpy).code(), AuthErrorCode::ProtocolError);
    QVERIFY(authError(mismatchSpy).protocolError().has_value());
    QCOMPARE(authError(mismatchSpy).protocolError()->code(),
             ProtocolErrorCode::InvalidCombination);
}

void AuthApiClientTest::appliesLoginAndLogoutTimeoutPolicies()
{
    using namespace std::chrono_literals;
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(sessionData()), 100));
    server.enqueueResponse(jsonResponse(200, envelope(QJsonValue::Null), 100));
    QNetworkAccessManager manager;
    HttpClient transport(&manager, {}, [](HttpRequestKind kind) {
        return kind == HttpRequestKind::Login ? 15ms : 20ms;
    });
    SessionService sessions;
    AuthApiClient client(server.url(), QUuid::createUuid(), &transport, &sessions);
    LoginRequestHandle *login = client.login(QStringLiteral("operator"), QStringLiteral("secret"));
    QSignalSpy loginFailure(login, &LoginRequestHandle::failed);
    QVERIFY(loginFailure.wait());
    QCOMPARE(authError(loginFailure).code(), AuthErrorCode::Timeout);

    LogoutRequestHandle *logout = client.logout(QStringLiteral("bearer-secret"), 9);
    QSignalSpy logoutFailure(logout, &LogoutRequestHandle::failed);
    QVERIFY(logoutFailure.wait());
    QCOMPARE(authError(logoutFailure).code(), AuthErrorCode::Timeout);
    QCOMPARE(server.requests().size(), 2);
}

QTEST_MAIN(AuthApiClientTest)

#include "tst_auth_api_client.moc"
