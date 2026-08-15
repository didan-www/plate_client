#include "services/HeartbeatService.h"

#include "infrastructure/http/HttpClient.h"
#include "services/SessionService.h"

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <limits>

using namespace std::chrono_literals;

HeartbeatService::HeartbeatService(HeartbeatApiClient *heartbeatApiClient,
                                   HttpClient *httpClient,
                                   SessionService *sessionService,
                                   QObject *parent)
    : HeartbeatService(heartbeatApiClient, httpClient, sessionService, 30s, parent)
{
}

HeartbeatService::HeartbeatService(HeartbeatApiClient *heartbeatApiClient,
                                   HttpClient *httpClient,
                                   SessionService *sessionService,
                                   std::chrono::milliseconds heartbeatInterval,
                                   QObject *parent)
    : QObject(parent)
    , heartbeatApiClient_(heartbeatApiClient)
    , sessionService_(sessionService)
{
    Q_ASSERT(heartbeatApiClient_);
    Q_ASSERT(httpClient);
    Q_ASSERT(sessionService_);

    const auto clamped = std::clamp<qint64>(heartbeatInterval.count(),
                                            1,
                                            std::numeric_limits<int>::max());
    heartbeatTimer_.setInterval(static_cast<int>(clamped));
    connect(&heartbeatTimer_, &QTimer::timeout, this, &HeartbeatService::sendHeartbeat);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &HeartbeatService::activate);
    connect(sessionService_, &SessionService::sessionInvalidated, this,
            [this](SessionInvalidationReason, quint64 generation) {
                if (generation == sessionGeneration_) {
                    stop();
                }
            });
    connect(httpClient, &HttpClient::responseReceived,
            this, &HeartbeatService::observeHttpResponse);

    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

HttpConnectivityState HeartbeatService::state() const noexcept
{
    return state_;
}

int HeartbeatService::consecutiveFailureCount() const noexcept
{
    return consecutiveFailureCount_;
}

bool HeartbeatService::isRunning() const noexcept
{
    return running_;
}

bool HeartbeatService::requestInFlight() const noexcept
{
    return !activeRequest_.isNull();
}

void HeartbeatService::stop()
{
    if (!running_ && !activeRequest_) {
        return;
    }
    running_ = false;
    heartbeatTimer_.stop();
    sessionGeneration_ = 0;
    consecutiveFailureCount_ = 0;
    QPointer<HeartbeatRequestHandle> request = activeRequest_;
    activeRequest_.clear();
    if (request) {
        request->cancel();
    }
}

void HeartbeatService::activate(const SessionSummary &summary)
{
    stop();
    running_ = true;
    sessionGeneration_ = summary.generation();
    consecutiveFailureCount_ = 0;
    setState(HttpConnectivityState::Detecting, true);
    heartbeatTimer_.start();
    const quint64 generation = sessionGeneration_;
    QTimer::singleShot(0, this, [this, generation] {
        if (running_ && sessionGeneration_ == generation) {
            sendHeartbeat();
        }
    });
}

void HeartbeatService::sendHeartbeat()
{
    if (!running_ || activeRequest_ || sessionGeneration_ == 0) {
        return;
    }
    const quint64 generation = sessionGeneration_;
    HeartbeatRequestHandle *request = heartbeatApiClient_->send(generation);
    activeRequest_ = request;
    connect(request, &HeartbeatRequestHandle::failed, this,
            [this, generation](const HeartbeatError &error) {
                handleHeartbeatFailure(error, generation);
            });
    connect(request, &HeartbeatRequestHandle::finished, this,
            [this, request] {
                if (activeRequest_ == request) {
                    activeRequest_.clear();
                }
            });
}

void HeartbeatService::observeHttpResponse(int statusCode, quint64 sessionGeneration)
{
    Q_UNUSED(statusCode)
    if (!running_ || sessionGeneration == 0 || sessionGeneration != sessionGeneration_) {
        return;
    }
    consecutiveFailureCount_ = 0;
    setState(HttpConnectivityState::Online);
}

void HeartbeatService::handleHeartbeatFailure(const HeartbeatError &error,
                                              quint64 sessionGeneration)
{
    if (!running_ || sessionGeneration != sessionGeneration_) {
        return;
    }
    emit heartbeatFailed(error);
    if (error.code() != HeartbeatErrorCode::NetworkError
        && error.code() != HeartbeatErrorCode::TlsError
        && error.code() != HeartbeatErrorCode::Timeout) {
        return;
    }
    ++consecutiveFailureCount_;
    if (consecutiveFailureCount_ >= 3) {
        setState(HttpConnectivityState::Abnormal);
    }
}

void HeartbeatService::setState(HttpConnectivityState state, bool forceNotification)
{
    if (!forceNotification && state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}
