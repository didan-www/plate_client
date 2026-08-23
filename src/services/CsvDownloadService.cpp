#include "services/CsvDownloadService.h"

#include "infrastructure/http/RecognitionApiClient.h"
#include "services/HistoryService.h"
#include "services/SessionService.h"

#include <QSaveFile>

#include <utility>

CsvDownloadService::CsvDownloadService(RecognitionApiClient *recognitionApiClient,
                                       SessionService *sessionService,
                                       QObject *parent)
    : QObject(parent)
    , recognitionApiClient_(recognitionApiClient)
    , sessionService_(sessionService)
{
    Q_ASSERT(recognitionApiClient_);
    Q_ASSERT(sessionService_);
    connect(sessionService_, &SessionService::sessionActivated,
            this, &CsvDownloadService::activate);
    connect(sessionService_, &SessionService::sessionInvalidated,
            this, &CsvDownloadService::invalidate);
    if (const auto summary = sessionService_->summary()) {
        activate(*summary);
    }
}

CsvDownloadService::~CsvDownloadService()
{
    cancelActiveForSession(sessionGeneration_, false);
}

bool CsvDownloadService::download(const HistoryFilter &filter,
                                  const QString &targetPath)
{
    const auto summary = sessionService_->summary();
    if (downloading_ || targetPath.trimmed().isEmpty()
        || sessionGeneration_ == 0 || !summary
        || summary->generation() != sessionGeneration_) {
        return false;
    }

    auto saveFile = std::make_unique<QSaveFile>(targetPath);
    saveFile->setDirectWriteFallback(false);
    if (!saveFile->open(QIODevice::WriteOnly)) {
        emit downloadFailed(QStringLiteral("无法创建 CSV 临时文件。"));
        return false;
    }

    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    const quint64 generation = requestGeneration_;
    RecognitionCsvRequestHandle *request = recognitionApiClient_->exportCsv(
        filter.startTime(),
        filter.endTime(),
        filter.deviceId(),
        sessionGeneration_,
        generation);
    activeRequest_ = request;
    activeFilterSnapshot_ = request->context().filterSnapshot();
    targetPath_ = targetPath;
    saveFile_ = std::move(saveFile);

    const QVariantMap filterSnapshot = activeFilterSnapshot_;
    connect(request, &RecognitionCsvRequestHandle::dataChunkReceived, this,
            [this, request, generation, filterSnapshot](const QByteArray &chunk) {
                writeChunk(request, generation, filterSnapshot, chunk);
            });
    connect(request, &RecognitionCsvRequestHandle::succeeded, this,
            [this, request, generation, filterSnapshot] {
                completeDownload(request, generation, filterSnapshot);
            });
    connect(request, &RecognitionCsvRequestHandle::failed, this,
            [this, request, generation, filterSnapshot](const RecognitionApiError &error) {
                failDownload(request,
                             generation,
                             filterSnapshot,
                             error.message(),
                             false);
            });
    setDownloading(true);
    return true;
}

bool CsvDownloadService::isDownloading() const noexcept
{
    return downloading_;
}

bool CsvDownloadService::hasActiveRequest() const noexcept
{
    return !activeRequest_.isNull();
}

void CsvDownloadService::cancelActive()
{
    cancelActiveForSession(sessionGeneration_, true);
}

void CsvDownloadService::cancelActiveForSession(quint64 sessionGeneration,
                                                bool notify)
{
    QPointer<RecognitionCsvRequestHandle> request = activeRequest_;
    activeRequest_.clear();
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    activeFilterSnapshot_.clear();
    targetPath_.clear();
    sessionGeneration_ = sessionGeneration;
    if (saveFile_) {
        saveFile_->cancelWriting();
        saveFile_.reset();
    }
    if (request) {
        request->cancel();
    }
    if (notify) {
        setDownloading(false);
    } else {
        downloading_ = false;
    }
}

void CsvDownloadService::activate(const SessionSummary &summary)
{
    cancelActiveForSession(summary.generation(), true);
}

void CsvDownloadService::invalidate(SessionInvalidationReason reason,
                                    quint64 generation)
{
    Q_UNUSED(reason)
    if (generation != sessionGeneration_) {
        return;
    }
    cancelActiveForSession(0, true);
}

void CsvDownloadService::writeChunk(RecognitionCsvRequestHandle *request,
                                    quint64 requestGeneration,
                                    const QVariantMap &filterSnapshot,
                                    const QByteArray &chunk)
{
    if (!responseIsCurrent(request, requestGeneration, filterSnapshot)
        || !saveFile_) {
        return;
    }
    if (saveFile_->write(chunk) != chunk.size()) {
        failDownload(request,
                     requestGeneration,
                     filterSnapshot,
                     QStringLiteral("CSV 文件写入失败。"),
                     true);
    }
}

void CsvDownloadService::completeDownload(RecognitionCsvRequestHandle *request,
                                          quint64 requestGeneration,
                                          const QVariantMap &filterSnapshot)
{
    if (!responseIsCurrent(request, requestGeneration, filterSnapshot)
        || !saveFile_) {
        return;
    }

    const QString targetPath = targetPath_;
    activeRequest_.clear();
    activeFilterSnapshot_.clear();
    targetPath_.clear();
    std::unique_ptr<QSaveFile> saveFile = std::move(saveFile_);
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    const bool committed = saveFile->commit();
    if (!committed) {
        saveFile->cancelWriting();
    }
    QPointer<CsvDownloadService> guarded(this);
    setDownloading(false);
    if (!guarded) {
        return;
    }
    if (!committed) {
        emit downloadFailed(QStringLiteral("CSV 文件原子保存失败。"));
        return;
    }
    emit downloadSucceeded(targetPath);
}

void CsvDownloadService::failDownload(RecognitionCsvRequestHandle *request,
                                      quint64 requestGeneration,
                                      const QVariantMap &filterSnapshot,
                                      const QString &message,
                                      bool cancelRequest)
{
    if (!responseIsCurrent(request, requestGeneration, filterSnapshot)) {
        return;
    }
    QPointer<RecognitionCsvRequestHandle> guardedRequest = activeRequest_;
    activeRequest_.clear();
    activeFilterSnapshot_.clear();
    targetPath_.clear();
    if (saveFile_) {
        saveFile_->cancelWriting();
        saveFile_.reset();
    }
    ++requestGeneration_;
    if (requestGeneration_ == 0) {
        ++requestGeneration_;
    }
    if (cancelRequest && guardedRequest) {
        guardedRequest->cancel();
    }
    QPointer<CsvDownloadService> guarded(this);
    setDownloading(false);
    if (!guarded) {
        return;
    }
    emit downloadFailed(message.isEmpty() ? QStringLiteral("CSV 导出失败。") : message);
}

void CsvDownloadService::setDownloading(bool downloading)
{
    if (downloading_ == downloading) {
        return;
    }
    downloading_ = downloading;
    emit downloadingChanged(downloading_);
}

bool CsvDownloadService::responseIsCurrent(
    RecognitionCsvRequestHandle *request,
    quint64 requestGeneration,
    const QVariantMap &filterSnapshot) const
{
    return request && activeRequest_ == request
           && requestGeneration == requestGeneration_
           && request->context().sessionGeneration() == sessionGeneration_
           && request->context().requestGeneration() == requestGeneration
           && request->context().filterSnapshot() == filterSnapshot
           && activeFilterSnapshot_ == filterSnapshot
           && sessionService_->isCurrent(request->context());
}
