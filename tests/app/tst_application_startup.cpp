#include "app/Application.h"

#include "app/AppController.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "services/ImageLoadService.h"
#include "services/HistoryService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/CsvDownloadService.h"
#include "support/MockHttpServer.h"
#include "support/MockMqttClient.h"
#include "ui/widgets/ConnectionStatusIndicator.h"
#include "ui/widgets/NavigationSidebar.h"
#include "ui/pages/RealtimePage.h"
#include "ui/pages/HistoryPage.h"
#include "ui/windows/LoginWindow.h"
#include "ui/windows/MainWindow.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

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

MockHttpResponse imageResponse(int delayMs = 0)
{
    QImage image(QSize(40, 24), QImage::Format_ARGB32);
    image.fill(Qt::green);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        qFatal("Unable to create startup test image.");
    }
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("image/png")});
    response.chunks.append(std::move(bytes));
    response.initialDelayMs = delayMs;
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

QByteArray recognitionEvent(qint64 revision = 1,
                            const QString &status = QStringLiteral("PROCESSING"),
                            const QString &recognitionId = QStringLiteral("rec-live"))
{
    const bool completed = status != QStringLiteral("PROCESSING");
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("recognitionId"), recognitionId},
                             {QStringLiteral("revision"), revision},
                             {QStringLiteral("deviceId"), QStringLiteral("device-1")},
                             {QStringLiteral("status"), status},
                             {QStringLiteral("plateNumber"),
                              status == QStringLiteral("SUCCEEDED")
                                  ? QJsonValue(QStringLiteral("京A12345"))
                                  : QJsonValue(QJsonValue::Null)},
                             {QStringLiteral("errorCode"), QJsonValue::Null},
                             {QStringLiteral("errorMessage"), QJsonValue::Null},
                             {QStringLiteral("capturedAt"),
                              QStringLiteral("2026-08-12T10:00:00+08:00")},
                             {QStringLiteral("startedAt"),
                              QStringLiteral("2026-08-12T10:00:01+08:00")},
                             {QStringLiteral("completedAt"),
                              completed
                                  ? QJsonValue(QStringLiteral("2026-08-12T10:00:02+08:00"))
                                  : QJsonValue(QJsonValue::Null)},
                             {QStringLiteral("durationMs"),
                              completed ? QJsonValue(1000) : QJsonValue(QJsonValue::Null)},
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
    auto runtime = std::make_unique<Application>(
        [](const QString &) { qFatal("Unexpected startup error."); },
        AppController::WindowFactory {},
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
    QVERIFY(runtime->startFromConfigFile(writeConfig(directory.path(), bytes)));

    LoginWindow *login = runtime->controller()->loginWindow();
    login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->setText(
        QStringLiteral("operator"));
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(
        QStringLiteral("secret"));
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();

    QTRY_COMPARE(server.requests().size(), 2);
    auto *window = qobject_cast<MainWindow *>(runtime->controller()->authenticatedWindow());
    QVERIFY(window);
    QVERIFY(window->realtimePage());
    QVERIFY(window->historyPage());
    QCOMPARE(window->realtimePage()->store(), runtime->realtimeStore());
    QCOMPARE(window->realtimePage()->imageLoadService(), runtime->imageLoadService());
    QCOMPARE(window->historyPage()->historyService(), runtime->historyService());
    QCOMPARE(window->historyPage()->csvDownloadService(),
             runtime->csvDownloadService());
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
    server.enqueueResponse(imageResponse());
    mqttClient.reportMessage(mqttClient.configuredGeneration,
                             QStringLiteral("plate/events"),
                             recognitionEvent());
    QTRY_COMPARE(runtime->realtimeStore()->items().size(), 1);
    QCOMPARE(runtime->realtimeStore()->items().constFirst().recognitionId(),
             QStringLiteral("rec-live"));
    QCOMPARE(runtime->realtimeStore()->current()->recognitionId(),
             QStringLiteral("rec-live"));
    QVERIFY(runtime->reconnectReconciler());
    QTRY_COMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().at(2).target,
             QByteArrayLiteral("/api/v1/recognitions/rec-live/image"));
    auto *imageLabel = window->realtimePage()->findChild<QLabel *>(
        QStringLiteral("currentImageLabel"));
    QVERIFY(imageLabel);
    QTRY_VERIFY(!imageLabel->pixmap().isNull());

    server.enqueueResponse(jsonResponse(envelope(
        QJsonDocument::fromJson(recognitionEvent(2, QStringLiteral("SUCCEEDED"))).object())));
    mqttClient.reportDisconnected(mqttClient.configuredGeneration);
    mqttClient.reportConnected(mqttClient.configuredGeneration);
    mqttClient.reportSubscriptionSucceeded(mqttClient.configuredGeneration);
    QTRY_COMPARE(server.requests().size(), 4);
    QCOMPARE(server.requests().at(3).target,
             QByteArrayLiteral("/api/v1/recognitions/rec-live"));
    QTRY_COMPARE(runtime->realtimeStore()->current()->revision(), qint64(2));
    QCOMPARE(runtime->realtimeStore()->current()->status(), RecognitionStatus::Succeeded);
    QCOMPARE(window->realtimePage()
                 ->findChild<QLabel *>(QStringLiteral("currentStatusLabel"))
                 ->text(),
             QStringLiteral("已识别完成"));

    server.enqueueResponse(imageResponse(200));
    mqttClient.reportMessage(mqttClient.configuredGeneration,
                             QStringLiteral("plate/events"),
                             recognitionEvent(1,
                                              QStringLiteral("PROCESSING"),
                                              QStringLiteral("rec-destroy")));
    QTRY_COMPARE(server.requests().size(), 5);
    QCOMPARE(server.requests().at(4).target,
             QByteArrayLiteral("/api/v1/recognitions/rec-destroy/image"));
    QTRY_VERIFY(runtime->imageLoadService()->requestInFlight());

    QStringList destructionOrder;
    QPointer<MainWindow> guardedWindow(window);
    QPointer<ImageLoadService> guardedImages(runtime->imageLoadService());
    QPointer<HistoryService> guardedHistory(runtime->historyService());
    QPointer<CsvDownloadService> guardedCsv(runtime->csvDownloadService());
    connect(window, &QObject::destroyed, this,
            [&destructionOrder] { destructionOrder.append(QStringLiteral("window")); });
    connect(runtime->imageLoadService(), &QObject::destroyed, this,
            [&destructionOrder] { destructionOrder.append(QStringLiteral("images")); });
    connect(runtime->historyService(), &QObject::destroyed, this,
            [&destructionOrder] { destructionOrder.append(QStringLiteral("history")); });
    connect(runtime->csvDownloadService(), &QObject::destroyed, this,
            [&destructionOrder] { destructionOrder.append(QStringLiteral("csv")); });
    runtime.reset();
    QVERIFY(guardedWindow.isNull());
    QVERIFY(guardedImages.isNull());
    QVERIFY(guardedHistory.isNull());
    QVERIFY(guardedCsv.isNull());
    QCOMPARE(destructionOrder,
             QStringList({QStringLiteral("window"),
                          QStringLiteral("csv"),
                          QStringLiteral("history"),
                          QStringLiteral("images")}));
}

QTEST_MAIN(ApplicationStartupTest)

#include "tst_application_startup.moc"
