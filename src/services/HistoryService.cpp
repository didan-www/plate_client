#include "services/HistoryService.h"

#include "infrastructure/http/RecognitionApiClient.h"
#include "services/SessionService.h"

#include <QTime>
#include <QTimeZone>

#include <utility>

namespace {
constexpr int beijingOffsetSeconds = 8 * 60 * 60;

QDateTime systemNow()
{
    return QDateTime::currentDateTime();
}

QDateTime inBeijing(const QDateTime &value)
{
    return value.toOffsetFromUtc(beijingOffsetSeconds);
}

QString protocolTime(const QDateTime &value)
{
    return inBeijing(value).toString(
        QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz+08:00"));
}

const QList<RecognitionSnapshot> &emptyItems()
{
    static const QList<RecognitionSnapshot> empty;
    return empty;
}
} // namespace

HistoryFilter::HistoryFilter(QDateTime startTime,
                             QDateTime endTime,
                             QString deviceId,
                             int page)
    : startTime_(inBeijing(startTime))
    , endTime_(inBeijing(endTime))
    , deviceId_(std::move(deviceId).trimmed())
    , page_(page)
{
}

const QDateTime &HistoryFilter::startTime() const noexcept
{
    return startTime_;
}

const QDateTime &HistoryFilter::endTime() const noexcept
{
    return endTime_;
}

const QString &HistoryFilter::deviceId() const noexcept
{
    return deviceId_;
}

int HistoryFilter::page() const noexcept
{
    return page_;
}

QVariantMap HistoryFilter::snapshot() const
{
    return QVariantMap {
        {QStringLiteral("startTime"), protocolTime(startTime_)},
        {QStringLiteral("endTime"), protocolTime(endTime_)},
        {QStringLiteral("deviceId"), deviceId_},
        {QStringLiteral("page"), page_},
        {QStringLiteral("pageSize"), HistoryService::pageSize},
    };
}

bool operator==(const HistoryFilter &left, const HistoryFilter &right) noexcept
{
    return left.startTime_ == right.startTime_ && left.endTime_ == right.endTime_
           && left.deviceId_ == right.deviceId_ && left.page_ == right.page_;
}

HistoryService::HistoryService(RecognitionApiClient *recognitionApiClient,
                               SessionService *sessionService,
                               QObject *parent)
    : HistoryService(recognitionApiClient, sessionService, systemNow, parent)
{
}

HistoryService::HistoryService(RecognitionApiClient *recognitionApiClient,
                               SessionService *sessionService,
                               Clock clock,
                               QObject *parent)
    : QObject(parent)
    , recognitionApiClient_(recognitionApiClient)
    , sessionService_(sessionService)
    , clock_(clock ? std::move(clock) : systemNow)
{
    Q_ASSERT(recognitionApiClient_);
    Q_ASSERT(sessionService_);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &HistoryService::activate);
    connect(sessionService_, &SessionService::sessionInvalidated,
            this, &HistoryService::invalidate);
    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

HistoryService::~HistoryService()
{
    cancelActive();
}

HistoryFilter HistoryService::defaultFilter() const
{
    const QDate today = inBeijing(clock_()).date();
    const QTimeZone beijing = QTimeZone::fromSecondsAheadOfUtc(beijingOffsetSeconds);
    const QDateTime start(today, QTime(0, 0), beijing);
    return HistoryFilter(start, start.addDays(1), {}, 1);
}

const std::optional<HistoryFilter> &HistoryService::lastSubmittedFilter() const noexcept
{
    return lastSubmittedFilter_;
}

const std::optional<PageResult<RecognitionSnapshot>> &
HistoryService::lastSuccessfulPage() const noexcept
{
    return lastSuccessfulPage_;
}

const QList<RecognitionSnapshot> &HistoryService::items() const noexcept
{
    return lastSuccessfulPage_ ? lastSuccessfulPage_->items() : emptyItems();
}

bool HistoryService::isLoading() const noexcept
{
    return loading_;
}

bool HistoryService::hasActiveRequest() const noexcept
{
    return !activeRequest_.isNull();
}

bool HistoryService::canGoPrevious() const noexcept
{
    return lastSuccessfulPage_ && lastSuccessfulPage_->page() > 1;
}

bool HistoryService::canGoNext() const noexcept
{
    return lastSuccessfulPage_
           && static_cast<qint64>(lastSuccessfulPage_->page()) * pageSize
                  < lastSuccessfulPage_->total();
}

void HistoryService::search(const QDateTime &startTime,
                            const QDateTime &endTime,
                            const QString &deviceId)
{
    submit(HistoryFilter(startTime, endTime, deviceId, 1));
}

void HistoryService::reset()
{
    submit(defaultFilter());
}

void HistoryService::refresh()
{
    if (lastSubmittedFilter_) {
        submit(*lastSubmittedFilter_);
    } else {
        submit(defaultFilter());
    }
}

bool HistoryService::previousPage()
{
    if (!canGoPrevious() || !lastSubmittedFilter_) {
        return false;
    }
    submit(HistoryFilter(lastSubmittedFilter_->startTime(),
                         lastSubmittedFilter_->endTime(),
                         lastSubmittedFilter_->deviceId(),
                         lastSuccessfulPage_->page() - 1));
    return true;
}

bool HistoryService::nextPage()
{
    if (!canGoNext() || !lastSubmittedFilter_) {
        return false;
    }
    submit(HistoryFilter(lastSubmittedFilter_->startTime(),
                         lastSubmittedFilter_->endTime(),
                         lastSubmittedFilter_->deviceId(),
                         lastSuccessfulPage_->page() + 1));
    return true;
}

void HistoryService::cancelActive()
{
    QPointer<RecognitionHistoryRequestHandle> request = activeRequest_;
    activeRequest_.clear();
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    setLoading(false);
    if (request) {
        request->cancel();
    }
}

void HistoryService::activate(const SessionSummary &summary)
{
    cancelActive();
    sessionGeneration_ = summary.generation();
    lastSubmittedFilter_.reset();
    clearResults();
}

void HistoryService::invalidate(SessionInvalidationReason reason, quint64 generation)
{
    Q_UNUSED(reason)
    if (generation != sessionGeneration_) {
        return;
    }
    cancelActive();
    sessionGeneration_ = 0;
    lastSubmittedFilter_.reset();
    clearResults();
}

void HistoryService::submit(HistoryFilter filter)
{
    QPointer<RecognitionHistoryRequestHandle> previous = activeRequest_;
    activeRequest_.clear();
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    if (previous) {
        previous->cancel();
    }

    lastSubmittedFilter_ = filter;
    const quint64 generation = requestGeneration_;
    RecognitionHistoryRequestHandle *request = recognitionApiClient_->getHistory(
        filter.startTime(),
        filter.endTime(),
        filter.deviceId(),
        filter.page(),
        sessionGeneration_,
        generation);
    activeRequest_ = request;
    setLoading(true);

    connect(request, &RecognitionHistoryRequestHandle::succeeded, this,
            [this, request, generation, filter] {
                if (!responseIsCurrent(request, generation, filter)
                    || !request->page()) {
                    return;
                }
                activeRequest_.clear();
                lastSuccessfulPage_ = *request->page();
                setLoading(false);
                emit resultsChanged();
            });
    connect(request, &RecognitionHistoryRequestHandle::failed, this,
            [this, request, generation, filter](const RecognitionApiError &error) {
                if (!responseIsCurrent(request, generation, filter)) {
                    return;
                }
                activeRequest_.clear();
                setLoading(false);
                if (error.code() != RecognitionApiErrorCode::Cancelled
                    && error.code() != RecognitionApiErrorCode::StaleResponse) {
                    emit queryFailed(error.message());
                }
            });
}

void HistoryService::setLoading(bool loading)
{
    if (loading_ == loading) {
        return;
    }
    loading_ = loading;
    emit loadingChanged(loading_);
}

void HistoryService::clearResults()
{
    if (!lastSuccessfulPage_) {
        return;
    }
    lastSuccessfulPage_.reset();
    emit resultsChanged();
}

bool HistoryService::responseIsCurrent(RecognitionHistoryRequestHandle *request,
                                       quint64 requestGeneration,
                                       const HistoryFilter &filter) const
{
    return request && activeRequest_ == request
           && requestGeneration == requestGeneration_
           && request->context().sessionGeneration() == sessionGeneration_
           && request->context().requestGeneration() == requestGeneration
           && request->context().filterSnapshot() == filter.snapshot()
           && lastSubmittedFilter_ && *lastSubmittedFilter_ == filter
           && sessionService_->isCurrent(request->context());
}
