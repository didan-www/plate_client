#include "ui/windows/MainWindow.h"

#include "ui/pages/RealtimePage.h"
#include "ui/widgets/NavigationSidebar.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QShowEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr int mainTitleBarHeight = 44;
constexpr int windowResizeBorder = 6;

class MainWindowTitleBar final : public QWidget
{
public:
    explicit MainWindowTitleBar(QWidget *hostWindow)
        : QWidget(hostWindow)
        , hostWindow_(hostWindow)
        , maximizeButton_(new QToolButton(this))
    {
        setObjectName(QStringLiteral("mainWindowTitleBar"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(mainTitleBarHeight);

        auto *title = new QLabel(QStringLiteral("车牌识别系统"), this);
        title->setObjectName(QStringLiteral("mainWindowTitleLabel"));
        title->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto createWindowButton = [this](const QString &objectName,
                                         const QString &text,
                                         const QString &accessibleName) {
            auto *button = new QToolButton(this);
            button->setObjectName(objectName);
            button->setText(text);
            button->setToolTip(accessibleName);
            button->setAccessibleName(accessibleName);
            button->setFocusPolicy(Qt::NoFocus);
            button->setFixedSize(46, mainTitleBarHeight);
            return button;
        };

        auto *minimizeButton = createWindowButton(QStringLiteral("mainMinimizeButton"),
                                                  QStringLiteral("\u2212"),
                                                  QStringLiteral("最小化"));
        maximizeButton_->setObjectName(QStringLiteral("mainMaximizeButton"));
        maximizeButton_->setFocusPolicy(Qt::NoFocus);
        maximizeButton_->setFixedSize(46, mainTitleBarHeight);
        auto *closeButton = createWindowButton(QStringLiteral("mainCloseButton"),
                                               QStringLiteral("\u00d7"),
                                               QStringLiteral("关闭"));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(title);
        layout->addStretch();
        layout->addWidget(minimizeButton);
        layout->addWidget(maximizeButton_);
        layout->addWidget(closeButton);

        connect(minimizeButton, &QToolButton::clicked, hostWindow_, &QWidget::showMinimized);
        connect(maximizeButton_, &QToolButton::pressed, this, [this] { toggleMaximized(); });
        connect(closeButton, &QToolButton::clicked, hostWindow_, &QWidget::close);

        hostWindow_->installEventFilter(this);
        updateMaximizeButton();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == hostWindow_ && event->type() == QEvent::WindowStateChange) {
            updateMaximizeButton();
        }
        return QWidget::eventFilter(watched, event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragPending_ = false;
            toggleMaximized();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton) && dragPending_
            && (event->position().toPoint() - dragStartPosition_).manhattanLength()
                >= QApplication::startDragDistance()
            && hostWindow_->windowHandle() && hostWindow_->windowHandle()->startSystemMove()) {
            dragPending_ = false;
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragStartPosition_ = event->position().toPoint();
            dragPending_ = true;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragPending_ = false;
        QWidget::mouseReleaseEvent(event);
    }

private:
    void toggleMaximized()
    {
        if (maximizeButton_->accessibleName() == QStringLiteral("还原")) {
            hostWindow_->showNormal();
        } else {
            hostWindow_->showMaximized();
        }
        updateMaximizeButton();
    }

    void updateMaximizeButton()
    {
        const bool maximized = hostWindow_->isMaximized();
        maximizeButton_->setText(maximized ? QStringLiteral("\u2750")
                                           : QStringLiteral("\u25a1"));
        const QString action = maximized ? QStringLiteral("还原") : QStringLiteral("最大化");
        maximizeButton_->setToolTip(action);
        maximizeButton_->setAccessibleName(action);
    }

    QWidget *hostWindow_;
    QToolButton *maximizeButton_;
    QPoint dragStartPosition_;
    bool dragPending_ = false;
};

QWidget *createPageShell(const QString &objectName, const QString &title)
{
    auto *page = new QWidget;
    page->setObjectName(objectName);

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("pageTitleLabel"));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(0);
    layout->addWidget(titleLabel);
    layout->addStretch();
    return page;
}
} // namespace

MainWindow::MainWindow(const QString &displayName, QWidget *parent)
    : MainWindow(displayName, nullptr, nullptr, parent)
{
}

MainWindow::MainWindow(const QString &displayName,
                       RealtimeRecognitionStore *realtimeStore,
                       ImageLoadService *imageLoadService,
                       QWidget *parent)
    : QMainWindow(parent)
    , sidebar_(new NavigationSidebar(displayName,
                                     QStringLiteral(PLATE_CLIENT_VERSION),
                                     QStringLiteral(PLATE_CLIENT_BUILD_DATE),
                                     this))
    , pageStack_(new QStackedWidget(this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("车牌识别系统"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    setMinimumSize(1024, 640);
    resize(1280, 800);

    pageStack_->setObjectName(QStringLiteral("pageStack"));
    if (realtimeStore && imageLoadService) {
        realtimePage_ = new RealtimePage(realtimeStore, imageLoadService, pageStack_);
        pageStack_->addWidget(realtimePage_);
    } else {
        pageStack_->addWidget(createPageShell(QStringLiteral("realtimePageShell"),
                                              QStringLiteral("实时监控")));
    }
    pageStack_->addWidget(createPageShell(QStringLiteral("historyPageShell"),
                                          QStringLiteral("日志记录")));
    pageStack_->addWidget(createPageShell(QStringLiteral("accessListPageShell"),
                                          QStringLiteral("黑白名单管理")));
    pageStack_->setCurrentIndex(NavigationSidebar::RealtimePage);

    auto *titleBar = new MainWindowTitleBar(this);
    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("mainBody"));
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(sidebar_);
    bodyLayout->addWidget(pageStack_, 1);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainContent"));
    auto *windowLayout = new QVBoxLayout(central);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(titleBar);
    windowLayout->addWidget(body, 1);
    setCentralWidget(central);

    connect(sidebar_, &NavigationSidebar::pageRequested, this, [this](int pageIndex) {
        if (pageIndex >= 0 && pageIndex < pageStack_->count()) {
            pageStack_->setCurrentIndex(pageIndex);
        }
    });
    connect(sidebar_, &NavigationSidebar::logoutRequested, this,
            &MainWindow::logoutRequested);

    setStyleSheet(QStringLiteral(R"(
        QWidget#mainWindowTitleBar {
            background: qlineargradient(
                x1: 0, y1: 0, x2: 1, y2: 0,
                stop: 0 #2A5B78,
                stop: 1 #3C7898
            );
        }
        QLabel#mainWindowTitleLabel {
            color: #FFFFFF;
            font-size: 14px;
            font-weight: bold;
        }
        QToolButton#mainMinimizeButton,
        QToolButton#mainMaximizeButton,
        QToolButton#mainCloseButton {
            color: #FFFFFF;
            background: transparent;
            border: 0;
            font-size: 18px;
        }
        QToolButton#mainMinimizeButton:hover,
        QToolButton#mainMaximizeButton:hover {
            background: rgba(255, 255, 255, 36);
        }
        QToolButton#mainCloseButton:hover {
            background: #C42B1C;
        }
        QWidget#mainContent,
        QWidget#mainBody,
        QStackedWidget#pageStack,
        QWidget#realtimePageShell,
        QWidget#historyPageShell,
        QWidget#accessListPageShell {
            background: #F4F6F8;
        }
        QLabel#pageTitleLabel {
            color: #1F2933;
            font-size: 24px;
            font-weight: bold;
        }
    )"));
}

NavigationSidebar *MainWindow::sidebar() const noexcept
{
    return sidebar_;
}

QStackedWidget *MainWindow::pageStack() const noexcept
{
    return pageStack_;
}

RealtimePage *MainWindow::realtimePage() const noexcept
{
    return realtimePage_;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage && nativeMessage->message == WM_NCHITTEST && !isFullScreen()) {
        POINT position{static_cast<LONG>(static_cast<short>(LOWORD(nativeMessage->lParam))),
                       static_cast<LONG>(static_cast<short>(HIWORD(nativeMessage->lParam)))};
        RECT clientRect{};
        if (ScreenToClient(nativeMessage->hwnd, &position)
            && GetClientRect(nativeMessage->hwnd, &clientRect)) {
            const int clientWidth = clientRect.right - clientRect.left;
            const int clientHeight = clientRect.bottom - clientRect.top;
            const qreal nativeScale = width() > 0 ? clientWidth / static_cast<qreal>(width()) : 1.0;
            const int x = position.x;
            const int y = position.y;
            const int border = qCeil(windowResizeBorder * nativeScale);
            if (!isMaximized()) {
                const bool left = x < border;
                const bool right = x >= clientWidth - border;
                const bool top = y < border;
                const bool bottom = y >= clientHeight - border;

                if (top && left) {
                    *result = HTTOPLEFT;
                } else if (top && right) {
                    *result = HTTOPRIGHT;
                } else if (bottom && left) {
                    *result = HTBOTTOMLEFT;
                } else if (bottom && right) {
                    *result = HTBOTTOMRIGHT;
                } else if (left) {
                    *result = HTLEFT;
                } else if (right) {
                    *result = HTRIGHT;
                } else if (top) {
                    *result = HTTOP;
                } else if (bottom) {
                    *result = HTBOTTOM;
                } else {
                    *result = HTCLIENT;
                }
                if (*result != HTCLIENT) {
                    return true;
                }
            }

            const int titleBarHeight = qCeil(mainTitleBarHeight * nativeScale);
            if (y < titleBarHeight) {
                *result = HTCLIENT;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (firstShow_) {
        firstShow_ = false;
        showMaximized();
    }
}
