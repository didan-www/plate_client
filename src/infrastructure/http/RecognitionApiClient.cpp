#include "infrastructure/http/RecognitionApiClient.h"

#include "domain/HttpEnvelope.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/JsonCodecs.h"
#include "services/SessionService.h"

#include <QTimer>

#include <utility>

namespace {
RecognitionApiError protocolFailure(const ProtocolError &error,
                                    std::optional<int> status = std::nullopt)
{
    return RecognitionApiError(RecognitionApiErrorCode::ProtocolError,
                               error.message(),
                               {},
                               {},
                               status,
                               error);
}

RecognitionApiErrorCode transportErrorCode(HttpErrorCode code)
{
    switch (code) {
    case HttpErrorCode::TlsError:
        return RecognitionApiErrorCode::TlsError;
    case HttpErrorCode::Timeout:
        return RecognitionApiErrorCode::Timeout;
    case HttpErrorCode::Cancelled:
        return RecognitionApiErrorCode::Cancelled;
    case HttpErrorCode::StaleResponse:
        return RecognitionApiErrorCode::StaleResponse;
    case HttpErrorCode::ClientError:
        return RecognitionApiErrorCode::ClientRejected;
    case HttpErrorCode::ServerError:
        return RecognitionApiErrorCode::ServerError;
    case HttpErrorCode::NetworkError:
        return RecognitionApiErrorCode::NetworkError;
    case HttpErrorCode::ResponseTooLarge:
    case HttpErrorCode::ParseFailure:
    case HttpErrorCode::RedirectRejected:
        return RecognitionApiErrorCode::ProtocolError;
    }
    return RecognitionApiErrorCode::NetworkError;
}

RecognitionApiError mapTransportFailure(const HttpError &error,
                                        const HttpResponse &response)
{
    const std::optional<int> status = error.httpStatus();
    if ((error.code() == HttpErrorCode::ClientError
         || error.code() == HttpErrorCode::ServerError)
        && status.has_value()) {
        auto decoded = JsonCodecs::decodeRecognitionEnvelope(response.body());
        if (!decoded.isSuccess()) {
            return protocolFailure(*decoded.error(), status);
        }
        auto envelope = decoded.takeValue();
        if (!envelope || envelope->success() || envelope->data().has_value()) {
            return RecognitionApiError(RecognitionApiErrorCode::ProtocolError,
                                       QStringLiteral("识别详情错误响应不符合统一信封。"),
                                       {},
                                       {},
                                       status);
        }

        RecognitionApiErrorCode code = RecognitionApiErrorCode::ClientRejected;
        if (*status == 401) {
            code = RecognitionApiErrorCode::Unauthorized;
        } else if (*status == 403) {
            code = RecognitionApiErrorCode::Forbidden;
        } else if (*status == 404) {
            code = RecognitionApiErrorCode::NotFound;
        } else if (*status >= 500) {
            code = RecognitionApiErrorCode::ServerError;
        }
        return RecognitionApiError(code,
                                   envelope->message().isEmpty()
                                       ? QStringLiteral("识别详情请求失败。")
                                       : envelope->message(),
                                   envelope->code(),
                                   envelope->requestId(),
                                   status);
    }

    return RecognitionApiError(transportErrorCode(error.code()),
                               error.message(),
                               {},
                               {},
                               status,
                               error.protocolError());
}
} // namespace

RecognitionDetailRequestHandle::RecognitionDetailRequestHandle(RequestContext context,
                                                               QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

RecognitionDetailRequestHandle::~RecognitionDetailRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void RecognitionDetailRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &RecognitionDetailRequestHandle::context() const noexcept
{
    return context_;
}

const std::optional<RecognitionSnapshot> &RecognitionDetailRequestHandle::snapshot() const noexcept
{
    return snapshot_;
}

void RecognitionDetailRequestHandle::completeSuccess(RecognitionSnapshot snapshot)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    snapshot_.emplace(std::move(snapshot));
    QPointer<RecognitionDetailRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void RecognitionDetailRequestHandle::completeFailure(RecognitionApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<RecognitionDetailRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

RecognitionApiClient::RecognitionApiClient(QUrl loginBaseUrl,
                                           HttpClient *httpClient,
                                           SessionService *sessionService,
                                           QObject *parent)
    : QObject(parent)
    , loginBaseUrl_(std::move(loginBaseUrl))
    , httpClient_(httpClient)
    , sessionService_(sessionService)
{
    Q_ASSERT(httpClient_);
    Q_ASSERT(sessionService_);
}

RecognitionDetailRequestHandle *RecognitionApiClient::getDetail(
    const QString &recognitionId, quint64 sessionGeneration)
{
    const RequestContext context(sessionGeneration,
                                 ++requestGeneration_,
                                 QStringLiteral("recognition.detail"),
                                 recognitionId);
    auto *handle = new RecognitionDetailRequestHandle(context, this);
    connect(handle, &RecognitionDetailRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    if (recognitionId.isEmpty() || sessionGeneration == 0 || !hasToken) {
        QTimer::singleShot(0, handle, [handle, recognitionId] {
            handle->completeFailure(RecognitionApiError(
                recognitionId.isEmpty() ? RecognitionApiErrorCode::InvalidRequest
                                        : RecognitionApiErrorCode::StaleResponse,
                recognitionId.isEmpty() ? QStringLiteral("识别记录 ID 不得为空。")
                                        : QStringLiteral("识别详情请求会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Get,
                        detailEndpoint(recognitionId),
                        HttpRequestKind::Query,
                        HttpResponseMode::JsonMemory,
                        context,
                        {},
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;
    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle, recognitionId](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                auto decoded = JsonCodecs::decodeRecognitionEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(), response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope || !envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::ProtocolError,
                        QStringLiteral("识别详情成功响应不符合统一信封。"),
                        envelope ? envelope->code() : QString {},
                        envelope ? envelope->requestId() : QString {},
                        response.statusCode()));
                    return;
                }
                std::optional<RecognitionSnapshot> snapshot = envelope->takeData();
                if (!snapshot || snapshot->recognitionId() != recognitionId) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::ProtocolError,
                        QStringLiteral("识别详情响应 ID 与请求不匹配。"),
                        {},
                        envelope->requestId(),
                        response.statusCode(),
                        ProtocolError(ProtocolErrorCode::InvalidCombination,
                                      QStringLiteral("$.data.recognitionId"),
                                      QStringLiteral("响应 ID 与请求 ID 不一致。"))));
                    return;
                }
                handle->completeSuccess(std::move(*snapshot));
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    sessionService_->handleUnauthorized(response.context().sessionGeneration());
                }
                handle->completeFailure(mapTransportFailure(error, response));
            });
    return handle;
}

QUrl RecognitionApiClient::detailEndpoint(const QString &recognitionId) const
{
    QUrl result = loginBaseUrl_;
    QString prefix = result.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(recognitionId));
    result.setPath(prefix + QStringLiteral("/api/v1/recognitions/") + encodedId,
                   QUrl::TolerantMode);
    return result;
}
