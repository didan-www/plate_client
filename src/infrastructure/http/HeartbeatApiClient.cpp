#include "infrastructure/http/HeartbeatApiClient.h"

#include "domain/HttpEnvelope.h"
#include "domain/RequestContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/JsonCodecs.h"
#include "services/SessionService.h"

#include <QTimer>

#include <utility>

namespace {
HeartbeatError protocolFailure(const ProtocolError &error, std::optional<int> status = std::nullopt)
{
    return HeartbeatError(HeartbeatErrorCode::ProtocolError,
                          error.message(),
                          {},
                          {},
                          status,
                          error);
}

HeartbeatErrorCode transportErrorCode(HttpErrorCode code)
{
    switch (code) {
    case HttpErrorCode::TlsError:
        return HeartbeatErrorCode::TlsError;
    case HttpErrorCode::Timeout:
        return HeartbeatErrorCode::Timeout;
    case HttpErrorCode::Cancelled:
        return HeartbeatErrorCode::Cancelled;
    case HttpErrorCode::StaleResponse:
        return HeartbeatErrorCode::StaleResponse;
    case HttpErrorCode::ClientError:
        return HeartbeatErrorCode::ClientRejected;
    case HttpErrorCode::ServerError:
        return HeartbeatErrorCode::ServerError;
    case HttpErrorCode::NetworkError:
        return HeartbeatErrorCode::NetworkError;
    case HttpErrorCode::ResponseTooLarge:
    case HttpErrorCode::ParseFailure:
    case HttpErrorCode::RedirectRejected:
        return HeartbeatErrorCode::ProtocolError;
    }
    return HeartbeatErrorCode::NetworkError;
}

HeartbeatError mapTransportFailure(const HttpError &error, const HttpResponse &response)
{
    const std::optional<int> status = error.httpStatus();
    if ((error.code() == HttpErrorCode::ClientError
         || error.code() == HttpErrorCode::ServerError)
        && status.has_value()) {
        auto decoded = JsonCodecs::decodeEmptyEnvelope(response.body());
        if (!decoded.isSuccess()) {
            return protocolFailure(*decoded.error(), status);
        }
        auto envelope = decoded.takeValue();
        if (!envelope.has_value() || envelope->success() || envelope->data().has_value()) {
            return HeartbeatError(HeartbeatErrorCode::ProtocolError,
                                  QStringLiteral("心跳错误响应不符合统一信封。"),
                                  {},
                                  {},
                                  status);
        }

        HeartbeatErrorCode code = HeartbeatErrorCode::ClientRejected;
        if (*status == 401) {
            code = HeartbeatErrorCode::Unauthorized;
        } else if (*status >= 500) {
            code = HeartbeatErrorCode::ServerError;
        }
        return HeartbeatError(code,
                              envelope->message().isEmpty()
                                  ? QStringLiteral("心跳请求失败。")
                                  : envelope->message(),
                              envelope->code(),
                              envelope->requestId(),
                              status);
    }

    return HeartbeatError(transportErrorCode(error.code()),
                          error.message(),
                          {},
                          {},
                          status,
                          error.protocolError());
}
} // namespace

HeartbeatRequestHandle::HeartbeatRequestHandle(QObject *parent)
    : QObject(parent)
{
}

HeartbeatRequestHandle::~HeartbeatRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void HeartbeatRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void HeartbeatRequestHandle::completeSuccess()
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<HeartbeatRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void HeartbeatRequestHandle::completeFailure(HeartbeatError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<HeartbeatRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

HeartbeatApiClient::HeartbeatApiClient(QUrl loginBaseUrl,
                                       QUuid clientId,
                                       QString appVersion,
                                       HttpClient *httpClient,
                                       SessionService *sessionService,
                                       QObject *parent)
    : QObject(parent)
    , loginBaseUrl_(std::move(loginBaseUrl))
    , clientId_(std::move(clientId))
    , appVersion_(std::move(appVersion))
    , httpClient_(httpClient)
    , sessionService_(sessionService)
{
    Q_ASSERT(httpClient_);
    Q_ASSERT(sessionService_);
}

HeartbeatRequestHandle *HeartbeatApiClient::send(quint64 sessionGeneration)
{
    auto *handle = new HeartbeatRequestHandle(this);
    connect(handle, &HeartbeatRequestHandle::finished, handle, &QObject::deleteLater);

    auto encoded = JsonCodecs::encodeHeartbeatRequest(clientId_, appVersion_);
    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    if (!encoded.isSuccess() || sessionGeneration == 0 || !hasToken) {
        const std::optional<ProtocolError> protocolError = encoded.error();
        QTimer::singleShot(0, handle, [handle, protocolError] {
            handle->completeFailure(
                HeartbeatError(protocolError.has_value() ? HeartbeatErrorCode::InvalidRequest
                                                         : HeartbeatErrorCode::StaleResponse,
                               protocolError.has_value() ? protocolError->message()
                                                         : QStringLiteral("心跳请求会话已经失效。"),
                               {},
                               {},
                               {},
                               protocolError));
        });
        return handle;
    }

    std::optional<QByteArray> body = encoded.takeValue();
    const RequestContext context(sessionGeneration,
                                 ++requestGeneration_,
                                 QStringLiteral("client.heartbeat"));
    HttpRequest request(HttpMethod::Post,
                        endpoint(QStringLiteral("/api/v1/clients/heartbeat")),
                        HttpRequestKind::Heartbeat,
                        HttpResponseMode::JsonMemory,
                        context,
                        std::move(*body),
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;

    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                auto decoded = JsonCodecs::decodeEmptyEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(), response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope.has_value() || !envelope->success()
                    || !envelope->data().has_value()) {
                    handle->completeFailure(
                        HeartbeatError(HeartbeatErrorCode::ProtocolError,
                                       QStringLiteral("心跳成功响应不符合统一信封。"),
                                       envelope.has_value() ? envelope->code() : QString {},
                                       envelope.has_value() ? envelope->requestId() : QString {},
                                       response.statusCode()));
                    return;
                }
                handle->completeSuccess();
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

QUrl HeartbeatApiClient::endpoint(const QString &path) const
{
    QUrl result = loginBaseUrl_;
    QString prefix = result.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    result.setPath(prefix + path);
    return result;
}
