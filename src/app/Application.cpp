#include "app/Application.h"

#include "infrastructure/config/BootstrapConfigLoader.h"
#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HeartbeatApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "infrastructure/logging/TechnicalLogger.h"
#include "infrastructure/mqtt/MqttRecognitionSubscriber.h"
#include "infrastructure/mqtt/RecognitionEventCodec.h"
#include "domain/RequestContext.h"
#include "services/HeartbeatService.h"
#include "services/CsvDownloadService.h"
#include "services/ImageLoadService.h"
#include "services/HistoryService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/ReconnectReconciler.h"
#include "services/SessionService.h"
#include "ui/widgets/NavigationSidebar.h"
#include "ui/windows/MainWindow.h"

#include <QMessageBox>
#include <QSettings>
#include <QWidget>

#include <utility>

namespace {
void showStartupError(const QString &message)
{
    QMessageBox::critical(nullptr, QStringLiteral("启动失败"), message);
}

} // namespace

Application::Application(StartupErrorPresenter errorPresenter,
                         AppController::WindowFactory windowFactory,
                         MqttSubscriberFactory mqttSubscriberFactory)
    : errorPresenter_(errorPresenter ? std::move(errorPresenter) : showStartupError)
    , windowFactory_(std::move(windowFactory))
    , mqttSubscriberFactory_(std::move(mqttSubscriberFactory))
{
}

Application::~Application() = default;

bool Application::startFromApplicationDirectory()
{
    return initialize(BootstrapConfigLoader::loadFromApplicationDirectory());
}

bool Application::startFromConfigFile(const QString &filePath)
{
    return initialize(BootstrapConfigLoader::loadFromFile(filePath));
}

AppController *Application::controller() const noexcept
{
    return controller_.get();
}

RealtimeRecognitionStore *Application::realtimeStore() const noexcept
{
    return realtimeStore_.get();
}

ReconnectReconciler *Application::reconnectReconciler() const noexcept
{
    return reconnectReconciler_.get();
}

ImageLoadService *Application::imageLoadService() const noexcept
{
    return imageLoadService_.get();
}

HistoryService *Application::historyService() const noexcept
{
    return historyService_.get();
}

CsvDownloadService *Application::csvDownloadService() const noexcept
{
    return csvDownloadService_.get();
}

bool Application::runtimeInitialized() const noexcept
{
    return controller_ != nullptr;
}

bool Application::initialize(BootstrapConfigLoadResult result)
{
    if (!result.isSuccess()) {
        errorPresenter_(result.errorMessage());
        return false;
    }
    if (controller_) {
        controller_->start();
        return true;
    }

    const BootstrapConfig &config = *result.config();
    logger_ = std::make_unique<TechnicalLogger>();
    sessionService_ = std::make_unique<SessionService>();
    httpClient_ = std::make_unique<HttpClient>(
        [sessionService = sessionService_.get()](const RequestContext &context) {
            return sessionService->isCurrent(context);
        });
    authApiClient_ = std::make_unique<AuthApiClient>(config.loginBaseUrl(),
                                                     config.clientId(),
                                                     httpClient_.get(),
                                                     sessionService_.get());
    heartbeatApiClient_ = std::make_unique<HeartbeatApiClient>(config.loginBaseUrl(),
                                                               config.clientId(),
                                                               QStringLiteral(PLATE_CLIENT_VERSION),
                                                               httpClient_.get(),
                                                               sessionService_.get());
    recognitionApiClient_ = std::make_unique<RecognitionApiClient>(config.loginBaseUrl(),
                                                                   httpClient_.get(),
                                                                   sessionService_.get());
    heartbeatService_ = std::make_unique<HeartbeatService>(heartbeatApiClient_.get(),
                                                           httpClient_.get(),
                                                           sessionService_.get());
    const QString mqttClientId = config.clientId().toString(QUuid::WithoutBraces);
    if (mqttSubscriberFactory_) {
        mqttSubscriber_ = mqttSubscriberFactory_(mqttClientId,
                                                 config.allowInsecureTransport(),
                                                 sessionService_.get());
    } else {
        mqttSubscriber_ = std::make_unique<MqttRecognitionSubscriber>(
            mqttClientId, config.allowInsecureTransport(), sessionService_.get());
    }
    Q_ASSERT(mqttSubscriber_);
    recognitionEventCodec_ = std::make_unique<RecognitionEventCodec>(logger_.get());
    realtimeStore_ = std::make_unique<RealtimeRecognitionStore>(sessionService_.get());
    reconnectReconciler_ = std::make_unique<ReconnectReconciler>(recognitionApiClient_.get(),
                                                                  mqttSubscriber_.get(),
                                                                  heartbeatService_.get(),
                                                                  realtimeStore_.get(),
                                                                  sessionService_.get());
    imageLoadService_ = std::make_unique<ImageLoadService>(recognitionApiClient_.get(),
                                                           sessionService_.get());
    historyService_ = std::make_unique<HistoryService>(recognitionApiClient_.get(),
                                                       sessionService_.get());
    csvDownloadService_ = std::make_unique<CsvDownloadService>(recognitionApiClient_.get(),
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
    settings_ = std::make_unique<QSettings>();
    AppController::WindowFactory authenticatedWindowFactory = windowFactory_;
    if (!authenticatedWindowFactory) {
        authenticatedWindowFactory = [heartbeat = heartbeatService_.get(),
                                       mqtt = mqttSubscriber_.get(),
                                       realtimeStore = realtimeStore_.get(),
                                       imageLoadService = imageLoadService_.get(),
                                       historyService = historyService_.get(),
                                       csvDownloadService = csvDownloadService_.get()](
                                          const QString &displayName) {
            auto *window = new MainWindow(displayName,
                                          realtimeStore,
                                          imageLoadService,
                                          historyService,
                                          csvDownloadService);
            window->sidebar()->setHttpConnectivityState(heartbeat->state());
            window->sidebar()->setMqttConnectivityState(mqtt->state(), mqtt->statusReason());
            QObject::connect(heartbeat, &HeartbeatService::stateChanged,
                             window->sidebar(), &NavigationSidebar::setHttpConnectivityState);
            QObject::connect(mqtt, &MqttRecognitionSubscriber::stateChanged,
                             window->sidebar(), &NavigationSidebar::setMqttConnectivityState);
            return window;
        };
    }
    controller_ = std::make_unique<AppController>(authApiClient_.get(),
                                                  settings_.get(),
                                                  std::move(authenticatedWindowFactory));
    controller_->start();
    return true;
}
