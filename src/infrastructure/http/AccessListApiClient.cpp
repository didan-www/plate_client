#include "infrastructure/http/AccessListApiClient.h"

#include "domain/HttpEnvelope.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/JsonCodecs.h"
#include "services/SessionService.h"

#include <QChar>
#include <QList>
#include <QTimer>
#include <QUrlQuery>

#include <limits>
#include <utility>

namespace {
constexpr qint64 maximumSafeJsonInteger = 9007199254740991LL;

QString listTypeText(AccessListType listType)
{
    switch (listType) {
    case AccessListType::White:
        return QStringLiteral("WHITE");
    case AccessListType::Black:
        return QStringLiteral("BLACK");
    }
    return {};
}

bool isUnicodeWhitespace(char32_t codePoint)
{
    if ((codePoint >= 0x0009U && codePoint <= 0x000DU) || codePoint == 0x0085U) {
        return true;
    }
    const QChar::Category category = QChar::category(codePoint);
    return category == QChar::Separator_Space || category == QChar::Separator_Line
           || category == QChar::Separator_Paragraph;
}

bool isWellFormedUtf16(const QString &value)
{
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar current = value.at(index);
        if (current.isHighSurrogate()) {
            if (index + 1 >= value.size() || !value.at(index + 1).isLowSurrogate()) {
                return false;
            }
            ++index;
        } else if (current.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool isNormalizedPlateLike(const QString &value, bool allowEmpty)
{
    if (!isWellFormedUtf16(value)) {
        return false;
    }
    const QList<uint> codePoints = value.toUcs4();
    if ((!allowEmpty && codePoints.isEmpty()) || codePoints.size() > 16) {
        return false;
    }
    for (const uint codePoint : codePoints) {
        if (isUnicodeWhitespace(codePoint)
            || (codePoint >= static_cast<uint>('a')
                && codePoint <= static_cast<uint>('z'))) {
            return false;
        }
    }
    return true;
}

bool hasValidRemark(const QString &remark)
{
    return isWellFormedUtf16(remark) && remark.toUcs4().size() <= 200;
}

AccessListApiError protocolFailure(const ProtocolError &error,
                                   std::optional<int> status = std::nullopt)
{
    return AccessListApiError(AccessListApiErrorCode::ProtocolError,
                              error.message(), {}, {}, status, error);
}

AccessListApiErrorCode transportErrorCode(HttpErrorCode code)
{
    switch (code) {
    case HttpErrorCode::TlsError:
        return AccessListApiErrorCode::TlsError;
    case HttpErrorCode::Timeout:
        return AccessListApiErrorCode::Timeout;
    case HttpErrorCode::Cancelled:
        return AccessListApiErrorCode::Cancelled;
    case HttpErrorCode::StaleResponse:
        return AccessListApiErrorCode::StaleResponse;
    case HttpErrorCode::ResponseTooLarge:
        return AccessListApiErrorCode::ResponseTooLarge;
    case HttpErrorCode::ClientError:
        return AccessListApiErrorCode::ClientRejected;
    case HttpErrorCode::ServerError:
        return AccessListApiErrorCode::ServerError;
    case HttpErrorCode::NetworkError:
        return AccessListApiErrorCode::NetworkError;
    case HttpErrorCode::ParseFailure:
    case HttpErrorCode::RedirectRejected:
        return AccessListApiErrorCode::ProtocolError;
    }
    return AccessListApiErrorCode::NetworkError;
}

AccessListApiError mapTransportFailure(const HttpError &error,
                                       const HttpResponse &response,
                                       const QString &description)
{
    const std::optional<int> status = error.httpStatus();
    if ((error.code() == HttpErrorCode::ClientError
         || error.code() == HttpErrorCode::ServerError)
        && status.has_value()) {
        auto decoded = JsonCodecs::decodeAccessListEnvelope(response.body());
        if (!decoded.isSuccess()) {
            return protocolFailure(*decoded.error(), status);
        }
        auto envelope = decoded.takeValue();
        if (!envelope || envelope->success() || envelope->data().has_value()) {
            return AccessListApiError(
                AccessListApiErrorCode::ProtocolError,
                description + QStringLiteral("错误响应不符合统一信封。"),
                {}, {}, status);
        }

        const QString &serverCode = envelope->code();
        AccessListApiErrorCode code = AccessListApiErrorCode::ClientRejected;
        if (*status == 401) {
            code = AccessListApiErrorCode::Unauthorized;
        } else if (*status == 403) {
            code = AccessListApiErrorCode::Forbidden;
        } else if (*status == 404) {
            if (serverCode != QStringLiteral("ACCESS_LIST_NOT_FOUND")) {
                return AccessListApiError(
                    AccessListApiErrorCode::ProtocolError,
                    QStringLiteral("名单 404 错误码不符合协议。"),
                    serverCode, envelope->requestId(), status);
            }
            code = AccessListApiErrorCode::NotFound;
        } else if (*status == 409) {
            if (serverCode == QStringLiteral("ACCESS_LIST_CONFLICT_WHITE")) {
                code = AccessListApiErrorCode::ConflictWhite;
            } else if (serverCode == QStringLiteral("ACCESS_LIST_CONFLICT_BLACK")) {
                code = AccessListApiErrorCode::ConflictBlack;
            } else {
                return AccessListApiError(
                    AccessListApiErrorCode::ProtocolError,
                    QStringLiteral("名单 409 错误码不符合协议。"),
                    serverCode, envelope->requestId(), status);
            }
        } else if (*status >= 500) {
            code = AccessListApiErrorCode::ServerError;
        }
        return AccessListApiError(
            code,
            envelope->message().isEmpty()
                ? description + QStringLiteral("请求失败。")
                : envelope->message(),
            serverCode,
            envelope->requestId(),
            status);
    }

    return AccessListApiError(transportErrorCode(error.code()),
                              error.message(), {}, {}, status, error.protocolError());
}

QString endpointPrefix(const QUrl &baseUrl)
{
    QString prefix = baseUrl.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    return prefix;
}

} // namespace

AccessListPageRequestHandle::AccessListPageRequestHandle(RequestContext context,
                                                         QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

AccessListPageRequestHandle::~AccessListPageRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void AccessListPageRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &AccessListPageRequestHandle::context() const noexcept
{
    return context_;
}

const std::optional<PageResult<AccessListRecord>> &
AccessListPageRequestHandle::page() const noexcept
{
    return page_;
}

void AccessListPageRequestHandle::completeSuccess(PageResult<AccessListRecord> page)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    page_.emplace(std::move(page));
    QPointer<AccessListPageRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void AccessListPageRequestHandle::completeFailure(AccessListApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<AccessListPageRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

AccessListLookupRequestHandle::AccessListLookupRequestHandle(RequestContext context,
                                                             QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

AccessListLookupRequestHandle::~AccessListLookupRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void AccessListLookupRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &AccessListLookupRequestHandle::context() const noexcept
{
    return context_;
}

const std::optional<AccessListRecord> &AccessListLookupRequestHandle::record() const noexcept
{
    return record_;
}

void AccessListLookupRequestHandle::completeSuccess(AccessListRecord record)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    record_.emplace(std::move(record));
    QPointer<AccessListLookupRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void AccessListLookupRequestHandle::completeFailure(AccessListApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<AccessListLookupRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

AccessListCreateRequestHandle::AccessListCreateRequestHandle(RequestContext context,
                                                             QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

AccessListCreateRequestHandle::~AccessListCreateRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void AccessListCreateRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &AccessListCreateRequestHandle::context() const noexcept
{
    return context_;
}

const std::optional<AccessListRecord> &AccessListCreateRequestHandle::record() const noexcept
{
    return record_;
}

void AccessListCreateRequestHandle::completeSuccess(AccessListRecord record)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    record_.emplace(std::move(record));
    QPointer<AccessListCreateRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void AccessListCreateRequestHandle::completeFailure(AccessListApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<AccessListCreateRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

AccessListDeleteRequestHandle::AccessListDeleteRequestHandle(RequestContext context,
                                                             QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

AccessListDeleteRequestHandle::~AccessListDeleteRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void AccessListDeleteRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &AccessListDeleteRequestHandle::context() const noexcept
{
    return context_;
}

void AccessListDeleteRequestHandle::completeSuccess()
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<AccessListDeleteRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void AccessListDeleteRequestHandle::completeFailure(AccessListApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<AccessListDeleteRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

AccessListApiClient::AccessListApiClient(QUrl loginBaseUrl,
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

AccessListPageRequestHandle *AccessListApiClient::getPage(
    AccessListType listType,
    const QString &keyword,
    int page,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    const QString type = listTypeText(listType);
    QVariantMap snapshot {{QStringLiteral("listType"), type},
                          {QStringLiteral("keyword"), keyword},
                          {QStringLiteral("page"), page},
                          {QStringLiteral("pageSize"), 100}};
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("access-list.page"),
                                 std::nullopt,
                                 std::move(snapshot));
    auto *handle = new AccessListPageRequestHandle(context, this);
    connect(handle, &AccessListPageRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    const bool invalidRequest = type.isEmpty() || !isNormalizedPlateLike(keyword, true)
                                || page < 1 || sessionGeneration == 0
                                || requestGeneration == 0;
    if (invalidRequest || !hasToken) {
        QTimer::singleShot(0, handle, [handle, invalidRequest] {
            handle->completeFailure(AccessListApiError(
                invalidRequest ? AccessListApiErrorCode::InvalidRequest
                               : AccessListApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("名单分页请求参数无效。")
                               : QStringLiteral("名单分页请求会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Get,
                        pageEndpoint(listType, keyword, page),
                        HttpRequestKind::Query,
                        HttpResponseMode::JsonMemory,
                        context,
                        {},
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;
    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle, listType, page](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                auto decoded = JsonCodecs::decodeAccessListPageEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(),
                                                            response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope || !envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(AccessListApiError(
                        AccessListApiErrorCode::ProtocolError,
                        QStringLiteral("名单分页成功响应不符合统一信封。"),
                        envelope ? envelope->code() : QString {},
                        envelope ? envelope->requestId() : QString {},
                        response.statusCode()));
                    return;
                }
                auto result = envelope->takeData();
                if (!result || result->page() != page) {
                    handle->completeFailure(protocolFailure(
                        ProtocolError(ProtocolErrorCode::InvalidCombination,
                                      QStringLiteral("$.data.page"),
                                      QStringLiteral("名单分页响应页码与请求不一致。")),
                        response.statusCode()));
                    return;
                }
                for (const AccessListRecord &record : result->items()) {
                    if (record.listType() != listType) {
                        handle->completeFailure(protocolFailure(
                            ProtocolError(ProtocolErrorCode::InvalidCombination,
                                          QStringLiteral("$.data.items"),
                                          QStringLiteral("名单分页记录类型与请求不一致。")),
                            response.statusCode()));
                        return;
                    }
                }
                handle->completeSuccess(std::move(*result));
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    QPointer<AccessListPageRequestHandle> guardedHandle(handle);
                    sessionService_->handleUnauthorized(
                        response.context().sessionGeneration());
                    if (!guardedHandle) {
                        return;
                    }
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("名单分页")));
            });
    return handle;
}

AccessListLookupRequestHandle *AccessListApiClient::lookup(
    const QString &plateNumber,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    QVariantMap snapshot {{QStringLiteral("plateNumber"), plateNumber}};
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("access-list.lookup"),
                                 std::nullopt,
                                 std::move(snapshot));
    auto *handle = new AccessListLookupRequestHandle(context, this);
    connect(handle, &AccessListLookupRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    const bool invalidRequest = !isNormalizedPlateLike(plateNumber, false)
                                || sessionGeneration == 0 || requestGeneration == 0;
    if (invalidRequest || !hasToken) {
        QTimer::singleShot(0, handle, [handle, invalidRequest] {
            handle->completeFailure(AccessListApiError(
                invalidRequest ? AccessListApiErrorCode::InvalidRequest
                               : AccessListApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("名单精确查询请求参数无效。")
                               : QStringLiteral("名单精确查询请求会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Get,
                        lookupEndpoint(plateNumber),
                        HttpRequestKind::Query,
                        HttpResponseMode::JsonMemory,
                        context,
                        {},
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;
    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle, plateNumber](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                auto decoded = JsonCodecs::decodeAccessListEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(),
                                                            response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope || !envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(AccessListApiError(
                        AccessListApiErrorCode::ProtocolError,
                        QStringLiteral("名单精确查询成功响应不符合统一信封。"),
                        envelope ? envelope->code() : QString {},
                        envelope ? envelope->requestId() : QString {},
                        response.statusCode()));
                    return;
                }
                auto record = envelope->takeData();
                if (!record || record->plateNumber() != plateNumber) {
                    handle->completeFailure(protocolFailure(
                        ProtocolError(ProtocolErrorCode::InvalidCombination,
                                      QStringLiteral("$.data.plateNumber"),
                                      QStringLiteral("精确查询响应车牌与请求不一致。")),
                        response.statusCode()));
                    return;
                }
                handle->completeSuccess(std::move(*record));
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    QPointer<AccessListLookupRequestHandle> guardedHandle(handle);
                    sessionService_->handleUnauthorized(
                        response.context().sessionGeneration());
                    if (!guardedHandle) {
                        return;
                    }
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("名单精确查询")));
            });
    return handle;
}

AccessListCreateRequestHandle *AccessListApiClient::create(
    AccessListType listType,
    const QString &plateNumber,
    const QString &remark,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    const QString type = listTypeText(listType);
    QVariantMap snapshot {{QStringLiteral("operation"), QStringLiteral("create")},
                          {QStringLiteral("listType"), type},
                          {QStringLiteral("plateNumber"), plateNumber},
                          {QStringLiteral("remark"), remark}};
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("access-list.create"),
                                 std::nullopt,
                                 std::move(snapshot));
    auto *handle = new AccessListCreateRequestHandle(context, this);
    connect(handle, &AccessListCreateRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    auto encoded = JsonCodecs::encodeAccessListCreateRequest(listType,
                                                             plateNumber,
                                                             remark);
    const bool invalidRequest = type.isEmpty() || !isNormalizedPlateLike(plateNumber, false)
                                || !hasValidRemark(remark) || !encoded.isSuccess()
                                || sessionGeneration == 0 || requestGeneration == 0;
    if (invalidRequest || !hasToken) {
        QTimer::singleShot(0, handle, [handle, invalidRequest] {
            handle->completeFailure(AccessListApiError(
                invalidRequest ? AccessListApiErrorCode::InvalidRequest
                               : AccessListApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("名单新增请求参数无效。")
                               : QStringLiteral("名单新增请求会话已经失效。")));
        });
        return handle;
    }

    auto body = encoded.takeValue();
    HttpRequest request(HttpMethod::Post,
                        createEndpoint(),
                        HttpRequestKind::AccessListWrite,
                        HttpResponseMode::JsonMemory,
                        context,
                        std::move(*body),
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;
    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle, listType, plateNumber, remark](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                auto decoded = JsonCodecs::decodeAccessListEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(),
                                                            response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope || !envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(AccessListApiError(
                        AccessListApiErrorCode::ProtocolError,
                        QStringLiteral("名单新增成功响应不符合统一信封。"),
                        envelope ? envelope->code() : QString {},
                        envelope ? envelope->requestId() : QString {},
                        response.statusCode()));
                    return;
                }
                auto record = envelope->takeData();
                if (!record || record->listType() != listType
                    || record->plateNumber() != plateNumber
                    || record->remark() != remark) {
                    handle->completeFailure(protocolFailure(
                        ProtocolError(ProtocolErrorCode::InvalidCombination,
                                      QStringLiteral("$.data"),
                                      QStringLiteral("新增响应记录与请求不一致。")),
                        response.statusCode()));
                    return;
                }
                handle->completeSuccess(std::move(*record));
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    QPointer<AccessListCreateRequestHandle> guardedHandle(handle);
                    sessionService_->handleUnauthorized(
                        response.context().sessionGeneration());
                    if (!guardedHandle) {
                        return;
                    }
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("名单新增")));
            });
    return handle;
}

AccessListDeleteRequestHandle *AccessListApiClient::remove(
    qint64 id,
    AccessListType listType,
    const QString &plateNumber,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    const QString type = listTypeText(listType);
    QVariantMap snapshot {{QStringLiteral("operation"), QStringLiteral("delete")},
                          {QStringLiteral("id"), id},
                          {QStringLiteral("listType"), type},
                          {QStringLiteral("plateNumber"), plateNumber}};
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("access-list.delete"),
                                 std::nullopt,
                                 std::move(snapshot));
    auto *handle = new AccessListDeleteRequestHandle(context, this);
    connect(handle, &AccessListDeleteRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    const bool invalidRequest = id <= 0 || id > maximumSafeJsonInteger || type.isEmpty()
                                || !isNormalizedPlateLike(plateNumber, false)
                                || sessionGeneration == 0 || requestGeneration == 0;
    if (invalidRequest || !hasToken) {
        QTimer::singleShot(0, handle, [handle, invalidRequest] {
            handle->completeFailure(AccessListApiError(
                invalidRequest ? AccessListApiErrorCode::InvalidRequest
                               : AccessListApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("名单删除请求参数无效。")
                               : QStringLiteral("名单删除请求会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Delete,
                        deleteEndpoint(id),
                        HttpRequestKind::AccessListWrite,
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
                    handle->completeFailure(protocolFailure(*decoded.error(),
                                                            response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope || !envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(AccessListApiError(
                        AccessListApiErrorCode::ProtocolError,
                        QStringLiteral("名单删除成功响应不符合统一信封。"),
                        envelope ? envelope->code() : QString {},
                        envelope ? envelope->requestId() : QString {},
                        response.statusCode()));
                    return;
                }
                handle->completeSuccess();
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    QPointer<AccessListDeleteRequestHandle> guardedHandle(handle);
                    sessionService_->handleUnauthorized(
                        response.context().sessionGeneration());
                    if (!guardedHandle) {
                        return;
                    }
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("名单删除")));
            });
    return handle;
}

QUrl AccessListApiClient::pageEndpoint(AccessListType listType,
                                       const QString &keyword,
                                       int page) const
{
    QUrl result = loginBaseUrl_;
    result.setPath(endpointPrefix(result) + QStringLiteral("/api/v1/access-lists"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("listType"), listTypeText(listType));
    query.addQueryItem(QStringLiteral("keyword"), keyword);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("100"));
    result.setQuery(query);
    return result;
}

QUrl AccessListApiClient::lookupEndpoint(const QString &plateNumber) const
{
    QUrl result = loginBaseUrl_;
    result.setPath(endpointPrefix(result)
                   + QStringLiteral("/api/v1/access-lists/lookup"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("plateNumber"), plateNumber);
    result.setQuery(query);
    return result;
}

QUrl AccessListApiClient::createEndpoint() const
{
    QUrl result = loginBaseUrl_;
    result.setPath(endpointPrefix(result) + QStringLiteral("/api/v1/access-lists"));
    return result;
}

QUrl AccessListApiClient::deleteEndpoint(qint64 id) const
{
    QUrl result = loginBaseUrl_;
    result.setPath(endpointPrefix(result) + QStringLiteral("/api/v1/access-lists/")
                   + QString::number(id));
    return result;
}
