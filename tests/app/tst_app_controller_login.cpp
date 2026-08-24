#include "app/AppController.h"

#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/windows/LoginWindow.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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
                             {QStringLiteral("requestId"), QStringLiteral("login-request")},
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
        {QStringLiteral("accessToken"), QStringLiteral("test-access-secret")},
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
} // namespace

class AppControllerLoginTest final : public QObject
{
    Q_OBJECT

private slots:
    void restoresLastSuccessfulUsername();
    void loginFailureRestoresFormAndAllowsRetry();
    void successfulLoginCreatesWindowWithoutMqttAndStoresOnlyUsername();
    void delayedRequestCannotBeSubmittedTwice();
    void destructionCancelsActiveLogin();
};

void AppControllerLoginTest::restoresLastSuccessfulUsername()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("login/lastUsername"), QStringLiteral("remembered-user"));
        settings.sync();
    }

    QSettings reopened(settingsPath, QSettings::IniFormat);
    HttpClient transport;
    SessionService sessions;
    AuthApiClient auth(QUrl(QStringLiteral("https://example.test")),
                       QUuid::createUuid(),
                       &transport,
                       &sessions);
    AppController controller(&auth, &sessions, &reopened,
                             [](const QString &) { return new QWidget; });
    controller.start();

    QCOMPARE(controller.loginWindow()->username(), QStringLiteral("remembered-user"));
}

void AppControllerLoginTest::loginFailureRestoresFormAndAllowsRetry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        401,
        envelope(QJsonValue::Null,
                 false,
                 QStringLiteral("INVALID_CREDENTIALS"),
                 QStringLiteral("server detail"))));
    HttpClient transport;
    SessionService sessions;
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    AppController controller(&auth, &sessions, &settings,
                             [](const QString &) { return new QWidget; });
    controller.start();

    LoginWindow *login = controller.loginWindow();
    auto *username = login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
    auto *password = login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    auto *button = login->findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *error = login->findChild<QLabel *>(QStringLiteral("errorLabel"));
    username->setText(QStringLiteral("operator"));
    password->setText(QStringLiteral("wrong-secret"));
    button->click();

    QTRY_VERIFY(button->isEnabled());
    QCOMPARE(error->text(), QStringLiteral("用户名或密码错误。"));
    QVERIFY(password->text().isEmpty());
    QVERIFY(!settings.contains(QStringLiteral("login/lastUsername")));

    server.enqueueResponse(jsonResponse(401,
                                        envelope(QJsonValue::Null,
                                                 false,
                                                 QStringLiteral("INVALID_CREDENTIALS"))));
    password->setText(QStringLiteral("retry-secret"));
    button->click();
    QTRY_COMPARE(server.requests().size(), 2);
}

void AppControllerLoginTest::successfulLoginCreatesWindowWithoutMqttAndStoresOnlyUsername()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(sessionData())));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    QString factoryDisplayName;
    AppController controller(&auth, &sessions, &settings,
                             [&factoryDisplayName](const QString &displayName) {
        factoryDisplayName = displayName;
        auto *window = new QWidget;
        window->setObjectName(QStringLiteral("placeholderWindow"));
        return window;
    });
    controller.start();

    LoginWindow *login = controller.loginWindow();
    auto *username = login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
    auto *password = login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    username->setText(QStringLiteral("remember-me"));
    password->setText(QStringLiteral("login-secret"));
    QSignalSpy succeeded(&controller, &AppController::loginSucceeded);
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    QVERIFY(succeeded.wait());

    QCOMPARE(factoryDisplayName, QStringLiteral("测试操作员"));
    QVERIFY(controller.authenticatedWindow() != nullptr);
    QVERIFY(controller.authenticatedWindow()->isVisible());
    QVERIFY(login->isHidden());
    QVERIFY(password->text().isEmpty());
    settings.sync();

    QSettings reopened(settingsPath, QSettings::IniFormat);
    QCOMPARE(reopened.value(QStringLiteral("login/lastUsername")).toString(),
             QStringLiteral("remember-me"));
    const QStringList keys = reopened.allKeys();
    QCOMPARE(keys, QStringList {QStringLiteral("login/lastUsername")});
    const QByteArray bytes = [&settingsPath] {
        QFile file(settingsPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray {};
        }
        return file.readAll();
    }();
    QVERIFY(!bytes.contains("login-secret"));
    QVERIFY(!bytes.contains("test-access-secret"));
    QVERIFY(!bytes.contains("mqtt-secret"));
}

void AppControllerLoginTest::delayedRequestCannotBeSubmittedTwice()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(sessionData()), 150));
    HttpClient transport;
    SessionService sessions;
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    AppController controller(&auth, &sessions, &settings,
                             [](const QString &) { return new QWidget; });
    controller.start();
    LoginWindow *login = controller.loginWindow();
    login->setUsername(QStringLiteral("operator"));
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(
        QStringLiteral("secret"));
    auto *button = login->findChild<QPushButton *>(QStringLiteral("loginButton"));
    button->click();
    button->click();
    QTRY_COMPARE(server.requests().size(), 1);
    QTRY_VERIFY(controller.authenticatedWindow() != nullptr);
    QCOMPARE(server.requests().size(), 1);
}

void AppControllerLoginTest::destructionCancelsActiveLogin()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(sessionData()), 500));
    HttpClient transport;
    SessionService sessions;
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    bool factoryCalled = false;
    auto *controller = new AppController(&auth, &sessions, &settings,
                                         [&factoryCalled](const QString &) {
        factoryCalled = true;
        return new QWidget;
    });
    controller->start();
    LoginWindow *login = controller->loginWindow();
    login->setUsername(QStringLiteral("operator"));
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(
        QStringLiteral("secret"));
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    QTRY_COMPARE(server.requests().size(), 1);

    delete controller;
    QTest::qWait(600);

    QVERIFY(!factoryCalled);
    QVERIFY(!sessions.hasActiveSession());
}

QTEST_MAIN(AppControllerLoginTest)

#include "tst_app_controller_login.moc"
