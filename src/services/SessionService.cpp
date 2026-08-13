#include "services/SessionService.h"

#include "domain/RequestContext.h"
#include "infrastructure/http/AuthApiClient.h"

#include <QPointer>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace {
QDateTime systemNow()
{
    return QDateTime::currentDateTime();
}
} // namespace

SessionService::SessionService(QObject *parent)
    : SessionService(systemNow, parent)
{
}

SessionService::SessionService(Clock clock, QObject *parent)
    : QObject(parent)
    , clock_(std::move(clock))
{
    expiryTimer_.setSingleShot(true);
    connect(&expiryTimer_, &QTimer::timeout, this, [this] {
        if (session_) {
            expireIfCurrent(session_->generation());
        }
    });
}

bool SessionService::establishSession(SessionContext session)
{
    if (session.generation() != 0 || !session.expiresAt().isValid()
        || session.expiresAt() <= clock_()) {
        return false;
    }
    if (session_) {
        invalidate(SessionInvalidationReason::Replaced);
    }
    ++generationCounter_;
    if (generationCounter_ == 0) {
        ++generationCounter_;
    }
    session.generation_ = generationCounter_;
    session_.emplace(std::move(session));
    scheduleExpiry();
    emit sessionActivated(*summary());
    return true;
}

bool SessionService::hasActiveSession() const noexcept
{
    return session_.has_value();
}

std::optional<SessionSummary> SessionService::summary() const
{
    if (!session_) {
        return std::nullopt;
    }
    return SessionSummary(session_->displayName(), session_->expiresAt(), session_->generation());
}

quint64 SessionService::currentGeneration() const noexcept
{
    return session_ ? session_->generation() : generationCounter_;
}

bool SessionService::isCurrent(const RequestContext &context) const noexcept
{
    if (context.sessionGeneration() == 0) {
        return !session_ && context.operation() == QStringLiteral("auth.login");
    }
    return session_ && context.sessionGeneration() == session_->generation();
}

bool SessionService::withAccessToken(quint64 generation,
                                     const AccessTokenConsumer &consumer) const
{
    if (!session_ || generation != session_->generation() || !consumer) {
        return false;
    }
    consumer(session_->accessToken());
    return true;
}

bool SessionService::withMqttCredentials(quint64 generation,
                                         const MqttCredentialsConsumer &consumer) const
{
    if (!session_ || generation != session_->generation() || !consumer) {
        return false;
    }
    consumer(session_->mqttHost(),
             session_->mqttPort(),
             session_->mqttTls(),
             session_->mqttUsername(),
             session_->mqttPassword(),
             session_->mqttEventTopic());
    return true;
}

void SessionService::handleUnauthorized(quint64 sessionGeneration)
{
    if (session_ && sessionGeneration == session_->generation()) {
        invalidate(SessionInvalidationReason::Unauthorized);
    }
}

LogoutRequestHandle *SessionService::logout(AuthApiClient *authApiClient)
{
    if (!session_) {
        return nullptr;
    }
    LogoutRequestHandle *handle = nullptr;
    if (authApiClient) {
        handle = authApiClient->logout(session_->accessToken(), session_->generation());
    }
    invalidate(SessionInvalidationReason::LoggedOut);
    return handle;
}

void SessionService::clear(SessionInvalidationReason reason)
{
    if (session_) {
        invalidate(reason);
    }
}

void SessionService::scheduleExpiry()
{
    if (!session_) {
        return;
    }
    const qint64 remaining = clock_().msecsTo(session_->expiresAt());
    if (remaining <= 0) {
        expireIfCurrent(session_->generation());
        return;
    }
    const qint64 interval = std::min<qint64>(remaining, std::numeric_limits<int>::max());
    expiryTimer_.start(static_cast<int>(interval));
}

void SessionService::expireIfCurrent(quint64 generation)
{
    if (!session_ || session_->generation() != generation) {
        return;
    }
    if (session_->expiresAt() > clock_()) {
        scheduleExpiry();
        return;
    }
    invalidate(SessionInvalidationReason::Expired);
}

void SessionService::invalidate(SessionInvalidationReason reason)
{
    if (!session_) {
        return;
    }
    expiryTimer_.stop();
    const quint64 invalidatedGeneration = session_->generation();
    session_.reset();
    ++generationCounter_;
    if (generationCounter_ == 0) {
        ++generationCounter_;
    }
    emit sessionInvalidated(reason, invalidatedGeneration);
}
