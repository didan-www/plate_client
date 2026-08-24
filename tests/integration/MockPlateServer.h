#pragma once

#include "domain/AccessListRecord.h"
#include "infrastructure/http/HttpRequestPolicy.h"
#include "infrastructure/mqtt/MqttError.h"
#include "support/MockHttpServer.h"
#include "support/MockMqttClient.h"

#include <QHash>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <chrono>
#include <memory>

class AccessListApiClient;
class AccessListService;
class AppController;
class AuthApiClient;
class CsvDownloadService;
class HeartbeatApiClient;
class HeartbeatService;
class HistoryService;
class HttpClient;
class ImageLoadService;
class MainWindow;
class MqttRecognitionSubscriber;
class QNetworkAccessManager;
class QSettings;
class QThreadPool;
class RecognitionApiClient;
class RecognitionEventCodec;
class ReconnectReconciler;
class RealtimeRecognitionStore;
class SessionService;
class TechnicalLogger;

class MockPlateServer final : public QObject
{
    Q_OBJECT

public:
    enum class HeartbeatMode
    {
        Success,
        Timeout,
        Disconnect,
    };

    explicit MockPlateServer(QObject *parent = nullptr);

    [[nodiscard]] bool listen();
    [[nodiscard]] QUrl baseUrl() const;
    [[nodiscard]] MockMqttClient *mqttClient() noexcept;
    [[nodiscard]] const QList<MockHttpRequest> &requests() const noexcept;
    [[nodiscard]] int requestCount(const QByteArray &method,
                                   const QByteArray &pathPrefix) const;

    void setHeartbeatMode(HeartbeatMode mode);
    void setHeartbeatDelay(int delayMs);
    void setHistoryPage(int page, QList<QJsonObject> items, qint64 total);
    void setHistoryDelay(int page, int delayMs);
    void setCsv(QByteArray csvBytes);
    void setCsvDelay(int delayMs);
    void setCsvFailure(bool fail);
    void setImage(const QString &recognitionId,
                  QByteArray bytes,
                  QByteArray contentType = QByteArrayLiteral("image/png"));
    void setImageDelay(const QString &recognitionId, int delayMs);
    void setImageFailures(const QString &recognitionId, QList<int> statuses);
    void setRecognition(QJsonObject snapshot);
    void publishRecognition(QJsonObject snapshot);

    void seedAccessList(QList<AccessListRecord> records);
    void delayNextAccessListCreate(int delayMs);
    void delayNextAccessListDelete(int delayMs);

    void completeMqttConnection();
    void disconnectMqtt();
    void failMqtt(MqttErrorCode code);

    [[nodiscard]] static QJsonObject processingSnapshot(
        const QString &recognitionId,
        const QString &deviceId = QStringLiteral("device-001"),
        qint64 revision = 1);
    [[nodiscard]] static QJsonObject succeededSnapshot(
        const QString &recognitionId,
        const QString &plateNumber = QStringLiteral("京A12345"),
        const QString &deviceId = QStringLiteral("device-001"),
        qint64 revision = 2);
    [[nodiscard]] static QJsonObject failedSnapshot(
        const QString &recognitionId,
        const QString &errorCode = QStringLiteral("MODEL_FAILED"),
        const QString &errorMessage = QStringLiteral("识别失败"),
        const QString &deviceId = QStringLiteral("device-001"),
        qint64 revision = 2);
    [[nodiscard]] static QByteArray pngImage(const QColor &color = Qt::green);
    [[nodiscard]] static QByteArray fixedCsv();

private:
    struct HistoryPage final
    {
        QList<QJsonObject> items;
        qint64 total = 0;
    };

    struct Image final
    {
        QByteArray bytes;
        QByteArray contentType;
        int delayMs = 0;
        QList<int> failureStatuses;
    };

    void routeRequest();
    [[nodiscard]] MockHttpResponse responseFor(const MockHttpRequest &request);
    [[nodiscard]] MockHttpResponse loginResponse() const;
    [[nodiscard]] MockHttpResponse heartbeatResponse() const;
    [[nodiscard]] MockHttpResponse recognitionResponse(const MockHttpRequest &request,
                                                       const QString &path);
    [[nodiscard]] MockHttpResponse accessListResponse(const MockHttpRequest &request,
                                                      const QString &path,
                                                      const QUrl &target);

    MockHttpServer http_;
    MockMqttClient mqtt_;
    HeartbeatMode heartbeatMode_ = HeartbeatMode::Success;
    int heartbeatDelayMs_ = 250;
    QHash<int, HistoryPage> historyPages_;
    QHash<int, int> historyDelays_;
    QByteArray csvBytes_;
    int csvDelayMs_ = 0;
    bool csvFailure_ = false;
    QHash<QString, QJsonObject> recognitions_;
    QHash<QString, Image> images_;
    QList<AccessListRecord> accessListRecords_;
    qint64 nextAccessListId_ = 1;
    int nextCreateDelayMs_ = 0;
    int nextDeleteDelayMs_ = 0;
    quint64 requestId_ = 0;
};

class IntegrationTestRuntime final
{
public:
    explicit IntegrationTestRuntime(MockPlateServer *server,
                                    QString temporaryRoot);
    ~IntegrationTestRuntime();

    IntegrationTestRuntime(const IntegrationTestRuntime &) = delete;
    IntegrationTestRuntime &operator=(const IntegrationTestRuntime &) = delete;

    [[nodiscard]] bool start();
    void submitLogin(const QString &username = QStringLiteral("operator"),
                     const QString &password = QStringLiteral("secret"));

    [[nodiscard]] const QString &temporaryRoot() const noexcept;
    [[nodiscard]] AppController *controller() const noexcept;
    [[nodiscard]] MainWindow *mainWindow() const noexcept;
    [[nodiscard]] SessionService *sessionService() const noexcept;
    [[nodiscard]] HttpClient *httpClient() const noexcept;
    [[nodiscard]] HeartbeatService *heartbeatService() const noexcept;
    [[nodiscard]] MqttRecognitionSubscriber *mqttSubscriber() const noexcept;
    [[nodiscard]] RealtimeRecognitionStore *realtimeStore() const noexcept;
    [[nodiscard]] ReconnectReconciler *reconnectReconciler() const noexcept;
    [[nodiscard]] ImageLoadService *imageLoadService() const noexcept;
    [[nodiscard]] HistoryService *historyService() const noexcept;
    [[nodiscard]] CsvDownloadService *csvDownloadService() const noexcept;
    [[nodiscard]] AccessListService *accessListService() const noexcept;

private:
    [[nodiscard]] std::chrono::milliseconds timeoutFor(HttpRequestKind kind) const;
    [[nodiscard]] QString writeTemporaryConfig() const;

    MockPlateServer *server_;
    QString temporaryRoot_;
    std::unique_ptr<TechnicalLogger> logger_;
    std::unique_ptr<SessionService> sessionService_;
    std::unique_ptr<QNetworkAccessManager> networkManager_;
    std::unique_ptr<HttpClient> httpClient_;
    std::unique_ptr<AuthApiClient> authApiClient_;
    std::unique_ptr<HeartbeatApiClient> heartbeatApiClient_;
    std::unique_ptr<RecognitionApiClient> recognitionApiClient_;
    std::unique_ptr<AccessListApiClient> accessListApiClient_;
    std::unique_ptr<HeartbeatService> heartbeatService_;
    std::unique_ptr<MqttRecognitionSubscriber> mqttSubscriber_;
    std::unique_ptr<RecognitionEventCodec> recognitionEventCodec_;
    std::unique_ptr<RealtimeRecognitionStore> realtimeStore_;
    std::unique_ptr<ReconnectReconciler> reconnectReconciler_;
    std::unique_ptr<QThreadPool> imageThreadPool_;
    std::unique_ptr<ImageLoadService> imageLoadService_;
    std::unique_ptr<HistoryService> historyService_;
    std::unique_ptr<CsvDownloadService> csvDownloadService_;
    std::unique_ptr<AccessListService> accessListService_;
    std::unique_ptr<QSettings> settings_;
    std::unique_ptr<AppController> controller_;
};
