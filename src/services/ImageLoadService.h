#pragma once

#include "domain/ImageLoadResult.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <functional>

class QThreadPool;
class RecognitionApiClient;
class RecognitionImageRequestHandle;
class SessionService;
enum class SessionInvalidationReason;

class ImageLoadService final : public QObject
{
    Q_OBJECT

public:
    using RetryDelayResolver = std::function<std::chrono::milliseconds(int)>;
    using DecodeObserver = std::function<void()>;

    ImageLoadService(RecognitionApiClient *recognitionApiClient,
                     SessionService *sessionService,
                     QObject *parent = nullptr);
    ImageLoadService(RecognitionApiClient *recognitionApiClient,
                     SessionService *sessionService,
                     QThreadPool *threadPool,
                     RetryDelayResolver retryDelayResolver,
                     DecodeObserver decodeObserver = {},
                     QObject *parent = nullptr);
    ~ImageLoadService() override;

    void load(const QString &recognitionId, quint64 sessionGeneration);
    [[nodiscard]] bool retry();
    void cancel();

    [[nodiscard]] quint64 requestGeneration() const noexcept;
    [[nodiscard]] int attemptCount() const noexcept;
    [[nodiscard]] bool requestInFlight() const noexcept;
    [[nodiscard]] bool retryPending() const noexcept;
    [[nodiscard]] bool manualRetryAvailable() const noexcept;
    [[nodiscard]] const QString &recognitionId() const noexcept;

signals:
    void resultReady(const ImageLoadResult &result);

private:
    void beginSeries(const QString &recognitionId, quint64 sessionGeneration);
    void startAttempt();
    void scheduleRetry();
    void beginDecode(QByteArray bytes,
                     QByteArray declaredFormat,
                     RequestContext context,
                     int attempts);
    void publishFailure(const RequestContext &context,
                        ImageLoadError error,
                        int attempts);
    void invalidateSeries();
    void cancelActiveRequest();
    [[nodiscard]] bool matchesCurrent(const RequestContext &context) const;

    RecognitionApiClient *recognitionApiClient_;
    SessionService *sessionService_;
    QThreadPool *threadPool_;
    RetryDelayResolver retryDelayResolver_;
    DecodeObserver decodeObserver_;
    QPointer<RecognitionImageRequestHandle> activeRequest_;
    QTimer retryTimer_;
    quint64 sessionGeneration_ = 0;
    quint64 requestGeneration_ = 0;
    QString recognitionId_;
    int attemptCount_ = 0;
    bool manualRetryAvailable_ = false;
};
