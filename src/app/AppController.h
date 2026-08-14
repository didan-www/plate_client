#pragma once

#include "infrastructure/http/AuthError.h"

#include <QObject>
#include <QPointer>

#include <functional>

class AuthApiClient;
class LoginRequestHandle;
class LoginWindow;
class QSettings;
class QWidget;

class AppController final : public QObject
{
    Q_OBJECT

public:
    using WindowFactory = std::function<QWidget *(const QString &displayName)>;

    AppController(AuthApiClient *authApiClient,
                  QSettings *settings,
                  WindowFactory windowFactory,
                  QObject *parent = nullptr);
    ~AppController() override;

    void start();
    [[nodiscard]] LoginWindow *loginWindow() const noexcept;
    [[nodiscard]] QWidget *authenticatedWindow() const noexcept;

signals:
    void loginSucceeded();

private:
    void submitLogin(const QString &username, const QString &password);
    void completeLoginSuccess(LoginRequestHandle *handle, const QString &username);
    void completeLoginFailure(const AuthError &error);
    [[nodiscard]] static QString displayMessage(const AuthError &error);

    AuthApiClient *authApiClient_;
    QSettings *settings_;
    WindowFactory windowFactory_;
    QPointer<LoginWindow> loginWindow_;
    QPointer<QWidget> authenticatedWindow_;
    QPointer<LoginRequestHandle> activeLogin_;
};
