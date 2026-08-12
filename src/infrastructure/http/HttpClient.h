#pragma once

#include "domain/RequestContext.h"
#include "infrastructure/http/HttpError.h"
#include "infrastructure/http/HttpRequestPolicy.h"

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QUrl>

#include <chrono>
#include <functional>
#include <utility>

enum class HttpMethod
{
    Get,
    Post,
    Delete,
};

class HttpRequest final
{
public:
    HttpRequest(HttpMethod method,
                QUrl url,
                HttpRequestKind kind,
                HttpResponseMode responseMode,
                RequestContext context,
                QByteArray body = {},
                QString bearerToken = {})
        : method_(method)
        , url_(std::move(url))
        , kind_(kind)
        , responseMode_(responseMode)
        , context_(std::move(context))
        , body_(std::move(body))
        , bearerToken_(std::move(bearerToken))
    {
    }

    [[nodiscard]] HttpMethod method() const noexcept { return method_; }
    [[nodiscard]] const QUrl &url() const noexcept { return url_; }
    [[nodiscard]] HttpRequestKind kind() const noexcept { return kind_; }
    [[nodiscard]] HttpResponseMode responseMode() const noexcept { return responseMode_; }
    [[nodiscard]] const RequestContext &context() const noexcept { return context_; }
    [[nodiscard]] const QByteArray &body() const noexcept { return body_; }
    [[nodiscard]] const QString &bearerToken() const noexcept { return bearerToken_; }

    void clearSensitiveData() noexcept
    {
        body_.fill('\0');
        body_.clear();
        bearerToken_.fill(QChar::Null);
        bearerToken_.clear();
    }

private:
    HttpMethod method_;
    QUrl url_;
    HttpRequestKind kind_;
    HttpResponseMode responseMode_;
    RequestContext context_;
    QByteArray body_;
    QString bearerToken_;
};

class HttpResponse final
{
public:
    using Header = QPair<QByteArray, QByteArray>;

    HttpResponse(int statusCode,
                 QList<Header> headers,
                 QByteArray body,
                 RequestContext context)
        : statusCode_(statusCode)
        , headers_(std::move(headers))
        , body_(std::move(body))
        , context_(std::move(context))
    {
    }

    [[nodiscard]] int statusCode() const noexcept { return statusCode_; }
    [[nodiscard]] const QList<Header> &headers() const noexcept { return headers_; }
    [[nodiscard]] const QByteArray &body() const noexcept { return body_; }
    [[nodiscard]] const RequestContext &context() const noexcept { return context_; }

private:
    int statusCode_;
    QList<Header> headers_;
    QByteArray body_;
    RequestContext context_;
};

class HttpRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit HttpRequestHandle(QObject *parent = nullptr);
    void cancel();

signals:
    void dataChunkReceived(const QByteArray &chunk);
    void succeeded(const HttpResponse &response);
    void failed(const HttpError &error, const HttpResponse &response);
    void finished();

private:
    friend class HttpClient;
    std::function<void()> cancelAction_;
};

class HttpClient final : public QObject
{
    Q_OBJECT

public:
    using ContextValidator = std::function<bool(const RequestContext &)>;
    using TimeoutResolver =
        std::function<std::chrono::milliseconds(HttpRequestKind)>;

    explicit HttpClient(ContextValidator contextValidator = {}, QObject *parent = nullptr);
    HttpClient(QNetworkAccessManager *networkManager,
               ContextValidator contextValidator,
               TimeoutResolver timeoutResolver,
               QObject *parent = nullptr);

    [[nodiscard]] HttpRequestHandle *send(const HttpRequest &request);

private:
    QNetworkAccessManager *networkManager_;
    ContextValidator contextValidator_;
    TimeoutResolver timeoutResolver_;
};

Q_DECLARE_METATYPE(HttpError)
Q_DECLARE_METATYPE(HttpResponse)
