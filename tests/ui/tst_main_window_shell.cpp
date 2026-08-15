#include "ui/windows/MainWindow.h"

#include "ui/widgets/ConnectionStatusIndicator.h"
#include "ui/widgets/NavigationSidebar.h"

#include <QColor>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QToolButton>
#include <QtTest>

namespace {
bool colorsAreClose(const QColor &actual, const QColor &expected, int tolerance = 2)
{
    return qAbs(actual.red() - expected.red()) <= tolerance
        && qAbs(actual.green() - expected.green()) <= tolerance
        && qAbs(actual.blue() - expected.blue()) <= tolerance
        && actual.alpha() == expected.alpha();
}
} // namespace

class MainWindowShellTest final : public QObject
{
    Q_OBJECT

private slots:
    void titleBarRendersGradientAndWhiteTitle();
    void titleBarProvidesWindowControls();
    void sidebarRendersConfiguredDarkBackground();
    void navigationOrderDisabledItemsAndPageMapping();
    void showsIdentityIndependentStatusesAndBuildInformation();
    void logoutButtonEmitsInteractionSignal();
    void firstShowIsMaximized();
    void layoutFitsSupportedSizes_data();
    void layoutFitsSupportedSizes();
};

void MainWindowShellTest::titleBarRendersGradientAndWhiteTitle()
{
    MainWindow window(QStringLiteral("测试操作员"));
    window.resize(1280, 800);
    window.ensurePolished();
    QVERIFY(window.layout()->activate());
    QVERIFY(window.centralWidget()->layout()->activate());

    auto *titleBar = window.findChild<QWidget *>(QStringLiteral("mainWindowTitleBar"));
    auto *title = window.findChild<QLabel *>(QStringLiteral("mainWindowTitleLabel"));
    QVERIFY(titleBar && title);
    QCOMPARE(titleBar->height(), 44);
    QVERIFY(titleBar->width() > 200);
    QCOMPARE(title->text(), QStringLiteral("车牌识别系统"));
    title->ensurePolished();
    QCOMPARE(title->palette().color(QPalette::WindowText), QColor(QStringLiteral("#FFFFFF")));

    QImage rendered(titleBar->size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    titleBar->render(&rendered);

    const QColor left = rendered.pixelColor(1, titleBar->height() / 2);
    const QColor right = rendered.pixelColor(titleBar->width() - 2, titleBar->height() / 2);
    QVERIFY2(colorsAreClose(left, QColor(QStringLiteral("#2A5B78"))),
             qPrintable(QStringLiteral("left title color: %1").arg(left.name(QColor::HexArgb))));
    QVERIFY2(colorsAreClose(right, QColor(QStringLiteral("#3C7898"))),
             qPrintable(QStringLiteral("right title color: %1").arg(right.name(QColor::HexArgb))));
}

void MainWindowShellTest::titleBarProvidesWindowControls()
{
    MainWindow window(QStringLiteral("测试操作员"));
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QTRY_VERIFY(window.isMaximized());
    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));

    auto *titleBar = window.findChild<QWidget *>(QStringLiteral("mainWindowTitleBar"));
    auto *minimize = window.findChild<QToolButton *>(QStringLiteral("mainMinimizeButton"));
    auto *maximize = window.findChild<QToolButton *>(QStringLiteral("mainMaximizeButton"));
    auto *close = window.findChild<QToolButton *>(QStringLiteral("mainCloseButton"));
    QVERIFY(titleBar && minimize && maximize && close);
    QCOMPARE(maximize->accessibleName(), QStringLiteral("还原"));

    maximize->click();
    QTRY_VERIFY(!window.isMaximized());
    QCOMPARE(maximize->accessibleName(), QStringLiteral("最大化"));

    QTest::mouseDClick(titleBar,
                       Qt::LeftButton,
                       Qt::NoModifier,
                       QPoint(titleBar->width() / 2, titleBar->height() / 2));
    QTRY_VERIFY(window.isMaximized());
    QCOMPARE(maximize->accessibleName(), QStringLiteral("还原"));

    minimize->click();
    QTRY_VERIFY(window.isMinimized());
    window.showNormal();
    QTRY_VERIFY(!window.isMinimized());

    close->click();
    QTRY_VERIFY(!window.isVisible());
}

void MainWindowShellTest::sidebarRendersConfiguredDarkBackground()
{
    NavigationSidebar sidebar(QStringLiteral("测试操作员"),
                              QStringLiteral("0.1.1"),
                              QStringLiteral("2026-08-15"));
    sidebar.resize(280, 768);
    sidebar.ensurePolished();
    QVERIFY(sidebar.layout()->activate());

    QImage rendered(sidebar.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    sidebar.render(&rendered);

    QCOMPARE(rendered.pixelColor(1, 1), QColor(QStringLiteral("#173B57")));
}

void MainWindowShellTest::navigationOrderDisabledItemsAndPageMapping()
{
    MainWindow window(QStringLiteral("测试操作员"));
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();

    NavigationSidebar *sidebar = window.sidebar();
    QVERIFY(sidebar != nullptr);
    auto *realtime = sidebar->findChild<QToolButton *>(QStringLiteral("realtimeNavButton"));
    auto *video = sidebar->findChild<QToolButton *>(
        QStringLiteral("videoRecognitionNavButton"));
    auto *camera = sidebar->findChild<QToolButton *>(
        QStringLiteral("cameraRecognitionNavButton"));
    auto *history = sidebar->findChild<QToolButton *>(QStringLiteral("historyNavButton"));
    auto *accessList = sidebar->findChild<QToolButton *>(
        QStringLiteral("accessListNavButton"));
    QVERIFY(realtime && video && camera && history && accessList);

    QVERIFY(realtime->y() < video->y());
    QVERIFY(video->y() < camera->y());
    QVERIFY(camera->y() < history->y());
    QVERIFY(history->y() < accessList->y());
    QVERIFY(video->isVisible());
    QVERIFY(camera->isVisible());
    QVERIFY(!video->isEnabled());
    QVERIFY(!camera->isEnabled());

    QStackedWidget *stack = window.pageStack();
    QCOMPARE(stack->count(), 3);
    QCOMPARE(stack->currentIndex(), NavigationSidebar::RealtimePage);
    QCOMPARE(stack->widget(0)->objectName(), QStringLiteral("realtimePageShell"));
    QCOMPARE(stack->widget(1)->objectName(), QStringLiteral("historyPageShell"));
    QCOMPARE(stack->widget(2)->objectName(), QStringLiteral("accessListPageShell"));

    history->click();
    QCOMPARE(stack->currentIndex(), NavigationSidebar::HistoryPage);
    video->click();
    QCOMPARE(stack->currentIndex(), NavigationSidebar::HistoryPage);
    camera->click();
    QCOMPARE(stack->currentIndex(), NavigationSidebar::HistoryPage);
    accessList->click();
    QCOMPARE(stack->currentIndex(), NavigationSidebar::AccessListPage);
    realtime->click();
    QCOMPARE(stack->currentIndex(), NavigationSidebar::RealtimePage);
}

void MainWindowShellTest::showsIdentityIndependentStatusesAndBuildInformation()
{
    MainWindow window(QStringLiteral("测试操作员"));
    NavigationSidebar *sidebar = window.sidebar();
    QCOMPARE(sidebar->findChild<QLabel *>(QStringLiteral("appTitleLabel"))->text(),
             QStringLiteral("车牌识别系统"));
    QCOMPARE(sidebar->findChild<QLabel *>(QStringLiteral("usernameLabel"))->text(),
             QStringLiteral("测试操作员"));
    QCOMPARE(sidebar->findChild<QLabel *>(QStringLiteral("versionLabel"))->text(),
             QStringLiteral("版本 %1").arg(QStringLiteral(PLATE_CLIENT_VERSION)));
    QCOMPARE(sidebar->findChild<QLabel *>(QStringLiteral("buildDateLabel"))->text(),
             QStringLiteral("构建 %1").arg(QStringLiteral(PLATE_CLIENT_BUILD_DATE)));
    QVERIFY(sidebar->findChild<QLabel *>(QStringLiteral("defaultAvatarLabel")) != nullptr);

    ConnectionStatusIndicator *http = sidebar->httpStatusIndicator();
    ConnectionStatusIndicator *mqtt = sidebar->mqttStatusIndicator();
    QVERIFY(http != mqtt);
    QCOMPARE(http->statusText(), QStringLiteral("检测中"));
    QCOMPARE(http->state(), ConnectionStatusIndicator::VisualState::Busy);
    QCOMPARE(mqtt->statusText(), QStringLiteral("未连接"));
    QCOMPARE(mqtt->state(), ConnectionStatusIndicator::VisualState::Neutral);

    http->setStatus(ConnectionStatusIndicator::VisualState::Online, QStringLiteral("在线"));
    QCOMPARE(http->statusText(), QStringLiteral("在线"));
    QCOMPARE(mqtt->statusText(), QStringLiteral("未连接"));
    mqtt->setStatus(ConnectionStatusIndicator::VisualState::Error,
                    QStringLiteral("配置或授权错误"));
    QCOMPARE(http->statusText(), QStringLiteral("在线"));
    QCOMPARE(mqtt->statusText(), QStringLiteral("配置或授权错误"));
}

void MainWindowShellTest::logoutButtonEmitsInteractionSignal()
{
    MainWindow window(QStringLiteral("测试操作员"));
    QSignalSpy logoutSpy(&window, &MainWindow::logoutRequested);
    auto *button = window.sidebar()->findChild<QPushButton *>(QStringLiteral("logoutButton"));
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(logoutSpy.count(), 1);
}

void MainWindowShellTest::firstShowIsMaximized()
{
    MainWindow window(QStringLiteral("测试操作员"));
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QTRY_VERIFY(window.isMaximized());
}

void MainWindowShellTest::layoutFitsSupportedSizes_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::newRow("1366x768") << QSize(1366, 768);
    QTest::newRow("1920x1080") << QSize(1920, 1080);
    QTest::newRow("3840x2160") << QSize(3840, 2160);
}

void MainWindowShellTest::layoutFitsSupportedSizes()
{
    QFETCH(QSize, windowSize);
    MainWindow window(QStringLiteral("这是一个较长的测试用户名"));
    window.resize(windowSize);
    window.ensurePolished();
    QVERIFY(window.layout()->activate());
    QVERIFY(window.centralWidget()->layout()->activate());

    NavigationSidebar *sidebar = window.sidebar();
    QStackedWidget *stack = window.pageStack();
    auto *titleBar = window.findChild<QWidget *>(QStringLiteral("mainWindowTitleBar"));
    auto *body = window.findChild<QWidget *>(QStringLiteral("mainBody"));
    QVERIFY(titleBar && body);
    QVERIFY(body->layout()->activate());
    QVERIFY(sidebar->layout()->activate());
    QCOMPARE(sidebar->width(), 280);
    QCOMPARE(titleBar->height(), 44);

    const QRect titleBarRect(titleBar->mapTo(window.centralWidget(), QPoint(0, 0)),
                             titleBar->size());
    const QRect sidebarRect(sidebar->mapTo(window.centralWidget(), QPoint(0, 0)),
                            sidebar->size());
    const QRect stackRect(stack->mapTo(window.centralWidget(), QPoint(0, 0)), stack->size());
    QVERIFY(sidebarRect.right() < stackRect.left());
    QVERIFY(titleBarRect.bottom() < sidebarRect.top());
    QVERIFY(window.centralWidget()->rect().contains(titleBarRect));
    QVERIFY(window.centralWidget()->rect().contains(sidebarRect));
    QVERIFY(window.centralWidget()->rect().contains(stackRect));

    const QList<QToolButton *> navigationButtons = sidebar->findChildren<QToolButton *>();
    QCOMPARE(navigationButtons.size(), 5);
    for (QToolButton *button : navigationButtons) {
        QVERIFY(sidebar->rect().contains(button->geometry()));
        const int availableTextWidth = button->width() - button->iconSize().width() - 56;
        QVERIFY2(button->fontMetrics().horizontalAdvance(button->text()) <= availableTextWidth,
                 qPrintable(button->text()));
    }
    const QList<QLabel *> labels = sidebar->findChildren<QLabel *>();
    for (QLabel *label : labels) {
        const QRect labelRect(label->mapTo(sidebar, QPoint(0, 0)), label->size());
        QVERIFY(sidebar->rect().contains(labelRect));
    }
}

QTEST_MAIN(MainWindowShellTest)

#include "tst_main_window_shell.moc"
