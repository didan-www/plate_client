#include "domain/RequestContext.h"
#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

namespace {
SessionContext makeSession(const QDateTime &expiresAt,
                           const QString &token = QStringLiteral("access-secret"))
{
    return SessionContext(QStringLiteral("操作员"),
                          token,
                          expiresAt,
                          QStringLiteral("broker.test"),
                          8883,
                          true,
                          QStringLiteral("mqtt-user"),
                          QStringLiteral("mqtt-secret"),
                          QStringLiteral("plate/events"));
}

QByteArray emptyEnvelope()
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("logout-request")},
                             {QStringLiteral("data"), QJsonValue::Null},
                         })
        .toJson(QJsonDocument::Compact);
}

MockHttpResponse delayedLogoutResponse()
{
    MockHttpResponse response;
    response.statusCode = 200;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(emptyEnvelope());
    response.initialDelayMs = 500;
    return response;
}
} // namespace

class SessionServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void assignsGenerationsAndRejectsOldContexts();
    void exposesCredentialsOnlyToCurrentGeneration();
    void rejectsExpiredOrPreassignedSession();
    void expiresAndInvalidatesOnce();
    void unauthorizedInvalidatesOnlyCurrentSessionOnce();
    void bestEffortLogoutClearsImmediately();
};

void SessionServiceTest::initTestCase()
{
    qRegisterMetaType<SessionSummary>();
    qRegisterMetaType<SessionInvalidationReason>();
}

void SessionServiceTest::assignsGenerationsAndRejectsOldContexts()
{
    QDateTime now = QDateTime::currentDateTime();
    SessionService service([&now] { return now; });
    QSignalSpy activated(&service, &SessionService::sessionActivated);
    QSignalSpy invalidated(&service, &SessionService::sessionInvalidated);

    QVERIFY(service.isCurrent(RequestContext(0, 1, QStringLiteral("auth.login"))));
    QVERIFY(!service.isCurrent(RequestContext(0, 1, QStringLiteral("history.query"))));

    QVERIFY(service.establishSession(makeSession(now.addSecs(60))));
    const quint64 first = service.currentGeneration();
    QVERIFY(first > 0);
    QVERIFY(!service.isCurrent(RequestContext(0, 1, QStringLiteral("auth.login"))));
    QVERIFY(service.isCurrent(RequestContext(first, 1, QStringLiteral("query"))));
    QVERIFY(!service.isCurrent(RequestContext(first + 1, 1, QStringLiteral("query"))));
    QCOMPARE(service.summary()->displayName(), QStringLiteral("操作员"));

    QVERIFY(service.establishSession(makeSession(now.addSecs(120), QStringLiteral("new-token"))));
    const quint64 second = service.currentGeneration();
    QVERIFY(second > first);
    QCOMPARE(activated.count(), 2);
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(qvariant_cast<SessionInvalidationReason>(invalidated.first().first()),
             SessionInvalidationReason::Replaced);
    QVERIFY(!service.isCurrent(RequestContext(first, 2, QStringLiteral("old"))));
    QVERIFY(service.isCurrent(RequestContext(second, 1, QStringLiteral("new"))));
}

void SessionServiceTest::exposesCredentialsOnlyToCurrentGeneration()
{
    const QDateTime now = QDateTime::currentDateTime();
    SessionService service([now] { return now; });
    QVERIFY(service.establishSession(makeSession(now.addSecs(60))));
    const quint64 generation = service.currentGeneration();
    QString token;
    QVERIFY(service.withAccessToken(generation, [&token](const QString &value) { token = value; }));
    QCOMPARE(token, QStringLiteral("access-secret"));
    QVERIFY(!service.withAccessToken(generation + 1, [](const QString &) {}));

    QString mqttPassword;
    QVERIFY(service.withMqttCredentials(
        generation,
        [&mqttPassword](const QString &host, quint16 port, bool tls, const QString &username,
                        const QString &password, const QString &topic) {
            QCOMPARE(host, QStringLiteral("broker.test"));
            QCOMPARE(port, quint16(8883));
            QVERIFY(tls);
            QCOMPARE(username, QStringLiteral("mqtt-user"));
            QCOMPARE(topic, QStringLiteral("plate/events"));
            mqttPassword = password;
        }));
    QCOMPARE(mqttPassword, QStringLiteral("mqtt-secret"));
    service.clear();
    QVERIFY(!service.withAccessToken(generation, [](const QString &) {}));
}

void SessionServiceTest::rejectsExpiredOrPreassignedSession()
{
    const QDateTime now = QDateTime::currentDateTime();
    SessionService service([now] { return now; });
    QVERIFY(!service.establishSession(makeSession(now.addMSecs(-1))));
    SessionContext preassigned(QStringLiteral("操作员"), QStringLiteral("token"), now.addSecs(60),
                               QStringLiteral("broker"), 8883, true, QStringLiteral("user"),
                               QStringLiteral("password"), QStringLiteral("events"), 8);
    QVERIFY(!service.establishSession(std::move(preassigned)));
    QVERIFY(!service.hasActiveSession());
}

void SessionServiceTest::expiresAndInvalidatesOnce()
{
    QDateTime now = QDateTime::currentDateTime();
    SessionService service([&now] { return now; });
    QSignalSpy invalidated(&service, &SessionService::sessionInvalidated);
    QVERIFY(service.establishSession(makeSession(now.addMSecs(30))));
    now = now.addMSecs(40);
    QTRY_COMPARE(invalidated.count(), 1);
    QCOMPARE(qvariant_cast<SessionInvalidationReason>(invalidated.first().first()),
             SessionInvalidationReason::Expired);
    QVERIFY(!service.hasActiveSession());
    QTest::qWait(30);
    QCOMPARE(invalidated.count(), 1);
}

void SessionServiceTest::unauthorizedInvalidatesOnlyCurrentSessionOnce()
{
    const QDateTime now = QDateTime::currentDateTime();
    SessionService service([now] { return now; });
    QSignalSpy invalidated(&service, &SessionService::sessionInvalidated);
    QVERIFY(service.establishSession(makeSession(now.addSecs(60))));
    const quint64 first = service.currentGeneration();
    service.handleUnauthorized(first + 1);
    QCOMPARE(invalidated.count(), 0);
    service.handleUnauthorized(first);
    service.handleUnauthorized(first);
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(qvariant_cast<SessionInvalidationReason>(invalidated.first().first()),
             SessionInvalidationReason::Unauthorized);

    QVERIFY(service.establishSession(makeSession(now.addSecs(60))));
    const quint64 second = service.currentGeneration();
    service.handleUnauthorized(first);
    QCOMPARE(invalidated.count(), 1);
    service.handleUnauthorized(second);
    QCOMPARE(invalidated.count(), 2);
}

void SessionServiceTest::bestEffortLogoutClearsImmediately()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(delayedLogoutResponse());
    HttpClient transport;
    const QDateTime now = QDateTime::currentDateTime();
    SessionService service([now] { return now; });
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &service);
    QSignalSpy invalidated(&service, &SessionService::sessionInvalidated);
    QVERIFY(service.establishSession(makeSession(now.addSecs(60))));

    LogoutRequestHandle *handle = service.logout(&auth);
    QVERIFY(handle != nullptr);
    QVERIFY(!service.hasActiveSession());
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(qvariant_cast<SessionInvalidationReason>(invalidated.first().first()),
             SessionInvalidationReason::LoggedOut);
    QTRY_COMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));
    handle->cancel();
    QVERIFY(!service.hasActiveSession());
}

QTEST_MAIN(SessionServiceTest)

#include "tst_session_service.moc"
