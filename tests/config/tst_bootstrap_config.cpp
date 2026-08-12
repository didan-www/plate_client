#include "infrastructure/config/BootstrapConfigLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class BootstrapConfigTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsValidHttpsConfig();
    void loadsExplicitlyAllowedHttpConfig();
    void defaultsToDenyingInsecureTransport();
    void rejectsMissingRequiredFields_data();
    void rejectsMissingRequiredFields();
    void rejectsWrongFieldTypes_data();
    void rejectsWrongFieldTypes();
    void rejectsInvalidUuid_data();
    void rejectsInvalidUuid();
    void rejectsInvalidBaseUrl_data();
    void rejectsInvalidBaseUrl();
    void rejectsUnknownFieldsWithoutEchoingValues();
    void rejectsMissingAndMalformedFiles();

private:
    static QJsonObject validObject();
    static BootstrapConfigLoadResult loadObject(const QJsonObject &object);
    static BootstrapConfigLoadResult loadBytes(const QByteArray &bytes);
};

QJsonObject BootstrapConfigTest::validObject()
{
    return {
        {QStringLiteral("clientId"), QStringLiteral("12345678-1234-4abc-8def-1234567890ab")},
        {QStringLiteral("loginBaseUrl"), QStringLiteral("https://example.test:8443/plate-api")},
        {QStringLiteral("allowInsecureTransport"), false},
    };
}

BootstrapConfigLoadResult BootstrapConfigTest::loadObject(const QJsonObject &object)
{
    return loadBytes(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

BootstrapConfigLoadResult BootstrapConfigTest::loadBytes(const QByteArray &bytes)
{
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        qFatal("Unable to create the test temporary directory.");
    }

    QDir root(temporaryDirectory.path());
    if (!root.mkpath(QStringLiteral("config"))) {
        qFatal("Unable to create the test config directory.");
    }

    QFile file(root.filePath(QStringLiteral("config/client.json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        qFatal("Unable to write the test config file.");
    }
    file.close();

    return BootstrapConfigLoader::loadFromDirectory(temporaryDirectory.path());
}

void BootstrapConfigTest::loadsValidHttpsConfig()
{
    const BootstrapConfigLoadResult result = loadObject(validObject());

    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QVERIFY(result.config().has_value());
    QCOMPARE(result.config()->clientId().toString(QUuid::WithoutBraces),
             QStringLiteral("12345678-1234-4abc-8def-1234567890ab"));
    QCOMPARE(result.config()->loginBaseUrl(), QUrl(QStringLiteral("https://example.test:8443/plate-api")));
    QVERIFY(!result.config()->allowInsecureTransport());
    QVERIFY(result.errorMessage().isEmpty());
}

void BootstrapConfigTest::loadsExplicitlyAllowedHttpConfig()
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("loginBaseUrl"), QStringLiteral("http://127.0.0.1:8080/api"));
    object.insert(QStringLiteral("allowInsecureTransport"), true);

    const BootstrapConfigLoadResult result = loadObject(object);

    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(result.config()->loginBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:8080/api")));
    QVERIFY(result.config()->allowInsecureTransport());
}

void BootstrapConfigTest::defaultsToDenyingInsecureTransport()
{
    QJsonObject httpsObject = validObject();
    httpsObject.remove(QStringLiteral("allowInsecureTransport"));
    const BootstrapConfigLoadResult httpsResult = loadObject(httpsObject);
    QVERIFY2(httpsResult.isSuccess(), qPrintable(httpsResult.errorMessage()));
    QVERIFY(!httpsResult.config()->allowInsecureTransport());

    httpsObject.insert(QStringLiteral("loginBaseUrl"), QStringLiteral("http://example.test"));
    const BootstrapConfigLoadResult httpResult = loadObject(httpsObject);
    QVERIFY(!httpResult.isSuccess());
    QVERIFY(!httpResult.config().has_value());
    QVERIFY(!httpResult.errorMessage().isEmpty());
}

void BootstrapConfigTest::rejectsMissingRequiredFields_data()
{
    QTest::addColumn<QString>("field");
    QTest::newRow("clientId") << QStringLiteral("clientId");
    QTest::newRow("loginBaseUrl") << QStringLiteral("loginBaseUrl");
}

void BootstrapConfigTest::rejectsMissingRequiredFields()
{
    QFETCH(QString, field);
    QJsonObject object = validObject();
    object.remove(field);

    const BootstrapConfigLoadResult result = loadObject(object);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.config().has_value());
    QVERIFY(!result.errorMessage().isEmpty());
}

void BootstrapConfigTest::rejectsWrongFieldTypes_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("clientId number") << QStringLiteral("clientId") << QJsonValue(123);
    QTest::newRow("base URL boolean") << QStringLiteral("loginBaseUrl") << QJsonValue(true);
    QTest::newRow("allow string") << QStringLiteral("allowInsecureTransport")
                                   << QJsonValue(QStringLiteral("false"));
}

void BootstrapConfigTest::rejectsWrongFieldTypes()
{
    QFETCH(QString, field);
    QFETCH(QJsonValue, value);
    QJsonObject object = validObject();
    object.insert(field, value);

    const BootstrapConfigLoadResult result = loadObject(object);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.config().has_value());
}

void BootstrapConfigTest::rejectsInvalidUuid_data()
{
    QTest::addColumn<QString>("clientId");
    QTest::newRow("empty") << QString();
    QTest::newRow("invalid characters") << QStringLiteral("not-a-uuid");
    QTest::newRow("missing group") << QStringLiteral("12345678-1234-4abc-8def");
    QTest::newRow("braced noncanonical")
        << QStringLiteral("{12345678-1234-4abc-8def-1234567890ab}");
}

void BootstrapConfigTest::rejectsInvalidUuid()
{
    QFETCH(QString, clientId);
    QJsonObject object = validObject();
    object.insert(QStringLiteral("clientId"), clientId);

    const BootstrapConfigLoadResult result = loadObject(object);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.config().has_value());
}

void BootstrapConfigTest::rejectsInvalidBaseUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::newRow("empty") << QString();
    QTest::newRow("relative") << QStringLiteral("api/v1");
    QTest::newRow("unsupported scheme") << QStringLiteral("ftp://example.test/api");
    QTest::newRow("missing host") << QStringLiteral("https:///api");
    QTest::newRow("query") << QStringLiteral("https://example.test/api?tenant=1");
    QTest::newRow("fragment") << QStringLiteral("https://example.test/api#section");
    QTest::newRow("user info") << QStringLiteral("https://user:secret@example.test/api");
    QTest::newRow("invalid port") << QStringLiteral("https://example.test:99999/api");
}

void BootstrapConfigTest::rejectsInvalidBaseUrl()
{
    QFETCH(QString, url);
    QJsonObject object = validObject();
    object.insert(QStringLiteral("loginBaseUrl"), url);

    const BootstrapConfigLoadResult result = loadObject(object);
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.config().has_value());
    QVERIFY(!result.errorMessage().contains(QStringLiteral("secret")));
}

void BootstrapConfigTest::rejectsUnknownFieldsWithoutEchoingValues()
{
    QJsonObject object = validObject();
    object.insert(QStringLiteral("password"), QStringLiteral("do-not-echo-this-secret"));

    const BootstrapConfigLoadResult result = loadObject(object);

    QVERIFY(!result.isSuccess());
    QVERIFY(!result.config().has_value());
    QVERIFY(!result.errorMessage().contains(QStringLiteral("password"), Qt::CaseInsensitive));
    QVERIFY(!result.errorMessage().contains(QStringLiteral("do-not-echo-this-secret")));
    QVERIFY(!result.errorMessage().contains(QStringLiteral("example.test")));
    QVERIFY(!result.errorMessage().contains(QStringLiteral("12345678-1234-4abc-8def-1234567890ab")));
}

void BootstrapConfigTest::rejectsMissingAndMalformedFiles()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const BootstrapConfigLoadResult missingResult =
        BootstrapConfigLoader::loadFromDirectory(temporaryDirectory.path());
    QVERIFY(!missingResult.isSuccess());
    QVERIFY(!missingResult.config().has_value());
    QVERIFY(!missingResult.errorMessage().isEmpty());

    const BootstrapConfigLoadResult malformedResult = loadBytes(QByteArrayLiteral("{not-json"));
    QVERIFY(!malformedResult.isSuccess());
    QVERIFY(!malformedResult.config().has_value());

    const BootstrapConfigLoadResult arrayResult = loadBytes(QByteArrayLiteral("[]"));
    QVERIFY(!arrayResult.isSuccess());
    QVERIFY(!arrayResult.config().has_value());
}

QTEST_MAIN(BootstrapConfigTest)

#include "tst_bootstrap_config.moc"
