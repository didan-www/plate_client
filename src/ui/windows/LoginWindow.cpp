#include "ui/windows/LoginWindow.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

static void initializeLoginResources()
{
    Q_INIT_RESOURCE(plate_client);
}

namespace {
constexpr int windowWidth = 520;
constexpr int windowHeight = 420;
constexpr int titleBarHeight = 44;
constexpr int formWidth = 392;

class LoginTitleBar final : public QWidget
{
public:
    explicit LoginTitleBar(QWidget *hostWindow)
        : QWidget(hostWindow)
        , hostWindow_(hostWindow)
    {
        setObjectName(QStringLiteral("titleBar"));
        setFixedHeight(titleBarHeight);
        setAutoFillBackground(true);
        QPalette titleBarPalette = palette();
        titleBarPalette.setColor(QPalette::Window, QColor(QStringLiteral("#1F5F99")));
        setPalette(titleBarPalette);

        auto *title = new QLabel(QStringLiteral("车牌识别系统"), this);
        title->setObjectName(QStringLiteral("windowTitleLabel"));
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont titleFont = title->font();
        titleFont.setPointSize(10);
        titleFont.setBold(true);
        title->setFont(titleFont);

        auto *minimizeButton = new QToolButton(this);
        minimizeButton->setObjectName(QStringLiteral("minimizeButton"));
        minimizeButton->setText(QStringLiteral("\u2212"));
        minimizeButton->setToolTip(QStringLiteral("最小化"));
        minimizeButton->setAccessibleName(QStringLiteral("最小化"));
        minimizeButton->setFocusPolicy(Qt::NoFocus);
        minimizeButton->setFixedSize(titleBarHeight, titleBarHeight);

        auto *closeButton = new QToolButton(this);
        closeButton->setObjectName(QStringLiteral("closeButton"));
        closeButton->setText(QStringLiteral("\u00d7"));
        closeButton->setToolTip(QStringLiteral("关闭"));
        closeButton->setAccessibleName(QStringLiteral("关闭"));
        closeButton->setFocusPolicy(Qt::NoFocus);
        closeButton->setFixedSize(titleBarHeight, titleBarHeight);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(title);
        layout->addStretch();
        layout->addWidget(minimizeButton);
        layout->addWidget(closeButton);

        connect(minimizeButton, &QToolButton::clicked, hostWindow_, &QWidget::showMinimized);
        connect(closeButton, &QToolButton::clicked, hostWindow_, &QWidget::close);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && hostWindow_ && hostWindow_->windowHandle()
            && hostWindow_->windowHandle()->startSystemMove()) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    QWidget *hostWindow_;
};

QLabel *createIconLabel(const QString &resourcePath,
                        const QString &objectName,
                        const QSize &displaySize,
                        QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setFixedSize(displaySize);
    label->setPixmap(QPixmap(resourcePath));
    label->setScaledContents(true);
    return label;
}

QWidget *createFormRow(const QString &rowObjectName,
                       const QString &iconPath,
                       const QString &iconObjectName,
                       const QString &labelText,
                       const QString &labelObjectName,
                       QLineEdit *editor,
                       QWidget *parent)
{
    auto *row = new QWidget(parent);
    row->setObjectName(rowObjectName);
    row->setFixedSize(formWidth, 44);

    auto *icon = createIconLabel(iconPath, iconObjectName, QSize(22, 22), row);
    auto *label = new QLabel(labelText, row);
    label->setObjectName(labelObjectName);
    label->setFixedWidth(56);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setBuddy(editor);

    editor->setParent(row);
    editor->setFixedSize(288, 44);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(icon, 0, Qt::AlignVCenter);
    layout->addSpacing(12);
    layout->addWidget(label);
    layout->addSpacing(14);
    layout->addWidget(editor);
    return row;
}
} // namespace

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
    , usernameEdit_(new QLineEdit)
    , passwordEdit_(new QLineEdit)
    , loginButton_(new QPushButton(QStringLiteral("登录"), this))
    , errorLabel_(new QLabel(this))
{
    initializeLoginResources();
    setObjectName(QStringLiteral("loginWindow"));
    setWindowTitle(QStringLiteral("车牌识别系统"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowCloseButtonHint);
    setFixedSize(windowWidth, windowHeight);

    auto *titleBar = new LoginTitleBar(this);
    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("loginBody"));

    auto *brand = new QWidget(body);
    brand->setObjectName(QStringLiteral("brandWidget"));
    brand->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *logo = createIconLabel(QStringLiteral(":/images/login/login_logo.png"),
                                 QStringLiteral("logoLabel"),
                                 QSize(32, 32),
                                 brand);

    auto *title = new QLabel(QStringLiteral("车牌识别系统"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName(QStringLiteral("titleLabel"));

    auto *brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(12);
    brandLayout->addWidget(logo);
    brandLayout->addWidget(title);

    usernameEdit_->setObjectName(QStringLiteral("usernameEdit"));
    usernameEdit_->setClearButtonEnabled(true);
    passwordEdit_->setObjectName(QStringLiteral("passwordEdit"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    errorLabel_->setObjectName(QStringLiteral("errorLabel"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setAlignment(Qt::AlignCenter);
    errorLabel_->setFixedSize(formWidth, 36);
    errorLabel_->hide();

    auto *usernameRow = createFormRow(QStringLiteral("usernameRow"),
                                      QStringLiteral(":/images/login/login_user.png"),
                                      QStringLiteral("usernameIconLabel"),
                                      QStringLiteral("用户名"),
                                      QStringLiteral("usernameTextLabel"),
                                      usernameEdit_,
                                      body);
    auto *passwordRow = createFormRow(QStringLiteral("passwordRow"),
                                      QStringLiteral(":/images/login/login_pwd.png"),
                                      QStringLiteral("passwordIconLabel"),
                                      QStringLiteral("密码"),
                                      QStringLiteral("passwordTextLabel"),
                                      passwordEdit_,
                                      body);

    loginButton_->setFixedSize(220, 48);

    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(64, 32, 64, 36);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(brand, 0, Qt::AlignHCenter);
    bodyLayout->addSpacing(34);
    bodyLayout->addWidget(usernameRow, 0, Qt::AlignHCenter);
    bodyLayout->addSpacing(14);
    bodyLayout->addWidget(passwordRow, 0, Qt::AlignHCenter);
    bodyLayout->addSpacing(8);
    bodyLayout->addWidget(errorLabel_, 0, Qt::AlignHCenter);
    bodyLayout->addStretch();
    bodyLayout->addWidget(loginButton_, 0, Qt::AlignHCenter);

    auto *windowLayout = new QVBoxLayout(this);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(titleBar);
    windowLayout->addWidget(body);

    setStyleSheet(QStringLiteral(R"(
        QWidget#loginWindow {
            background: #F4F6F8;
            border: 1px solid #8EA0AF;
        }
        QWidget#loginBody {
            background: #F4F6F8;
        }
        QLabel#windowTitleLabel {
            color: #FFFFFF;
        }
        QToolButton#minimizeButton,
        QToolButton#closeButton {
            color: #FFFFFF;
            background: transparent;
            border: 0;
            font-size: 18px;
        }
        QToolButton#minimizeButton:hover {
            background: rgba(255, 255, 255, 36);
        }
        QToolButton#closeButton:hover {
            background: #C42B1C;
        }
        QLabel#titleLabel {
            color: #1F2933;
        }
        QLabel#usernameTextLabel,
        QLabel#passwordTextLabel {
            color: #344054;
            font-size: 14px;
        }
        QLineEdit {
            color: #1F2933;
            background: #FFFFFF;
            border: 1px solid #B8C2CC;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            selection-background-color: #1F5F99;
        }
        QLineEdit:focus {
            border: 2px solid #1F5F99;
        }
        QLineEdit:disabled {
            color: #667085;
            background: #E9EEF2;
            border-color: #C9D2DA;
        }
        QLabel#errorLabel {
            color: #B42318;
            font-size: 13px;
        }
        QPushButton#loginButton {
            color: #FFFFFF;
            background: #1F5F99;
            border: 0;
            border-radius: 4px;
            font-size: 15px;
            font-weight: bold;
        }
        QPushButton#loginButton:hover {
            background: #174E7E;
        }
        QPushButton#loginButton:pressed {
            background: #103F68;
        }
        QPushButton#loginButton:disabled {
            color: #F2F4F7;
            background: #94A3B0;
        }
    )"));

    connect(loginButton_, &QPushButton::clicked, this, &LoginWindow::submit);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &LoginWindow::submit);
}

QString LoginWindow::username() const
{
    return usernameEdit_->text();
}

QString LoginWindow::password() const
{
    return passwordEdit_->text();
}

void LoginWindow::setUsername(const QString &username)
{
    usernameEdit_->setText(username);
    if (username.isEmpty()) {
        usernameEdit_->setFocus();
    } else {
        passwordEdit_->setFocus();
    }
}

void LoginWindow::setSubmitting(bool submitting)
{
    submitting_ = submitting;
    usernameEdit_->setEnabled(!submitting);
    passwordEdit_->setEnabled(!submitting);
    loginButton_->setEnabled(!submitting);
    loginButton_->setText(submitting ? QStringLiteral("登录中...") : QStringLiteral("登录"));
}

void LoginWindow::showError(const QString &message)
{
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
}

void LoginWindow::clearPassword()
{
    passwordEdit_->clear();
}

void LoginWindow::submit()
{
    if (submitting_) {
        return;
    }
    showError({});
    setSubmitting(true);
    emit loginRequested(usernameEdit_->text(), passwordEdit_->text());
}
