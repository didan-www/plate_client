#include "infrastructure/http/AuthApiClient.h"

#include "domain/HttpEnvelope.h"
#include "domain/RequestContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/JsonCodecs.h"

#include <QPointer>
#include <QTimer>

#include <utility>

namespace {
AuthError protocolFailure(const ProtocolError &error, std::optional<int> status = std::nullopt)
{
    return AuthError(AuthErrorCode::ProtocolError,
                     error.message(),
                     {},
                     {},
                     status,
                     error);
}

AuthErrorCode transportErrorCode(HttpErrorCode code)
{
    switch (code) {
    case HttpErrorCode::TlsError:
        return AuthErrorCode::TlsError;
    case HttpErrorCode::Timeout:
        return AuthErrorCode::Timeout;
    case HttpErrorCode::Cancelled:
        return AuthErrorCode::Cancelled;
    case HttpErrorCode::StaleResponse:
        return AuthErrorCode::StaleResponse;
    case HttpErrorCode::ResponseTooLarge:
    case HttpErrorCode::ParseFailure:
        return AuthErrorCode::ProtocolError;
    case HttpErrorCode::ClientError:
        return AuthErrorCode::ClientRejected;
    case HttpErrorCode::ServerError:
        return AuthErrorCode::ServerError;
    case HttpErrorCode::NetworkError:
    case HttpErrorCode::RedirectRejected:
        return AuthErrorCode::NetworkError;
    }
    return AuthErrorCode::NetworkError;
}

template<typename Envelope>
AuthError envelopeFailure(const Envelope &envelope, int status)
{
    AuthErrorCode code = AuthErrorCode::ClientRejected;
    if (status == 401) {
        code = AuthErrorCode::Unauthorized;
    } else if (status == 403) {
        code = AuthErrorCode::Forbidden;
    } else if (status >= 500) {
        code = AuthErrorCode::ServerError;
    }
    return AuthError(code,
                     envelope.message().isEmpty() ? QStringLiteral("认证请求失败。")
                                                  : envelope.message(),
                     envelope.code(),
                     envelope.requestId(),
                     status);
}

template<typename Decode>
AuthError mapTransportFailure(const HttpError &error,
                              const HttpResponse &response,
                              Decode decode)
{
    const std::optional<int> status = error.httpStatus();
    if ((error.code() == HttpErrorCode::ClientError
         || error.code() == HttpErrorCode::ServerError)
        && status.has_value()) {
        auto decoded = decode(response.body());
        if (!decoded.isSuccess()) {
            return protocolFailure(*decoded.error(), status);
        }
        auto envelope = decoded.takeValue();
        if (!envelope.has_value() || envelope->success() || envelope->data().has_value()) {
            return AuthError(AuthErrorCode::ProtocolError,
                             QStringLiteral("认证错误响应不符合统一信封。"),
                             {},
                             {},
                             status);
        }
        return envelopeFailure(*envelope, *status);
    }
    return AuthError(transportErrorCode(error.code()), error.message(), {}, {}, status,
                     error.protocolError());
}

} // namespace

LoginRequestHandle::LoginRequestHandle(QObject *parent)
    : QObject(parent)
{
}

LoginRequestHandle::~LoginRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void LoginRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const std::optional<SessionSummary> &LoginRequestHandle::summary() const noexcept
{
    return summary_;
}

void LoginRequestHandle::completeFailure(AuthError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<LoginRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

LogoutRequestHandle::LogoutRequestHandle(QObject *parent)
    : QObject(parent)
{
}

LogoutRequestHandle::~LogoutRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void LogoutRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void LogoutRequestHandle::completeFailure(AuthError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<LogoutRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

AuthApiClient::AuthApiClient(QUrl loginBaseUrl,
                             QUuid clientId,
                             HttpClient *httpClient,
                             SessionService *sessionService,
                             QObject *parent)
    : QObject(parent)
    , loginBaseUrl_(std::move(loginBaseUrl))
    , clientId_(std::move(clientId))
    , httpClient_(httpClient)
    , sessionService_(sessionService)
{
    Q_ASSERT(httpClient_);
    Q_ASSERT(sessionService_);
}

LoginRequestHandle *AuthApiClient::login(const QString &username, const QString &password)
{
    auto *handle = new LoginRequestHandle(this);
    connect(handle, &LoginRequestHandle::finished, handle, &QObject::deleteLater);

    auto encoded = JsonCodecs::encodeLoginRequest(username, password, clientId_);
    if (!encoded.isSuccess()) {
        const ProtocolError error = *encoded.error();
        QTimer::singleShot(0, handle, [handle, error] {
            handle->completeFailure(
                AuthError(AuthErrorCode::InvalidRequest, error.message(), {}, {}, {}, error));
        });
        return handle;
    }

    std::optional<QByteArray> body = encoded.takeValue();
    const quint64 loginRequestGeneration = ++requestGeneration_;
    currentLoginRequestGeneration_ = loginRequestGeneration;
    const RequestContext context(0, loginRequestGeneration, QStringLiteral("auth.login"));
    HttpRequest request(HttpMethod::Post,
                        endpoint(QStringLiteral("/api/v1/auth/login")),
                        HttpRequestKind::Login,
                        HttpResponseMode::JsonMemory,
                        context,
                        std::move(*body));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;

    connect(transport, &HttpRequestHandle::succeeded, handle,
            [this, handle, loginRequestGeneration](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                if (loginRequestGeneration != currentLoginRequestGeneration_) {
                    handle->completeFailure(
                        AuthError(AuthErrorCode::StaleResponse,
                                  QStringLiteral("登录响应已经失效。")));
                    return;
                }
                auto decoded = JsonCodecs::decodeLoginEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(), response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(
                        AuthError(AuthErrorCode::ProtocolError,
                                  QStringLiteral("登录成功响应不符合统一信封。"),
                                  envelope->code(),
                                  envelope->requestId(),
                                  response.statusCode()));
                    return;
                }
                std::optional<SessionContext> session = envelope->takeData();
                if (!session || !sessionService_->establishSession(std::move(*session))) {
                    handle->completeFailure(
                        AuthError(AuthErrorCode::ProtocolError,
                                  QStringLiteral("登录响应无法建立有效会话。"),
                                  {},
                                  envelope->requestId(),
                                  response.statusCode()));
                    return;
                }
                handle->summary_ = sessionService_->summary();
                handle->completed_ = true;
                QPointer<LoginRequestHandle> guarded(handle);
                emit handle->succeeded();
                if (guarded) {
                    emit guarded->finished();
                }
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle, loginRequestGeneration](const HttpError &error,
                                                   const HttpResponse &response) {
                if (loginRequestGeneration != currentLoginRequestGeneration_) {
                    handle->completeFailure(
                        AuthError(AuthErrorCode::StaleResponse,
                                  QStringLiteral("登录响应已经失效。")));
                    return;
                }
                handle->completeFailure(
                    mapTransportFailure(error, response, JsonCodecs::decodeLoginEnvelope));
            });
    return handle;
}

LogoutRequestHandle *AuthApiClient::logout(QString accessToken,
                                           quint64 sessionGeneration)
{
    auto *handle = new LogoutRequestHandle(this);
    connect(handle, &LogoutRequestHandle::finished, handle, &QObject::deleteLater);
    if (accessToken.isEmpty() || sessionGeneration == 0) {
        QTimer::singleShot(0, handle, [handle] {
            handle->completeFailure(
                AuthError(AuthErrorCode::InvalidRequest,
                          QStringLiteral("注销请求缺少有效会话。")));
        });
        return handle;
    }

    const RequestContext context(sessionGeneration,
                                 ++requestGeneration_,
                                 QStringLiteral("auth.logout"));
    HttpRequest request(HttpMethod::Post,
                        endpoint(QStringLiteral("/api/v1/auth/logout")),
                        HttpRequestKind::Logout,
                        HttpResponseMode::JsonMemory,
                        context,
                        {},
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
                if (!envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(
                        AuthError(AuthErrorCode::ProtocolError,
                                  QStringLiteral("注销成功响应不符合统一信封。"),
                                  envelope->code(),
                                  envelope->requestId(),
                                  response.statusCode()));
                    return;
                }
                handle->completed_ = true;
                QPointer<LogoutRequestHandle> guarded(handle);
                emit handle->succeeded();
                if (guarded) {
                    emit guarded->finished();
                }
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [handle](const HttpError &error, const HttpResponse &response) {
                handle->completeFailure(
                    mapTransportFailure(error, response, JsonCodecs::decodeEmptyEnvelope));
            });
    return handle;
}

QUrl AuthApiClient::endpoint(const QString &path) const
{
    QUrl result = loginBaseUrl_;
    QString prefix = result.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    result.setPath(prefix + path);
    return result;
}
