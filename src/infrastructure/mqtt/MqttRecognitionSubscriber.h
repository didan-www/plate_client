#pragma once

#include "infrastructure/mqtt/MqttError.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>

class SessionService;
class SessionSummary;

struct MqttClientConfiguration final
{
    QString host;
    quint16 port = 0;
    bool tls = false;
    QString clientId;
    QString username;
    QString password;
    int protocolVersion = 4;
    bool cleanSession = true;
};

class MqttClientInterface : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~MqttClientInterface() override = default;

    virtual void configure(quint64 sessionGeneration,
                           const MqttClientConfiguration &configuration) = 0;
    virtual void connectToHost() = 0;
    virtual void disconnectFromHost() = 0;
    virtual bool subscribe(const QString &topic, quint8 qos) = 0;
    virtual void clearCredentials() = 0;

signals:
    void connected(quint64 sessionGeneration);
    void disconnected(quint64 sessionGeneration);
    void failed(quint64 sessionGeneration, MqttErrorCode code);
    void subscriptionSucceeded(quint64 sessionGeneration);
    void subscriptionFailed(quint64 sessionGeneration);
};

class MqttRecognitionSubscriber final : public QObject
{
    Q_OBJECT

public:
    MqttRecognitionSubscriber(QString clientId,
                              bool allowInsecureTransport,
                              SessionService *sessionService,
                              QObject *parent = nullptr);
    MqttRecognitionSubscriber(QString clientId,
                              bool allowInsecureTransport,
                              SessionService *sessionService,
                              MqttClientInterface *mqttClient,
                              std::chrono::milliseconds reconnectSecond,
                              QObject *parent = nullptr);

    [[nodiscard]] MqttConnectivityState state() const noexcept;
    [[nodiscard]] QString statusReason() const;
    [[nodiscard]] quint64 sessionGeneration() const noexcept;
    [[nodiscard]] int reconnectAttempt() const noexcept;
    [[nodiscard]] int scheduledReconnectSeconds() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    void stop();

signals:
    void stateChanged(MqttConnectivityState state, const QString &safeReason);
    void reconnectScheduled(int delaySeconds);
    void permanentError(const MqttError &error);

private:
    void activate(const SessionSummary &summary);
    void connectCurrentSession();
    void handleConnected(quint64 generation);
    void handleDisconnected(quint64 generation);
    void handleFailure(quint64 generation, MqttErrorCode code);
    void handleSubscriptionSucceeded(quint64 generation);
    void handleSubscriptionFailed(quint64 generation);
    void scheduleReconnect(MqttErrorCode code);
    void enterPermanentError(MqttErrorCode code);
    void setState(MqttConnectivityState state,
                  const QString &safeReason = {},
                  bool forceNotification = false);
    [[nodiscard]] static MqttError describeError(MqttErrorCode code);

    QString clientId_;
    bool allowInsecureTransport_;
    SessionService *sessionService_;
    MqttClientInterface *mqttClient_;
    QTimer reconnectTimer_;
    std::chrono::milliseconds reconnectSecond_;
    QString eventTopic_;
    QString statusReason_;
    MqttConnectivityState state_ = MqttConnectivityState::Disconnected;
    quint64 sessionGeneration_ = 0;
    int reconnectAttempt_ = 0;
    int scheduledReconnectSeconds_ = 0;
    bool running_ = false;
    bool subscriptionPending_ = false;
};
