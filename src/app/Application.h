#pragma once

#include "app/AppController.h"

#include <QString>

#include <functional>
#include <memory>

class AuthApiClient;
class AccessListApiClient;
class AccessListService;
class CsvDownloadService;
class HeartbeatApiClient;
class HeartbeatService;
class HistoryService;
class HttpClient;
class ImageLoadService;
class MqttRecognitionSubscriber;
class RecognitionApiClient;
class RecognitionEventCodec;
class ReconnectReconciler;
class RealtimeRecognitionStore;
class QSettings;
class SessionService;
class TechnicalLogger;

class Application final
{
public:
    using StartupErrorPresenter = std::function<void(const QString &message)>;
    using MqttSubscriberFactory =
        std::function<std::unique_ptr<MqttRecognitionSubscriber>(const QString &clientId,
                                                                 bool allowInsecureTransport,
                                                                 SessionService *sessionService)>;

    explicit Application(StartupErrorPresenter errorPresenter = {},
                         AppController::WindowFactory windowFactory = {},
                         MqttSubscriberFactory mqttSubscriberFactory = {});
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    [[nodiscard]] bool startFromApplicationDirectory();
    [[nodiscard]] bool startFromConfigFile(const QString &filePath);
    [[nodiscard]] AppController *controller() const noexcept;
    [[nodiscard]] RealtimeRecognitionStore *realtimeStore() const noexcept;
    [[nodiscard]] ReconnectReconciler *reconnectReconciler() const noexcept;
    [[nodiscard]] ImageLoadService *imageLoadService() const noexcept;
    [[nodiscard]] HistoryService *historyService() const noexcept;
    [[nodiscard]] CsvDownloadService *csvDownloadService() const noexcept;
    [[nodiscard]] AccessListService *accessListService() const noexcept;
    [[nodiscard]] AccessListApiClient *accessListApiClient() const noexcept;
    [[nodiscard]] bool runtimeInitialized() const noexcept;

private:
    [[nodiscard]] bool initialize(class BootstrapConfigLoadResult result);

    StartupErrorPresenter errorPresenter_;
    AppController::WindowFactory windowFactory_;
    MqttSubscriberFactory mqttSubscriberFactory_;
    std::unique_ptr<TechnicalLogger> logger_;
    std::unique_ptr<SessionService> sessionService_;
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
    std::unique_ptr<ImageLoadService> imageLoadService_;
    std::unique_ptr<HistoryService> historyService_;
    std::unique_ptr<CsvDownloadService> csvDownloadService_;
    std::unique_ptr<AccessListService> accessListService_;
    std::unique_ptr<QSettings> settings_;
    std::unique_ptr<AppController> controller_;
};
