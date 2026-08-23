#include "infrastructure/http/RecognitionApiClient.h"

#include "domain/HttpEnvelope.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/JsonCodecs.h"
#include "services/SessionService.h"

#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
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
    case HttpErrorCode::ResponseTooLarge:
        return RecognitionApiErrorCode::ResponseTooLarge;
    case HttpErrorCode::ClientError:
        return RecognitionApiErrorCode::ClientRejected;
    case HttpErrorCode::ServerError:
        return RecognitionApiErrorCode::ServerError;
    case HttpErrorCode::NetworkError:
        return RecognitionApiErrorCode::NetworkError;
    case HttpErrorCode::ParseFailure:
    case HttpErrorCode::RedirectRejected:
        return RecognitionApiErrorCode::ProtocolError;
    }
    return RecognitionApiErrorCode::NetworkError;
}

RecognitionApiError mapTransportFailure(const HttpError &error,
                                        const HttpResponse &response,
                                        const QString &requestDescription)
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
                                       requestDescription + QStringLiteral("错误响应不符合统一信封。"),
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
                                       ? requestDescription + QStringLiteral("请求失败。")
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

std::optional<QByteArray> declaredImageFormat(const HttpResponse &response)
{
    std::optional<QByteArray> value;
    for (const auto &header : response.headers()) {
        if (header.first.trimmed().compare(QByteArrayLiteral("Content-Type"),
                                           Qt::CaseInsensitive)
            != 0) {
            continue;
        }
        if (value.has_value()) {
            return std::nullopt;
        }
        value = header.second.trimmed().toLower();
    }
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (*value == QByteArrayLiteral("image/jpeg")) {
        return QByteArrayLiteral("jpeg");
    }
    if (*value == QByteArrayLiteral("image/png")) {
        return QByteArrayLiteral("png");
    }
    return std::nullopt;
}

QByteArray csvPrefix()
{
    return QByteArray::fromHex("EFBBBF")
           + QStringLiteral("记录ID,设备,车牌号,状态,错误码,错误信息,拍摄时间,开始时间,完成时间,耗时(ms)\r\n")
                 .toUtf8();
}

bool hasValidCsvContentType(const HttpResponse &response)
{
    std::optional<QByteArray> value;
    for (const auto &header : response.headers()) {
        if (header.first.trimmed().compare(QByteArrayLiteral("Content-Type"),
                                           Qt::CaseInsensitive)
            != 0) {
            continue;
        }
        if (value.has_value()) {
            return false;
        }
        value = header.second;
    }
    if (!value.has_value()) {
        return false;
    }

    const QList<QByteArray> parts = value->split(';');
    if (parts.size() != 2
        || parts.at(0).trimmed().compare(QByteArrayLiteral("text/csv"),
                                         Qt::CaseInsensitive)
               != 0) {
        return false;
    }
    const QByteArray parameter = parts.at(1).trimmed();
    const qsizetype equals = parameter.indexOf('=');
    if (equals <= 0 || equals != parameter.lastIndexOf('=')) {
        return false;
    }
    return parameter.left(equals).trimmed().compare(QByteArrayLiteral("charset"),
                                                     Qt::CaseInsensitive)
               == 0
           && parameter.mid(equals + 1).trimmed().compare(QByteArrayLiteral("utf-8"),
                                                          Qt::CaseInsensitive)
                  == 0;
}

QString historyTime(const QDateTime &value)
{
    return value.toOffsetFromUtc(8 * 60 * 60)
               .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz+08:00"));
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
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("识别详情")));
            });
    return handle;
}

RecognitionImageRequestHandle::RecognitionImageRequestHandle(RequestContext context,
                                                             QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

RecognitionImageRequestHandle::~RecognitionImageRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void RecognitionImageRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &RecognitionImageRequestHandle::context() const noexcept
{
    return context_;
}

const QByteArray &RecognitionImageRequestHandle::imageBytes() const noexcept
{
    return imageBytes_;
}

const QByteArray &RecognitionImageRequestHandle::declaredFormat() const noexcept
{
    return declaredFormat_;
}

void RecognitionImageRequestHandle::completeSuccess(QByteArray imageBytes,
                                                     QByteArray declaredFormat)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    imageBytes_ = std::move(imageBytes);
    declaredFormat_ = std::move(declaredFormat);
    QPointer<RecognitionImageRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void RecognitionImageRequestHandle::completeFailure(RecognitionApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<RecognitionImageRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

RecognitionHistoryRequestHandle::RecognitionHistoryRequestHandle(RequestContext context,
                                                                 QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

RecognitionHistoryRequestHandle::~RecognitionHistoryRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void RecognitionHistoryRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &RecognitionHistoryRequestHandle::context() const noexcept
{
    return context_;
}

const std::optional<PageResult<RecognitionSnapshot>> &
RecognitionHistoryRequestHandle::page() const noexcept
{
    return page_;
}

void RecognitionHistoryRequestHandle::completeSuccess(
    PageResult<RecognitionSnapshot> page)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    page_.emplace(std::move(page));
    QPointer<RecognitionHistoryRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void RecognitionHistoryRequestHandle::completeFailure(RecognitionApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<RecognitionHistoryRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

RecognitionCsvRequestHandle::RecognitionCsvRequestHandle(RequestContext context,
                                                         QObject *parent)
    : QObject(parent)
    , context_(std::move(context))
{
}

RecognitionCsvRequestHandle::~RecognitionCsvRequestHandle()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

void RecognitionCsvRequestHandle::cancel()
{
    if (transportHandle_) {
        transportHandle_->cancel();
    }
}

const RequestContext &RecognitionCsvRequestHandle::context() const noexcept
{
    return context_;
}

void RecognitionCsvRequestHandle::consumeChunk(const QByteArray &chunk)
{
    if (completed_ || chunk.isEmpty()) {
        return;
    }
    if (prefixValidated_) {
        emit dataChunkReceived(chunk);
        return;
    }

    const QByteArray expected = csvPrefix();
    const qsizetype needed = expected.size() - pendingPrefix_.size();
    const qsizetype prefixBytes = std::min(chunk.size(), needed);
    pendingPrefix_.append(chunk.constData(), prefixBytes);
    if (pendingPrefix_ != expected.left(pendingPrefix_.size())) {
        QPointer<HttpRequestHandle> transport = transportHandle_;
        completeFailure(RecognitionApiError(
            RecognitionApiErrorCode::ProtocolError,
            QStringLiteral("CSV 响应 BOM 或固定表头无效。"),
            {},
            {},
            std::nullopt,
            ProtocolError(ProtocolErrorCode::InvalidValue,
                          QStringLiteral("$csv.header"),
                          QStringLiteral("CSV 必须包含 UTF-8 BOM、固定表头和 CRLF。"))));
        if (transport) {
            transport->cancel();
        }
        return;
    }
    if (pendingPrefix_.size() != expected.size()) {
        return;
    }

    prefixValidated_ = true;
    QByteArray prefix;
    prefix.swap(pendingPrefix_);
    const QByteArray remainder = chunk.mid(prefixBytes);
    QPointer<RecognitionCsvRequestHandle> guarded(this);
    emit dataChunkReceived(prefix);
    if (!guarded || guarded->completed_) {
        return;
    }
    if (!remainder.isEmpty()) {
        emit guarded->dataChunkReceived(remainder);
    }
}

void RecognitionCsvRequestHandle::completeSuccess()
{
    if (completed_) {
        return;
    }
    completed_ = true;
    QPointer<RecognitionCsvRequestHandle> guarded(this);
    emit succeeded();
    if (guarded) {
        emit guarded->finished();
    }
}

void RecognitionCsvRequestHandle::completeFailure(RecognitionApiError error)
{
    if (completed_) {
        return;
    }
    completed_ = true;
    pendingPrefix_.clear();
    QPointer<RecognitionCsvRequestHandle> guarded(this);
    emit failed(error);
    if (guarded) {
        emit guarded->finished();
    }
}

RecognitionImageRequestHandle *RecognitionApiClient::getImage(
    const QString &recognitionId,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("recognition.image"),
                                 recognitionId);
    auto *handle = new RecognitionImageRequestHandle(context, this);
    connect(handle, &RecognitionImageRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    if (recognitionId.isEmpty() || sessionGeneration == 0 || requestGeneration == 0
        || !hasToken) {
        QTimer::singleShot(0, handle,
                           [handle, recognitionId, sessionGeneration, requestGeneration] {
            const bool invalidRequest = recognitionId.isEmpty() || sessionGeneration == 0
                                        || requestGeneration == 0;
            handle->completeFailure(RecognitionApiError(
                invalidRequest ? RecognitionApiErrorCode::InvalidRequest
                               : RecognitionApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("图片请求上下文无效。")
                               : QStringLiteral("图片请求会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Get,
                        imageEndpoint(recognitionId),
                        HttpRequestKind::Image,
                        HttpResponseMode::ImageMemory,
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
                const auto format = declaredImageFormat(response);
                if (!format.has_value()) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::InvalidContentType,
                        QStringLiteral("图片响应 Content-Type 无效。"),
                        {},
                        {},
                        response.statusCode()));
                    return;
                }
                handle->completeSuccess(response.body(), *format);
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    sessionService_->handleUnauthorized(response.context().sessionGeneration());
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("识别图片")));
            });
    return handle;
}

RecognitionHistoryRequestHandle *RecognitionApiClient::getHistory(
    const QDateTime &startTime,
    const QDateTime &endTime,
    const QString &deviceId,
    int page,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    const QString normalizedStart = historyTime(startTime);
    const QString normalizedEnd = historyTime(endTime);
    const QString normalizedDevice = deviceId.trimmed();
    QVariantMap filterSnapshot {
        {QStringLiteral("startTime"), normalizedStart},
        {QStringLiteral("endTime"), normalizedEnd},
        {QStringLiteral("deviceId"), normalizedDevice},
        {QStringLiteral("page"), page},
        {QStringLiteral("pageSize"), 100},
    };
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("recognition.history"),
                                 std::nullopt,
                                 std::move(filterSnapshot));
    auto *handle = new RecognitionHistoryRequestHandle(context, this);
    connect(handle, &RecognitionHistoryRequestHandle::finished,
            handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    const bool invalidRequest = !startTime.isValid() || !endTime.isValid()
                                || startTime >= endTime || page < 1
                                || sessionGeneration == 0 || requestGeneration == 0;
    if (invalidRequest || !hasToken) {
        QTimer::singleShot(0, handle, [handle, invalidRequest] {
            handle->completeFailure(RecognitionApiError(
                invalidRequest ? RecognitionApiErrorCode::InvalidRequest
                               : RecognitionApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("历史查询条件无效。")
                               : QStringLiteral("历史查询会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Get,
                        historyEndpoint(normalizedStart,
                                        normalizedEnd,
                                        normalizedDevice,
                                        page),
                        HttpRequestKind::Query,
                        HttpResponseMode::JsonMemory,
                        context,
                        {},
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;
    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle, page](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                auto decoded = JsonCodecs::decodeRecognitionPageEnvelope(response.body());
                if (!decoded.isSuccess()) {
                    handle->completeFailure(protocolFailure(*decoded.error(),
                                                            response.statusCode()));
                    return;
                }
                auto envelope = decoded.takeValue();
                if (!envelope || !envelope->success() || !envelope->data().has_value()) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::ProtocolError,
                        QStringLiteral("历史查询成功响应不符合统一信封。"),
                        envelope ? envelope->code() : QString {},
                        envelope ? envelope->requestId() : QString {},
                        response.statusCode()));
                    return;
                }
                std::optional<PageResult<RecognitionSnapshot>> result = envelope->takeData();
                if (!result || result->page() != page) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::ProtocolError,
                        QStringLiteral("历史查询响应页码与请求不匹配。"),
                        {},
                        envelope->requestId(),
                        response.statusCode(),
                        ProtocolError(ProtocolErrorCode::InvalidCombination,
                                      QStringLiteral("$.data.page"),
                                      QStringLiteral("响应页码与请求页码不一致。"))));
                    return;
                }
                handle->completeSuccess(std::move(*result));
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (error.httpStatus() == 401) {
                    sessionService_->handleUnauthorized(
                        response.context().sessionGeneration());
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("历史查询")));
            });
    return handle;
}

RecognitionCsvRequestHandle *RecognitionApiClient::exportCsv(
    const QDateTime &startTime,
    const QDateTime &endTime,
    const QString &deviceId,
    quint64 sessionGeneration,
    quint64 requestGeneration)
{
    const QString normalizedStart = historyTime(startTime);
    const QString normalizedEnd = historyTime(endTime);
    const QString normalizedDevice = deviceId.trimmed();
    QVariantMap filterSnapshot {
        {QStringLiteral("startTime"), normalizedStart},
        {QStringLiteral("endTime"), normalizedEnd},
        {QStringLiteral("deviceId"), normalizedDevice},
    };
    const RequestContext context(sessionGeneration,
                                 requestGeneration,
                                 QStringLiteral("recognition.export"),
                                 std::nullopt,
                                 std::move(filterSnapshot));
    auto *handle = new RecognitionCsvRequestHandle(context, this);
    connect(handle, &RecognitionCsvRequestHandle::finished, handle, &QObject::deleteLater);

    QString accessToken;
    const bool hasToken = sessionService_->withAccessToken(
        sessionGeneration,
        [&accessToken](const QString &token) { accessToken = token; });
    const bool invalidRequest = !startTime.isValid() || !endTime.isValid()
                                || startTime >= endTime || sessionGeneration == 0
                                || requestGeneration == 0;
    if (invalidRequest || !hasToken) {
        QTimer::singleShot(0, handle, [handle, invalidRequest] {
            handle->completeFailure(RecognitionApiError(
                invalidRequest ? RecognitionApiErrorCode::InvalidRequest
                               : RecognitionApiErrorCode::StaleResponse,
                invalidRequest ? QStringLiteral("CSV 导出条件无效。")
                               : QStringLiteral("CSV 导出会话已经失效。")));
        });
        return handle;
    }

    HttpRequest request(HttpMethod::Get,
                        exportEndpoint(normalizedStart, normalizedEnd, normalizedDevice),
                        HttpRequestKind::Csv,
                        HttpResponseMode::CsvStream,
                        context,
                        {},
                        std::move(accessToken));
    HttpRequestHandle *transport = httpClient_->send(std::move(request));
    handle->transportHandle_ = transport;
    connect(transport, &HttpRequestHandle::dataChunkReceived,
            handle, &RecognitionCsvRequestHandle::consumeChunk);
    connect(transport, &HttpRequestHandle::succeeded, handle,
            [handle](const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                if (!hasValidCsvContentType(response)) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::InvalidContentType,
                        QStringLiteral("CSV 响应 Content-Type 无效。"),
                        {},
                        {},
                        response.statusCode()));
                    return;
                }
                if (!handle->prefixValidated_) {
                    handle->completeFailure(RecognitionApiError(
                        RecognitionApiErrorCode::ProtocolError,
                        QStringLiteral("CSV 响应缺少完整 BOM 或固定表头。"),
                        {},
                        {},
                        response.statusCode(),
                        ProtocolError(ProtocolErrorCode::InvalidValue,
                                      QStringLiteral("$csv.header"),
                                      QStringLiteral("CSV 必须包含 UTF-8 BOM、固定表头和 CRLF。"))));
                    return;
                }
                handle->completeSuccess();
            });
    connect(transport, &HttpRequestHandle::failed, handle,
            [this, handle](const HttpError &error, const HttpResponse &response) {
                if (handle->completed_) {
                    return;
                }
                if (error.httpStatus() == 401) {
                    sessionService_->handleUnauthorized(
                        response.context().sessionGeneration());
                }
                handle->completeFailure(mapTransportFailure(
                    error, response, QStringLiteral("CSV 导出")));
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

QUrl RecognitionApiClient::imageEndpoint(const QString &recognitionId) const
{
    QUrl result = loginBaseUrl_;
    QString prefix = result.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(recognitionId));
    result.setPath(prefix + QStringLiteral("/api/v1/recognitions/") + encodedId
                       + QStringLiteral("/image"),
                   QUrl::TolerantMode);
    return result;
}

QUrl RecognitionApiClient::historyEndpoint(const QString &startTime,
                                           const QString &endTime,
                                           const QString &deviceId,
                                           int page) const
{
    QUrl result = loginBaseUrl_;
    QString prefix = result.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    result.setPath(prefix + QStringLiteral("/api/v1/recognitions"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("startTime"), startTime);
    query.addQueryItem(QStringLiteral("endTime"), endTime);
    if (!deviceId.isEmpty()) {
        query.addQueryItem(QStringLiteral("deviceId"), deviceId);
    }
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("100"));
    result.setQuery(query);
    return result;
}

QUrl RecognitionApiClient::exportEndpoint(const QString &startTime,
                                          const QString &endTime,
                                          const QString &deviceId) const
{
    QUrl result = loginBaseUrl_;
    QString prefix = result.path();
    if (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    result.setPath(prefix + QStringLiteral("/api/v1/recognitions/export"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("startTime"), startTime);
    query.addQueryItem(QStringLiteral("endTime"), endTime);
    if (!deviceId.isEmpty()) {
        query.addQueryItem(QStringLiteral("deviceId"), deviceId);
    }
    result.setQuery(query);
    return result;
}
