#include "app/AppController.h"
#include "domain/ImageLoadResult.h"
#include "domain/RecognitionSnapshot.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "integration/MockPlateServer.h"
#include "services/HeartbeatService.h"
#include "services/ImageLoadService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"
#include "ui/models/RecognitionTableModel.h"
#include "ui/pages/RealtimePage.h"
#include "ui/windows/MainWindow.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto firstId = "11111111-1111-4111-8111-111111111111";
constexpr auto secondId = "22222222-2222-4222-8222-222222222222";

void login(IntegrationTestRuntime &runtime)
{
    QSignalSpy succeeded(runtime.controller(), &AppController::loginSucceeded);
    runtime.submitLogin();
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 1000);
    QVERIFY(runtime.mainWindow());
    QTRY_VERIFY(runtime.mainWindow()->isVisible());
    QTest::qWait(20);
}
} // namespace

class RealtimeFlowTest final : public QObject
{
    Q_OBJECT

private slots:
    void loginHeartbeatMqttAndRecognitionFlow();
    void reconnectCalibratesCurrentProcessingRecord();
};

void RealtimeFlowTest::loginHeartbeatMqttAndRecognitionFlow()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    login(runtime);

    QTRY_COMPARE(runtime.heartbeatService()->state(), HttpConnectivityState::Online);
    QCOMPARE(server.mqttClient()->configureCount, 1);
    QCOMPARE(server.mqttClient()->connectCount, 1);
    QCOMPARE(server.mqttClient()->lastConfiguration.protocolVersion, 4);
    QVERIFY(server.mqttClient()->lastConfiguration.cleanSession);
    QCOMPARE(server.mqttClient()->lastConfiguration.clientId,
             QStringLiteral("12345678-1234-4abc-8def-1234567890ab"));
    QVERIFY(runtime.sessionService()->hasActiveSession());
    QCOMPARE(runtime.mqttSubscriber()->sessionGeneration(),
             server.mqttClient()->configuredGeneration);

    server.completeMqttConnection();
    QVERIFY(runtime.sessionService()->hasActiveSession());
    QCOMPARE(runtime.mqttSubscriber()->sessionGeneration(),
             server.mqttClient()->configuredGeneration);
    QCOMPARE(runtime.mqttSubscriber()->state(), MqttConnectivityState::Connected);
    QCOMPARE(server.mqttClient()->subscriptions.size(), 1);
    QCOMPARE(server.mqttClient()->subscriptions.constFirst().topic,
             QStringLiteral("plate/management/recognition-events"));
    QCOMPARE(server.mqttClient()->subscriptions.constFirst().qos, quint8(1));

    server.setImage(QString::fromLatin1(firstId), MockPlateServer::pngImage(Qt::green));
    QSignalSpy imageResults(runtime.imageLoadService(), &ImageLoadService::resultReady);
    server.publishRecognition(MockPlateServer::processingSnapshot(
        QString::fromLatin1(firstId)));
    QTRY_COMPARE(runtime.realtimeStore()->items().size(), 1);
    QCOMPARE(runtime.realtimeStore()->current()->status(), RecognitionStatus::Processing);
    QTRY_COMPARE_WITH_TIMEOUT(imageResults.count(), 1, 1000);
    const auto imageResult = *static_cast<const ImageLoadResult *>(
        imageResults.constFirst().at(0).constData());
    QVERIFY(imageResult.isSuccess());

    auto *page = runtime.mainWindow()->realtimePage();
    QVERIFY(page);
    auto *imageLabel = page->findChild<QLabel *>(QStringLiteral("currentImageLabel"));
    QVERIFY(imageLabel);
    QTRY_VERIFY(!imageLabel->pixmap().isNull());

    server.publishRecognition(MockPlateServer::succeededSnapshot(
        QString::fromLatin1(firstId)));
    QTRY_COMPARE(runtime.realtimeStore()->current()->status(), RecognitionStatus::Succeeded);
    QCOMPARE(runtime.realtimeStore()->current()->plateNumber(),
             std::optional<QString>(QStringLiteral("京A12345")));
    QCOMPARE(page->tableModel()->rowCount(), 1);

    server.setImage(QString::fromLatin1(secondId), MockPlateServer::pngImage(Qt::blue));
    server.publishRecognition(MockPlateServer::processingSnapshot(
        QString::fromLatin1(secondId)));
    QTRY_COMPARE(runtime.realtimeStore()->items().size(), 2);
    server.publishRecognition(MockPlateServer::failedSnapshot(
        QString::fromLatin1(secondId)));
    QTRY_COMPARE(runtime.realtimeStore()->current()->status(), RecognitionStatus::Failed);
    QCOMPARE(runtime.realtimeStore()->current()->errorCode(),
             std::optional<QString>(QStringLiteral("MODEL_FAILED")));
    QCOMPARE(page->tableModel()->rowCount(), 2);

    server.publishRecognition(MockPlateServer::failedSnapshot(
        QString::fromLatin1(secondId)));
    QTest::qWait(20);
    QCOMPARE(runtime.realtimeStore()->items().size(), 2);
    QCOMPARE(page->tableModel()->rowCount(), 2);
}

void RealtimeFlowTest::reconnectCalibratesCurrentProcessingRecord()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    QTemporaryDir temporaryRoot;
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    login(runtime);
    QTRY_COMPARE(runtime.heartbeatService()->state(), HttpConnectivityState::Online);
    server.completeMqttConnection();

    server.setImage(QString::fromLatin1(firstId), MockPlateServer::pngImage());
    server.publishRecognition(MockPlateServer::processingSnapshot(
        QString::fromLatin1(firstId)));
    QTRY_COMPARE(runtime.realtimeStore()->current()->status(), RecognitionStatus::Processing);

    const int previousConnects = server.mqttClient()->connectCount;
    server.disconnectMqtt();
    server.setRecognition(MockPlateServer::succeededSnapshot(
        QString::fromLatin1(firstId), QStringLiteral("沪B67890"),
        QStringLiteral("device-001"), 2));
    QTRY_VERIFY(server.mqttClient()->connectCount > previousConnects);
    server.completeMqttConnection();

    QTRY_COMPARE_WITH_TIMEOUT(runtime.realtimeStore()->current()->revision(), qint64(2), 1000);
    QCOMPARE(runtime.realtimeStore()->current()->status(), RecognitionStatus::Succeeded);
    QCOMPARE(runtime.realtimeStore()->current()->plateNumber(),
             std::optional<QString>(QStringLiteral("沪B67890")));
    QCOMPARE(server.requestCount(QByteArrayLiteral("GET"),
                                 QByteArrayLiteral("/api/v1/recognitions/")),
             2);
}

QTEST_MAIN(RealtimeFlowTest)

#include "tst_realtime_flow.moc"
