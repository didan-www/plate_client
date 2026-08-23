#pragma once

#include "domain/PageResult.h"
#include "domain/RecognitionSnapshot.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <optional>

class RecognitionApiClient;
class RecognitionHistoryRequestHandle;
class SessionService;
class SessionSummary;
enum class SessionInvalidationReason;

class HistoryFilter final
{
public:
    HistoryFilter(QDateTime startTime,
                  QDateTime endTime,
                  QString deviceId,
                  int page);

    [[nodiscard]] const QDateTime &startTime() const noexcept;
    [[nodiscard]] const QDateTime &endTime() const noexcept;
    [[nodiscard]] const QString &deviceId() const noexcept;
    [[nodiscard]] int page() const noexcept;
    [[nodiscard]] QVariantMap snapshot() const;

    friend bool operator==(const HistoryFilter &left, const HistoryFilter &right) noexcept;
    friend bool operator!=(const HistoryFilter &left, const HistoryFilter &right) noexcept
    {
        return !(left == right);
    }

private:
    QDateTime startTime_;
    QDateTime endTime_;
    QString deviceId_;
    int page_;
};
class HistoryService final : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;
    static constexpr int pageSize = 100;

    HistoryService(RecognitionApiClient *recognitionApiClient,
                   SessionService *sessionService,
                   QObject *parent = nullptr);
    HistoryService(RecognitionApiClient *recognitionApiClient,
                   SessionService *sessionService,
                   Clock clock,
                   QObject *parent = nullptr);
    ~HistoryService() override;

    [[nodiscard]] HistoryFilter defaultFilter() const;
    [[nodiscard]] const std::optional<HistoryFilter> &lastSubmittedFilter() const noexcept;
    [[nodiscard]] const std::optional<PageResult<RecognitionSnapshot>> &lastSuccessfulPage()
        const noexcept;
    [[nodiscard]] const QList<RecognitionSnapshot> &items() const noexcept;
    [[nodiscard]] bool isLoading() const noexcept;
    [[nodiscard]] bool hasActiveRequest() const noexcept;
    [[nodiscard]] bool canGoPrevious() const noexcept;
    [[nodiscard]] bool canGoNext() const noexcept;

    void search(const QDateTime &startTime,
                const QDateTime &endTime,
                const QString &deviceId);
    void reset();
    void refresh();
    bool previousPage();
    bool nextPage();
    void cancelActive();

signals:
    void resultsChanged();
    void loadingChanged(bool loading);
    void queryFailed(const QString &message);

private:
    void activate(const SessionSummary &summary);
    void invalidate(SessionInvalidationReason reason, quint64 generation);
    void submit(HistoryFilter filter);
    void setLoading(bool loading);
    void clearResults();
    [[nodiscard]] bool responseIsCurrent(RecognitionHistoryRequestHandle *request,
                                         quint64 requestGeneration,
                                         const HistoryFilter &filter) const;

    RecognitionApiClient *recognitionApiClient_;
    SessionService *sessionService_;
    Clock clock_;
    QPointer<RecognitionHistoryRequestHandle> activeRequest_;
    std::optional<HistoryFilter> lastSubmittedFilter_;
    std::optional<PageResult<RecognitionSnapshot>> lastSuccessfulPage_;
    quint64 sessionGeneration_ = 0;
    quint64 requestGeneration_ = 0;
    bool loading_ = false;
};
