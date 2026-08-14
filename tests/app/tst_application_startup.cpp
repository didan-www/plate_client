#include "app/Application.h"

#include "app/AppController.h"
#include "ui/windows/LoginWindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString writeConfig(const QString &directory, const QByteArray &bytes)
{
    const QString filePath = directory + QStringLiteral("/client.json");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        qFatal("Unable to write startup test config.");
    }
    return filePath;
}
} // namespace

class ApplicationStartupTest final : public QObject
{
    Q_OBJECT

private slots:
    void invalidConfigBlocksBeforeRuntimeInitialization();
    void validConfigCreatesLoginFlow();
};

void ApplicationStartupTest::invalidConfigBlocksBeforeRuntimeInitialization()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString config = writeConfig(directory.path(), QByteArrayLiteral("{not-json"));
    QString presentedError;
    bool factoryCalled = false;
    Application runtime([&presentedError](const QString &message) { presentedError = message; },
                        [&factoryCalled](const QString &) {
                            factoryCalled = true;
                            return new QWidget;
                        });

    QVERIFY(!runtime.startFromConfigFile(config));
    QVERIFY(!runtime.runtimeInitialized());
    QVERIFY(runtime.controller() == nullptr);
    QVERIFY(!presentedError.isEmpty());
    QVERIFY(!factoryCalled);
}

void ApplicationStartupTest::validConfigCreatesLoginFlow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = QJsonDocument(QJsonObject {
                                              {QStringLiteral("clientId"),
                                               QStringLiteral("12345678-1234-4abc-8def-1234567890ab")},
                                              {QStringLiteral("loginBaseUrl"),
                                               QStringLiteral("https://example.test")},
                                              {QStringLiteral("allowInsecureTransport"), false},
                                          })
                                 .toJson(QJsonDocument::Compact);
    const QString config = writeConfig(directory.path(), bytes);
    QString presentedError;
    Application runtime([&presentedError](const QString &message) { presentedError = message; });

    QVERIFY(runtime.startFromConfigFile(config));
    QVERIFY(runtime.runtimeInitialized());
    QVERIFY(runtime.controller() != nullptr);
    QVERIFY(runtime.controller()->loginWindow() != nullptr);
    QVERIFY(runtime.controller()->loginWindow()->isVisible());
    QVERIFY(presentedError.isEmpty());
}

QTEST_MAIN(ApplicationStartupTest)

#include "tst_application_startup.moc"
