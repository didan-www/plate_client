#include "app/Application.h"

#include "app/AppController.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "services/RealtimeRecognitionStore.h"
#include "support/MockHttpServer.h"
#include "support/MockMqttClient.h"
#include "ui/widgets/ConnectionStatusIndicator.h"
#include "ui/widgets/NavigationSidebar.h"
#include "ui/windows/LoginWindow.h"
#include "ui/windows/MainWindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString writeConfig(const QString &directory, const QByteArray &bytes)
{
    const QString filePath = directory + QStringLiteral("/client.json");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        qFatal("Unable to write startup test config.");
    }
    return filePath;
}

QByteArray envelope(const QJsonValue &data)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("startup-request")},
                             {QStringLiteral("data"), data},
                         })
        .toJson(QJsonDocument::Compact);
}

MockHttpResponse jsonResponse(QByteArray body)
{
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(std::move(body));
    return response;
}

QJsonObject sessionData()
{
    const QString expiry = QDateTime::currentDateTime()
                               .addSecs(3600)
                               .toOffsetFromUtc(8 * 3600)
                               .toString(Qt::ISODate);
    return QJsonObject {
        {QStringLiteral("displayName"), QStringLiteral("测试操作员")},
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

QByteArray recognitionEvent()
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("recognitionId"), QStringLiteral("rec-live")},
                             {QStringLiteral("revision"), 1},
                             {QStringLiteral("deviceId"), QStringLiteral("device-1")},
                             {QStringLiteral("status"), QStringLiteral("PROCESSING")},
                             {QStringLiteral("plateNumber"), QJsonValue::Null},
                             {QStringLiteral("errorCode"), QJsonValue::Null},
                             {QStringLiteral("errorMessage"), QJsonValue::Null},
                             {QStringLiteral("capturedAt"),
                              QStringLiteral("2026-08-12T10:00:00+08:00")},
                             {QStringLiteral("startedAt"),
                              QStringLiteral("2026-08-12T10:00:01+08:00")},
                             {QStringLiteral("completedAt"), QJsonValue::Null},
                             {QStringLiteral("durationMs"), QJsonValue::Null},
                         })
        .toJson(QJsonDocument::Compact);
}
} // namespace

class ApplicationStartupTest final : public QObject
{
    Q_OBJECT

private slots:
    void invalidConfigBlocksBeforeRuntimeInitialization();
    void validConfigCreatesLoginFlow();
    void successfulLoginStartsHeartbeatAndUpdatesMainWindow();
};

void ApplicationStartupTest::invalidConfigBlocksBeforeRuntimeInitialization()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString config = writeConfig(directory.path(), QByteArrayLiteral("{not-json"));
    QString presentedError;
    bool factoryCalled = false;
    Application runtime([&presentedError](const QString &message) { presentedError = message; },
                        [&factoryCalled](const QString &) {
                            factoryCalled = true;
                            return new QWidget;
                        });

    QVERIFY(!runtime.startFromConfigFile(config));
    QVERIFY(!runtime.runtimeInitialized());
    QVERIFY(runtime.controller() == nullptr);
    QVERIFY(!presentedError.isEmpty());
    QVERIFY(!factoryCalled);
}

void ApplicationStartupTest::validConfigCreatesLoginFlow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = QJsonDocument(QJsonObject {
                                              {QStringLiteral("clientId"),
                                               QStringLiteral("12345678-1234-4abc-8def-1234567890ab")},
                                              {QStringLiteral("loginBaseUrl"),
                                               QStringLiteral("https://example.test")},
                                              {QStringLiteral("allowInsecureTransport"), false},
                                          })
                                 .toJson(QJsonDocument::Compact);
    const QString config = writeConfig(directory.path(), bytes);
    QString presentedError;
    Application runtime([&presentedError](const QString &message) { presentedError = message; });

    QVERIFY(runtime.startFromConfigFile(config));
    QVERIFY(runtime.runtimeInitialized());
    QVERIFY(runtime.controller() != nullptr);
    QVERIFY(runtime.controller()->loginWindow() != nullptr);
    QVERIFY(runtime.controller()->loginWindow()->isVisible());
    QVERIFY(presentedError.isEmpty());
}

void ApplicationStartupTest::successfulLoginStartsHeartbeatAndUpdatesMainWindow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(envelope(sessionData())));
    server.enqueueResponse(jsonResponse(envelope(QJsonValue::Null)));
    const QByteArray bytes = QJsonDocument(QJsonObject {
                                               {QStringLiteral("clientId"),
                                                QStringLiteral("12345678-1234-4abc-8def-1234567890ab")},
                                               {QStringLiteral("loginBaseUrl"),
                                                server.url().toString()},
                                               {QStringLiteral("allowInsecureTransport"), true},
                                           })
                                  .toJson(QJsonDocument::Compact);
    MockMqttClient mqttClient;
    Application runtime(
        [](const QString &) { qFatal("Unexpected startup error."); },
        {},
        [&mqttClient](const QString &clientId,
                      bool allowInsecureTransport,
                      SessionService *sessions) {
            return std::make_unique<MqttRecognitionSubscriber>(
                clientId,
                allowInsecureTransport,
                sessions,
                &mqttClient,
                std::chrono::milliseconds(1));
        });
    QVERIFY(runtime.startFromConfigFile(writeConfig(directory.path(), bytes)));

    LoginWindow *login = runtime.controller()->loginWindow();
    login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->setText(
        QStringLiteral("operator"));
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(
        QStringLiteral("secret"));
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();

    QTRY_COMPARE(server.requests().size(), 2);
    auto *window = qobject_cast<MainWindow *>(runtime.controller()->authenticatedWindow());
    QVERIFY(window);
    QCOMPARE(server.requests().at(1).target,
             QByteArrayLiteral("/api/v1/clients/heartbeat"));
    QTRY_COMPARE(window->sidebar()->httpStatusIndicator()->statusText(),
                 QStringLiteral("在线"));
    QCOMPARE(window->sidebar()->mqttStatusIndicator()->statusText(),
             QStringLiteral("重连中"));
    mqttClient.reportConnected(mqttClient.configuredGeneration);
    QCOMPARE(window->sidebar()->mqttStatusIndicator()->statusText(),
             QStringLiteral("重连中"));
    mqttClient.reportSubscriptionSucceeded(mqttClient.configuredGeneration);
    QCOMPARE(window->sidebar()->mqttStatusIndicator()->statusText(),
             QStringLiteral("已连接"));
    mqttClient.reportMessage(mqttClient.configuredGeneration,
                             QStringLiteral("plate/events"),
                             recognitionEvent());
    QTRY_COMPARE(runtime.realtimeStore()->items().size(), 1);
    QCOMPARE(runtime.realtimeStore()->items().constFirst().recognitionId(),
             QStringLiteral("rec-live"));
    QCOMPARE(runtime.realtimeStore()->current()->recognitionId(),
             QStringLiteral("rec-live"));
}

QTEST_MAIN(ApplicationStartupTest)

#include "tst_application_startup.moc"
