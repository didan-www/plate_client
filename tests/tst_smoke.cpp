#include <QCoreApplication>
#include <QFuture>
#include <QMqttClient>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QtConcurrentRun>
#include <QtTest>
#include <QWidget>

class SmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void buildMetadataIsInjected();
    void requiredQtModulesAreUsable();
};

void SmokeTest::initTestCase()
{
    QCoreApplication::setApplicationVersion(QStringLiteral(PLATE_CLIENT_VERSION));
}

void SmokeTest::buildMetadataIsInjected()
{
    QCOMPARE(QCoreApplication::applicationVersion(), QStringLiteral("0.1.1"));
    QVERIFY(QRegularExpression(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}$)"))
                .match(QStringLiteral(PLATE_CLIENT_BUILD_DATE))
                .hasMatch());
}

void SmokeTest::requiredQtModulesAreUsable()
{
    QWidget widget;
    QNetworkAccessManager networkManager;
    QMqttClient mqttClient;
    const QFuture<int> future = QtConcurrent::run([] { return 42; });

    widget.show();

    QTRY_VERIFY(widget.isVisible());
    QCOMPARE(future.result(), 42);
    QVERIFY(networkManager.thread() == QCoreApplication::instance()->thread());
    QVERIFY(mqttClient.thread() == QCoreApplication::instance()->thread());
}

QTEST_MAIN(SmokeTest)

#include "tst_smoke.moc"
