#include "domain/SessionContext.h"
#include "infrastructure/http/HeartbeatApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "services/HeartbeatService.h"
#include "services/ReconnectReconciler.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "support/MockMqttClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <chrono>
#include <memory>
#include <optional>

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

RecognitionSnapshot domainSnapshot(const QString &id,
                                   qint64 revision,
                                   RecognitionStatus status)
{
    const bool completed = status != RecognitionStatus::Processing;
    return RecognitionSnapshot(
        1,
        id,
        revision,
        QStringLiteral("device-1"),
        status,
        status == RecognitionStatus::Succeeded
            ? std::optional<QString>(QStringLiteral("京A12345")) : std::nullopt,
        status == RecognitionStatus::Failed
            ? std::optional<QString>(QStringLiteral("MODEL_ERROR")) : std::nullopt,
        status == RecognitionStatus::Failed
            ? std::optional<QString>(QStringLiteral("识别失败")) : std::nullopt,
        QDateTime::fromString(QStringLiteral("2026-08-12T10:00:00+08:00"), Qt::ISODate),
        QDateTime::fromString(QStringLiteral("2026-08-12T10:00:01+08:00"), Qt::ISODate),
        completed
            ? std::optional<QDateTime>(QDateTime::fromString(
                  QStringLiteral("2026-08-12T10:00:02+08:00"), Qt::ISODate))
            : std::nullopt,
        completed ? std::optional<qint64>(1000) : std::nullopt);
}

QJsonObject jsonSnapshot(const QString &id,
                         qint64 revision,
                         RecognitionStatus status)
{
    QString statusText = QStringLiteral("PROCESSING");
    if (status == RecognitionStatus::Succeeded) {
        statusText = QStringLiteral("SUCCEEDED");
    } else if (status == RecognitionStatus::Failed) {
        statusText = QStringLiteral("FAILED");
    }
    const bool completed = status != RecognitionStatus::Processing;
    return QJsonObject {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("recognitionId"), id},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
        {QStringLiteral("status"), statusText},
        {QStringLiteral("plateNumber"),
         status == RecognitionStatus::Succeeded ? QJsonValue(QStringLiteral("京A12345"))
                                                : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("errorCode"),
         status == RecognitionStatus::Failed ? QJsonValue(QStringLiteral("MODEL_ERROR"))
                                             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("errorMessage"),
         status == RecognitionStatus::Failed ? QJsonValue(QStringLiteral("识别失败"))
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
                             {QStringLiteral("requestId"), QStringLiteral("request-reconcile")},
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

class ReconcilerRig final
{
public:
    ReconcilerRig()
    {
        if (!server.listen()) {
            qFatal("Mock HTTP server failed to listen.");
        }
        http = std::make_unique<HttpClient>([this](const RequestContext &context) {
            return sessions.isCurrent(context);
        });
        heartbeatApi = std::make_unique<HeartbeatApiClient>(server.url(),
                                                            QUuid::createUuid(),
                                                            QStringLiteral("1.0.0"),
                                                            http.get(),
                                                            &sessions);
        recognitionApi = std::make_unique<RecognitionApiClient>(server.url(),
                                                                 http.get(),
                                                                 &sessions);
        heartbeat = std::make_unique<HeartbeatService>(heartbeatApi.get(),
                                                       http.get(),
                                                       &sessions,
                                                       std::chrono::hours(1));
        mqtt = std::make_unique<MqttRecognitionSubscriber>(QStringLiteral("client-id"),
                                                            true,
                                                            &sessions,
                                                            &mqttClient,
                                                            std::chrono::milliseconds(1));
        store = std::make_unique<RealtimeRecognitionStore>(&sessions);
        reconciler = std::make_unique<ReconnectReconciler>(recognitionApi.get(),
                                                            mqtt.get(),
                                                            heartbeat.get(),
                                                            store.get(),
                                                            &sessions);
    }

    bool start()
    {
        server.enqueueResponse(jsonResponse(200, envelope(QJsonValue::Null)));
        return sessions.establishSession(session());
    }

    void connectInitial()
    {
        const quint64 generation = sessions.currentGeneration();
        mqttClient.reportConnected(generation);
        mqttClient.reportSubscriptionSucceeded(generation);
    }

    void reconnect()
    {
        const quint64 generation = sessions.currentGeneration();
        mqttClient.reportDisconnected(generation);
        mqttClient.reportConnected(generation);
        mqttClient.reportSubscriptionSucceeded(generation);
    }

    MockHttpServer server;
    SessionService sessions;
    MockMqttClient mqttClient;
    std::unique_ptr<HttpClient> http;
    std::unique_ptr<HeartbeatApiClient> heartbeatApi;
    std::unique_ptr<RecognitionApiClient> recognitionApi;
    std::unique_ptr<HeartbeatService> heartbeat;
    std::unique_ptr<MqttRecognitionSubscriber> mqtt;
    std::unique_ptr<RealtimeRecognitionStore> store;
    std::unique_ptr<ReconnectReconciler> reconciler;
};
} // namespace

class ReconnectReconcilerTest final : public QObject
{
    Q_OBJECT

private slots:
    void initialConnectionAndNonProcessingCurrentDoNotQuery();
    void reconnectMergesOnlyHigherRevisionForBoundCurrent();
    void failureDoesNotLoopAndHttpRecoveryCompensatesOnce();
    void activeCalibrationRecoveryDoesNotStartSecondQuery();
    void newReconnectCycleCancelsPreviousRequest();
    void currentChangeAndSessionInvalidationCancelOldRequests();
    void destructionCancelsAndDropsLateResponse();
};

void ReconnectReconcilerTest::initialConnectionAndNonProcessingCurrentDoNotQuery()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-initial"), 1,
                                            RecognitionStatus::Processing)));
    rig.connectInitial();
    QTest::qWait(30);
    QCOMPARE(rig.server.requests().size(), 1);

    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-initial"), 2,
                                            RecognitionStatus::Succeeded)));
    rig.reconnect();
    QTest::qWait(30);
    QCOMPARE(rig.server.requests().size(), 1);
    QVERIFY(!rig.reconciler->requestInFlight());
}

void ReconnectReconcilerTest::reconnectMergesOnlyHigherRevisionForBoundCurrent()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    rig.connectInitial();
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-current"), 3,
                                            RecognitionStatus::Processing)));

    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-current"), 2,
                                   RecognitionStatus::Processing))));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 2);
    QTRY_VERIFY(!rig.reconciler->requestInFlight());
    QCOMPARE(rig.store->current()->revision(), qint64(3));

    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-current"), 3,
                                   RecognitionStatus::Processing))));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 3);
    QTRY_VERIFY(!rig.reconciler->requestInFlight());
    QCOMPARE(rig.store->current()->revision(), qint64(3));

    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-current"), 4,
                                   RecognitionStatus::Succeeded))));
    rig.reconnect();
    QTRY_COMPARE(rig.store->current()->revision(), qint64(4));
    QCOMPARE(rig.store->current()->status(), RecognitionStatus::Succeeded);
    QCOMPARE(rig.store->items().size(), 1);
    QCOMPARE(rig.server.requests().constLast().target,
             QByteArrayLiteral("/api/v1/recognitions/rec-current"));
}

void ReconnectReconcilerTest::failureDoesNotLoopAndHttpRecoveryCompensatesOnce()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    rig.connectInitial();
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-recovery"), 1,
                                            RecognitionStatus::Processing)));
    rig.server.enqueueResponse(jsonResponse(
        503, envelope(QJsonValue::Null, false, QStringLiteral("TEMPORARY"))));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 2);
    QTRY_VERIFY(!rig.reconciler->requestInFlight());
    QTest::qWait(30);
    QCOMPARE(rig.server.requests().size(), 2);
    QCOMPARE(rig.store->current()->revision(), qint64(1));

    rig.heartbeat->stateChanged(HttpConnectivityState::Abnormal);
    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-recovery"), 2,
                                   RecognitionStatus::Succeeded))));
    rig.heartbeat->stateChanged(HttpConnectivityState::Online);
    QTRY_COMPARE(rig.server.requests().size(), 3);
    QTRY_COMPARE(rig.store->current()->revision(), qint64(2));

    rig.heartbeat->stateChanged(HttpConnectivityState::Abnormal);
    rig.heartbeat->stateChanged(HttpConnectivityState::Online);
    QTest::qWait(30);
    QCOMPARE(rig.server.requests().size(), 3);
}

void ReconnectReconcilerTest::activeCalibrationRecoveryDoesNotStartSecondQuery()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    rig.connectInitial();
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-active"), 1,
                                            RecognitionStatus::Processing)));
    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-active"), 2,
                                   RecognitionStatus::Processing)), 100));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 2);
    QVERIFY(rig.reconciler->requestInFlight());

    rig.heartbeat->stateChanged(HttpConnectivityState::Abnormal);
    rig.heartbeat->stateChanged(HttpConnectivityState::Online);
    QTest::qWait(20);
    QCOMPARE(rig.server.requests().size(), 2);
    QTRY_COMPARE(rig.store->current()->revision(), qint64(2));
    QTRY_VERIFY(!rig.reconciler->requestInFlight());

    rig.heartbeat->stateChanged(HttpConnectivityState::Abnormal);
    rig.heartbeat->stateChanged(HttpConnectivityState::Online);
    QTest::qWait(30);
    QCOMPARE(rig.server.requests().size(), 2);
}

void ReconnectReconcilerTest::newReconnectCycleCancelsPreviousRequest()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    rig.connectInitial();
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-cycle"), 1,
                                            RecognitionStatus::Processing)));
    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-cycle"), 2,
                                   RecognitionStatus::Succeeded)), 150));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 2);
    QVERIFY(rig.reconciler->requestInFlight());

    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-cycle"), 3,
                                   RecognitionStatus::Succeeded))));
    const quint64 previousCycle = rig.reconciler->cycleGeneration();
    rig.reconnect();
    QVERIFY(rig.reconciler->cycleGeneration() > previousCycle);
    QTRY_COMPARE(rig.server.requests().size(), 3);
    QTRY_COMPARE(rig.store->current()->revision(), qint64(3));
    QCOMPARE(rig.store->current()->status(), RecognitionStatus::Succeeded);
    QTest::qWait(170);
    QCOMPARE(rig.store->current()->revision(), qint64(3));
}

void ReconnectReconcilerTest::currentChangeAndSessionInvalidationCancelOldRequests()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    rig.connectInitial();
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-old"), 1,
                                            RecognitionStatus::Processing)));
    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-old"), 2,
                                   RecognitionStatus::Succeeded)), 100));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 2);
    QVERIFY(rig.reconciler->requestInFlight());

    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-new"), 1,
                                            RecognitionStatus::Processing)));
    QTRY_VERIFY(!rig.reconciler->requestInFlight());
    QTest::qWait(120);
    QCOMPARE(rig.store->current()->recognitionId(), QStringLiteral("rec-new"));
    QCOMPARE(rig.store->items().at(1).revision(), qint64(1));

    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-new"), 2,
                                   RecognitionStatus::Succeeded)), 100));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 3);
    QVERIFY(rig.reconciler->requestInFlight());
    rig.sessions.clear();
    QTRY_VERIFY(!rig.reconciler->requestInFlight());
    QTest::qWait(120);
    QVERIFY(rig.store->items().isEmpty());
    QVERIFY(!rig.store->current().has_value());
}

void ReconnectReconcilerTest::destructionCancelsAndDropsLateResponse()
{
    ReconcilerRig rig;
    QVERIFY(rig.start());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    rig.connectInitial();
    const quint64 generation = rig.sessions.currentGeneration();
    QVERIFY(rig.store->apply(generation,
                             domainSnapshot(QStringLiteral("rec-destroy"), 1,
                                            RecognitionStatus::Processing)));
    rig.server.enqueueResponse(jsonResponse(
        200, envelope(jsonSnapshot(QStringLiteral("rec-destroy"), 2,
                                   RecognitionStatus::Succeeded)), 100));
    rig.reconnect();
    QTRY_COMPARE(rig.server.requests().size(), 2);
    QVERIFY(rig.reconciler->requestInFlight());
    rig.reconciler.reset();
    QTest::qWait(120);
    QCOMPARE(rig.store->current()->revision(), qint64(1));
    QCOMPARE(rig.store->current()->status(), RecognitionStatus::Processing);
}

QTEST_MAIN(ReconnectReconcilerTest)

#include "tst_reconnect_reconciler.moc"
