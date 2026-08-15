#include "services/HttpConnectivityState.h"
#include "ui/widgets/ConnectionStatusIndicator.h"
#include "ui/widgets/NavigationSidebar.h"

#include <QToolButton>
#include <QtTest>

class HttpStatusIndicatorTest final : public QObject
{
    Q_OBJECT

private slots:
    void mapsConnectivityStates();
    void abnormalStateDoesNotDisableAvailableNavigation();
};

void HttpStatusIndicatorTest::mapsConnectivityStates()
{
    ConnectionStatusIndicator indicator(QStringLiteral("HTTP服务"));

    indicator.setHttpConnectivityState(HttpConnectivityState::Detecting);
    QCOMPARE(indicator.state(), ConnectionStatusIndicator::VisualState::Busy);
    QCOMPARE(indicator.statusText(), QStringLiteral("检测中"));

    indicator.setHttpConnectivityState(HttpConnectivityState::Online);
    QCOMPARE(indicator.state(), ConnectionStatusIndicator::VisualState::Online);
    QCOMPARE(indicator.statusText(), QStringLiteral("在线"));

    indicator.setHttpConnectivityState(HttpConnectivityState::Abnormal);
    QCOMPARE(indicator.state(), ConnectionStatusIndicator::VisualState::Error);
    QCOMPARE(indicator.statusText(), QStringLiteral("异常"));
}

void HttpStatusIndicatorTest::abnormalStateDoesNotDisableAvailableNavigation()
{
    NavigationSidebar sidebar(QStringLiteral("测试操作员"),
                              QStringLiteral("0.1.1"),
                              QStringLiteral("2026-08-15"));
    sidebar.setHttpConnectivityState(HttpConnectivityState::Abnormal);

    QCOMPARE(sidebar.httpStatusIndicator()->statusText(), QStringLiteral("异常"));
    const QStringList enabledNames {QStringLiteral("realtimeNavButton"),
                                    QStringLiteral("historyNavButton"),
                                    QStringLiteral("accessListNavButton")};
    for (const QString &name : enabledNames) {
        auto *button = sidebar.findChild<QToolButton *>(name);
        QVERIFY(button);
        QVERIFY(button->isEnabled());
    }
    QVERIFY(!sidebar.findChild<QToolButton *>(QStringLiteral("videoRecognitionNavButton"))
                 ->isEnabled());
    QVERIFY(!sidebar.findChild<QToolButton *>(QStringLiteral("cameraRecognitionNavButton"))
                 ->isEnabled());
}

QTEST_MAIN(HttpStatusIndicatorTest)

#include "tst_http_status_indicator.moc"
