#pragma once

#include <QMetaType>
#include <QString>

#include <utility>

enum class MqttErrorCode
{
    NetworkUnavailable,
    BrokerUnavailable,
    BadCredentials,
    NotAuthorized,
    ClientIdRejected,
    ProtocolUnsupported,
    SubscriptionRejected,
    TlsCertificateRejected,
    InsecureTransportRejected,
    ProtocolViolation,
    Unknown,
};

enum class MqttConnectivityState
{
    Disconnected,
    Reconnecting,
    Connected,
    PermanentError,
};

class MqttError final
{
public:
    MqttError(MqttErrorCode code, QString safeMessage, bool permanent)
        : code_(code)
        , safeMessage_(std::move(safeMessage))
        , permanent_(permanent)
    {
    }

    [[nodiscard]] MqttErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &safeMessage() const noexcept { return safeMessage_; }
    [[nodiscard]] bool isPermanent() const noexcept { return permanent_; }

private:
    MqttErrorCode code_;
    QString safeMessage_;
    bool permanent_;
};

Q_DECLARE_METATYPE(MqttErrorCode)
Q_DECLARE_METATYPE(MqttConnectivityState)
Q_DECLARE_METATYPE(MqttError)
