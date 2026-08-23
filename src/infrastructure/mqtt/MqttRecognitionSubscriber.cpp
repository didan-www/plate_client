#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"

#include "services/SessionService.h"

#include <QAbstractSocket>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QMqttTopicFilter>
#include <QPointer>
#include <QSslError>
#include <QSslSocket>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

using namespace std::chrono_literals;

namespace {
constexpr int mqtt311ProtocolVersion = 4;
constexpr quint8 managementQos = 1;

class QtMqttClientAdapter final : public MqttClientInterface
{
public:
    explicit QtMqttClientAdapter(QObject *parent = nullptr)
        : MqttClientInterface(parent)
    {
    }

    ~QtMqttClientAdapter() override
    {
        resetClient();
    }

    void configure(quint64 sessionGeneration,
                   const MqttClientConfiguration &configuration) override
    {
        resetClient();
        generation_ = sessionGeneration;
        tls_ = configuration.tls;
        client_ = std::make_unique<QMqttClient>();
        client_->setHostname(configuration.host);
        client_->setPort(configuration.port);
        client_->setClientId(configuration.clientId);
        client_->setUsername(configuration.username);
        client_->setPassword(configuration.password);
        client_->setProtocolVersion(QMqttClient::MQTT_3_1_1);
        client_->setCleanSession(true);

        const quint64 generation = generation_;
        connect(client_.get(), &QMqttClient::connected, this, [this, generation] {
            if (generation == generation_) {
                emit connected(generation);
            }
        });
        connect(client_.get(), &QMqttClient::disconnected, this, [this, generation] {
            if (generation == generation_) {
                emit disconnected(generation);
            }
        });
        connect(client_.get(), &QMqttClient::errorChanged, this,
                [this, generation](QMqttClient::ClientError error) {
                    if (generation != generation_ || error == QMqttClient::NoError) {
                        return;
                    }
                    emit failed(generation, mapClientError(error));
                });
        connect(client_.get(), &QMqttClient::messageReceived, this,
                [this, generation](const QByteArray &payload,
                                   const QMqttTopicName &topic) {
                    if (generation == generation_) {
                        emit messageReceived(generation, topic.name(), payload);
                    }
                });

        if (tls_) {
            tlsSocket_ = std::make_unique<QSslSocket>();
            connect(tlsSocket_.get(), &QSslSocket::sslErrors, this,
                    [this, generation](const QList<QSslError> &) {
                        if (generation != generation_) {
                            return;
                        }
                        emit failed(generation, MqttErrorCode::TlsCertificateRejected);
                        tlsSocket_->abort();
                    });
            connect(tlsSocket_.get(), &QAbstractSocket::errorOccurred, this,
                    [this, generation](QAbstractSocket::SocketError error) {
                        if (generation != generation_) {
                            return;
                        }
                        const auto code = error == QAbstractSocket::SslHandshakeFailedError
                                              ? MqttErrorCode::TlsCertificateRejected
                                              : MqttErrorCode::NetworkUnavailable;
                        emit failed(generation, code);
                    });
            client_->setTransport(tlsSocket_.get(), QMqttClient::SecureSocket);
        }
    }

    void connectToHost() override
    {
        if (client_) {
            client_->connectToHost();
        }
    }

    void disconnectFromHost() override
    {
        if (client_ && client_->state() != QMqttClient::Disconnected) {
            client_->disconnectFromHost();
        }
    }

    bool subscribe(const QString &topic, quint8 qos) override
    {
        if (!client_ || generation_ == 0) {
            return false;
        }
        QMqttSubscription *subscription = client_->subscribe(QMqttTopicFilter(topic), qos);
        if (!subscription) {
            return false;
        }
        subscription_ = subscription;
        const quint64 generation = generation_;
        connect(subscription, &QMqttSubscription::stateChanged, this,
                [this, generation, subscription](QMqttSubscription::SubscriptionState state) {
                    if (generation != generation_ || subscription_ != subscription) {
                        return;
                    }
                    if (state == QMqttSubscription::Subscribed) {
                        emit subscriptionSucceeded(generation);
                    } else if (state == QMqttSubscription::Error) {
                        emit subscriptionFailed(generation);
                    }
                });
        return true;
    }

    void clearCredentials() override
    {
        if (client_) {
            client_->setUsername({});
            client_->setPassword({});
        }
    }

private:
    static MqttErrorCode mapClientError(QMqttClient::ClientError error)
    {
        switch (error) {
        case QMqttClient::InvalidProtocolVersion:
            return MqttErrorCode::ProtocolUnsupported;
        case QMqttClient::IdRejected:
            return MqttErrorCode::ClientIdRejected;
        case QMqttClient::ServerUnavailable:
            return MqttErrorCode::BrokerUnavailable;
        case QMqttClient::BadUsernameOrPassword:
            return MqttErrorCode::BadCredentials;
        case QMqttClient::NotAuthorized:
            return MqttErrorCode::NotAuthorized;
        case QMqttClient::TransportInvalid:
            return MqttErrorCode::NetworkUnavailable;
        case QMqttClient::ProtocolViolation:
        case QMqttClient::Mqtt5SpecificError:
            return MqttErrorCode::ProtocolViolation;
        case QMqttClient::UnknownError:
            return MqttErrorCode::Unknown;
        case QMqttClient::NoError:
            break;
        }
        return MqttErrorCode::Unknown;
    }

    void resetClient()
    {
        subscription_.clear();
        if (client_) {
            client_->setUsername({});
            client_->setPassword({});
        }
        client_.reset();
        tlsSocket_.reset();
    }

    std::unique_ptr<QMqttClient> client_;
    std::unique_ptr<QSslSocket> tlsSocket_;
    QPointer<QMqttSubscription> subscription_;
    quint64 generation_ = 0;
    bool tls_ = false;
};

int reconnectDelay(int attempt)
{
    if (attempt >= 6) {
        return 60;
    }
    return 1 << attempt;
}
} // namespace

MqttRecognitionSubscriber::MqttRecognitionSubscriber(QString clientId,
                                                     bool allowInsecureTransport,
                                                     SessionService *sessionService,
                                                     QObject *parent)
    : MqttRecognitionSubscriber(std::move(clientId),
                                allowInsecureTransport,
                                sessionService,
                                new QtMqttClientAdapter,
                                1s,
                                parent)
{
    mqttClient_->setParent(this);
}

MqttRecognitionSubscriber::MqttRecognitionSubscriber(
    QString clientId,
    bool allowInsecureTransport,
    SessionService *sessionService,
    MqttClientInterface *mqttClient,
    std::chrono::milliseconds reconnectSecond,
    QObject *parent)
    : QObject(parent)
    , clientId_(std::move(clientId))
    , allowInsecureTransport_(allowInsecureTransport)
    , sessionService_(sessionService)
    , mqttClient_(mqttClient)
    , reconnectSecond_(std::max(reconnectSecond, 1ms))
{
    Q_ASSERT(sessionService_);
    Q_ASSERT(mqttClient_);
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this,
            &MqttRecognitionSubscriber::connectCurrentSession);
    connect(mqttClient_, &MqttClientInterface::connected,
            this, &MqttRecognitionSubscriber::handleConnected);
    connect(mqttClient_, &MqttClientInterface::disconnected,
            this, &MqttRecognitionSubscriber::handleDisconnected);
    connect(mqttClient_, &MqttClientInterface::failed,
            this, &MqttRecognitionSubscriber::handleFailure);
    connect(mqttClient_, &MqttClientInterface::subscriptionSucceeded,
            this, &MqttRecognitionSubscriber::handleSubscriptionSucceeded);
    connect(mqttClient_, &MqttClientInterface::subscriptionFailed,
            this, &MqttRecognitionSubscriber::handleSubscriptionFailed);
    connect(mqttClient_, &MqttClientInterface::messageReceived,
            this, &MqttRecognitionSubscriber::handleMessage);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &MqttRecognitionSubscriber::activate);
    connect(sessionService_, &SessionService::sessionInvalidated, this,
            [this](SessionInvalidationReason, quint64 generation) {
                if (generation == sessionGeneration_) {
                    stop();
                }
            });

    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

MqttConnectivityState MqttRecognitionSubscriber::state() const noexcept
{
    return state_;
}

QString MqttRecognitionSubscriber::statusReason() const
{
    return statusReason_;
}

quint64 MqttRecognitionSubscriber::sessionGeneration() const noexcept
{
    return sessionGeneration_;
}

int MqttRecognitionSubscriber::reconnectAttempt() const noexcept
{
    return reconnectAttempt_;
}

int MqttRecognitionSubscriber::scheduledReconnectSeconds() const noexcept
{
    return scheduledReconnectSeconds_;
}

bool MqttRecognitionSubscriber::isRunning() const noexcept
{
    return running_;
}

void MqttRecognitionSubscriber::stop()
{
    const bool changed = running_ || state_ != MqttConnectivityState::Disconnected;
    running_ = false;
    reconnectTimer_.stop();
    subscriptionPending_ = false;
    sessionGeneration_ = 0;
    reconnectAttempt_ = 0;
    scheduledReconnectSeconds_ = 0;
    eventTopic_.clear();
    mqttClient_->disconnectFromHost();
    mqttClient_->clearCredentials();
    setState(MqttConnectivityState::Disconnected, {}, changed);
}

void MqttRecognitionSubscriber::activate(const SessionSummary &summary)
{
    stop();
    sessionGeneration_ = summary.generation();
    running_ = true;
    reconnectAttempt_ = 0;
    setState(MqttConnectivityState::Reconnecting, {}, true);
    connectCurrentSession();
}

void MqttRecognitionSubscriber::connectCurrentSession()
{
    if (!running_ || sessionGeneration_ == 0
        || state_ == MqttConnectivityState::PermanentError) {
        return;
    }
    reconnectTimer_.stop();
    scheduledReconnectSeconds_ = 0;
    subscriptionPending_ = false;
    const quint64 generation = sessionGeneration_;
    bool configured = false;
    const bool credentialsAvailable = sessionService_->withMqttCredentials(
        generation,
        [this, generation, &configured](const QString &host,
                                        quint16 port,
                                        bool tls,
                                        const QString &username,
                                        const QString &password,
                                        const QString &eventTopic) {
            if (!tls && !allowInsecureTransport_) {
                return;
            }
            eventTopic_ = eventTopic;
            MqttClientConfiguration configuration;
            configuration.host = host;
            configuration.port = port;
            configuration.tls = tls;
            configuration.clientId = clientId_;
            configuration.username = username;
            configuration.password = password;
            configuration.protocolVersion = mqtt311ProtocolVersion;
            configuration.cleanSession = true;
            mqttClient_->configure(generation, configuration);
            configured = true;
        });
    if (!credentialsAvailable) {
        stop();
        return;
    }
    if (!configured) {
        enterPermanentError(MqttErrorCode::InsecureTransportRejected);
        return;
    }
    mqttClient_->connectToHost();
}

void MqttRecognitionSubscriber::handleConnected(quint64 generation)
{
    if (!running_ || generation != sessionGeneration_
        || state_ == MqttConnectivityState::PermanentError) {
        return;
    }
    mqttClient_->clearCredentials();
    subscriptionPending_ = true;
    if (!mqttClient_->subscribe(eventTopic_, managementQos)) {
        handleSubscriptionFailed(generation);
    }
}

void MqttRecognitionSubscriber::handleDisconnected(quint64 generation)
{
    if (!running_ || generation != sessionGeneration_
        || state_ == MqttConnectivityState::PermanentError) {
        return;
    }
    mqttClient_->clearCredentials();
    subscriptionPending_ = false;
    scheduleReconnect(MqttErrorCode::NetworkUnavailable);
}

void MqttRecognitionSubscriber::handleFailure(quint64 generation, MqttErrorCode code)
{
    if (!running_ || generation != sessionGeneration_
        || state_ == MqttConnectivityState::PermanentError) {
        return;
    }
    mqttClient_->clearCredentials();
    const MqttError error = describeError(code);
    if (error.isPermanent()) {
        enterPermanentError(code);
    } else {
        scheduleReconnect(code);
    }
}

void MqttRecognitionSubscriber::handleSubscriptionSucceeded(quint64 generation)
{
    if (!running_ || generation != sessionGeneration_ || !subscriptionPending_
        || state_ == MqttConnectivityState::PermanentError) {
        return;
    }
    subscriptionPending_ = false;
    reconnectAttempt_ = 0;
    scheduledReconnectSeconds_ = 0;
    setState(MqttConnectivityState::Connected);
}

void MqttRecognitionSubscriber::handleSubscriptionFailed(quint64 generation)
{
    if (!running_ || generation != sessionGeneration_
        || state_ == MqttConnectivityState::PermanentError) {
        return;
    }
    subscriptionPending_ = false;
    enterPermanentError(MqttErrorCode::SubscriptionRejected);
}

void MqttRecognitionSubscriber::handleMessage(quint64 generation,
                                              const QString &topic,
                                              const QByteArray &payload)
{
    if (!running_ || generation != sessionGeneration_
        || state_ != MqttConnectivityState::Connected || subscriptionPending_
        || topic != eventTopic_) {
        return;
    }
    emit eventPayloadReceived(generation, payload);
}

void MqttRecognitionSubscriber::scheduleReconnect(MqttErrorCode code)
{
    Q_UNUSED(code)
    if (reconnectTimer_.isActive()) {
        return;
    }
    subscriptionPending_ = false;
    setState(MqttConnectivityState::Reconnecting);
    scheduledReconnectSeconds_ = reconnectDelay(reconnectAttempt_);
    if (reconnectAttempt_ < 6) {
        ++reconnectAttempt_;
    }
    const qint64 interval = std::min<qint64>(
        static_cast<qint64>(scheduledReconnectSeconds_) * reconnectSecond_.count(),
        std::numeric_limits<int>::max());
    reconnectTimer_.start(static_cast<int>(std::max<qint64>(interval, 1)));
    emit reconnectScheduled(scheduledReconnectSeconds_);
}

void MqttRecognitionSubscriber::enterPermanentError(MqttErrorCode code)
{
    reconnectTimer_.stop();
    subscriptionPending_ = false;
    scheduledReconnectSeconds_ = 0;
    const MqttError error = describeError(code);
    mqttClient_->disconnectFromHost();
    mqttClient_->clearCredentials();
    setState(MqttConnectivityState::PermanentError, error.safeMessage());
    emit permanentError(error);
}

void MqttRecognitionSubscriber::setState(MqttConnectivityState state,
                                         const QString &safeReason,
                                         bool forceNotification)
{
    if (!forceNotification && state_ == state && statusReason_ == safeReason) {
        return;
    }
    state_ = state;
    statusReason_ = safeReason;
    emit stateChanged(state_, statusReason_);
}

MqttError MqttRecognitionSubscriber::describeError(MqttErrorCode code)
{
    switch (code) {
    case MqttErrorCode::NetworkUnavailable:
        return MqttError(code, QStringLiteral("网络暂时不可用"), false);
    case MqttErrorCode::BrokerUnavailable:
        return MqttError(code, QStringLiteral("Broker 暂时不可用"), false);
    case MqttErrorCode::BadCredentials:
        return MqttError(code, QStringLiteral("认证信息错误"), true);
    case MqttErrorCode::NotAuthorized:
        return MqttError(code, QStringLiteral("配置或授权错误"), true);
    case MqttErrorCode::ClientIdRejected:
        return MqttError(code, QStringLiteral("客户端标识被拒绝"), true);
    case MqttErrorCode::ProtocolUnsupported:
        return MqttError(code, QStringLiteral("协议不受支持"), true);
    case MqttErrorCode::SubscriptionRejected:
        return MqttError(code, QStringLiteral("订阅失败"), true);
    case MqttErrorCode::TlsCertificateRejected:
        return MqttError(code, QStringLiteral("证书校验失败"), true);
    case MqttErrorCode::InsecureTransportRejected:
        return MqttError(code, QStringLiteral("不允许明文连接"), true);
    case MqttErrorCode::ProtocolViolation:
        return MqttError(code, QStringLiteral("协议错误"), true);
    case MqttErrorCode::Unknown:
        return MqttError(code, QStringLiteral("配置或连接错误"), true);
    }
    return MqttError(MqttErrorCode::Unknown, QStringLiteral("配置或连接错误"), true);
}
