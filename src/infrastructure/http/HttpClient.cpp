#include "infrastructure/http/HttpClient.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslError>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <limits>
#include <utility>

namespace {
class RequestState final : public QObject
{
public:
    RequestState(HttpRequest request, HttpClient::ContextValidator validator, QObject *parent)
        : QObject(parent)
        , request(std::move(request))
        , validator(std::move(validator))
    {
        timer.setSingleShot(true);
    }

    HttpRequest request;
    HttpClient::ContextValidator validator;
    QPointer<QNetworkReply> reply;
    QTimer timer;
    QByteArray body;
    bool completed = false;
};

std::optional<int> responseStatus(const RequestState *state)
{
    if (!state->reply) {
        return std::nullopt;
    }
    bool valid = false;
    const int status =
        state->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(&valid);
    return valid ? std::optional<int>(status) : std::nullopt;
}

HttpResponse responseFrom(const RequestState *state)
{
    QList<HttpResponse::Header> headers;
    int statusCode = 0;
    if (state->reply) {
        const auto pairs = state->reply->rawHeaderPairs();
        headers.reserve(pairs.size());
        for (const auto &pair : pairs) {
            headers.append(pair);
        }
        statusCode = state->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    return HttpResponse(statusCode, std::move(headers), state->body, state->request.context());
}

void finishFailure(RequestState *state, HttpRequestHandle *handle, HttpError error)
{
    if (state->completed) {
        return;
    }
    state->completed = true;
    state->timer.stop();
    const HttpResponse response = responseFrom(state);
    state->request.clearSensitiveData();
    QPointer<HttpRequestHandle> guardedHandle(handle);
    emit handle->failed(error, response);
    if (guardedHandle) {
        emit guardedHandle->finished();
    }
}

bool isCurrent(RequestState *state)
{
    return !state->validator || state->validator(state->request.context());
}

std::optional<qint64> contentLength(const QNetworkReply *reply)
{
    bool valid = false;
    const qint64 length = reply->rawHeader(QByteArrayLiteral("Content-Length")).toLongLong(&valid);
    if (!valid || length < 0) {
        return std::nullopt;
    }
    return length;
}

qint64 maximumBufferedBytes(const RequestState *state)
{
    if (!state->reply) {
        return HttpRequestPolicy::maximumBufferedBytes(state->request.responseMode());
    }
    const int status =
        state->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400 && status <= 599) {
        return HttpRequestPolicy::jsonMaximumBytes;
    }
    return HttpRequestPolicy::maximumBufferedBytes(state->request.responseMode());
}

QString networkMessage()
{
    return QStringLiteral("HTTP 网络请求失败。");
}
} // namespace

HttpRequestHandle::HttpRequestHandle(QObject *parent)
    : QObject(parent)
{
}

void HttpRequestHandle::cancel()
{
    if (cancelAction_) {
        cancelAction_();
    }
}

HttpClient::HttpClient(ContextValidator contextValidator, QObject *parent)
    : HttpClient(new QNetworkAccessManager(parent), std::move(contextValidator), {}, parent)
{
    networkManager_->setParent(this);
}

HttpClient::HttpClient(QNetworkAccessManager *networkManager,
                       ContextValidator contextValidator,
                       TimeoutResolver timeoutResolver,
                       QObject *parent)
    : QObject(parent)
    , networkManager_(networkManager)
    , contextValidator_(std::move(contextValidator))
    , timeoutResolver_(std::move(timeoutResolver))
{
    Q_ASSERT(networkManager_);
}

HttpRequestHandle *HttpClient::send(const HttpRequest &request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto *handle = new HttpRequestHandle(this);
    auto *state = new RequestState(request, contextValidator_, handle);
    connect(handle, &HttpRequestHandle::finished, handle, &QObject::deleteLater);

    QNetworkRequest networkRequest(request.url());
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setRawHeader(QByteArrayLiteral("Accept"),
                                HttpRequestPolicy::acceptHeader(request.responseMode()));
    if (request.responseMode() == HttpResponseMode::JsonMemory) {
        networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                                 QByteArrayLiteral("application/json"));
    }
    if (!request.bearerToken().isEmpty()) {
        networkRequest.setRawHeader(QByteArrayLiteral("Authorization"),
                                    QByteArrayLiteral("Bearer ")
                                        + request.bearerToken().toUtf8());
    }

    switch (request.method()) {
    case HttpMethod::Get:
        state->reply = networkManager_->get(networkRequest);
        break;
    case HttpMethod::Post:
        state->reply = networkManager_->post(networkRequest, request.body());
        break;
    case HttpMethod::Delete:
        state->reply = request.body().isEmpty()
                           ? networkManager_->deleteResource(networkRequest)
                           : networkManager_->sendCustomRequest(
                                 networkRequest, QByteArrayLiteral("DELETE"), request.body());
        break;
    }

    connect(handle, &QObject::destroyed, state->reply, [state, reply = state->reply] {
        state->completed = true;
        state->timer.stop();
        state->request.clearSensitiveData();
        if (reply && reply->isRunning()) {
            reply->abort();
        }
    });
    connect(state->reply, &QNetworkReply::finished, state->reply, &QObject::deleteLater);

    const auto failTooLarge = [state, handle] {
        if (state->completed) {
            return;
        }
        QPointer<QNetworkReply> reply = state->reply;
        finishFailure(state,
                      handle,
                      HttpError(HttpErrorCode::ResponseTooLarge,
                                QStringLiteral("HTTP 响应超过允许大小。"),
                                responseStatus(state)));
        if (reply) {
            reply->abort();
        }
    };

    const auto checkDeclaredLength = [state, failTooLarge] {
        if (state->completed || !state->reply) {
            return;
        }
        const int status =
            state->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (state->request.responseMode() == HttpResponseMode::CsvStream) {
            if (status < 400 || status > 599) {
                return;
            }
        }
        const auto length = contentLength(state->reply);
        const qint64 maximum = maximumBufferedBytes(state);
        if (length && *length > maximum) {
            failTooLarge();
        }
    };

    connect(state->reply, &QNetworkReply::metaDataChanged, handle, checkDeclaredLength);
    connect(state->reply, &QNetworkReply::sslErrors, handle,
            [state, handle](const QList<QSslError> &) {
                if (state->completed) {
                    return;
                }
                QPointer<QNetworkReply> reply = state->reply;
                finishFailure(state,
                              handle,
                              HttpError(HttpErrorCode::TlsError,
                                        QStringLiteral("TLS 证书校验失败。"),
                                        responseStatus(state)));
                if (reply) {
                    reply->abort();
                }
            });

    const auto consumeAvailable = [state, handle, failTooLarge] {
        if (state->completed || !state->reply) {
            return;
        }
        if (!isCurrent(state)) {
            QPointer<QNetworkReply> reply = state->reply;
            finishFailure(state,
                          handle,
                          HttpError(HttpErrorCode::StaleResponse,
                                    QStringLiteral("HTTP 响应上下文已失效。"),
                                    responseStatus(state)));
            if (reply) {
                reply->abort();
            }
            return;
        }
        const QByteArray chunk = state->reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        if (state->request.responseMode() == HttpResponseMode::CsvStream) {
            const int status =
                state->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 200 && status <= 299) {
                emit handle->dataChunkReceived(chunk);
                return;
            }
            if (status < 400 || status > 599) {
                return;
            }
        }
        const qint64 maximum = maximumBufferedBytes(state);
        if (state->body.size() > maximum - chunk.size()) {
            failTooLarge();
            return;
        }
        state->body.append(chunk);
    };

    connect(state->reply, &QIODevice::readyRead, handle, consumeAvailable);

    connect(&state->timer, &QTimer::timeout, handle, [state, handle] {
        if (state->completed) {
            return;
        }
        QPointer<QNetworkReply> reply = state->reply;
        finishFailure(state,
                      handle,
                      HttpError(HttpErrorCode::Timeout,
                                QStringLiteral("HTTP 请求超过总时限。"),
                                responseStatus(state)));
        if (reply) {
            reply->abort();
        }
    });

    handle->cancelAction_ = [state, handle] {
        if (state->completed) {
            return;
        }
        QPointer<QNetworkReply> reply = state->reply;
        finishFailure(state,
                      handle,
                      HttpError(HttpErrorCode::Cancelled,
                                QStringLiteral("HTTP 请求已取消。"),
                                responseStatus(state)));
        if (reply) {
            reply->abort();
        }
    };

    connect(state->reply, &QNetworkReply::finished, handle,
            [state, handle, consumeAvailable] {
                if (state->completed) {
                    if (state->reply) {
                        state->reply->deleteLater();
                    }
                    return;
                }
                state->timer.stop();
                QPointer<HttpRequestHandle> guardedHandle(handle);
                consumeAvailable();
                if (!guardedHandle) {
                    return;
                }
                if (state->completed) {
                    if (state->reply) {
                        state->reply->deleteLater();
                    }
                    return;
                }
                if (!isCurrent(state)) {
                    finishFailure(state,
                                  handle,
                                  HttpError(HttpErrorCode::StaleResponse,
                                            QStringLiteral("HTTP 响应上下文已失效。")));
                } else {
                    const HttpResponse response = responseFrom(state);
                    const int status = response.statusCode();
                    const bool redirected =
                        state->reply->attribute(QNetworkRequest::RedirectionTargetAttribute)
                            .isValid()
                        || (status >= 300 && status <= 399);
                    if (redirected) {
                        finishFailure(state,
                                      handle,
                                      HttpError(HttpErrorCode::RedirectRejected,
                                                QStringLiteral("HTTP 重定向已拒绝。"), status));
                    } else if (state->reply->error() != QNetworkReply::NoError
                               && (status < 400 || status > 599)) {
                        finishFailure(
                            state,
                            handle,
                            HttpError(HttpErrorCode::NetworkError,
                                      networkMessage(),
                                      status > 0 ? std::optional<int>(status) : std::nullopt,
                                      static_cast<int>(state->reply->error())));
                    } else if (state->request.responseMode() == HttpResponseMode::JsonMemory) {
                        QByteArray contentType =
                            state->reply->header(QNetworkRequest::ContentTypeHeader)
                                .toByteArray();
                        const qsizetype separator = contentType.indexOf(';');
                        if (separator >= 0) {
                            contentType.truncate(separator);
                        }
                        contentType = contentType.trimmed().toLower();
                        QJsonParseError parseError;
                        QJsonDocument::fromJson(response.body(), &parseError);
                        if (contentType != QByteArrayLiteral("application/json")
                            || parseError.error != QJsonParseError::NoError) {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::ParseFailure,
                                                    QStringLiteral("HTTP JSON 响应无法解析。"),
                                                    status));
                        } else if (status >= 400 && status <= 499) {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::ClientError,
                                                    QStringLiteral("HTTP 客户端请求失败。"),
                                                    status));
                        } else if (status >= 500 && status <= 599) {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::ServerError,
                                                    QStringLiteral("HTTP 服务端请求失败。"),
                                                    status));
                        } else if (state->reply->error() != QNetworkReply::NoError) {
                            finishFailure(
                                state,
                                handle,
                                HttpError(HttpErrorCode::NetworkError,
                                          networkMessage(),
                                          std::nullopt,
                                          static_cast<int>(state->reply->error())));
                        } else if (status >= 200 && status <= 299) {
                            state->completed = true;
                            state->request.clearSensitiveData();
                            QPointer<HttpRequestHandle> guardedHandle(handle);
                            emit handle->succeeded(response);
                            if (guardedHandle) {
                                emit guardedHandle->finished();
                            }
                        } else {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::NetworkError,
                                                    QStringLiteral("HTTP 状态无效。"), status));
                        }
                    } else if (status >= 400 && status <= 599) {
                        QByteArray contentType =
                            state->reply->header(QNetworkRequest::ContentTypeHeader)
                                .toByteArray();
                        const qsizetype separator = contentType.indexOf(';');
                        if (separator >= 0) {
                            contentType.truncate(separator);
                        }
                        contentType = contentType.trimmed().toLower();
                        QJsonParseError parseError;
                        QJsonDocument::fromJson(response.body(), &parseError);
                        if (contentType != QByteArrayLiteral("application/json")
                            || parseError.error != QJsonParseError::NoError) {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::ParseFailure,
                                                    QStringLiteral("HTTP JSON 错误响应无法解析。"),
                                                    status));
                        } else if (status <= 499) {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::ClientError,
                                                    QStringLiteral("HTTP 客户端请求失败。"),
                                                    status));
                        } else {
                            finishFailure(state,
                                          handle,
                                          HttpError(HttpErrorCode::ServerError,
                                                    QStringLiteral("HTTP 服务端请求失败。"),
                                                    status));
                        }
                    } else if (state->reply->error() != QNetworkReply::NoError) {
                        finishFailure(
                            state,
                            handle,
                            HttpError(HttpErrorCode::NetworkError,
                                      networkMessage(),
                                      std::nullopt,
                                      static_cast<int>(state->reply->error())));
                    } else if (status >= 200 && status <= 299) {
                        state->completed = true;
                        state->request.clearSensitiveData();
                        QPointer<HttpRequestHandle> guardedHandle(handle);
                        emit handle->succeeded(response);
                        if (guardedHandle) {
                            emit guardedHandle->finished();
                        }
                    } else {
                        finishFailure(state,
                                      handle,
                                      HttpError(HttpErrorCode::NetworkError,
                                                QStringLiteral("HTTP 状态无效。"), status));
                    }
                }
            });

    const std::chrono::milliseconds timeout = timeoutResolver_
                                                  ? timeoutResolver_(request.kind())
                                                  : HttpRequestPolicy::timeout(request.kind());
    const qint64 timeoutCount = timeout.count();
    const int timerInterval = timeoutCount > std::numeric_limits<int>::max()
                                  ? std::numeric_limits<int>::max()
                                  : static_cast<int>(timeoutCount);
    state->timer.start(timerInterval);
    QTimer::singleShot(0, handle, checkDeclaredLength);
    return handle;
}
