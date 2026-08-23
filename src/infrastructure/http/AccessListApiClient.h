#pragma once

#include "domain/AccessListRecord.h"
#include "domain/Errors.h"
#include "domain/PageResult.h"
#include "domain/RequestContext.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <optional>
#include <utility>

class HttpClient;
class HttpRequestHandle;
class SessionService;

enum class AccessListApiErrorCode
{
    InvalidRequest,
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
    Cancelled,
    StaleResponse,
    ResponseTooLarge,
    ProtocolError,
};

class AccessListApiError final
{
public:
    AccessListApiError(AccessListApiErrorCode code,
                       QString message,
                       QString serverCode = {},
                       QString requestId = {},
                       std::optional<int> httpStatus = std::nullopt,
                       std::optional<ProtocolError> protocolError = std::nullopt)
        : code_(code)
        , message_(std::move(message))
        , serverCode_(std::move(serverCode))
        , requestId_(std::move(requestId))
        , httpStatus_(httpStatus)
        , protocolError_(std::move(protocolError))
    {
    }

    [[nodiscard]] AccessListApiErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const QString &serverCode() const noexcept { return serverCode_; }
    [[nodiscard]] const QString &requestId() const noexcept { return requestId_; }
    [[nodiscard]] const std::optional<int> &httpStatus() const noexcept { return httpStatus_; }
    [[nodiscard]] const std::optional<ProtocolError> &protocolError() const noexcept
    {
        return protocolError_;
    }

private:
    AccessListApiErrorCode code_;
    QString message_;
    QString serverCode_;
    QString requestId_;
    std::optional<int> httpStatus_;
    std::optional<ProtocolError> protocolError_;
};

class AccessListPageRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit AccessListPageRequestHandle(RequestContext context, QObject *parent = nullptr);
    ~AccessListPageRequestHandle() override;

    void cancel();
    [[nodiscard]] const RequestContext &context() const noexcept;
    [[nodiscard]] const std::optional<PageResult<AccessListRecord>> &page() const noexcept;

signals:
    void succeeded();
    void failed(const AccessListApiError &error);
    void finished();

private:
    friend class AccessListApiClient;
    void completeSuccess(PageResult<AccessListRecord> page);
    void completeFailure(AccessListApiError error);

    RequestContext context_;
    QPointer<HttpRequestHandle> transportHandle_;
    std::optional<PageResult<AccessListRecord>> page_;
    bool completed_ = false;
};

class AccessListLookupRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit AccessListLookupRequestHandle(RequestContext context, QObject *parent = nullptr);
    ~AccessListLookupRequestHandle() override;

    void cancel();
    [[nodiscard]] const RequestContext &context() const noexcept;
    [[nodiscard]] const std::optional<AccessListRecord> &record() const noexcept;

signals:
    void succeeded();
    void failed(const AccessListApiError &error);
    void finished();

private:
    friend class AccessListApiClient;
    void completeSuccess(AccessListRecord record);
    void completeFailure(AccessListApiError error);

    RequestContext context_;
    QPointer<HttpRequestHandle> transportHandle_;
    std::optional<AccessListRecord> record_;
    bool completed_ = false;
};

class AccessListCreateRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit AccessListCreateRequestHandle(RequestContext context, QObject *parent = nullptr);
    ~AccessListCreateRequestHandle() override;

    void cancel();
    [[nodiscard]] const RequestContext &context() const noexcept;
    [[nodiscard]] const std::optional<AccessListRecord> &record() const noexcept;

signals:
    void succeeded();
    void failed(const AccessListApiError &error);
    void finished();

private:
    friend class AccessListApiClient;
    void completeSuccess(AccessListRecord record);
    void completeFailure(AccessListApiError error);

    RequestContext context_;
    QPointer<HttpRequestHandle> transportHandle_;
    std::optional<AccessListRecord> record_;
    bool completed_ = false;
};

class AccessListDeleteRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit AccessListDeleteRequestHandle(RequestContext context, QObject *parent = nullptr);
    ~AccessListDeleteRequestHandle() override;

    void cancel();
    [[nodiscard]] const RequestContext &context() const noexcept;

signals:
    void succeeded();
    void failed(const AccessListApiError &error);
    void finished();

private:
    friend class AccessListApiClient;
    void completeSuccess();
    void completeFailure(AccessListApiError error);

    RequestContext context_;
    QPointer<HttpRequestHandle> transportHandle_;
    bool completed_ = false;
};

class AccessListApiClient final : public QObject
{
    Q_OBJECT

public:
    AccessListApiClient(QUrl loginBaseUrl,
                        HttpClient *httpClient,
                        SessionService *sessionService,
                        QObject *parent = nullptr);

    [[nodiscard]] AccessListPageRequestHandle *getPage(
        AccessListType listType,
        const QString &keyword,
        int page,
        quint64 sessionGeneration,
        quint64 requestGeneration);
    [[nodiscard]] AccessListLookupRequestHandle *lookup(
        const QString &plateNumber,
        quint64 sessionGeneration,
        quint64 requestGeneration);
    [[nodiscard]] AccessListCreateRequestHandle *create(
        AccessListType listType,
        const QString &plateNumber,
        const QString &remark,
        quint64 sessionGeneration,
        quint64 requestGeneration);
    [[nodiscard]] AccessListDeleteRequestHandle *remove(
        qint64 id,
        AccessListType listType,
        const QString &plateNumber,
        quint64 sessionGeneration,
        quint64 requestGeneration);

private:
    [[nodiscard]] QUrl pageEndpoint(AccessListType listType,
                                    const QString &keyword,
                                    int page) const;
    [[nodiscard]] QUrl lookupEndpoint(const QString &plateNumber) const;
    [[nodiscard]] QUrl createEndpoint() const;
    [[nodiscard]] QUrl deleteEndpoint(qint64 id) const;

    QUrl loginBaseUrl_;
    HttpClient *httpClient_;
    SessionService *sessionService_;
};

Q_DECLARE_METATYPE(AccessListApiError)
