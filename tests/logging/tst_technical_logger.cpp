#include "infrastructure/logging/TechnicalLogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

class TechnicalLoggerTest final : public QObject
{
    Q_OBJECT

private slots:
    void writesUtf8JsonLinesByLocalDate();
    void retainsSevenDayBoundaryAndProtectsOtherFiles();
    void redactsSensitiveKeysAndTextPatterns();
    void toleratesUnwritableLogDirectory();

private:
    static QDateTime fixedNow();
    static void writeFile(const QString &filePath, const QByteArray &content = QByteArrayLiteral("x"));
    static QList<QJsonObject> readRecords(const QString &filePath);
};

QDateTime TechnicalLoggerTest::fixedNow()
{
    return QDateTime(QDate(2026, 8, 12), QTime(22, 30, 15, 123), QTimeZone::fromSecondsAheadOfUtc(8 * 3600));
}

void TechnicalLoggerTest::writeFile(const QString &filePath, const QByteArray &content)
{
    QFile file(filePath);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(content), content.size());
}

QList<QJsonObject> TechnicalLoggerTest::readRecords(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Unable to read the test log file.");
    }

    QList<QJsonObject> records;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            qFatal("The test log line is not a JSON object.");
        }
        records.append(document.object());
    }
    return records;
}

void TechnicalLoggerTest::writesUtf8JsonLinesByLocalDate()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString logDirectory = QDir(temporaryDirectory.path()).filePath(QStringLiteral("logs"));
    TechnicalLogger logger(logDirectory, [] { return fixedNow(); });

    TechnicalLogEntry first;
    first.level = TechnicalLogLevel::Warning;
    first.module = QStringLiteral("配置模块");
    first.errorCode = QStringLiteral("CONFIG_INVALID");
    first.requestId = QStringLiteral("request-001");
    first.recognitionId = QStringLiteral("recognition-001");
    first.context = {{QStringLiteral("reason"), QStringLiteral("字段无效")}};
    QVERIFY(logger.write(first));

    TechnicalLogEntry second;
    second.level = TechnicalLogLevel::Info;
    second.module = QStringLiteral("startup");
    second.errorCode = QStringLiteral("OK");
    second.requestId = QStringLiteral("request-002");
    QVERIFY(logger.write(second));

    const QString filePath =
        QDir(logDirectory).filePath(QStringLiteral("plate-client-2026-08-12.log"));
    const QList<QJsonObject> records = readRecords(filePath);
    QCOMPARE(records.size(), 2);
    const QSet<QString> requiredKeys = {
        QStringLiteral("timestamp"),
        QStringLiteral("level"),
        QStringLiteral("module"),
        QStringLiteral("errorCode"),
        QStringLiteral("requestId"),
        QStringLiteral("recognitionId"),
        QStringLiteral("context"),
    };
    const QStringList firstKeys = records.at(0).keys();
    QCOMPARE(QSet<QString>(firstKeys.cbegin(), firstKeys.cend()), requiredKeys);
    QCOMPARE(records.at(0).value(QStringLiteral("timestamp")).toString(),
             QStringLiteral("2026-08-12T22:30:15.123+08:00"));
    QCOMPARE(records.at(0).value(QStringLiteral("level")).toString(), QStringLiteral("WARNING"));
    QCOMPARE(records.at(0).value(QStringLiteral("module")).toString(), QStringLiteral("配置模块"));
    QCOMPARE(records.at(0).value(QStringLiteral("errorCode")).toString(),
             QStringLiteral("CONFIG_INVALID"));
    QCOMPARE(records.at(0).value(QStringLiteral("requestId")).toString(),
             QStringLiteral("request-001"));
    QCOMPARE(records.at(0).value(QStringLiteral("recognitionId")).toString(),
             QStringLiteral("recognition-001"));
    QCOMPARE(records.at(0).value(QStringLiteral("context")).toObject().value(QStringLiteral("reason")).toString(),
             QStringLiteral("字段无效"));
    QVERIFY(!records.at(1).contains(QStringLiteral("recognitionId")));
    QSet<QString> secondExpectedKeys = requiredKeys;
    secondExpectedKeys.remove(QStringLiteral("recognitionId"));
    const QStringList secondKeys = records.at(1).keys();
    QCOMPARE(QSet<QString>(secondKeys.cbegin(), secondKeys.cend()), secondExpectedKeys);
    QCOMPARE(records.at(1).value(QStringLiteral("context")).toObject(), QJsonObject());
    QVERIFY(TechnicalLogger::defaultLogDirectory().endsWith(QStringLiteral("/logs"))
            || TechnicalLogger::defaultLogDirectory().endsWith(QStringLiteral("\\logs")));
}

void TechnicalLoggerTest::retainsSevenDayBoundaryAndProtectsOtherFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir directory(temporaryDirectory.path());

    const QString expired = directory.filePath(QStringLiteral("plate-client-2026-08-04.log"));
    const QString boundary = directory.filePath(QStringLiteral("plate-client-2026-08-05.log"));
    const QString current = directory.filePath(QStringLiteral("plate-client-2026-08-12.log"));
    const QString invalidDate = directory.filePath(QStringLiteral("plate-client-2026-02-30.log"));
    const QString unrelated = directory.filePath(QStringLiteral("application-2026-08-01.log"));
    writeFile(expired);
    writeFile(boundary);
    writeFile(current);
    writeFile(invalidDate);
    writeFile(unrelated);

    TechnicalLogger logger(temporaryDirectory.path(), [] { return fixedNow(); });

    QVERIFY(!QFileInfo::exists(expired));
    QVERIFY(QFileInfo::exists(boundary));
    QVERIFY(QFileInfo::exists(current));
    QVERIFY(QFileInfo::exists(invalidDate));
    QVERIFY(QFileInfo::exists(unrelated));
}

void TechnicalLoggerTest::redactsSensitiveKeysAndTextPatterns()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    TechnicalLogger logger(temporaryDirectory.path(), [] { return fixedNow(); });

    TechnicalLogEntry entry;
    entry.level = TechnicalLogLevel::Error;
    entry.module = QStringLiteral("http");
    entry.errorCode = QStringLiteral("AUTH_FAILED");
    entry.requestId = QStringLiteral("request-secret-test");
    entry.context = {
        {QStringLiteral("Authorization"), QStringLiteral("Bearer header-secret")},
        {QStringLiteral("password"), QStringLiteral("password-secret")},
        {QStringLiteral("access_token"), QStringLiteral("access-secret")},
        {QStringLiteral("nested"), QJsonObject {
             {QStringLiteral("mqttPassword"), QStringLiteral("mqtt-secret")},
             {QStringLiteral("message"), QStringLiteral("Authorization: Bearer text-secret")},
         }},
        {QStringLiteral("values"), QJsonArray {
             QStringLiteral("password=form-secret"),
             QStringLiteral("token=query-secret"),
         }},
        {QStringLiteral("responseBody"), QStringLiteral("full-response-secret")},
        {QStringLiteral("httpResponseBody"), QStringLiteral("full-http-response-secret")},
        {QStringLiteral("mqttRawPayload"), QStringLiteral("full-mqtt-secret")},
        {QStringLiteral("imageContent"), QStringLiteral("full-image-secret")},
        {QStringLiteral("safe"), QStringLiteral("kept")},
    };
    QVERIFY(logger.write(entry));

    const QString filePath = QDir(temporaryDirectory.path())
                                 .filePath(QStringLiteral("plate-client-2026-08-12.log"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();

    const QList<QByteArray> forbidden = {
        QByteArrayLiteral("header-secret"),
        QByteArrayLiteral("password-secret"),
        QByteArrayLiteral("access-secret"),
        QByteArrayLiteral("mqtt-secret"),
        QByteArrayLiteral("text-secret"),
        QByteArrayLiteral("form-secret"),
        QByteArrayLiteral("query-secret"),
        QByteArrayLiteral("full-response-secret"),
        QByteArrayLiteral("full-http-response-secret"),
        QByteArrayLiteral("full-mqtt-secret"),
        QByteArrayLiteral("full-image-secret"),
    };
    for (const QByteArray &secret : forbidden) {
        QVERIFY2(!bytes.contains(secret), secret.constData());
    }
    QVERIFY(bytes.contains(QByteArrayLiteral("[REDACTED]")));
    QVERIFY(bytes.contains(QByteArrayLiteral("[OMITTED]")));
    QVERIFY(bytes.contains(QByteArrayLiteral("kept")));
}

void TechnicalLoggerTest::toleratesUnwritableLogDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString fileInsteadOfDirectory =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("not-a-directory"));
    writeFile(fileInsteadOfDirectory);

    TechnicalLogger logger(fileInsteadOfDirectory, [] { return fixedNow(); });
    TechnicalLogEntry entry;
    entry.module = QStringLiteral("startup");
    entry.errorCode = QStringLiteral("WRITE_FAILED");
    entry.requestId = QStringLiteral("request-003");

    QVERIFY(!logger.write(entry));
    QVERIFY(QFileInfo(fileInsteadOfDirectory).isFile());
}

QTEST_MAIN(TechnicalLoggerTest)

#include "tst_technical_logger.moc"
