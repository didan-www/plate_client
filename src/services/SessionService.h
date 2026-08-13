#pragma once

#include "domain/SessionContext.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

class AuthApiClient;
class LogoutRequestHandle;
class RequestContext;

class SessionSummary final
{
public:
    SessionSummary(QString displayName, QDateTime expiresAt, quint64 generation)
        : displayName_(std::move(displayName))
        , expiresAt_(std::move(expiresAt))
        , generation_(generation)
    {
    }

    [[nodiscard]] const QString &displayName() const noexcept { return displayName_; }
    [[nodiscard]] const QDateTime &expiresAt() const noexcept { return expiresAt_; }
    [[nodiscard]] quint64 generation() const noexcept { return generation_; }

private:
    QString displayName_;
    QDateTime expiresAt_;
    quint64 generation_;
};

enum class SessionInvalidationReason
{
    Replaced,
    LoggedOut,
    Expired,
    Unauthorized,
};

class SessionService final : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;
    using AccessTokenConsumer = std::function<void(const QString &)>;
    using MqttCredentialsConsumer =
        std::function<void(const QString &, quint16, bool, const QString &, const QString &,
                           const QString &)>;

    explicit SessionService(QObject *parent = nullptr);
    SessionService(Clock clock, QObject *parent = nullptr);

    [[nodiscard]] bool establishSession(SessionContext session);
    [[nodiscard]] bool hasActiveSession() const noexcept;
    [[nodiscard]] std::optional<SessionSummary> summary() const;
    [[nodiscard]] quint64 currentGeneration() const noexcept;
    [[nodiscard]] bool isCurrent(const RequestContext &context) const noexcept;
    [[nodiscard]] bool withAccessToken(quint64 generation,
                                       const AccessTokenConsumer &consumer) const;
    [[nodiscard]] bool withMqttCredentials(quint64 generation,
                                           const MqttCredentialsConsumer &consumer) const;

    void handleUnauthorized(quint64 sessionGeneration);
    [[nodiscard]] LogoutRequestHandle *logout(AuthApiClient *authApiClient);
    void clear(SessionInvalidationReason reason = SessionInvalidationReason::LoggedOut);

signals:
    void sessionActivated(const SessionSummary &summary);
    void sessionInvalidated(SessionInvalidationReason reason, quint64 generation);

private:
    void scheduleExpiry();
    void expireIfCurrent(quint64 generation);
    void invalidate(SessionInvalidationReason reason);

    Clock clock_;
    QTimer expiryTimer_;
    std::optional<SessionContext> session_;
    quint64 generationCounter_ = 0;
};

Q_DECLARE_METATYPE(SessionSummary)
Q_DECLARE_METATYPE(SessionInvalidationReason)
