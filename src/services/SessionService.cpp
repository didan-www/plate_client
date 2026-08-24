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

SessionTerminationContext::SessionTerminationContext(QString accessToken,
                                                     quint64 generation)
    : accessToken_(std::move(accessToken))
    , generation_(generation)
{
}

SessionTerminationContext::SessionTerminationContext(SessionTerminationContext &&other) noexcept
    : accessToken_(std::move(other.accessToken_))
    , generation_(other.generation_)
{
    other.clearSensitiveData();
    other.generation_ = 0;
}

SessionTerminationContext &SessionTerminationContext::operator=(
    SessionTerminationContext &&other) noexcept
{
    if (this != &other) {
        clearSensitiveData();
        accessToken_ = std::move(other.accessToken_);
        generation_ = other.generation_;
        other.clearSensitiveData();
        other.generation_ = 0;
    }
    return *this;
}

SessionTerminationContext::~SessionTerminationContext()
{
    clearSensitiveData();
}

quint64 SessionTerminationContext::generation() const noexcept
{
    return generation_;
}

QString SessionTerminationContext::takeAccessToken() noexcept
{
    QString result = std::move(accessToken_);
    accessToken_.clear();
    return result;
}

void SessionTerminationContext::clearSensitiveData() noexcept
{
    accessToken_.fill(QChar::Null);
    accessToken_.clear();
}

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
        if (state_ == State::Active && session_) {
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
    if (state_ == State::LoggingOut) {
        return false;
    }
    if (state_ == State::Active) {
        clear(SessionInvalidationReason::Replaced);
    }
    advanceGeneration();
    session.generation_ = generationCounter_;
    session_.emplace(std::move(session));
    state_ = State::Active;
    scheduleExpiry();
    emit sessionActivated(*summary());
    return true;
}

bool SessionService::hasActiveSession() const noexcept
{
    return state_ == State::Active && session_.has_value();
}

std::optional<SessionSummary> SessionService::summary() const
{
    if (state_ != State::Active || !session_) {
        return std::nullopt;
    }
    return SessionSummary(session_->displayName(), session_->expiresAt(), session_->generation());
}

quint64 SessionService::currentGeneration() const noexcept
{
    return state_ == State::Active && session_ ? session_->generation() : generationCounter_;
}

bool SessionService::isCurrent(const RequestContext &context) const noexcept
{
    if (context.sessionGeneration() == 0) {
        return state_ == State::Inactive && !session_
               && context.operation() == QStringLiteral("auth.login");
    }
    return state_ == State::Active && session_
           && context.sessionGeneration() == session_->generation();
}

bool SessionService::withAccessToken(quint64 generation,
                                     const AccessTokenConsumer &consumer) const
{
    if (state_ != State::Active || !session_
        || generation != session_->generation() || !consumer) {
        return false;
    }
    consumer(session_->accessToken());
    return true;
}

bool SessionService::withMqttCredentials(quint64 generation,
                                         const MqttCredentialsConsumer &consumer) const
{
    if (state_ != State::Active || !session_
        || generation != session_->generation() || !consumer) {
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
    if (state_ == State::Active && session_
        && sessionGeneration == session_->generation()) {
        requestTermination(SessionInvalidationReason::Unauthorized);
    }
}

std::optional<SessionTerminationContext>
SessionService::beginTermination(SessionInvalidationReason reason)
{
    if (state_ != State::Active || !session_) {
        return std::nullopt;
    }

    expiryTimer_.stop();
    state_ = State::LoggingOut;
    terminationRequestPending_ = false;
    terminationReason_ = reason;
    terminatingGeneration_ = session_->generation();
    QString accessToken = std::move(session_->accessToken_);
    advanceGeneration();
    return SessionTerminationContext(std::move(accessToken), terminatingGeneration_);
}

void SessionService::completeTermination(quint64 generation)
{
    if (state_ != State::LoggingOut || !session_ || !terminationReason_
        || generation == 0 || generation != terminatingGeneration_) {
        return;
    }

    const SessionInvalidationReason reason = *terminationReason_;
    session_.reset();
    state_ = State::Inactive;
    terminationReason_.reset();
    terminatingGeneration_ = 0;
    emit sessionInvalidated(reason, generation);
}

LogoutRequestHandle *SessionService::logout(AuthApiClient *authApiClient)
{
    auto termination = beginTermination(SessionInvalidationReason::LoggedOut);
    if (!termination) {
        return nullptr;
    }
    LogoutRequestHandle *handle = nullptr;
    if (authApiClient) {
        handle = authApiClient->logout(termination->takeAccessToken(),
                                       termination->generation());
    }
    completeTermination(termination->generation());
    return handle;
}

void SessionService::clear(SessionInvalidationReason reason)
{
    auto termination = beginTermination(reason);
    if (termination) {
        completeTermination(termination->generation());
    }
}

void SessionService::scheduleExpiry()
{
    if (state_ != State::Active || !session_) {
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
    if (state_ != State::Active || !session_ || session_->generation() != generation) {
        return;
    }
    if (session_->expiresAt() > clock_()) {
        scheduleExpiry();
        return;
    }
    requestTermination(SessionInvalidationReason::Expired);
}

void SessionService::requestTermination(SessionInvalidationReason reason)
{
    if (state_ != State::Active || !session_ || terminationRequestPending_) {
        return;
    }
    terminationRequestPending_ = true;
    const quint64 generation = session_->generation();
    emit terminationRequested(reason, generation);

    // Standalone services retain the historical immediate invalidation behavior.
    if (state_ == State::Active && terminationRequestPending_) {
        auto termination = beginTermination(reason);
        if (termination) {
            completeTermination(termination->generation());
        }
    }
}

void SessionService::advanceGeneration()
{
    ++generationCounter_;
    if (generationCounter_ == 0) {
        ++generationCounter_;
    }
}
