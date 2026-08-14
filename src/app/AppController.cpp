#include "app/AppController.h"

#include "infrastructure/http/AuthApiClient.h"
#include "ui/windows/LoginWindow.h"

#include <QSettings>
#include <QWidget>

#include <utility>

namespace {
constexpr auto lastUsernameKey = "login/lastUsername";
}

AppController::AppController(AuthApiClient *authApiClient,
                             QSettings *settings,
                             WindowFactory windowFactory,
                             QObject *parent)
    : QObject(parent)
    , authApiClient_(authApiClient)
    , settings_(settings)
    , windowFactory_(std::move(windowFactory))
{
    Q_ASSERT(authApiClient_);
    Q_ASSERT(settings_);
    Q_ASSERT(windowFactory_);
}

AppController::~AppController()
{
    if (activeLogin_) {
        disconnect(activeLogin_, nullptr, this, nullptr);
        activeLogin_->cancel();
    }
    delete authenticatedWindow_.data();
    delete loginWindow_.data();
}

void AppController::start()
{
    if (loginWindow_) {
        loginWindow_->show();
        loginWindow_->raise();
        loginWindow_->activateWindow();
        return;
    }
    loginWindow_ = new LoginWindow;
    loginWindow_->setAttribute(Qt::WA_DeleteOnClose);
    loginWindow_->setUsername(settings_->value(QLatin1StringView(lastUsernameKey)).toString());
    connect(loginWindow_, &LoginWindow::loginRequested, this, &AppController::submitLogin);
    loginWindow_->show();
}

LoginWindow *AppController::loginWindow() const noexcept
{
    return loginWindow_;
}

QWidget *AppController::authenticatedWindow() const noexcept
{
    return authenticatedWindow_;
}

void AppController::submitLogin(const QString &username, const QString &password)
{
    if (activeLogin_) {
        return;
    }
    activeLogin_ = authApiClient_->login(username, password);
    connect(activeLogin_, &LoginRequestHandle::succeeded, this,
            [this, handle = activeLogin_, username] {
                completeLoginSuccess(handle, username);
            });
    connect(activeLogin_, &LoginRequestHandle::failed, this,
            [this](const AuthError &error) { completeLoginFailure(error); });
}

void AppController::completeLoginSuccess(LoginRequestHandle *handle, const QString &username)
{
    if (!activeLogin_ || activeLogin_ != handle || !handle->summary().has_value()) {
        return;
    }
    const QString displayName = handle->summary()->displayName();
    activeLogin_.clear();
    settings_->setValue(QLatin1StringView(lastUsernameKey), username);
    settings_->sync();
    if (loginWindow_) {
        loginWindow_->clearPassword();
        loginWindow_->hide();
    }
    authenticatedWindow_ = windowFactory_(displayName);
    if (authenticatedWindow_) {
        authenticatedWindow_->setAttribute(Qt::WA_DeleteOnClose);
        authenticatedWindow_->show();
    }
    emit loginSucceeded();
}

void AppController::completeLoginFailure(const AuthError &error)
{
    activeLogin_.clear();
    if (!loginWindow_) {
        return;
    }
    loginWindow_->clearPassword();
    loginWindow_->setSubmitting(false);
    loginWindow_->showError(displayMessage(error));
}

QString AppController::displayMessage(const AuthError &error)
{
    switch (error.code()) {
    case AuthErrorCode::Unauthorized:
        return QStringLiteral("用户名或密码错误。");
    case AuthErrorCode::Forbidden:
        return QStringLiteral("当前账号无权登录。");
    case AuthErrorCode::Timeout:
        return QStringLiteral("登录超时，请重试。");
    case AuthErrorCode::TlsError:
        return QStringLiteral("服务器证书校验失败。");
    case AuthErrorCode::NetworkError:
        return QStringLiteral("无法连接登录服务。");
    case AuthErrorCode::ServerError:
        return QStringLiteral("登录服务暂时不可用。");
    case AuthErrorCode::InvalidRequest:
        return error.message().isEmpty() ? QStringLiteral("登录失败，请重试。")
                                         : error.message();
    case AuthErrorCode::ClientRejected:
        return QStringLiteral("登录请求被拒绝。");
    case AuthErrorCode::Cancelled:
        return QStringLiteral("登录已取消。");
    case AuthErrorCode::StaleResponse:
        return QStringLiteral("登录请求已失效，请重试。");
    case AuthErrorCode::ProtocolError:
        return QStringLiteral("登录响应格式错误。");
    }
    return QStringLiteral("登录失败，请重试。");
}
