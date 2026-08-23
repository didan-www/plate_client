#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <memory>

class QByteArray;
class HistoryFilter;
class QSaveFile;
class RecognitionApiClient;
class RecognitionCsvRequestHandle;
class SessionService;
class SessionSummary;
enum class SessionInvalidationReason;

class CsvDownloadService final : public QObject
{
    Q_OBJECT

public:
    CsvDownloadService(RecognitionApiClient *recognitionApiClient,
                       SessionService *sessionService,
                       QObject *parent = nullptr);
    ~CsvDownloadService() override;

    [[nodiscard]] bool download(const HistoryFilter &filter,
                                const QString &targetPath);
    [[nodiscard]] bool isDownloading() const noexcept;
    [[nodiscard]] bool hasActiveRequest() const noexcept;
    void cancelActive();

signals:
    void downloadingChanged(bool downloading);
    void downloadSucceeded(const QString &targetPath);
    void downloadFailed(const QString &message);

private:
    void activate(const SessionSummary &summary);
    void invalidate(SessionInvalidationReason reason, quint64 generation);
    void writeChunk(RecognitionCsvRequestHandle *request,
                    quint64 requestGeneration,
                    const QVariantMap &filterSnapshot,
                    const QByteArray &chunk);
    void completeDownload(RecognitionCsvRequestHandle *request,
                          quint64 requestGeneration,
                          const QVariantMap &filterSnapshot);
    void failDownload(RecognitionCsvRequestHandle *request,
                      quint64 requestGeneration,
                      const QVariantMap &filterSnapshot,
                      const QString &message,
                      bool cancelRequest);
    void cancelActiveForSession(quint64 sessionGeneration, bool notify);
    void setDownloading(bool downloading);
    [[nodiscard]] bool responseIsCurrent(RecognitionCsvRequestHandle *request,
                                         quint64 requestGeneration,
                                         const QVariantMap &filterSnapshot) const;

    RecognitionApiClient *recognitionApiClient_;
    SessionService *sessionService_;
    QPointer<RecognitionCsvRequestHandle> activeRequest_;
    std::unique_ptr<QSaveFile> saveFile_;
    QVariantMap activeFilterSnapshot_;
    QString targetPath_;
    quint64 sessionGeneration_ = 0;
    quint64 requestGeneration_ = 0;
    bool downloading_ = false;
};
