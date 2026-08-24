#include "app/AppController.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "integration/MockPlateServer.h"
#include "services/AccessListService.h"
#include "services/HeartbeatService.h"
#include "services/HistoryService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"
#include "ui/windows/MainWindow.h"

#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto recognitionId = "11111111-1111-4111-8111-111111111111";

void login(IntegrationTestRuntime &runtime)
{
    QSignalSpy succeeded(runtime.controller(), &AppController::loginSucceeded);
    runtime.submitLogin();
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 1000);
    QVERIFY(runtime.mainWindow());
    QTRY_VERIFY(runtime.mainWindow()->isVisible());
    QTest::qWait(20);
}

void verifyNoBusinessArtifactsOrSecrets(const QString &root)
{
    QDirIterator iterator(root, QDir::Files | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString suffix = QFileInfo(path).suffix().toLower();
        QVERIFY2(suffix != QStringLiteral("sqlite") && suffix != QStringLiteral("sqlite3")
                     && suffix != QStringLiteral("db") && suffix != QStringLiteral("png")
                     && suffix != QStringLiteral("jpg") && suffix != QStringLiteral("jpeg")
                     && suffix != QStringLiteral("csv"),
                 qPrintable(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();
        QVERIFY2(!content.contains("integration-access-token"), qPrintable(path));
        QVERIFY2(!content.contains("mqtt-password"), qPrintable(path));
        QVERIFY2(!content.contains("secret"), qPrintable(path));
    }
}
} // namespace

class IndependentConnectivityTest final : public QObject
{
    Q_OBJECT

private slots:
    void mqttAndHttpFailuresRemainIndependentAndNewLoginRecovers();
};

void IndependentConnectivityTest::mqttAndHttpFailuresRemainIndependentAndNewLoginRecovers()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    server.setHistoryPage(1,
                          {MockPlateServer::succeededSnapshot(
                              QString::fromLatin1(recognitionId))},
                          1);
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    {
        IntegrationTestRuntime runtime(&server, temporaryRoot.path());
        QVERIFY(runtime.start());
        login(runtime);

        QCOMPARE(runtime.mqttSubscriber()->state(), MqttConnectivityState::Reconnecting);
        server.completeMqttConnection();
        QCOMPARE(runtime.mqttSubscriber()->state(), MqttConnectivityState::Connected);
        QTRY_COMPARE(runtime.heartbeatService()->state(), HttpConnectivityState::Online);
        const QDateTime start = QDateTime::fromString(
            QStringLiteral("2026-08-01T00:00:00.000+08:00"), Qt::ISODateWithMs);
        const QDateTime end = QDateTime::fromString(
            QStringLiteral("2026-09-01T00:00:00.000+08:00"), Qt::ISODateWithMs);
        runtime.historyService()->search(start, end, QStringLiteral("device-001"));
        QTRY_COMPARE(runtime.historyService()->items().size(), 1);

        server.setHeartbeatMode(MockPlateServer::HeartbeatMode::Timeout);
        server.setHeartbeatDelay(200);
        QTRY_VERIFY_WITH_TIMEOUT(
            runtime.heartbeatService()->consecutiveFailureCount() >= 3, 1000);
        QCOMPARE(runtime.heartbeatService()->state(), HttpConnectivityState::Abnormal);
        QCOMPARE(runtime.mqttSubscriber()->state(), MqttConnectivityState::Connected);
        server.publishRecognition(MockPlateServer::succeededSnapshot(
            QString::fromLatin1(recognitionId)));
        QTRY_COMPARE(runtime.realtimeStore()->items().size(), 1);
        QCOMPARE(runtime.realtimeStore()->items().constFirst().status(),
                 RecognitionStatus::Succeeded);
        QVERIFY(runtime.accessListService()->search(AccessListType::White, {}));
        QTRY_VERIFY(!runtime.accessListService()->isQueryLoading());

        const int reconnectsBeforeFailure = server.mqttClient()->connectCount;
        server.failMqtt(MqttErrorCode::BadCredentials);
        QCOMPARE(runtime.mqttSubscriber()->state(), MqttConnectivityState::PermanentError);
        QTest::qWait(30);
        QCOMPARE(server.mqttClient()->connectCount, reconnectsBeforeFailure);
        runtime.historyService()->search(start, end, QStringLiteral("device-001"));
        QTRY_COMPARE(runtime.historyService()->items().size(), 1);

        emit runtime.mainWindow()->logoutRequested();
        QTRY_VERIFY(runtime.controller()->loginWindow());
        const int configurationsBeforeLogin = server.mqttClient()->configureCount;
        login(runtime);
        QTRY_COMPARE(server.mqttClient()->configureCount, configurationsBeforeLogin + 1);
        QCOMPARE(runtime.mqttSubscriber()->state(), MqttConnectivityState::Reconnecting);
        QVERIFY(runtime.sessionService()->hasActiveSession());
    }
    verifyNoBusinessArtifactsOrSecrets(temporaryRoot.path());
}

QTEST_MAIN(IndependentConnectivityTest)

#include "tst_independent_connectivity.moc"
