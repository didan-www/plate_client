#pragma once

#include "domain/Errors.h"
#include "domain/RecognitionSnapshot.h"
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

enum class RecognitionApiErrorCode
{
    InvalidRequest,
    Unauthorized,
    Forbidden,
    NotFound,
    ClientRejected,
    ServerError,
    NetworkError,
    TlsError,
    Timeout,
    Cancelled,
    StaleResponse,
    ProtocolError,
};

class RecognitionApiError final
{
public:
    RecognitionApiError(RecognitionApiErrorCode code,
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

    [[nodiscard]] RecognitionApiErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const QString &serverCode() const noexcept { return serverCode_; }
    [[nodiscard]] const QString &requestId() const noexcept { return requestId_; }
    [[nodiscard]] const std::optional<int> &httpStatus() const noexcept { return httpStatus_; }
    [[nodiscard]] const std::optional<ProtocolError> &protocolError() const noexcept
    {
        return protocolError_;
    }

private:
    RecognitionApiErrorCode code_;
    QString message_;
    QString serverCode_;
    QString requestId_;
    std::optional<int> httpStatus_;
    std::optional<ProtocolError> protocolError_;
};

class RecognitionDetailRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit RecognitionDetailRequestHandle(RequestContext context,
                                            QObject *parent = nullptr);
    ~RecognitionDetailRequestHandle() override;

    void cancel();
    [[nodiscard]] const RequestContext &context() const noexcept;
    [[nodiscard]] const std::optional<RecognitionSnapshot> &snapshot() const noexcept;

signals:
    void succeeded();
    void failed(const RecognitionApiError &error);
    void finished();

private:
    friend class RecognitionApiClient;
    void completeSuccess(RecognitionSnapshot snapshot);
    void completeFailure(RecognitionApiError error);

    RequestContext context_;
    QPointer<HttpRequestHandle> transportHandle_;
    std::optional<RecognitionSnapshot> snapshot_;
    bool completed_ = false;
};

class RecognitionApiClient final : public QObject
{
    Q_OBJECT

public:
    RecognitionApiClient(QUrl loginBaseUrl,
                         HttpClient *httpClient,
                         SessionService *sessionService,
                         QObject *parent = nullptr);

    [[nodiscard]] RecognitionDetailRequestHandle *getDetail(
        const QString &recognitionId, quint64 sessionGeneration);

private:
    [[nodiscard]] QUrl detailEndpoint(const QString &recognitionId) const;

    QUrl loginBaseUrl_;
    HttpClient *httpClient_;
    SessionService *sessionService_;
    quint64 requestGeneration_ = 0;
};

Q_DECLARE_METATYPE(RecognitionApiError)
