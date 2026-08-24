#include "integration/MockPlateServer.h"

#include "app/AppController.h"
#include "domain/BootstrapConfig.h"
#include "infrastructure/config/BootstrapConfigLoader.h"
#include "infrastructure/http/AccessListApiClient.h"
#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HeartbeatApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "infrastructure/logging/TechnicalLogger.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "infrastructure/mqtt/RecognitionEventCodec.h"
#include "services/AccessListService.h"
#include "services/CsvDownloadService.h"
#include "services/HeartbeatService.h"
#include "services/HistoryService.h"
#include "services/ImageLoadService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/ReconnectReconciler.h"
#include "services/SessionService.h"
#include "ui/widgets/NavigationSidebar.h"
#include "ui/windows/LoginWindow.h"
#include "ui/windows/MainWindow.h"

#include <QBuffer>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QSettings>
#include <QThreadPool>
#include <QTimeZone>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

using namespace std::chrono_literals;

namespace {
constexpr char eventTopic[] = "plate/management/recognition-events";
constexpr char accessToken[] = "integration-access-token";

QJsonObject envelope(bool success,
                     const QString &code,
                     const QString &message,
                     const QString &requestId,
                     QJsonValue data)
{
    return QJsonObject {{QStringLiteral("success"), success},
                        {QStringLiteral("code"), code},
                        {QStringLiteral("message"), message},
                        {QStringLiteral("requestId"), requestId},
                        {QStringLiteral("data"), std::move(data)}};
}

MockHttpResponse jsonResponse(int status,
                              const QJsonObject &body,
                              int delayMs = 0)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = status >= 400 ? QByteArrayLiteral("Error")
                                           : QByteArrayLiteral("OK");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(QJsonDocument(body).toJson(QJsonDocument::Compact));
    response.initialDelayMs = delayMs;
    return response;
}

MockHttpResponse disconnectedResponse()
{
    MockHttpResponse response;
    response.statusCode = 200;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(QByteArrayLiteral("{"));
    response.declaredContentLength = 100;
    response.disconnectAfterChunks = 1;
    return response;
}

QString listTypeName(AccessListType type)
{
    return type == AccessListType::White ? QStringLiteral("WHITE")
                                         : QStringLiteral("BLACK");
}

QJsonObject accessListJson(const AccessListRecord &record)
{
    return QJsonObject {{QStringLiteral("id"), record.id()},
                        {QStringLiteral("listType"), listTypeName(record.listType())},
                        {QStringLiteral("plateNumber"), record.plateNumber()},
                        {QStringLiteral("remark"), record.remark()},
                        {QStringLiteral("createdBy"), record.createdBy()},
                        {QStringLiteral("createdAt"),
                         record.createdAt().toString(Qt::ISODateWithMs)}};
}

QJsonObject pageJson(const QList<QJsonObject> &items, int page, qint64 total)
{
    QJsonArray array;
    for (const QJsonObject &item : items) {
        array.append(item);
    }
    return QJsonObject {{QStringLiteral("items"), array},
                        {QStringLiteral("page"), page},
                        {QStringLiteral("pageSize"), 100},
                        {QStringLiteral("total"), total}};
}

QDateTime beijingTime(int seconds = 0)
{
    return QDateTime(QDate(2026, 8, 24), QTime(10, 0, 0), QTimeZone::fromSecondsAheadOfUtc(8 * 3600))
        .addSecs(seconds);
}
} // namespace

MockPlateServer::MockPlateServer(QObject *parent)
    : QObject(parent)
    , http_(this)
    , mqtt_(this)
    , csvBytes_(fixedCsv())
{
    connect(&http_, &MockHttpServer::requestReceived,
            this, &MockPlateServer::routeRequest, Qt::DirectConnection);
}

bool MockPlateServer::listen()
{
    return http_.listen();
}

QUrl MockPlateServer::baseUrl() const
{
    return http_.url();
}

MockMqttClient *MockPlateServer::mqttClient() noexcept
{
    return &mqtt_;
}

const QList<MockHttpRequest> &MockPlateServer::requests() const noexcept
{
    return http_.requests();
}

int MockPlateServer::requestCount(const QByteArray &method,
                                  const QByteArray &pathPrefix) const
{
    return static_cast<int>(std::count_if(
        requests().cbegin(), requests().cend(),
        [&method, &pathPrefix](const MockHttpRequest &request) {
            const QByteArray path = request.target.left(request.target.indexOf('?') < 0
                                                             ? request.target.size()
                                                             : request.target.indexOf('?'));
            return request.method == method && path.startsWith(pathPrefix);
        }));
}

void MockPlateServer::setHeartbeatMode(HeartbeatMode mode)
{
    heartbeatMode_ = mode;
}

void MockPlateServer::setHeartbeatDelay(int delayMs)
{
    heartbeatDelayMs_ = std::max(delayMs, 0);
}

void MockPlateServer::setHistoryPage(int page,
                                     QList<QJsonObject> items,
                                     qint64 total)
{
    historyPages_.insert(page, HistoryPage {std::move(items), total});
}

void MockPlateServer::setHistoryDelay(int page, int delayMs)
{
    historyDelays_.insert(page, std::max(delayMs, 0));
}

void MockPlateServer::setCsv(QByteArray csvBytes)
{
    csvBytes_ = std::move(csvBytes);
}

void MockPlateServer::setCsvDelay(int delayMs)
{
    csvDelayMs_ = std::max(delayMs, 0);
}

void MockPlateServer::setCsvFailure(bool fail)
{
    csvFailure_ = fail;
}

void MockPlateServer::setImage(const QString &recognitionId,
                               QByteArray bytes,
                               QByteArray contentType)
{
    Image image;
    image.bytes = std::move(bytes);
    image.contentType = std::move(contentType);
    images_.insert(recognitionId, std::move(image));
}

void MockPlateServer::setImageDelay(const QString &recognitionId, int delayMs)
{
    images_[recognitionId].delayMs = std::max(delayMs, 0);
}

void MockPlateServer::setImageFailures(const QString &recognitionId,
                                       QList<int> statuses)
{
    images_[recognitionId].failureStatuses = std::move(statuses);
}

void MockPlateServer::setRecognition(QJsonObject snapshot)
{
    recognitions_.insert(snapshot.value(QStringLiteral("recognitionId")).toString(),
                         std::move(snapshot));
}

void MockPlateServer::publishRecognition(QJsonObject snapshot)
{
    setRecognition(snapshot);
    const QJsonObject current = recognitions_.value(
        snapshot.value(QStringLiteral("recognitionId")).toString());
    mqtt_.reportMessage(mqtt_.configuredGeneration,
                        QString::fromLatin1(eventTopic),
                        QJsonDocument(current).toJson(QJsonDocument::Compact));
}

void MockPlateServer::seedAccessList(QList<AccessListRecord> records)
{
    accessListRecords_ = std::move(records);
    nextAccessListId_ = 1;
    for (const AccessListRecord &record : std::as_const(accessListRecords_)) {
        nextAccessListId_ = std::max(nextAccessListId_, record.id() + 1);
    }
}

void MockPlateServer::delayNextAccessListCreate(int delayMs)
{
    nextCreateDelayMs_ = std::max(delayMs, 0);
}

void MockPlateServer::delayNextAccessListDelete(int delayMs)
{
    nextDeleteDelayMs_ = std::max(delayMs, 0);
}

void MockPlateServer::completeMqttConnection()
{
    mqtt_.reportConnected(mqtt_.configuredGeneration);
    mqtt_.reportSubscriptionSucceeded(mqtt_.configuredGeneration);
}

void MockPlateServer::disconnectMqtt()
{
    mqtt_.reportDisconnected(mqtt_.configuredGeneration);
}

void MockPlateServer::failMqtt(MqttErrorCode code)
{
    mqtt_.reportFailure(mqtt_.configuredGeneration, code);
}

QJsonObject MockPlateServer::processingSnapshot(const QString &recognitionId,
                                                const QString &deviceId,
                                                qint64 revision)
{
    return QJsonObject {{QStringLiteral("schemaVersion"), 1},
                        {QStringLiteral("recognitionId"), recognitionId},
                        {QStringLiteral("revision"), revision},
                        {QStringLiteral("deviceId"), deviceId},
                        {QStringLiteral("status"), QStringLiteral("PROCESSING")},
                        {QStringLiteral("plateNumber"), QJsonValue::Null},
                        {QStringLiteral("errorCode"), QJsonValue::Null},
                        {QStringLiteral("errorMessage"), QJsonValue::Null},
                        {QStringLiteral("capturedAt"),
                         beijingTime().toString(Qt::ISODateWithMs)},
                        {QStringLiteral("startedAt"),
                         beijingTime(1).toString(Qt::ISODateWithMs)},
                        {QStringLiteral("completedAt"), QJsonValue::Null},
                        {QStringLiteral("durationMs"), QJsonValue::Null}};
}

QJsonObject MockPlateServer::succeededSnapshot(const QString &recognitionId,
                                               const QString &plateNumber,
                                               const QString &deviceId,
                                               qint64 revision)
{
    QJsonObject result = processingSnapshot(recognitionId, deviceId, revision);
    result.insert(QStringLiteral("status"), QStringLiteral("SUCCEEDED"));
    result.insert(QStringLiteral("plateNumber"), plateNumber);
    result.insert(QStringLiteral("completedAt"),
                  beijingTime(2).toString(Qt::ISODateWithMs));
    result.insert(QStringLiteral("durationMs"), 1000);
    return result;
}

QJsonObject MockPlateServer::failedSnapshot(const QString &recognitionId,
                                            const QString &errorCode,
                                            const QString &errorMessage,
                                            const QString &deviceId,
                                            qint64 revision)
{
    QJsonObject result = processingSnapshot(recognitionId, deviceId, revision);
    result.insert(QStringLiteral("status"), QStringLiteral("FAILED"));
    result.insert(QStringLiteral("errorCode"), errorCode);
    result.insert(QStringLiteral("errorMessage"), errorMessage);
    result.insert(QStringLiteral("completedAt"),
                  beijingTime(2).toString(Qt::ISODateWithMs));
    result.insert(QStringLiteral("durationMs"), 1000);
    return result;
}

QByteArray MockPlateServer::pngImage(const QColor &color)
{
    QImage image(QSize(48, 30), QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes;
}

QByteArray MockPlateServer::fixedCsv()
{
    return QByteArray::fromHex("efbbbf")
           + QStringLiteral("记录ID,设备,车牌号,状态,错误码,错误信息,拍摄时间,开始时间,完成时间,耗时(ms)\r\n")
                 .toUtf8()
           + QStringLiteral("rec-1,device-001,京A12345,SUCCEEDED,,,2026-08-24T10:00:00+08:00,2026-08-24T10:00:01+08:00,2026-08-24T10:00:02+08:00,1000\r\n")
                 .toUtf8();
}

void MockPlateServer::routeRequest()
{
    if (http_.requests().isEmpty()) {
        return;
    }
    http_.enqueueResponse(responseFor(http_.requests().constLast()));
}

MockHttpResponse MockPlateServer::responseFor(const MockHttpRequest &request)
{
    const QUrl target = QUrl::fromEncoded(request.target);
    const QString path = target.path();
    if (request.method == QByteArrayLiteral("POST")
        && path == QStringLiteral("/api/v1/auth/login")) {
        return loginResponse();
    }

    const QString requestId = QStringLiteral("integration-%1").arg(++requestId_);
    if (request.header(QByteArrayLiteral("Authorization"))
        != QByteArrayLiteral("Bearer ") + QByteArrayLiteral(accessToken)) {
        return jsonResponse(401,
                            envelope(false,
                                     QStringLiteral("UNAUTHORIZED"),
                                     QStringLiteral("unauthorized"),
                                     requestId,
                                     QJsonValue::Null));
    }
    if (request.method == QByteArrayLiteral("POST")
        && path == QStringLiteral("/api/v1/clients/heartbeat")) {
        return heartbeatResponse();
    }
    if (request.method == QByteArrayLiteral("POST")
        && path == QStringLiteral("/api/v1/auth/logout")) {
        return jsonResponse(200,
                            envelope(true, QStringLiteral("OK"), {}, requestId,
                                     QJsonValue::Null));
    }
    if (path.startsWith(QStringLiteral("/api/v1/recognitions"))) {
        return recognitionResponse(request, path);
    }
    if (path.startsWith(QStringLiteral("/api/v1/access-lists"))) {
        return accessListResponse(request, path, target);
    }
    return jsonResponse(404,
                        envelope(false,
                                 QStringLiteral("NOT_FOUND"),
                                 QStringLiteral("not found"),
                                 requestId,
                                 QJsonValue::Null));
}

MockHttpResponse MockPlateServer::loginResponse() const
{
    const QString expiry = QDateTime::currentDateTime()
                               .addSecs(3600)
                               .toOffsetFromUtc(8 * 3600)
                               .toString(Qt::ISODateWithMs);
    const QJsonObject mqtt {{QStringLiteral("host"), QStringLiteral("mock-broker.test")},
                            {QStringLiteral("port"), 1883},
                            {QStringLiteral("tls"), false},
                            {QStringLiteral("username"), QStringLiteral("mqtt-user")},
                            {QStringLiteral("password"), QStringLiteral("mqtt-password")},
                            {QStringLiteral("expiresAt"), expiry},
                            {QStringLiteral("eventTopic"), QString::fromLatin1(eventTopic)}};
    const QJsonObject data {{QStringLiteral("displayName"), QStringLiteral("集成测试操作员")},
                            {QStringLiteral("accessToken"), QString::fromLatin1(accessToken)},
                            {QStringLiteral("expiresAt"), expiry},
                            {QStringLiteral("mqtt"), mqtt}};
    return jsonResponse(200,
                        envelope(true,
                                 QStringLiteral("OK"),
                                 {},
                                 QStringLiteral("integration-login"),
                                 data));
}

MockHttpResponse MockPlateServer::heartbeatResponse() const
{
    if (heartbeatMode_ == HeartbeatMode::Disconnect) {
        return disconnectedResponse();
    }
    const int delay = heartbeatMode_ == HeartbeatMode::Timeout ? heartbeatDelayMs_ : 0;
    return jsonResponse(200,
                        envelope(true,
                                 QStringLiteral("OK"),
                                 {},
                                 QStringLiteral("integration-heartbeat"),
                                 QJsonValue::Null),
                        delay);
}

MockHttpResponse MockPlateServer::recognitionResponse(const MockHttpRequest &request,
                                                      const QString &path)
{
    const QString requestId = QStringLiteral("integration-%1").arg(requestId_);
    if (request.method != QByteArrayLiteral("GET")) {
        return jsonResponse(405,
                            envelope(false, QStringLiteral("METHOD_NOT_ALLOWED"), {}, requestId,
                                     QJsonValue::Null));
    }
    if (path == QStringLiteral("/api/v1/recognitions/export")) {
        if (csvFailure_) {
            return jsonResponse(503,
                                envelope(false,
                                         QStringLiteral("UNAVAILABLE"),
                                         QStringLiteral("csv unavailable"),
                                         requestId,
                                         QJsonValue::Null));
        }
        MockHttpResponse response;
        response.headers.append({QByteArrayLiteral("Content-Type"),
                                 QByteArrayLiteral("text/csv; charset=utf-8")});
        const qsizetype split = std::min<qsizetype>(32, csvBytes_.size());
        response.chunks.append(csvBytes_.left(split));
        response.chunks.append(csvBytes_.mid(split));
        response.chunkDelayMs = 1;
        response.initialDelayMs = csvDelayMs_;
        return response;
    }
    if (path == QStringLiteral("/api/v1/recognitions")) {
        const QUrl target = QUrl::fromEncoded(request.target);
        const int page = QUrlQuery(target).queryItemValue(QStringLiteral("page")).toInt();
        const HistoryPage result = historyPages_.value(page, HistoryPage {{}, 0});
        return jsonResponse(200,
                            envelope(true,
                                     QStringLiteral("OK"),
                                     {},
                                     requestId,
                                     pageJson(result.items, page, result.total)),
                            historyDelays_.value(page, 0));
    }

    const QString prefix = QStringLiteral("/api/v1/recognitions/");
    QString suffix = path.mid(prefix.size());
    const bool imageRequest = suffix.endsWith(QStringLiteral("/image"));
    if (imageRequest) {
        suffix.chop(6);
        Image &image = images_[suffix];
        if (!image.failureStatuses.isEmpty()) {
            const int status = image.failureStatuses.takeFirst();
            return jsonResponse(status,
                                envelope(false,
                                         QStringLiteral("IMAGE_UNAVAILABLE"),
                                         QStringLiteral("image unavailable"),
                                         requestId,
                                         QJsonValue::Null));
        }
        if (image.bytes.isEmpty()) {
            return jsonResponse(404,
                                envelope(false,
                                         QStringLiteral("IMAGE_NOT_FOUND"),
                                         QStringLiteral("image not found"),
                                         requestId,
                                         QJsonValue::Null));
        }
        MockHttpResponse response;
        response.headers.append({QByteArrayLiteral("Content-Type"), image.contentType});
        response.chunks.append(image.bytes);
        response.initialDelayMs = image.delayMs;
        return response;
    }

    if (!recognitions_.contains(suffix)) {
        return jsonResponse(404,
                            envelope(false,
                                     QStringLiteral("RECOGNITION_NOT_FOUND"),
                                     QStringLiteral("recognition not found"),
                                     requestId,
                                     QJsonValue::Null));
    }
    return jsonResponse(200,
                        envelope(true,
                                 QStringLiteral("OK"),
                                 {},
                                 requestId,
                                 recognitions_.value(suffix)));
}

MockHttpResponse MockPlateServer::accessListResponse(const MockHttpRequest &request,
                                                     const QString &path,
                                                     const QUrl &target)
{
    const QString requestId = QStringLiteral("integration-%1").arg(requestId_);
    if (request.method == QByteArrayLiteral("GET")
        && path == QStringLiteral("/api/v1/access-lists/lookup")) {
        const QString plate = QUrlQuery(target).queryItemValue(QStringLiteral("plateNumber"));
        const auto found = std::find_if(accessListRecords_.cbegin(), accessListRecords_.cend(),
                                        [&plate](const AccessListRecord &record) {
                                            return record.plateNumber() == plate;
                                        });
        if (found == accessListRecords_.cend()) {
            return jsonResponse(404,
                                envelope(false,
                                         QStringLiteral("ACCESS_LIST_NOT_FOUND"),
                                         QStringLiteral("not found"),
                                         requestId,
                                         QJsonValue::Null));
        }
        return jsonResponse(200,
                            envelope(true, QStringLiteral("OK"), {}, requestId,
                                     accessListJson(*found)));
    }
    if (request.method == QByteArrayLiteral("GET")
        && path == QStringLiteral("/api/v1/access-lists")) {
        const QUrlQuery query(target);
        const AccessListType type =
            query.queryItemValue(QStringLiteral("listType")) == QStringLiteral("BLACK")
                ? AccessListType::Black
                : AccessListType::White;
        const QString keyword = query.queryItemValue(QStringLiteral("keyword"));
        const int page = query.queryItemValue(QStringLiteral("page")).toInt();
        QList<QJsonObject> items;
        for (auto iterator = accessListRecords_.crbegin();
             iterator != accessListRecords_.crend(); ++iterator) {
            if (iterator->listType() == type
                && iterator->plateNumber().contains(keyword)) {
                items.append(accessListJson(*iterator));
            }
        }
        return jsonResponse(200,
                            envelope(true,
                                     QStringLiteral("OK"),
                                     {},
                                     requestId,
                                     pageJson(items, page, items.size())));
    }
    if (request.method == QByteArrayLiteral("POST")
        && path == QStringLiteral("/api/v1/access-lists")) {
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        const QString plate = body.value(QStringLiteral("plateNumber")).toString();
        const auto conflict = std::find_if(accessListRecords_.cbegin(), accessListRecords_.cend(),
                                           [&plate](const AccessListRecord &record) {
                                               return record.plateNumber() == plate;
                                           });
        if (conflict != accessListRecords_.cend()) {
            const QString code = conflict->listType() == AccessListType::White
                                     ? QStringLiteral("ACCESS_LIST_CONFLICT_WHITE")
                                     : QStringLiteral("ACCESS_LIST_CONFLICT_BLACK");
            return jsonResponse(409,
                                envelope(false, code, QStringLiteral("conflict"), requestId,
                                         QJsonValue::Null));
        }
        const AccessListType type =
            body.value(QStringLiteral("listType")).toString() == QStringLiteral("BLACK")
                ? AccessListType::Black
                : AccessListType::White;
        const qint64 id = nextAccessListId_++;
        AccessListRecord record(id,
                                type,
                                plate,
                                body.value(QStringLiteral("remark")).toString(),
                                QStringLiteral("operator"),
                                beijingTime(static_cast<int>(id)));
        accessListRecords_.append(record);
        const int delay = std::exchange(nextCreateDelayMs_, 0);
        return jsonResponse(200,
                            envelope(true, QStringLiteral("OK"), {}, requestId,
                                     accessListJson(record)),
                            delay);
    }
    if (request.method == QByteArrayLiteral("DELETE")
        && path.startsWith(QStringLiteral("/api/v1/access-lists/"))) {
        const qint64 id = path.mid(QStringLiteral("/api/v1/access-lists/").size()).toLongLong();
        const auto found = std::find_if(accessListRecords_.begin(), accessListRecords_.end(),
                                        [id](const AccessListRecord &record) {
                                            return record.id() == id;
                                        });
        if (found == accessListRecords_.end()) {
            return jsonResponse(404,
                                envelope(false,
                                         QStringLiteral("ACCESS_LIST_NOT_FOUND"),
                                         QStringLiteral("not found"),
                                         requestId,
                                         QJsonValue::Null));
        }
        accessListRecords_.erase(found);
        const int delay = std::exchange(nextDeleteDelayMs_, 0);
        return jsonResponse(200,
                            envelope(true, QStringLiteral("OK"), {}, requestId,
                                     QJsonValue::Null),
                            delay);
    }
    return jsonResponse(404,
                        envelope(false,
                                 QStringLiteral("ACCESS_LIST_NOT_FOUND"),
                                 QStringLiteral("not found"),
                                 requestId,
                                 QJsonValue::Null));
}

IntegrationTestRuntime::IntegrationTestRuntime(MockPlateServer *server,
                                               QString temporaryRoot)
    : server_(server)
    , temporaryRoot_(std::move(temporaryRoot))
{
}

IntegrationTestRuntime::~IntegrationTestRuntime() = default;

bool IntegrationTestRuntime::start()
{
    if (!server_ || !QDir().mkpath(temporaryRoot_)) {
        return false;
    }
    const auto configResult = BootstrapConfigLoader::loadFromFile(writeTemporaryConfig());
    if (!configResult.isSuccess()) {
        return false;
    }
    const BootstrapConfig &config = *configResult.config();
    logger_ = std::make_unique<TechnicalLogger>(
        QDir(temporaryRoot_).filePath(QStringLiteral("logs")));
    sessionService_ = std::make_unique<SessionService>();
    networkManager_ = std::make_unique<QNetworkAccessManager>();
    httpClient_ = std::make_unique<HttpClient>(
        networkManager_.get(),
        [session = sessionService_.get()](const RequestContext &context) {
            return session->isCurrent(context);
        },
        [this](HttpRequestKind kind) { return timeoutFor(kind); });
    authApiClient_ = std::make_unique<AuthApiClient>(config.loginBaseUrl(),
                                                     config.clientId(),
                                                     httpClient_.get(),
                                                     sessionService_.get());
    heartbeatApiClient_ = std::make_unique<HeartbeatApiClient>(config.loginBaseUrl(),
                                                               config.clientId(),
                                                               QStringLiteral("integration"),
                                                               httpClient_.get(),
                                                               sessionService_.get());
    recognitionApiClient_ = std::make_unique<RecognitionApiClient>(config.loginBaseUrl(),
                                                                   httpClient_.get(),
                                                                   sessionService_.get());
    accessListApiClient_ = std::make_unique<AccessListApiClient>(config.loginBaseUrl(),
                                                                 httpClient_.get(),
                                                                 sessionService_.get());
    heartbeatService_ = std::make_unique<HeartbeatService>(heartbeatApiClient_.get(),
                                                           httpClient_.get(),
                                                           sessionService_.get(),
                                                           20ms);
    mqttSubscriber_ = std::make_unique<MqttRecognitionSubscriber>(
        config.clientId().toString(QUuid::WithoutBraces),
        config.allowInsecureTransport(),
        sessionService_.get(),
        server_->mqttClient(),
        5ms);
    recognitionEventCodec_ = std::make_unique<RecognitionEventCodec>(logger_.get());
    realtimeStore_ = std::make_unique<RealtimeRecognitionStore>(sessionService_.get());
    reconnectReconciler_ = std::make_unique<ReconnectReconciler>(recognitionApiClient_.get(),
                                                                  mqttSubscriber_.get(),
                                                                  heartbeatService_.get(),
                                                                  realtimeStore_.get(),
                                                                  sessionService_.get());
    imageThreadPool_ = std::make_unique<QThreadPool>();
    imageThreadPool_->setMaxThreadCount(2);
    imageLoadService_ = std::make_unique<ImageLoadService>(
        recognitionApiClient_.get(),
        sessionService_.get(),
        imageThreadPool_.get(),
        [](int) { return 5ms; });
    historyService_ = std::make_unique<HistoryService>(recognitionApiClient_.get(),
                                                       sessionService_.get());
    csvDownloadService_ = std::make_unique<CsvDownloadService>(recognitionApiClient_.get(),
                                                               sessionService_.get());
    accessListService_ = std::make_unique<AccessListService>(accessListApiClient_.get(),
                                                             sessionService_.get());
    QObject::connect(
        mqttSubscriber_.get(), &MqttRecognitionSubscriber::eventPayloadReceived,
        realtimeStore_.get(),
        [codec = recognitionEventCodec_.get(), store = realtimeStore_.get()](
            quint64 generation, const QByteArray &payload) {
            auto snapshot = codec->decode(payload);
            if (snapshot) {
                (void)store->apply(generation, std::move(*snapshot));
            }
        });
    settings_ = std::make_unique<QSettings>(
        QDir(temporaryRoot_).filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat);

    AppController::WindowFactory windowFactory =
        [heartbeat = heartbeatService_.get(),
         mqtt = mqttSubscriber_.get(),
         store = realtimeStore_.get(),
         images = imageLoadService_.get(),
         history = historyService_.get(),
         csv = csvDownloadService_.get(),
         accessLists = accessListService_.get()](const QString &displayName) {
            auto *window = new MainWindow(displayName,
                                          store,
                                          images,
                                          history,
                                          csv,
                                          accessLists);
            window->sidebar()->setHttpConnectivityState(heartbeat->state());
            window->sidebar()->setMqttConnectivityState(mqtt->state(), mqtt->statusReason());
            QObject::connect(heartbeat, &HeartbeatService::stateChanged,
                             window->sidebar(), &NavigationSidebar::setHttpConnectivityState);
            QObject::connect(mqtt, &MqttRecognitionSubscriber::stateChanged,
                             window->sidebar(), &NavigationSidebar::setMqttConnectivityState);
            return window;
        };
    AppController::TerminationActions actions;
    actions.stopScheduling = [heartbeat = heartbeatService_.get(),
                              mqtt = mqttSubscriber_.get()] {
        heartbeat->stopScheduling();
        mqtt->stopReconnect();
    };
    actions.cancelAsync = [images = imageLoadService_.get(),
                           csv = csvDownloadService_.get(),
                           http = httpClient_.get()] {
        images->cancel();
        csv->cancelActive();
        http->cancelAll();
    };
    actions.disconnectMqtt = [mqtt = mqttSubscriber_.get()] {
        mqtt->disconnectAndClear();
    };
    actions.quitApplication = [] {};
    controller_ = std::make_unique<AppController>(authApiClient_.get(),
                                                  sessionService_.get(),
                                                  settings_.get(),
                                                  std::move(windowFactory),
                                                  std::move(actions));
    controller_->start();
    return true;
}

void IntegrationTestRuntime::submitLogin(const QString &username,
                                         const QString &password)
{
    LoginWindow *login = controller_ ? controller_->loginWindow() : nullptr;
    if (!login) {
        return;
    }
    login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->setText(username);
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(password);
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
}

const QString &IntegrationTestRuntime::temporaryRoot() const noexcept
{
    return temporaryRoot_;
}

AppController *IntegrationTestRuntime::controller() const noexcept
{
    return controller_.get();
}

MainWindow *IntegrationTestRuntime::mainWindow() const noexcept
{
    return controller_ ? qobject_cast<MainWindow *>(controller_->authenticatedWindow()) : nullptr;
}

SessionService *IntegrationTestRuntime::sessionService() const noexcept
{
    return sessionService_.get();
}

HttpClient *IntegrationTestRuntime::httpClient() const noexcept
{
    return httpClient_.get();
}

HeartbeatService *IntegrationTestRuntime::heartbeatService() const noexcept
{
    return heartbeatService_.get();
}

MqttRecognitionSubscriber *IntegrationTestRuntime::mqttSubscriber() const noexcept
{
    return mqttSubscriber_.get();
}

RealtimeRecognitionStore *IntegrationTestRuntime::realtimeStore() const noexcept
{
    return realtimeStore_.get();
}

ReconnectReconciler *IntegrationTestRuntime::reconnectReconciler() const noexcept
{
    return reconnectReconciler_.get();
}

ImageLoadService *IntegrationTestRuntime::imageLoadService() const noexcept
{
    return imageLoadService_.get();
}

HistoryService *IntegrationTestRuntime::historyService() const noexcept
{
    return historyService_.get();
}

CsvDownloadService *IntegrationTestRuntime::csvDownloadService() const noexcept
{
    return csvDownloadService_.get();
}

AccessListService *IntegrationTestRuntime::accessListService() const noexcept
{
    return accessListService_.get();
}

std::chrono::milliseconds IntegrationTestRuntime::timeoutFor(HttpRequestKind kind) const
{
    switch (kind) {
    case HttpRequestKind::Heartbeat:
        return 25ms;
    case HttpRequestKind::AccessListWrite:
        return 100ms;
    case HttpRequestKind::Login:
    case HttpRequestKind::Logout:
    case HttpRequestKind::Query:
    case HttpRequestKind::Image:
    case HttpRequestKind::Csv:
        return 180ms;
    }
    return 180ms;
}

QString IntegrationTestRuntime::writeTemporaryConfig() const
{
    const QString configPath = QDir(temporaryRoot_).filePath(QStringLiteral("client.json"));
    QFile file(configPath);
    const QByteArray bytes = QJsonDocument(QJsonObject {
                                               {QStringLiteral("clientId"),
                                                QStringLiteral("12345678-1234-4abc-8def-1234567890ab")},
                                               {QStringLiteral("loginBaseUrl"),
                                                server_->baseUrl().toString()},
                                               {QStringLiteral("allowInsecureTransport"), true},
                                           })
                                 .toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return configPath;
}
