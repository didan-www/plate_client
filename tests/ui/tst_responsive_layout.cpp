#include "app/AppController.h"
#include "integration/MockPlateServer.h"
#include "ui/pages/AccessListPage.h"
#include "ui/pages/HistoryPage.h"
#include "ui/pages/RealtimePage.h"
#include "ui/widgets/NavigationSidebar.h"
#include "ui/windows/LoginWindow.h"
#include "ui/windows/MainWindow.h"

#include <QAbstractButton>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QHeaderView>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QRect mappedGeometry(QWidget *widget, QWidget *ancestor)
{
    return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
}

void verifyContained(QWidget *container, QWidget *widget)
{
    QVERIFY2(widget, qPrintable(container->objectName()));
    const QRect geometry = mappedGeometry(widget, container);
    QVERIFY2(container->rect().contains(geometry),
             qPrintable(QStringLiteral("%1 outside %2: %3")
                            .arg(widget->objectName(),
                                 container->objectName(),
                                 QString::fromLatin1("%1,%2 %3x%4")
                                     .arg(geometry.x())
                                     .arg(geometry.y())
                                     .arg(geometry.width())
                                     .arg(geometry.height()))));
}

void verifyButtonTextFits(QWidget *page)
{
    const QList<QAbstractButton *> buttons = page->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (!button->isVisibleTo(page) || button->text().isEmpty()) {
            continue;
        }
        verifyContained(page, button);
        QVERIFY2(button->fontMetrics().horizontalAdvance(button->text())
                     <= button->contentsRect().width(),
                 qPrintable(button->objectName()));
    }
}

void verifyNoOverlap(QWidget *container, const QStringList &objectNames)
{
    QList<QWidget *> controls;
    for (const QString &objectName : objectNames) {
        QWidget *control = container->findChild<QWidget *>(objectName);
        QVERIFY2(control, qPrintable(objectName));
        verifyContained(container, control);
        controls.append(control);
    }
    for (qsizetype left = 0; left < controls.size(); ++left) {
        for (qsizetype right = left + 1; right < controls.size(); ++right) {
            const QRect leftRect = mappedGeometry(controls.at(left), container);
            const QRect rightRect = mappedGeometry(controls.at(right), container);
            QVERIFY2(!leftRect.intersects(rightRect),
                     qPrintable(QStringLiteral("%1 overlaps %2")
                                    .arg(controls.at(left)->objectName(),
                                         controls.at(right)->objectName())));
        }
    }
}

void verifyLabelsAndTableHeadersFit(QWidget *page)
{
    for (QLabel *label : page->findChildren<QLabel *>()) {
        if (!label->isVisibleTo(page) || label->wordWrap() || label->text().isEmpty()) {
            continue;
        }
        verifyContained(page, label);
        QVERIFY2(label->fontMetrics().horizontalAdvance(label->text()) <= label->width(),
                 qPrintable(label->objectName()));
    }
    for (QTableView *table : page->findChildren<QTableView *>()) {
        verifyContained(page, table);
        QVERIFY(table->horizontalHeader());
        for (int column = 0; column < table->model()->columnCount(); ++column) {
            const QString header = table->model()
                                       ->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                                       .toString();
            QVERIFY2(table->fontMetrics().horizontalAdvance(header) + 20
                         <= table->horizontalHeader()->sectionSize(column),
                     qPrintable(header));
        }
    }
    for (QTabBar *tabBar : page->findChildren<QTabBar *>()) {
        if (!tabBar->isVisibleTo(page)) {
            continue;
        }
        verifyContained(page, tabBar);
        for (int tab = 0; tab < tabBar->count(); ++tab) {
            QVERIFY2(tabBar->fontMetrics().horizontalAdvance(tabBar->tabText(tab)) + 24
                         <= tabBar->tabRect(tab).width(),
                     qPrintable(tabBar->tabText(tab)));
        }
    }
}

void verifyPage(QWidget *page, const QStringList &requiredObjects)
{
    QVERIFY(page);
    QVERIFY(page->layout());
    (void)page->layout()->activate();
    for (const QString &objectName : requiredObjects) {
        QWidget *control = page->findChild<QWidget *>(objectName);
        QVERIFY2(control, qPrintable(objectName));
        verifyContained(page, control);
    }
    verifyButtonTextFits(page);
    verifyLabelsAndTableHeadersFit(page);
}
} // namespace

class ResponsiveLayoutTest final : public QObject
{
    Q_OBJECT

private slots:
    void loginFitsCurrentDpi();
    void fullClientPagesFitTargetResolution_data();
    void fullClientPagesFitTargetResolution();
};

void ResponsiveLayoutTest::loginFitsCurrentDpi()
{
    LoginWindow window;
    window.showError(QStringLiteral("登录失败，请检查用户名和密码后重试。"));
    window.show();
    QTRY_VERIFY(window.isVisible());
    (void)window.layout()->activate();
    QCOMPARE(window.size(), QSize(520, 420));

    const QStringList controls {
        QStringLiteral("titleBar"),
        QStringLiteral("logoLabel"),
        QStringLiteral("titleLabel"),
        QStringLiteral("usernameIconLabel"),
        QStringLiteral("usernameTextLabel"),
        QStringLiteral("usernameEdit"),
        QStringLiteral("passwordIconLabel"),
        QStringLiteral("passwordTextLabel"),
        QStringLiteral("passwordEdit"),
        QStringLiteral("errorLabel"),
        QStringLiteral("loginButton"),
    };
    for (const QString &objectName : controls) {
        QWidget *control = window.findChild<QWidget *>(objectName);
        QVERIFY2(control, qPrintable(objectName));
        verifyContained(&window, control);
    }

    for (QLabel *label : window.findChildren<QLabel *>()) {
        if (!label->wordWrap() && !label->text().isEmpty()) {
            QVERIFY2(label->fontMetrics().horizontalAdvance(label->text()) <= label->width(),
                     qPrintable(label->objectName()));
        }
    }
    verifyButtonTextFits(&window);
}

void ResponsiveLayoutTest::fullClientPagesFitTargetResolution_data()
{
    QTest::addColumn<QSize>("resolution");
    QTest::newRow("1366x768") << QSize(1366, 768);
    QTest::newRow("1920x1080") << QSize(1920, 1080);
    QTest::newRow("3840x2160") << QSize(3840, 2160);
}

void ResponsiveLayoutTest::fullClientPagesFitTargetResolution()
{
    QFETCH(QSize, resolution);
    MockPlateServer server;
    QVERIFY(server.listen());
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    QSignalSpy loginSucceeded(runtime.controller(), &AppController::loginSucceeded);
    runtime.submitLogin();
    QTRY_COMPARE_WITH_TIMEOUT(loginSucceeded.count(), 1, 1000);

    MainWindow *window = runtime.mainWindow();
    QVERIFY(window);
    window->showNormal();
    QTRY_VERIFY(!window->isMaximized());
    window->resize(resolution);
    QTRY_COMPARE(window->size(), resolution);
    window->ensurePolished();
    (void)window->layout()->activate();
    (void)window->centralWidget()->layout()->activate();

    auto *titleBar = window->findChild<QWidget *>(QStringLiteral("mainWindowTitleBar"));
    auto *body = window->findChild<QWidget *>(QStringLiteral("mainBody"));
    auto *stack = window->pageStack();
    auto *sidebar = window->sidebar();
    QVERIFY(titleBar && body && stack && sidebar);
    (void)body->layout()->activate();
    (void)sidebar->layout()->activate();
    verifyContained(window->centralWidget(), titleBar);
    verifyContained(window->centralWidget(), body);
    verifyContained(body, sidebar);
    verifyContained(body, stack);
    QVERIFY(mappedGeometry(sidebar, body).right() < mappedGeometry(stack, body).left());

    const QList<QPair<QWidget *, QStringList>> pages {
        {runtime.mainWindow()->realtimePage(),
         {QStringLiteral("realtimeTitleLabel"),
          QStringLiteral("currentImagePanel"),
          QStringLiteral("currentDetailsPanel"),
          QStringLiteral("realtimeTable")}},
        {runtime.mainWindow()->historyPage(),
         {QStringLiteral("historyStartTimeEdit"),
          QStringLiteral("historyEndTimeEdit"),
          QStringLiteral("historyDeviceEdit"),
          QStringLiteral("historySearchButton"),
          QStringLiteral("historyTable"),
          QStringLiteral("historyExportButton")}},
        {runtime.mainWindow()->accessListPage(),
         {QStringLiteral("accessListTabs"),
          QStringLiteral("accessListKeywordEdit"),
          QStringLiteral("accessListSearchButton"),
          QStringLiteral("accessListTable"),
          QStringLiteral("accessListCreateButton")}},
    };

    for (const auto &entry : pages) {
        QWidget *page = entry.first;
        stack->setCurrentWidget(page);
        QTRY_VERIFY(page->isVisible());
        verifyPage(page, entry.second);
        if (page == runtime.mainWindow()->realtimePage()) {
            verifyNoOverlap(page,
                            {QStringLiteral("currentImagePanel"),
                             QStringLiteral("currentDetailsPanel"),
                             QStringLiteral("realtimeTable")});
        } else if (page == runtime.mainWindow()->historyPage()) {
            verifyNoOverlap(page,
                            {QStringLiteral("historyStartTimeEdit"),
                             QStringLiteral("historyEndTimeEdit"),
                             QStringLiteral("historyDeviceEdit"),
                             QStringLiteral("historySearchButton"),
                             QStringLiteral("historyResetButton")});
            verifyNoOverlap(page,
                            {QStringLiteral("historyPreviousButton"),
                             QStringLiteral("historyNextButton")});
        } else {
            verifyNoOverlap(page,
                            {QStringLiteral("accessListKeywordEdit"),
                             QStringLiteral("accessListSearchButton"),
                             QStringLiteral("accessListResetButton"),
                             QStringLiteral("accessListRefreshButton")});
            verifyNoOverlap(page,
                            {QStringLiteral("accessListCreateButton"),
                             QStringLiteral("accessListDeleteButton")});
            verifyNoOverlap(page,
                            {QStringLiteral("accessListPreviousButton"),
                             QStringLiteral("accessListNextButton")});
        }
    }
}

QTEST_MAIN(ResponsiveLayoutTest)

#include "tst_responsive_layout.moc"
