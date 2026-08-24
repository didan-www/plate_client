#pragma once

#include "infrastructure/http/HeartbeatApiClient.h"
#include "services/HttpConnectivityState.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>

class HttpClient;
class SessionService;
class SessionSummary;

class HeartbeatService final : public QObject
{
    Q_OBJECT

public:
    HeartbeatService(HeartbeatApiClient *heartbeatApiClient,
                     HttpClient *httpClient,
                     SessionService *sessionService,
                     QObject *parent = nullptr);
    HeartbeatService(HeartbeatApiClient *heartbeatApiClient,
                     HttpClient *httpClient,
                     SessionService *sessionService,
                     std::chrono::milliseconds heartbeatInterval,
                     QObject *parent = nullptr);

    [[nodiscard]] HttpConnectivityState state() const noexcept;
    [[nodiscard]] int consecutiveFailureCount() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool requestInFlight() const noexcept;

    void stopScheduling();
    void stop();

signals:
    void stateChanged(HttpConnectivityState state);
    void heartbeatFailed(const HeartbeatError &error);

private:
    void activate(const SessionSummary &summary);
    void sendHeartbeat();
    void observeHttpResponse(int statusCode, quint64 sessionGeneration);
    void handleHeartbeatFailure(const HeartbeatError &error, quint64 sessionGeneration);
    void setState(HttpConnectivityState state, bool forceNotification = false);

    HeartbeatApiClient *heartbeatApiClient_;
    SessionService *sessionService_;
    QTimer heartbeatTimer_;
    QPointer<HeartbeatRequestHandle> activeRequest_;
    HttpConnectivityState state_ = HttpConnectivityState::Detecting;
    quint64 sessionGeneration_ = 0;
    int consecutiveFailureCount_ = 0;
    bool running_ = false;
};
