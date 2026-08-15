#include "ui/widgets/NavigationSidebar.h"

#include "ui/widgets/ConnectionStatusIndicator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int sidebarWidth = 280;

QToolButton *createNavigationButton(QWidget *parent,
                                    const QString &objectName,
                                    const QString &text,
                                    QStyle::StandardPixmap icon,
                                    bool enabled)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setIcon(parent->style()->standardIcon(icon));
    button->setIconSize(QSize(20, 20));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(enabled);
    button->setAutoExclusive(enabled);
    button->setEnabled(enabled);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFixedHeight(48);
    return button;
}
} // namespace

NavigationSidebar::NavigationSidebar(const QString &displayName,
                                     const QString &version,
                                     const QString &buildDate,
                                     QWidget *parent)
    : QWidget(parent)
    , httpStatusIndicator_(new ConnectionStatusIndicator(QStringLiteral("HTTP服务"), this))
    , mqttStatusIndicator_(new ConnectionStatusIndicator(QStringLiteral("MQTT"), this))
{
    setObjectName(QStringLiteral("navigationSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(sidebarWidth);

    auto *appTitle = new QLabel(QStringLiteral("车牌识别系统"), this);
    appTitle->setObjectName(QStringLiteral("appTitleLabel"));
    appTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *realtimeButton = createNavigationButton(this,
                                                  QStringLiteral("realtimeNavButton"),
                                                  QStringLiteral("实时监控"),
                                                  QStyle::SP_ComputerIcon,
                                                  true);
    auto *videoButton = createNavigationButton(this,
                                               QStringLiteral("videoRecognitionNavButton"),
                                               QStringLiteral("视频识别"),
                                               QStyle::SP_MediaPlay,
                                               false);
    auto *cameraButton = createNavigationButton(this,
                                                QStringLiteral("cameraRecognitionNavButton"),
                                                QStringLiteral("摄像头识别"),
                                                QStyle::SP_DriveNetIcon,
                                                false);
    auto *historyButton = createNavigationButton(this,
                                                 QStringLiteral("historyNavButton"),
                                                 QStringLiteral("日志记录"),
                                                 QStyle::SP_FileDialogDetailedView,
                                                 true);
    auto *accessListButton = createNavigationButton(this,
                                                    QStringLiteral("accessListNavButton"),
                                                    QStringLiteral("黑白名单管理"),
                                                    QStyle::SP_FileDialogListView,
                                                    true);
    realtimeButton->setChecked(true);

    connect(realtimeButton, &QToolButton::clicked, this,
            [this] { emit pageRequested(RealtimePage); });
    connect(historyButton, &QToolButton::clicked, this,
            [this] { emit pageRequested(HistoryPage); });
    connect(accessListButton, &QToolButton::clicked, this,
            [this] { emit pageRequested(AccessListPage); });

    auto *avatarLabel = new QLabel(QStringLiteral("用"), this);
    avatarLabel->setObjectName(QStringLiteral("defaultAvatarLabel"));
    avatarLabel->setFixedSize(40, 40);
    avatarLabel->setAlignment(Qt::AlignCenter);

    auto *usernameLabel = new QLabel(displayName, this);
    usernameLabel->setObjectName(QStringLiteral("usernameLabel"));
    usernameLabel->setWordWrap(true);
    usernameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *identityLayout = new QHBoxLayout;
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(12);
    identityLayout->addWidget(avatarLabel);
    identityLayout->addWidget(usernameLabel, 1);

    auto *logoutButton = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogCloseButton), QStringLiteral("退出登录"), this);
    logoutButton->setObjectName(QStringLiteral("logoutButton"));
    logoutButton->setFixedHeight(40);
    connect(logoutButton, &QPushButton::clicked, this, &NavigationSidebar::logoutRequested);

    httpStatusIndicator_->setObjectName(QStringLiteral("httpStatusIndicator"));
    httpStatusIndicator_->setStatus(ConnectionStatusIndicator::VisualState::Busy,
                                    QStringLiteral("检测中"));
    mqttStatusIndicator_->setObjectName(QStringLiteral("mqttStatusIndicator"));
    mqttStatusIndicator_->setStatus(ConnectionStatusIndicator::VisualState::Neutral,
                                    QStringLiteral("未连接"));

    auto *versionLabel = new QLabel(QStringLiteral("版本 %1").arg(version), this);
    versionLabel->setObjectName(QStringLiteral("versionLabel"));
    auto *buildDateLabel = new QLabel(QStringLiteral("构建 %1").arg(buildDate), this);
    buildDateLabel->setObjectName(QStringLiteral("buildDateLabel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(8);
    layout->addWidget(appTitle);
    layout->addSpacing(24);
    layout->addWidget(realtimeButton);
    layout->addWidget(videoButton);
    layout->addWidget(cameraButton);
    layout->addWidget(historyButton);
    layout->addWidget(accessListButton);
    layout->addStretch();
    layout->addLayout(identityLayout);
    layout->addSpacing(4);
    layout->addWidget(logoutButton);
    layout->addSpacing(8);
    layout->addWidget(httpStatusIndicator_);
    layout->addWidget(mqttStatusIndicator_);
    layout->addSpacing(8);
    layout->addWidget(versionLabel);
    layout->addWidget(buildDateLabel);

    setStyleSheet(QStringLiteral(R"(
        QWidget#navigationSidebar {
            background: #173B57;
            color: #F7FAFC;
        }
        QLabel#appTitleLabel {
            color: #FFFFFF;
            font-size: 22px;
            font-weight: bold;
        }
        QToolButton {
            color: #DCE6EE;
            background: transparent;
            border: 0;
            border-radius: 4px;
            padding: 0 14px;
            text-align: left;
            font-size: 14px;
        }
        QToolButton:hover {
            background: #245778;
            color: #FFFFFF;
        }
        QToolButton:checked {
            background: #E8F1F7;
            color: #163A54;
            font-weight: bold;
        }
        QToolButton:disabled {
            color: #7890A2;
            background: transparent;
        }
        QLabel#defaultAvatarLabel {
            color: #173B57;
            background: #E8F1F7;
            border-radius: 20px;
            font-size: 16px;
            font-weight: bold;
        }
        QLabel#usernameLabel {
            color: #FFFFFF;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton#logoutButton {
            color: #E7EEF3;
            background: #24506E;
            border: 1px solid #476E88;
            border-radius: 4px;
            text-align: left;
            padding: 0 12px;
        }
        QPushButton#logoutButton:hover {
            background: #2D607F;
        }
        QLabel#statusTitleLabel {
            color: #B8CAD7;
        }
        QLabel#versionLabel,
        QLabel#buildDateLabel {
            color: #9DB2C1;
            font-size: 12px;
        }
    )"));
}

ConnectionStatusIndicator *NavigationSidebar::httpStatusIndicator() const noexcept
{
    return httpStatusIndicator_;
}

ConnectionStatusIndicator *NavigationSidebar::mqttStatusIndicator() const noexcept
{
    return mqttStatusIndicator_;
}

void NavigationSidebar::setHttpConnectivityState(HttpConnectivityState state)
{
    httpStatusIndicator_->setHttpConnectivityState(state);
}
