#pragma once

#include "infrastructure/http/AuthError.h"

#include <QObject>
#include <QPointer>

#include <functional>

class AuthApiClient;
class LoginRequestHandle;
class LoginWindow;
class QSettings;
class SessionService;
class QWidget;
enum class SessionInvalidationReason;

class AppController final : public QObject
{
    Q_OBJECT

public:
    using WindowFactory = std::function<QWidget *(const QString &displayName)>;
    struct TerminationActions final
    {
        std::function<void()> stopScheduling;
        std::function<void()> cancelAsync;
        std::function<void()> disconnectMqtt;
        std::function<void()> quitApplication;
    };

    AppController(AuthApiClient *authApiClient,
                  SessionService *sessionService,
                  QSettings *settings,
                  WindowFactory windowFactory,
                  TerminationActions terminationActions = {},
                  QObject *parent = nullptr);
    ~AppController() override;

    void start();
    [[nodiscard]] LoginWindow *loginWindow() const noexcept;
    [[nodiscard]] QWidget *authenticatedWindow() const noexcept;

signals:
    void loginSucceeded();

private:
    enum class TerminationDisposition
    {
        ReturnToLogin,
        QuitApplication,
    };

    void submitLogin(const QString &username, const QString &password);
    void completeLoginSuccess(LoginRequestHandle *handle, const QString &username);
    void completeLoginFailure(const AuthError &error);
    void terminate(SessionInvalidationReason reason,
                   TerminationDisposition disposition);
    [[nodiscard]] static QString displayMessage(const AuthError &error);

    AuthApiClient *authApiClient_;
    SessionService *sessionService_;
    QSettings *settings_;
    WindowFactory windowFactory_;
    TerminationActions terminationActions_;
    QPointer<LoginWindow> loginWindow_;
    QPointer<QWidget> authenticatedWindow_;
    QPointer<LoginRequestHandle> activeLogin_;
    bool terminationInProgress_ = false;
};
