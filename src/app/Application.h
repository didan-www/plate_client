#pragma once

#include "app/AppController.h"

#include <QString>

#include <functional>
#include <memory>

class AuthApiClient;
class HttpClient;
class QSettings;
class SessionService;
class TechnicalLogger;

class Application final
{
public:
    using StartupErrorPresenter = std::function<void(const QString &message)>;

    explicit Application(StartupErrorPresenter errorPresenter = {},
                         AppController::WindowFactory windowFactory = {});
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    [[nodiscard]] bool startFromApplicationDirectory();
    [[nodiscard]] bool startFromConfigFile(const QString &filePath);
    [[nodiscard]] AppController *controller() const noexcept;
    [[nodiscard]] bool runtimeInitialized() const noexcept;

private:
    [[nodiscard]] bool initialize(class BootstrapConfigLoadResult result);

    StartupErrorPresenter errorPresenter_;
    AppController::WindowFactory windowFactory_;
    std::unique_ptr<TechnicalLogger> logger_;
    std::unique_ptr<SessionService> sessionService_;
    std::unique_ptr<HttpClient> httpClient_;
    std::unique_ptr<AuthApiClient> authApiClient_;
    std::unique_ptr<QSettings> settings_;
    std::unique_ptr<AppController> controller_;
};
