#include "services/ImageLoadService.h"

#include "infrastructure/http/RecognitionApiClient.h"
#include "services/SessionService.h"

#include <QBuffer>
#include <QFutureWatcher>
#include <QImageReader>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace {
using namespace std::chrono_literals;

std::chrono::milliseconds defaultRetryDelay(int failedAttempt)
{
    return failedAttempt == 1 ? 1s : 3s;
}

QByteArray canonicalFormat(QByteArray format)
{
    format = format.trimmed().toLower();
    if (format == QByteArrayLiteral("jpg")) {
        return QByteArrayLiteral("jpeg");
    }
    if (format == QByteArrayLiteral("jpeg") || format == QByteArrayLiteral("png")) {
        return format;
    }
    return {};
}

ImageLoadError mapApiError(const RecognitionApiError &error)
{
    ImageLoadErrorCode code = ImageLoadErrorCode::ProtocolError;
    switch (error.code()) {
    case RecognitionApiErrorCode::InvalidRequest:
        code = ImageLoadErrorCode::InvalidRequest;
        break;
    case RecognitionApiErrorCode::Unauthorized:
        code = ImageLoadErrorCode::Unauthorized;
        break;
    case RecognitionApiErrorCode::Forbidden:
        code = ImageLoadErrorCode::Forbidden;
        break;
    case RecognitionApiErrorCode::NotFound:
        code = ImageLoadErrorCode::NotFound;
        break;
    case RecognitionApiErrorCode::ClientRejected:
        code = ImageLoadErrorCode::ClientRejected;
        break;
    case RecognitionApiErrorCode::ServerError:
        code = ImageLoadErrorCode::ServerError;
        break;
    case RecognitionApiErrorCode::NetworkError:
        code = ImageLoadErrorCode::NetworkError;
        break;
    case RecognitionApiErrorCode::TlsError:
        code = ImageLoadErrorCode::TlsError;
        break;
    case RecognitionApiErrorCode::Timeout:
        code = ImageLoadErrorCode::Timeout;
        break;
    case RecognitionApiErrorCode::Cancelled:
        code = ImageLoadErrorCode::Cancelled;
        break;
    case RecognitionApiErrorCode::StaleResponse:
        code = ImageLoadErrorCode::StaleResponse;
        break;
    case RecognitionApiErrorCode::ResponseTooLarge:
        code = ImageLoadErrorCode::ResponseTooLarge;
        break;
    case RecognitionApiErrorCode::InvalidContentType:
        code = ImageLoadErrorCode::InvalidContentType;
        break;
    case RecognitionApiErrorCode::ProtocolError:
        code = ImageLoadErrorCode::ProtocolError;
        break;
    }
    return ImageLoadError(code, error.message(), error.httpStatus());
}

bool isRetryable(const RecognitionApiError &error)
{
    if (error.code() == RecognitionApiErrorCode::NetworkError
        || error.code() == RecognitionApiErrorCode::Timeout) {
        return true;
    }
    if (error.code() != RecognitionApiErrorCode::ServerError
        || !error.httpStatus().has_value()) {
        return false;
    }
    const int status = *error.httpStatus();
    return status == 500 || status == 502 || status == 503 || status == 504;
}

ImageLoadResult decodeImage(QByteArray bytes,
                            QByteArray declaredFormat,
                            RequestContext context,
                            int attempts)
{
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::DecodeFailed,
                           QStringLiteral("无法读取图片响应。")),
            attempts,
            true);
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead()) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::UnsupportedFormat,
                           QStringLiteral("图片数据不是有效的 JPEG 或 PNG。")),
            attempts,
            true);
    }
    const QByteArray actualFormat = canonicalFormat(reader.format());
    if (actualFormat.isEmpty()) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::UnsupportedFormat,
                           QStringLiteral("图片实际格式不受支持。")),
            attempts,
            true);
    }
    if (actualFormat != canonicalFormat(std::move(declaredFormat))) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::FormatMismatch,
                           QStringLiteral("图片实际格式与 Content-Type 不一致。")),
            attempts,
            true);
    }

    const QSize declaredSize = reader.size();
    if (!declaredSize.isValid() || declaredSize.width() <= 0
        || declaredSize.height() <= 0) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::InvalidDimensions,
                           QStringLiteral("图片尺寸元数据无效。")),
            attempts,
            true);
    }
    if (!ImageLoadLimits::accepts(declaredSize)) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::DimensionsExceeded,
                           QStringLiteral("图片尺寸超过允许范围。")),
            attempts,
            true);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::DecodeFailed,
                           QStringLiteral("图片解码失败。")),
            attempts,
            true);
    }
    if (!ImageLoadLimits::accepts(image.size())) {
        return ImageLoadResult::failure(
            std::move(context),
            ImageLoadError(ImageLoadErrorCode::DimensionsExceeded,
                           QStringLiteral("图片解码后尺寸超过允许范围。")),
            attempts,
            true);
    }
    return ImageLoadResult::success(std::move(context), std::move(image), attempts);
}
} // namespace

ImageLoadService::ImageLoadService(RecognitionApiClient *recognitionApiClient,
                                   SessionService *sessionService,
                                   QObject *parent)
    : ImageLoadService(recognitionApiClient,
                       sessionService,
                       QThreadPool::globalInstance(),
                       defaultRetryDelay,
                       {},
                       parent)
{
}

ImageLoadService::ImageLoadService(RecognitionApiClient *recognitionApiClient,
                                   SessionService *sessionService,
                                   QThreadPool *threadPool,
                                   RetryDelayResolver retryDelayResolver,
                                   DecodeObserver decodeObserver,
                                   QObject *parent)
    : QObject(parent)
    , recognitionApiClient_(recognitionApiClient)
    , sessionService_(sessionService)
    , threadPool_(threadPool)
    , retryDelayResolver_(std::move(retryDelayResolver))
    , decodeObserver_(std::move(decodeObserver))
{
    Q_ASSERT(recognitionApiClient_);
    Q_ASSERT(sessionService_);
    Q_ASSERT(threadPool_);
    if (!retryDelayResolver_) {
        retryDelayResolver_ = defaultRetryDelay;
    }
    retryTimer_.setSingleShot(true);
    connect(&retryTimer_, &QTimer::timeout, this, &ImageLoadService::startAttempt);
    connect(sessionService_, &SessionService::sessionInvalidated, this,
            [this](SessionInvalidationReason reason, quint64 generation) {
                Q_UNUSED(reason)
                if (generation == sessionGeneration_) {
                    invalidateSeries();
                }
            });
}

ImageLoadService::~ImageLoadService()
{
    invalidateSeries();
}

void ImageLoadService::load(const QString &recognitionId, quint64 sessionGeneration)
{
    beginSeries(recognitionId, sessionGeneration);
}

bool ImageLoadService::retry()
{
    if (!manualRetryAvailable_ || recognitionId_.isEmpty() || sessionGeneration_ == 0) {
        return false;
    }
    const RequestContext current(sessionGeneration_,
                                 requestGeneration_,
                                 QStringLiteral("recognition.image"),
                                 recognitionId_);
    if (!sessionService_->isCurrent(current)) {
        invalidateSeries();
        return false;
    }
    const QString recognitionId = recognitionId_;
    const quint64 sessionGeneration = sessionGeneration_;
    beginSeries(recognitionId, sessionGeneration);
    return true;
}

void ImageLoadService::cancel()
{
    invalidateSeries();
}

quint64 ImageLoadService::requestGeneration() const noexcept
{
    return requestGeneration_;
}

int ImageLoadService::attemptCount() const noexcept
{
    return attemptCount_;
}

bool ImageLoadService::requestInFlight() const noexcept
{
    return !activeRequest_.isNull();
}

bool ImageLoadService::retryPending() const noexcept
{
    return retryTimer_.isActive();
}

bool ImageLoadService::manualRetryAvailable() const noexcept
{
    return manualRetryAvailable_;
}

const QString &ImageLoadService::recognitionId() const noexcept
{
    return recognitionId_;
}

void ImageLoadService::beginSeries(const QString &recognitionId,
                                   quint64 sessionGeneration)
{
    cancelActiveRequest();
    retryTimer_.stop();
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    recognitionId_ = recognitionId;
    sessionGeneration_ = sessionGeneration;
    attemptCount_ = 0;
    manualRetryAvailable_ = false;

    const RequestContext context(sessionGeneration_,
                                 requestGeneration_,
                                 QStringLiteral("recognition.image"),
                                 recognitionId_);
    if (recognitionId_.isEmpty() || sessionGeneration_ == 0) {
        emit resultReady(ImageLoadResult::failure(
            context,
            ImageLoadError(ImageLoadErrorCode::InvalidRequest,
                           QStringLiteral("图片请求上下文无效。")),
            0,
            false));
        return;
    }
    if (!sessionService_->isCurrent(context)) {
        invalidateSeries();
        return;
    }
    startAttempt();
}

void ImageLoadService::startAttempt()
{
    if (recognitionId_.isEmpty() || sessionGeneration_ == 0 || activeRequest_) {
        return;
    }
    const RequestContext context(sessionGeneration_,
                                 requestGeneration_,
                                 QStringLiteral("recognition.image"),
                                 recognitionId_);
    if (!sessionService_->isCurrent(context)) {
        invalidateSeries();
        return;
    }

    ++attemptCount_;
    RecognitionImageRequestHandle *request = recognitionApiClient_->getImage(
        recognitionId_, sessionGeneration_, requestGeneration_);
    activeRequest_ = request;
    const quint64 generation = requestGeneration_;
    const int attempts = attemptCount_;

    connect(request, &RecognitionImageRequestHandle::succeeded, this,
            [this, request, generation, attempts] {
                if (activeRequest_ != request || generation != requestGeneration_
                    || !matchesCurrent(request->context())) {
                    return;
                }
                const QByteArray bytes = request->imageBytes();
                const QByteArray declaredFormat = request->declaredFormat();
                const RequestContext context = request->context();
                activeRequest_.clear();
                beginDecode(bytes, declaredFormat, context, attempts);
            });
    connect(request, &RecognitionImageRequestHandle::failed, this,
            [this, request, generation, attempts](const RecognitionApiError &error) {
                if (activeRequest_ != request || generation != requestGeneration_
                    || !matchesCurrent(request->context())) {
                    return;
                }
                activeRequest_.clear();
                if (attempts < 3 && isRetryable(error)) {
                    scheduleRetry();
                    return;
                }
                publishFailure(request->context(), mapApiError(error), attempts);
            });
    connect(request, &RecognitionImageRequestHandle::finished, this,
            [this, request, generation] {
                if (activeRequest_ == request && generation == requestGeneration_) {
                    activeRequest_.clear();
                }
            });
}

void ImageLoadService::scheduleRetry()
{
    const auto delay = std::max(retryDelayResolver_(attemptCount_), 0ms);
    retryTimer_.start(static_cast<int>(std::min<qint64>(
        delay.count(), std::numeric_limits<int>::max())));
}

void ImageLoadService::beginDecode(QByteArray bytes,
                                   QByteArray declaredFormat,
                                   RequestContext context,
                                   int attempts)
{
    auto *watcher = new QFutureWatcher<ImageLoadResult>(this);
    const DecodeObserver observer = decodeObserver_;
    connect(watcher, &QFutureWatcher<ImageLoadResult>::finished, this,
            [this, watcher] {
                const ImageLoadResult result = watcher->result();
                watcher->deleteLater();
                if (!matchesCurrent(result.context())) {
                    return;
                }
                manualRetryAvailable_ = result.manualRetryAvailable();
                emit resultReady(result);
            });
    watcher->setFuture(QtConcurrent::run(
        threadPool_,
        [bytes = std::move(bytes), declaredFormat = std::move(declaredFormat),
         context = std::move(context), attempts, observer]() mutable {
            if (observer) {
                observer();
            }
            return decodeImage(std::move(bytes),
                               std::move(declaredFormat),
                               std::move(context),
                               attempts);
        }));
}

void ImageLoadService::publishFailure(const RequestContext &context,
                                      ImageLoadError error,
                                      int attempts)
{
    if (!matchesCurrent(context)) {
        return;
    }
    manualRetryAvailable_ = true;
    emit resultReady(ImageLoadResult::failure(context,
                                              std::move(error),
                                              attempts,
                                              true));
}

void ImageLoadService::invalidateSeries()
{
    retryTimer_.stop();
    cancelActiveRequest();
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    sessionGeneration_ = 0;
    recognitionId_.clear();
    attemptCount_ = 0;
    manualRetryAvailable_ = false;
}

void ImageLoadService::cancelActiveRequest()
{
    QPointer<RecognitionImageRequestHandle> request = activeRequest_;
    activeRequest_.clear();
    if (request) {
        request->cancel();
    }
}

bool ImageLoadService::matchesCurrent(const RequestContext &context) const
{
    return sessionGeneration_ != 0
           && context.sessionGeneration() == sessionGeneration_
           && context.requestGeneration() == requestGeneration_
           && context.recognitionId() == recognitionId_
           && sessionService_->isCurrent(context);
}
