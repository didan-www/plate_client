#include "app/AppController.h"

#include "domain/RequestContext.h"
#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/windows/LoginWindow.h"
#include "ui/windows/MainWindow.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray envelope(const QJsonValue &data)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("lifecycle-request")},
                             {QStringLiteral("data"), data},
                         })
        .toJson(QJsonDocument::Compact);
}

QJsonObject sessionData()
{
    const QString expiry = QDateTime::currentDateTime()
                               .addSecs(3600)
                               .toOffsetFromUtc(8 * 3600)
                               .toString(Qt::ISODate);
    return QJsonObject {
        {QStringLiteral("displayName"), QStringLiteral("测试操作员")},
        {QStringLiteral("accessToken"), QStringLiteral("lifecycle-token")},
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

MockHttpResponse jsonResponse(QByteArray body, int delayMs = 0)
{
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(std::move(body));
    response.initialDelayMs = delayMs;
    return response;
}

void submitLogin(AppController &controller)
{
    LoginWindow *login = controller.loginWindow();
    login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->setText(
        QStringLiteral("operator"));
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(
        QStringLiteral("secret"));
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
}
} // namespace

class LogoutLifecycleTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void explicitLogoutRunsOrderedOnceAndReturnsCleanLogin();
    void closeRunsSameCleanupAndSelectsQuit();
    void automaticTerminationReturnsToLogin_data();
    void automaticTerminationReturnsToLogin();
};

void LogoutLifecycleTest::initTestCase()
{
    qRegisterMetaType<SessionInvalidationReason>();
}

void LogoutLifecycleTest::explicitLogoutRunsOrderedOnceAndReturnsCleanLogin()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(envelope(sessionData())));
    server.enqueueResponse(jsonResponse(envelope(QJsonValue::Null), 250));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    QStringList order;
    int quitCount = 0;
    bool oldContextRejectedBeforeCancellation = false;
    quint64 oldGeneration = 0;
    AppController::TerminationActions actions;
    actions.stopScheduling = [&] {
        order.append(QStringLiteral("stop-scheduling"));
        oldContextRejectedBeforeCancellation =
            !sessions.isCurrent(RequestContext(oldGeneration, 1,
                                               QStringLiteral("history.query")));
    };
    actions.cancelAsync = [&] {
        order.append(QStringLiteral("cancel-async"));
        transport.cancelAll();
    };
    actions.disconnectMqtt = [&] { order.append(QStringLiteral("disconnect-mqtt")); };
    actions.quitApplication = [&] { ++quitCount; };
    AppController controller(&auth, &sessions, &settings,
                             [](const QString &displayName) {
                                 return new MainWindow(displayName);
                             },
                             std::move(actions));
    connect(&sessions, &SessionService::sessionInvalidated, this,
            [&order](SessionInvalidationReason, quint64) {
                order.append(QStringLiteral("session-invalidated"));
            });
    controller.start();
    submitLogin(controller);
    QTRY_VERIFY(controller.authenticatedWindow());
    oldGeneration = sessions.currentGeneration();
    auto *window = qobject_cast<MainWindow *>(controller.authenticatedWindow());
    QVERIFY(window);
    QPointer<MainWindow> oldWindow(window);

    window->logoutRequested();
    window->logoutRequested();

    QCOMPARE(order, QStringList({QStringLiteral("stop-scheduling"),
                                 QStringLiteral("cancel-async"),
                                 QStringLiteral("disconnect-mqtt"),
                                 QStringLiteral("session-invalidated")}));
    QVERIFY(oldContextRejectedBeforeCancellation);
    QCOMPARE(quitCount, 0);
    QVERIFY(!sessions.hasActiveSession());
    QVERIFY(controller.authenticatedWindow() == nullptr);
    QVERIFY(controller.loginWindow());
    QVERIFY(controller.loginWindow()->isVisible());
    QVERIFY(controller.loginWindow()->password().isEmpty());
    QCOMPARE(controller.loginWindow()
                 ->findChild<QLabel *>(QStringLiteral("errorLabel"))->text(),
             QString());
    QTRY_VERIFY(oldWindow.isNull());
    QTRY_COMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(1).target, QByteArrayLiteral("/api/v1/auth/logout"));
    QCOMPARE(server.requests().at(1).header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer lifecycle-token"));
}

void LogoutLifecycleTest::closeRunsSameCleanupAndSelectsQuit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(envelope(sessionData())));
    server.enqueueResponse(jsonResponse(envelope(QJsonValue::Null), 250));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    QStringList order;
    int quitCount = 0;
    AppController::TerminationActions actions;
    actions.stopScheduling = [&] { order.append(QStringLiteral("stop-scheduling")); };
    actions.cancelAsync = [&] {
        order.append(QStringLiteral("cancel-async"));
        transport.cancelAll();
    };
    actions.disconnectMqtt = [&] { order.append(QStringLiteral("disconnect-mqtt")); };
    actions.quitApplication = [&] {
        order.append(QStringLiteral("quit"));
        ++quitCount;
    };
    AppController controller(&auth, &sessions, &settings,
                             [](const QString &displayName) {
                                 return new MainWindow(displayName);
                             },
                             std::move(actions));
    connect(&sessions, &SessionService::sessionInvalidated, this,
            [&order](SessionInvalidationReason, quint64) {
                order.append(QStringLiteral("session-invalidated"));
            });
    controller.start();
    submitLogin(controller);
    QTRY_VERIFY(controller.authenticatedWindow());
    auto *window = qobject_cast<MainWindow *>(controller.authenticatedWindow());
    QVERIFY(window);
    QPointer<MainWindow> oldWindow(window);
    QSignalSpy closeRequests(window, &MainWindow::closeRequested);

    window->close();
    window->close();

    QCOMPARE(closeRequests.count(), 1);
    QCOMPARE(quitCount, 1);
    QCOMPARE(order, QStringList({QStringLiteral("stop-scheduling"),
                                 QStringLiteral("cancel-async"),
                                 QStringLiteral("disconnect-mqtt"),
                                 QStringLiteral("session-invalidated"),
                                 QStringLiteral("quit")}));
    QVERIFY(controller.loginWindow() == nullptr);
    QVERIFY(controller.authenticatedWindow() == nullptr);
    QTRY_VERIFY(oldWindow.isNull());
    QTRY_COMPARE(server.requests().size(), 2);
}

void LogoutLifecycleTest::automaticTerminationReturnsToLogin_data()
{
    QTest::addColumn<SessionInvalidationReason>("reason");
    QTest::newRow("expired") << SessionInvalidationReason::Expired;
    QTest::newRow("unauthorized") << SessionInvalidationReason::Unauthorized;
}

void LogoutLifecycleTest::automaticTerminationReturnsToLogin()
{
    QFETCH(SessionInvalidationReason, reason);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(envelope(sessionData())));
    server.enqueueResponse(jsonResponse(envelope(QJsonValue::Null)));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    int cleanupCount = 0;
    AppController::TerminationActions actions;
    actions.stopScheduling = [&] { ++cleanupCount; };
    actions.cancelAsync = [&] {
        ++cleanupCount;
        transport.cancelAll();
    };
    actions.disconnectMqtt = [&] { ++cleanupCount; };
    AppController controller(&auth, &sessions, &settings,
                             [](const QString &displayName) {
                                 return new MainWindow(displayName);
                             },
                             std::move(actions));
    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);
    controller.start();
    submitLogin(controller);
    QTRY_VERIFY(controller.authenticatedWindow());
    const quint64 generation = sessions.currentGeneration();

    if (reason == SessionInvalidationReason::Unauthorized) {
        sessions.handleUnauthorized(generation);
    } else {
        sessions.terminationRequested(reason, generation);
    }

    QCOMPARE(cleanupCount, 3);
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(qvariant_cast<SessionInvalidationReason>(invalidated.first().first()), reason);
    QVERIFY(controller.authenticatedWindow() == nullptr);
    QVERIFY(controller.loginWindow());
    QVERIFY(controller.loginWindow()->isVisible());
    QTRY_COMPARE(server.requests().size(), 2);
}

QTEST_MAIN(LogoutLifecycleTest)

#include "tst_logout_lifecycle.moc"
