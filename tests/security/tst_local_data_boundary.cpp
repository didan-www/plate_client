#include "infrastructure/logging/TechnicalLogger.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString normalizedRelativePath(const QDir &root, const QString &path)
{
    return QDir::fromNativeSeparators(root.relativeFilePath(path));
}

bool isForbiddenBusinessFile(const QFileInfo &file)
{
    const QString name = file.fileName().toLower();
    const QString suffix = file.suffix().toLower();
    return name.contains(QStringLiteral("sqlite"))
        || name.contains(QStringLiteral("mysql"))
        || name.startsWith(QStringLiteral("qsql"))
        || suffix == QStringLiteral("db")
        || suffix == QStringLiteral("sqlite")
        || suffix == QStringLiteral("sqlite3")
        || suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("png")
        || suffix == QStringLiteral("csv");
}

bool isProductionLogPath(const QString &relativePath)
{
    static const QRegularExpression logPathPattern(
        QStringLiteral(R"(^logs/plate-client-\d{4}-\d{2}-\d{2}\.log$)"));
    return logPathPattern.match(relativePath).hasMatch();
}
} // namespace

class LocalDataBoundaryTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultQtLocationsMatchProductionBoundaryWithoutWriting();
    void productionSourceDoesNotEnableForbiddenPersistence();
    void applicationRuntimeDirectoryHasNoBusinessDataOrDatabaseDriver();
    void temporaryUserDataContainsOnlyUsernameAndSanitizedTechnicalLog();
};

void LocalDataBoundaryTest::defaultQtLocationsMatchProductionBoundaryWithoutWriting()
{
    const QString previousOrganization = QCoreApplication::organizationName();
    const QString previousApplication = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("PlateRecognitionTeachingSystem"));
    QCoreApplication::setApplicationName(QStringLiteral("plate_client"));

    const QString appLocalData = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QVERIFY(!appLocalData.isEmpty());
    QCOMPARE(TechnicalLogger::defaultLogDirectory(),
             QDir(appLocalData).filePath(QStringLiteral("logs")));

    const QDir appLocalDataDirectory(appLocalData);
    if (appLocalDataDirectory.exists()) {
        QDirIterator iterator(appLocalDataDirectory.absolutePath(),
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QFileInfo file(iterator.next());
            const QString relative = normalizedRelativePath(appLocalDataDirectory,
                                                            file.absoluteFilePath());
            QVERIFY2(!isForbiddenBusinessFile(file), qPrintable(relative));
            QVERIFY2(isProductionLogPath(relative), qPrintable(relative));
        }
    }

    QSettings settings;
    QCOMPARE(settings.scope(), QSettings::UserScope);
    QCOMPARE(settings.format(), QSettings::NativeFormat);
    const QString settingsLocation = QDir::fromNativeSeparators(settings.fileName()).toLower();
    QVERIFY2(settingsLocation.contains(QStringLiteral("platerecognitionteachingsystem")),
             qPrintable(settingsLocation));
    QVERIFY2(settingsLocation.contains(QStringLiteral("plate_client")),
             qPrintable(settingsLocation));

    QCoreApplication::setOrganizationName(previousOrganization);
    QCoreApplication::setApplicationName(previousApplication);
}

void LocalDataBoundaryTest::productionSourceDoesNotEnableForbiddenPersistence()
{
    const QDir sourceRoot(QStringLiteral(PLATE_CLIENT_SOURCE_DIR));
    QVERIFY(sourceRoot.exists(QStringLiteral("src")));
    const QStringList forbiddenTokens {
        QStringLiteral("qtsql"),
        QStringLiteral("qsqldatabase"),
        QStringLiteral("qsqlquery"),
        QStringLiteral("qnetworkdiskcache"),
        QStringLiteral("sqlite"),
        QStringLiteral("mysql"),
    };

    QDirIterator iterator(sourceRoot.filePath(QStringLiteral("src")),
                          {QStringLiteral("*.h"),
                           QStringLiteral("*.cpp"),
                           QStringLiteral("CMakeLists.txt")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString source = readTextFile(path).toLower();
        for (const QString &token : forbiddenTokens) {
            QVERIFY2(!source.contains(token),
                     qPrintable(QStringLiteral("forbidden token '%1' in %2")
                                    .arg(token, normalizedRelativePath(sourceRoot, path))));
        }
    }

    const QString controller = readTextFile(
        sourceRoot.filePath(QStringLiteral("src/app/AppController.cpp")));
    QCOMPARE(controller.count(QStringLiteral("settings_->setValue")), 1);
    QVERIFY(controller.contains(QStringLiteral("login/lastUsername")));
}

void LocalDataBoundaryTest::applicationRuntimeDirectoryHasNoBusinessDataOrDatabaseDriver()
{
    const QFileInfo application(QStringLiteral(PLATE_CLIENT_APP_PATH));
    QVERIFY2(application.isFile(), qPrintable(application.absoluteFilePath()));
    const QDir runtimeDirectory(application.absolutePath());

    QDirIterator iterator(runtimeDirectory.absolutePath(),
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo file(iterator.next());
        const QString relative = normalizedRelativePath(runtimeDirectory,
                                                        file.absoluteFilePath());
        if (relative.startsWith(QStringLiteral("CMakeFiles/"))
            || relative.startsWith(QStringLiteral(".qt/"))) {
            continue;
        }
        QVERIFY2(!isForbiddenBusinessFile(file), qPrintable(relative));
    }
}

void LocalDataBoundaryTest::temporaryUserDataContainsOnlyUsernameAndSanitizedTechnicalLog()
{
    QTemporaryDir userData;
    QVERIFY(userData.isValid());
    const QDir root(userData.path());
    const QString settingsPath = root.filePath(QStringLiteral("settings.ini"));
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("login/lastUsername"),
                          QStringLiteral("operator"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        QCOMPARE(settings.allKeys(), QStringList {QStringLiteral("login/lastUsername")});
    }

    constexpr auto accessSecret = "task022-access-secret";
    constexpr auto passwordSecret = "task022-password-secret";
    constexpr auto mqttSecret = "task022-mqtt-secret";
    constexpr auto imageSecret = "task022-image-bytes";
    const QDateTime now = QDateTime::fromString(
        QStringLiteral("2026-08-25T10:20:30+08:00"), Qt::ISODate);
    TechnicalLogger logger(root.filePath(QStringLiteral("logs")), [now] { return now; });
    TechnicalLogEntry entry;
    entry.level = TechnicalLogLevel::Warning;
    entry.module = QStringLiteral("security.acceptance");
    entry.errorCode = QStringLiteral("BOUNDARY_TEST");
    entry.requestId = QStringLiteral("request-task022");
    entry.context = QJsonObject {
        {QStringLiteral("accessToken"), QString::fromLatin1(accessSecret)},
        {QStringLiteral("password"), QString::fromLatin1(passwordSecret)},
        {QStringLiteral("mqttPassword"), QString::fromLatin1(mqttSecret)},
        {QStringLiteral("mqttPayload"), QStringLiteral("complete payload")},
        {QStringLiteral("imageBytes"), QString::fromLatin1(imageSecret)},
        {QStringLiteral("note"),
         QStringLiteral("Authorization: Bearer %1").arg(
             QString::fromLatin1(accessSecret))},
    };
    QVERIFY(logger.write(entry));

    const QString logPath = root.filePath(
        QStringLiteral("logs/plate-client-2026-08-25.log"));
    const QString log = readTextFile(logPath);
    QVERIFY(!log.isEmpty());
    QVERIFY(log.contains(QStringLiteral("[REDACTED]")));
    QVERIFY(log.contains(QStringLiteral("[OMITTED]")));
    QVERIFY(!log.contains(QString::fromLatin1(accessSecret)));
    QVERIFY(!log.contains(QString::fromLatin1(passwordSecret)));
    QVERIFY(!log.contains(QString::fromLatin1(mqttSecret)));
    QVERIFY(!log.contains(QString::fromLatin1(imageSecret)));

    QStringList files;
    QDirIterator iterator(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo file(iterator.next());
        const QString relative = normalizedRelativePath(root, file.absoluteFilePath());
        files.append(relative);
        QVERIFY2(!isForbiddenBusinessFile(file), qPrintable(relative));
    }
    files.sort();
    QCOMPARE(files,
             QStringList({QStringLiteral("logs/plate-client-2026-08-25.log"),
                          QStringLiteral("settings.ini")}));
}

QTEST_MAIN(LocalDataBoundaryTest)

#include "tst_local_data_boundary.moc"
