#pragma once

#include "domain/AccessListRecord.h"
#include "domain/PageResult.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <utility>

class AccessListApiClient;
class AccessListCreateRequestHandle;
class AccessListDeleteRequestHandle;
class AccessListLookupRequestHandle;
class AccessListPageRequestHandle;
class RequestContext;
class SessionService;
class SessionSummary;
enum class SessionInvalidationReason;

class AccessListFilter final
{
public:
    AccessListFilter(AccessListType listType, QString keyword, int page);

    [[nodiscard]] AccessListType listType() const noexcept;
    [[nodiscard]] const QString &keyword() const noexcept;
    [[nodiscard]] int page() const noexcept;
    [[nodiscard]] QVariantMap snapshot() const;

    friend bool operator==(const AccessListFilter &left,
                           const AccessListFilter &right) noexcept;
    friend bool operator!=(const AccessListFilter &left,
                           const AccessListFilter &right) noexcept
    {
        return !(left == right);
    }

private:
    AccessListType listType_;
    QString keyword_;
    int page_;
};

enum class AccessListWriteOperation
{
    Create,
    Delete,
};

enum class AccessListWriteDisposition
{
    Applied,
    AlreadyAbsent,
    NotApplied,
    Unknown,
};

class AccessListWriteResult final
{
public:
    AccessListWriteResult(AccessListWriteOperation operation,
                          AccessListWriteDisposition disposition,
                          AccessListType targetListType,
                          QString plateNumber,
                          std::optional<qint64> targetId,
                          std::optional<AccessListRecord> serverRecord = std::nullopt)
        : operation_(operation)
        , disposition_(disposition)
        , targetListType_(targetListType)
        , plateNumber_(std::move(plateNumber))
        , targetId_(targetId)
        , serverRecord_(std::move(serverRecord))
    {
    }

    [[nodiscard]] AccessListWriteOperation operation() const noexcept { return operation_; }
    [[nodiscard]] AccessListWriteDisposition disposition() const noexcept
    {
        return disposition_;
    }
    [[nodiscard]] AccessListType targetListType() const noexcept { return targetListType_; }
    [[nodiscard]] const QString &plateNumber() const noexcept { return plateNumber_; }
    [[nodiscard]] const std::optional<qint64> &targetId() const noexcept { return targetId_; }
    [[nodiscard]] const std::optional<AccessListRecord> &serverRecord() const noexcept
    {
        return serverRecord_;
    }

private:
    AccessListWriteOperation operation_;
    AccessListWriteDisposition disposition_;
    AccessListType targetListType_;
    QString plateNumber_;
    std::optional<qint64> targetId_;
    std::optional<AccessListRecord> serverRecord_;
};

enum class AccessListServiceErrorCode
{
    InvalidInput,
    Busy,
    Unauthorized,
    Forbidden,
    NotFound,
    ConflictWhite,
    ConflictBlack,
    ClientRejected,
    ServerError,
    NetworkError,
    TlsError,
    Timeout,
    ResponseTooLarge,
    ProtocolError,
};

class AccessListServiceError final
{
public:
    AccessListServiceError(AccessListServiceErrorCode code,
                           QString message,
                           QString serverCode = {},
                           QString requestId = {})
        : code_(code)
        , message_(std::move(message))
        , serverCode_(std::move(serverCode))
        , requestId_(std::move(requestId))
    {
    }

    [[nodiscard]] AccessListServiceErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const QString &serverCode() const noexcept { return serverCode_; }
    [[nodiscard]] const QString &requestId() const noexcept { return requestId_; }

private:
    AccessListServiceErrorCode code_;
    QString message_;
    QString serverCode_;
    QString requestId_;
};

class AccessListService final : public QObject
{
    Q_OBJECT

public:
    static constexpr int pageSize = 100;

    AccessListService(AccessListApiClient *accessListApiClient,
                      SessionService *sessionService,
                      QObject *parent = nullptr);
    ~AccessListService() override;

    [[nodiscard]] static std::optional<QString> normalizePlateNumber(const QString &value);
    [[nodiscard]] static std::optional<QString> normalizeKeyword(const QString &value);
    [[nodiscard]] static bool isValidRemark(const QString &value);

    [[nodiscard]] const std::optional<AccessListFilter> &lastSubmittedFilter() const noexcept;
    [[nodiscard]] const std::optional<PageResult<AccessListRecord>> &lastSuccessfulPage()
        const noexcept;
    [[nodiscard]] const QList<AccessListRecord> &items() const noexcept;
    [[nodiscard]] bool isQueryLoading() const noexcept;
    [[nodiscard]] bool isWriteBusy() const noexcept;
    [[nodiscard]] bool hasActiveQuery() const noexcept;
    [[nodiscard]] bool canGoPrevious() const noexcept;
    [[nodiscard]] bool canGoNext() const noexcept;

    bool search(AccessListType listType, const QString &keyword);
    void refresh();
    bool previousPage();
    bool nextPage();
    bool create(AccessListType listType,
                const QString &plateNumber,
                const QString &remark);
    bool remove(const AccessListRecord &record);
    void cancelQuery();
    void cancelWrite();

signals:
    void resultsChanged();
    void queryLoadingChanged(bool loading);
    void queryFailed(const AccessListServiceError &error);
    void writeBusyChanged(bool busy);
    void writeCompleted(const AccessListWriteResult &result);
    void writeFailed(const AccessListServiceError &error);

private:
    struct WriteCommand final
    {
        AccessListWriteOperation operation;
        AccessListType listType;
        QString plateNumber;
        QString remark;
        std::optional<qint64> id;

        [[nodiscard]] QVariantMap snapshot() const;
        [[nodiscard]] bool operator==(const WriteCommand &other) const noexcept;
    };

    void activate(const SessionSummary &summary);
    void invalidate(SessionInvalidationReason reason, quint64 generation);
    void submitQuery(AccessListFilter filter);
    void beginVerification(quint64 writeGeneration, const WriteCommand &command);
    void finishWrite(AccessListWriteResult result,
                     quint64 writeGeneration,
                     const WriteCommand &command);
    void failWrite(AccessListServiceError error,
                   quint64 writeGeneration,
                   const WriteCommand &command);
    void cancelQueryInternal(bool clearResults);
    void cancelWriteInternal();
    void setQueryLoading(bool loading);
    void setWriteBusy(bool busy);
    [[nodiscard]] bool queryResponseIsCurrent(AccessListPageRequestHandle *request,
                                              quint64 queryGeneration,
                                              const AccessListFilter &filter) const;
    [[nodiscard]] bool writeContextIsCurrent(quint64 writeGeneration,
                                             const WriteCommand &command) const;
    [[nodiscard]] bool writeResponseIsCurrent(const RequestContext &context,
                                              quint64 writeGeneration,
                                              const WriteCommand &command,
                                              const QVariantMap &expectedSnapshot) const;

    AccessListApiClient *accessListApiClient_;
    SessionService *sessionService_;
    QPointer<AccessListPageRequestHandle> activeQuery_;
    QPointer<AccessListCreateRequestHandle> activeCreate_;
    QPointer<AccessListDeleteRequestHandle> activeDelete_;
    QPointer<AccessListLookupRequestHandle> activeVerification_;
    std::optional<AccessListFilter> lastSubmittedFilter_;
    std::optional<PageResult<AccessListRecord>> lastSuccessfulPage_;
    std::optional<WriteCommand> writeCommand_;
    quint64 sessionGeneration_ = 0;
    quint64 queryGeneration_ = 0;
    quint64 writeGeneration_ = 0;
    bool queryLoading_ = false;
    bool writeBusy_ = false;
};

Q_DECLARE_METATYPE(AccessListWriteResult)
Q_DECLARE_METATYPE(AccessListServiceError)
