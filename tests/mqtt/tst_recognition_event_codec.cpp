#include "infrastructure/logging/TechnicalLogger.h"
#include "infrastructure/mqtt/RecognitionEventCodec.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

Q_DECLARE_METATYPE(RecognitionStatus)

namespace {
QJsonObject snapshot(const QString &status)
{
    QJsonObject value {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("recognitionId"), QStringLiteral("rec-1")},
        {QStringLiteral("revision"), 1},
        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
        {QStringLiteral("status"), status},
        {QStringLiteral("plateNumber"), QJsonValue::Null},
        {QStringLiteral("errorCode"), QJsonValue::Null},
        {QStringLiteral("errorMessage"), QJsonValue::Null},
        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-12T10:00:00+08:00")},
        {QStringLiteral("startedAt"), QStringLiteral("2026-08-12T10:00:01+08:00")},
        {QStringLiteral("completedAt"), QJsonValue::Null},
        {QStringLiteral("durationMs"), QJsonValue::Null},
    };
    if (status == QStringLiteral("SUCCEEDED")) {
        value.insert(QStringLiteral("plateNumber"), QStringLiteral("京A12345"));
        value.insert(QStringLiteral("completedAt"),
                     QStringLiteral("2026-08-12T10:00:02+08:00"));
        value.insert(QStringLiteral("durationMs"), 1000);
    } else if (status == QStringLiteral("FAILED")) {
        value.insert(QStringLiteral("errorCode"), QStringLiteral("MODEL_ERROR"));
        value.insert(QStringLiteral("errorMessage"), QStringLiteral("识别失败"));
        value.insert(QStringLiteral("completedAt"),
                     QStringLiteral("2026-08-12T10:00:02+08:00"));
        value.insert(QStringLiteral("durationMs"), 1000);
    }
    return value;
}

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray logContents(const QString &directory)
{
    const QStringList files = QDir(directory).entryList(
        {QStringLiteral("plate-client-*.log")}, QDir::Files);
    if (files.size() != 1) {
        return {};
    }
    QFile file(QDir(directory).filePath(files.constFirst()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
} // namespace

class RecognitionEventCodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void decodesCompleteSnapshots_data();
    void decodesCompleteSnapshots();
    void rejectsEveryProtocolErrorWithoutLoggingPayload_data();
    void rejectsEveryProtocolErrorWithoutLoggingPayload();
};

void RecognitionEventCodecTest::decodesCompleteSnapshots_data()
{
    QTest::addColumn<QString>("status");
    QTest::addColumn<RecognitionStatus>("expectedStatus");
    QTest::newRow("processing nulls")
        << QStringLiteral("PROCESSING") << RecognitionStatus::Processing;
    QTest::newRow("succeeded final")
        << QStringLiteral("SUCCEEDED") << RecognitionStatus::Succeeded;
    QTest::newRow("failed final")
        << QStringLiteral("FAILED") << RecognitionStatus::Failed;
}

void RecognitionEventCodecTest::decodesCompleteSnapshots()
{
    QFETCH(QString, status);
    QFETCH(RecognitionStatus, expectedStatus);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TechnicalLogger logger(directory.path());
    RecognitionEventCodec codec(&logger);

    const auto decoded = codec.decode(compact(snapshot(status)));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->status(), expectedStatus);
    QCOMPARE(decoded->recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(decoded->revision(), qint64(1));
    if (expectedStatus == RecognitionStatus::Processing) {
        QVERIFY(!decoded->plateNumber().has_value());
        QVERIFY(!decoded->errorCode().has_value());
        QVERIFY(!decoded->errorMessage().has_value());
        QVERIFY(!decoded->completedAt().has_value());
        QVERIFY(!decoded->durationMs().has_value());
    } else {
        QVERIFY(decoded->completedAt().has_value());
        QCOMPARE(*decoded->durationMs(), qint64(1000));
    }
    QVERIFY(logContents(directory.path()).isEmpty());
}

void RecognitionEventCodecTest::rejectsEveryProtocolErrorWithoutLoggingPayload_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QString>("errorCode");
    QTest::addColumn<QString>("fieldPath");

    QTest::newRow("malformed")
        << QByteArrayLiteral("{secret-payload")
        << QStringLiteral("MQTT_EVENT_MALFORMED_JSON") << QStringLiteral("$");
    QTest::newRow("root type")
        << QByteArrayLiteral("[\"secret-payload\"]")
        << QStringLiteral("MQTT_EVENT_ROOT_TYPE_MISMATCH") << QStringLiteral("$");

    QJsonObject value = snapshot(QStringLiteral("PROCESSING"));
    value.remove(QStringLiteral("plateNumber"));
    QTest::newRow("missing field") << compact(value)
        << QStringLiteral("MQTT_EVENT_MISSING_FIELD") << QStringLiteral("$.plateNumber");
    value = snapshot(QStringLiteral("PROCESSING"));
    value.insert(QStringLiteral("unexpected"), QStringLiteral("secret-payload"));
    QTest::newRow("unknown field") << compact(value)
        << QStringLiteral("MQTT_EVENT_UNKNOWN_FIELD") << QStringLiteral("$");
    value = snapshot(QStringLiteral("PROCESSING"));
    value.insert(QStringLiteral("revision"), QStringLiteral("secret-payload"));
    QTest::newRow("wrong type") << compact(value)
        << QStringLiteral("MQTT_EVENT_TYPE_MISMATCH") << QStringLiteral("$.revision");
    value = snapshot(QStringLiteral("PROCESSING"));
    value.insert(QStringLiteral("schemaVersion"), 2);
    QTest::newRow("unsupported schema") << compact(value)
        << QStringLiteral("MQTT_EVENT_OUT_OF_RANGE") << QStringLiteral("$.schemaVersion");
    value = snapshot(QStringLiteral("UNKNOWN"));
    QTest::newRow("invalid enum") << compact(value)
        << QStringLiteral("MQTT_EVENT_INVALID_VALUE") << QStringLiteral("$.status");
    value = snapshot(QStringLiteral("PROCESSING"));
    value.insert(QStringLiteral("durationMs"), 0);
    QTest::newRow("invalid state combination") << compact(value)
        << QStringLiteral("MQTT_EVENT_INVALID_COMBINATION") << QStringLiteral("$");
}

void RecognitionEventCodecTest::rejectsEveryProtocolErrorWithoutLoggingPayload()
{
    QFETCH(QByteArray, payload);
    QFETCH(QString, errorCode);
    QFETCH(QString, fieldPath);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TechnicalLogger logger(directory.path());
    RecognitionEventCodec codec(&logger);

    QVERIFY(!codec.decode(payload).has_value());
    const QByteArray log = logContents(directory.path());
    QVERIFY(!log.isEmpty());
    QVERIFY(!log.contains(payload));
    QVERIFY(!log.contains("secret-payload"));
    QVERIFY(!log.contains("\"status\":\"PROCESSING\""));

    const QJsonObject record = QJsonDocument::fromJson(log.trimmed()).object();
    QCOMPARE(record.value(QStringLiteral("module")).toString(),
             QStringLiteral("mqtt.event_codec"));
    QCOMPARE(record.value(QStringLiteral("errorCode")).toString(), errorCode);
    QVERIFY(!record.contains(QStringLiteral("recognitionId")));
    const QJsonObject context = record.value(QStringLiteral("context")).toObject();
    QCOMPARE(context.keys(),
             QStringList({QStringLiteral("fieldPath"), QStringLiteral("payloadBytes")}));
    QCOMPARE(context.value(QStringLiteral("fieldPath")).toString(), fieldPath);
    QCOMPARE(context.value(QStringLiteral("payloadBytes")).toInteger(),
             static_cast<qint64>(payload.size()));
}

QTEST_MAIN(RecognitionEventCodecTest)

#include "tst_recognition_event_codec.moc"
