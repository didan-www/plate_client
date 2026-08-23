#include "services/ReconnectReconciler.h"

#include "infrastructure/http/RecognitionApiClient.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "services/HeartbeatService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"

ReconnectReconciler::ReconnectReconciler(RecognitionApiClient *recognitionApiClient,
                                         MqttRecognitionSubscriber *mqttSubscriber,
                                         HeartbeatService *heartbeatService,
                                         RealtimeRecognitionStore *realtimeStore,
                                         SessionService *sessionService,
                                         QObject *parent)
    : QObject(parent)
    , recognitionApiClient_(recognitionApiClient)
    , realtimeStore_(realtimeStore)
    , sessionService_(sessionService)
    , previousHttpState_(heartbeatService ? heartbeatService->state()
                                          : HttpConnectivityState::Detecting)
    , previousMqttState_(mqttSubscriber ? mqttSubscriber->state()
                                        : MqttConnectivityState::Disconnected)
{
    Q_ASSERT(recognitionApiClient_);
    Q_ASSERT(mqttSubscriber);
    Q_ASSERT(heartbeatService);
    Q_ASSERT(realtimeStore_);
    Q_ASSERT(sessionService_);

    connect(mqttSubscriber, &MqttRecognitionSubscriber::stateChanged,
            this, &ReconnectReconciler::observeMqttState);
    connect(heartbeatService, &HeartbeatService::stateChanged,
            this, &ReconnectReconciler::observeHttpState);
    connect(realtimeStore_, &RealtimeRecognitionStore::currentChanged,
            this, &ReconnectReconciler::observeCurrent);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &ReconnectReconciler::activate);
    connect(sessionService_, &SessionService::sessionInvalidated,
            this, &ReconnectReconciler::invalidate);

    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

ReconnectReconciler::~ReconnectReconciler()
{
    cancelActiveRequest();
}

bool ReconnectReconciler::requestInFlight() const noexcept
{
    return !activeRequest_.isNull();
}

quint64 ReconnectReconciler::cycleGeneration() const noexcept
{
    return cycleGeneration_;
}

QString ReconnectReconciler::cycleRecognitionId() const
{
    return cycleRecognitionId_;
}

void ReconnectReconciler::activate(const SessionSummary &summary)
{
    resetSession(summary.generation());
}

void ReconnectReconciler::invalidate(SessionInvalidationReason reason, quint64 generation)
{
    Q_UNUSED(reason)
    if (generation == sessionGeneration_) {
        resetSession(0);
    }
}

void ReconnectReconciler::observeMqttState(MqttConnectivityState state,
                                           const QString &safeReason)
{
    Q_UNUSED(safeReason)
    const bool becameConnected = state == MqttConnectivityState::Connected
                                 && previousMqttState_ != MqttConnectivityState::Connected;
    previousMqttState_ = state;
    if (!becameConnected || sessionGeneration_ == 0) {
        return;
    }
    if (!hasConnectedOnce_) {
        hasConnectedOnce_ = true;
        return;
    }
    beginReconnectCycle();
}

void ReconnectReconciler::observeHttpState(HttpConnectivityState state)
{
    const bool recovered = previousHttpState_ == HttpConnectivityState::Abnormal
                           && state == HttpConnectivityState::Online;
    previousHttpState_ = state;
    if (!recovered || !compensationAvailable_ || cycleRecognitionId_.isEmpty()) {
        return;
    }

    compensationAvailable_ = false;
    if (activeRequest_) {
        return;
    }
    const auto &current = realtimeStore_->current();
    if (!current || current->status() != RecognitionStatus::Processing
        || current->recognitionId() != cycleRecognitionId_) {
        cycleRecognitionId_.clear();
        return;
    }
    sendCalibration();
}

void ReconnectReconciler::observeCurrent()
{
    if (cycleRecognitionId_.isEmpty()) {
        return;
    }
    const auto &current = realtimeStore_->current();
    if (!current || current->status() != RecognitionStatus::Processing
        || current->recognitionId() != cycleRecognitionId_) {
        cancelActiveRequest();
        cycleRecognitionId_.clear();
        compensationAvailable_ = false;
    }
}

void ReconnectReconciler::beginReconnectCycle()
{
    cancelActiveRequest();
    ++cycleGeneration_;
    if (cycleGeneration_ == 0) {
        ++cycleGeneration_;
    }
    cycleRecognitionId_.clear();
    compensationAvailable_ = false;

    const auto &current = realtimeStore_->current();
    if (!current || current->status() != RecognitionStatus::Processing) {
        return;
    }
    cycleRecognitionId_ = current->recognitionId();
    compensationAvailable_ = true;
    sendCalibration();
}

void ReconnectReconciler::sendCalibration()
{
    if (activeRequest_ || sessionGeneration_ == 0 || cycleRecognitionId_.isEmpty()) {
        return;
    }
    const quint64 cycle = cycleGeneration_;
    RecognitionDetailRequestHandle *request = recognitionApiClient_->getDetail(
        cycleRecognitionId_, sessionGeneration_);
    activeRequest_ = request;
    activeRequestGeneration_ = request->context().requestGeneration();
    const quint64 requestGeneration = activeRequestGeneration_;
    const QString recognitionId = cycleRecognitionId_;

    connect(request, &RecognitionDetailRequestHandle::succeeded, this,
            [this, request, cycle, requestGeneration, recognitionId] {
                if (activeRequest_ != request || cycle != cycleGeneration_
                    || requestGeneration != activeRequestGeneration_
                    || request->context().sessionGeneration() != sessionGeneration_
                    || request->context().recognitionId() != recognitionId
                    || !sessionService_->isCurrent(request->context())) {
                    return;
                }
                const auto &current = realtimeStore_->current();
                const auto &snapshot = request->snapshot();
                if (!current || current->status() != RecognitionStatus::Processing
                    || current->recognitionId() != recognitionId || !snapshot
                    || snapshot->recognitionId() != recognitionId) {
                    return;
                }
                activeRequest_.clear();
                (void)realtimeStore_->apply(sessionGeneration_, *snapshot);
            });
    connect(request, &RecognitionDetailRequestHandle::finished, this,
            [this, request, cycle, requestGeneration] {
                if (activeRequest_ == request && cycle == cycleGeneration_
                    && requestGeneration == activeRequestGeneration_) {
                    activeRequest_.clear();
                }
            });
}

void ReconnectReconciler::cancelActiveRequest()
{
    QPointer<RecognitionDetailRequestHandle> request = activeRequest_;
    activeRequest_.clear();
    ++activeRequestGeneration_;
    if (request) {
        request->cancel();
    }
}

void ReconnectReconciler::resetSession(quint64 generation)
{
    cancelActiveRequest();
    sessionGeneration_ = generation;
    ++cycleGeneration_;
    cycleRecognitionId_.clear();
    hasConnectedOnce_ = false;
    compensationAvailable_ = false;
}
