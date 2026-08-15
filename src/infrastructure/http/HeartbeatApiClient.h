#pragma once

#include "domain/Errors.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <optional>
#include <utility>

class HttpClient;
class HttpRequestHandle;
class SessionService;

enum class HeartbeatErrorCode
{
    InvalidRequest,
    Unauthorized,
    ClientRejected,
    ServerError,
    NetworkError,
    TlsError,
    Timeout,
    Cancelled,
    StaleResponse,
    ProtocolError,
};

class HeartbeatError final
{
public:
    HeartbeatError(HeartbeatErrorCode code,
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

    [[nodiscard]] HeartbeatErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const QString &serverCode() const noexcept { return serverCode_; }
    [[nodiscard]] const QString &requestId() const noexcept { return requestId_; }
    [[nodiscard]] const std::optional<int> &httpStatus() const noexcept { return httpStatus_; }
    [[nodiscard]] const std::optional<ProtocolError> &protocolError() const noexcept
    {
        return protocolError_;
    }

private:
    HeartbeatErrorCode code_;
    QString message_;
    QString serverCode_;
    QString requestId_;
    std::optional<int> httpStatus_;
    std::optional<ProtocolError> protocolError_;
};

class HeartbeatRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit HeartbeatRequestHandle(QObject *parent = nullptr);
    ~HeartbeatRequestHandle() override;

    void cancel();

signals:
    void succeeded();
    void failed(const HeartbeatError &error);
    void finished();

private:
    friend class HeartbeatApiClient;
    void completeSuccess();
    void completeFailure(HeartbeatError error);

    QPointer<HttpRequestHandle> transportHandle_;
    bool completed_ = false;
};

class HeartbeatApiClient final : public QObject
{
    Q_OBJECT

public:
    HeartbeatApiClient(QUrl loginBaseUrl,
                       QUuid clientId,
                       QString appVersion,
                       HttpClient *httpClient,
                       SessionService *sessionService,
                       QObject *parent = nullptr);

    [[nodiscard]] HeartbeatRequestHandle *send(quint64 sessionGeneration);

private:
    [[nodiscard]] QUrl endpoint(const QString &path) const;

    QUrl loginBaseUrl_;
    QUuid clientId_;
    QString appVersion_;
    HttpClient *httpClient_;
    SessionService *sessionService_;
    quint64 requestGeneration_ = 0;
};

Q_DECLARE_METATYPE(HeartbeatError)
