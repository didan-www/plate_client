#pragma once

#include "services/HttpConnectivityState.h"

#include <QObject>
#include <QPointer>
#include <QString>

class HeartbeatService;
class MqttRecognitionSubscriber;
class RecognitionApiClient;
class RecognitionDetailRequestHandle;
class RealtimeRecognitionStore;
class SessionService;
class SessionSummary;
enum class MqttConnectivityState;
enum class SessionInvalidationReason;

class ReconnectReconciler final : public QObject
{
    Q_OBJECT

public:
    ReconnectReconciler(RecognitionApiClient *recognitionApiClient,
                        MqttRecognitionSubscriber *mqttSubscriber,
                        HeartbeatService *heartbeatService,
                        RealtimeRecognitionStore *realtimeStore,
                        SessionService *sessionService,
                        QObject *parent = nullptr);
    ~ReconnectReconciler() override;

    [[nodiscard]] bool requestInFlight() const noexcept;
    [[nodiscard]] quint64 cycleGeneration() const noexcept;
    [[nodiscard]] QString cycleRecognitionId() const;

private:
    void activate(const SessionSummary &summary);
    void invalidate(SessionInvalidationReason reason, quint64 generation);
    void observeMqttState(MqttConnectivityState state, const QString &safeReason);
    void observeHttpState(HttpConnectivityState state);
    void observeCurrent();
    void beginReconnectCycle();
    void sendCalibration();
    void cancelActiveRequest();
    void resetSession(quint64 generation);

    RecognitionApiClient *recognitionApiClient_;
    RealtimeRecognitionStore *realtimeStore_;
    SessionService *sessionService_;
    QPointer<RecognitionDetailRequestHandle> activeRequest_;
    HttpConnectivityState previousHttpState_ = HttpConnectivityState::Detecting;
    MqttConnectivityState previousMqttState_;
    quint64 sessionGeneration_ = 0;
    quint64 cycleGeneration_ = 0;
    quint64 activeRequestGeneration_ = 0;
    QString cycleRecognitionId_;
    bool hasConnectedOnce_ = false;
    bool compensationAvailable_ = false;
};
