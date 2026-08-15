#include "app/Application.h"

#include "infrastructure/config/BootstrapConfigLoader.h"
#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/logging/TechnicalLogger.h"
#include "services/SessionService.h"
#include "domain/RequestContext.h"
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

QWidget *createMainWindow(const QString &displayName)
{
    return new MainWindow(displayName);
}
} // namespace

Application::Application(StartupErrorPresenter errorPresenter,
                         AppController::WindowFactory windowFactory)
    : errorPresenter_(errorPresenter ? std::move(errorPresenter) : showStartupError)
    , windowFactory_(windowFactory ? std::move(windowFactory) : createMainWindow)
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
    settings_ = std::make_unique<QSettings>();
    controller_ = std::make_unique<AppController>(authApiClient_.get(),
                                                  settings_.get(),
                                                  windowFactory_);
    controller_->start();
    return true;
}
