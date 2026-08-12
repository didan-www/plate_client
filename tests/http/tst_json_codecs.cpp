#include "infrastructure/http/JsonCodecs.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class JsonCodecsTest final : public QObject
{
    Q_OBJECT

private slots:
    void encodesLoginAndAccessListRequests();
    void decodesValidLoginEnvelope();
    void rejectsInvalidLoginContracts_data();
    void rejectsInvalidLoginContracts();
    void decodesRecognitionStates_data();
    void decodesRecognitionStates();
    void decodesRecognitionEnvelopeAndPage();
    void rejectsRecognitionNullabilityAndLimits();
    void rejectsMissingNullWrongTypeAndUnknownFields();
    void decodesAccessListAndPage();
    void rejectsAccessListAndPaginationLimits();
    void handlesSuccessFailureAndEmptyEnvelopes();
    void reportsMalformedJsonAndNestedItemPath();

private:
    static QByteArray compact(const QJsonObject &object);
    static QJsonObject envelope(const QJsonValue &data, bool success = true);
    static QJsonObject loginData();
    static QJsonObject recognitionData(const QString &status);
    static QJsonObject accessListData();
};

QByteArray JsonCodecsTest::compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject JsonCodecsTest::envelope(const QJsonValue &data, bool success)
{
    return {{QStringLiteral("success"), success},
            {QStringLiteral("code"), success ? QStringLiteral("OK") : QStringLiteral("ERROR")},
            {QStringLiteral("message"), success ? QString() : QStringLiteral("request failed")},
            {QStringLiteral("requestId"), QStringLiteral("request-1")},
            {QStringLiteral("data"), data}};
}

QJsonObject JsonCodecsTest::loginData()
{
    return {{QStringLiteral("displayName"), QStringLiteral("操作员")},
            {QStringLiteral("accessToken"), QStringLiteral("access-token")},
            {QStringLiteral("expiresAt"), QStringLiteral("2026-08-12T18:00:00+08:00")},
            {QStringLiteral("mqtt"), QJsonObject {
                 {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                 {QStringLiteral("port"), 8883},
                 {QStringLiteral("tls"), true},
                 {QStringLiteral("username"), QStringLiteral("mqtt-user")},
                 {QStringLiteral("password"), QStringLiteral("mqtt-password")},
                 {QStringLiteral("expiresAt"), QStringLiteral("2026-08-12T18:00:00+08:00")},
                 {QStringLiteral("eventTopic"), QStringLiteral("plate/events")}}}};
}

QJsonObject JsonCodecsTest::recognitionData(const QString &status)
{
    QJsonObject object {{QStringLiteral("schemaVersion"), 1},
                        {QStringLiteral("recognitionId"), QStringLiteral("rec-1")},
                        {QStringLiteral("revision"), 3},
                        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
                        {QStringLiteral("status"), status},
                        {QStringLiteral("plateNumber"), QJsonValue::Null},
                        {QStringLiteral("errorCode"), QJsonValue::Null},
                        {QStringLiteral("errorMessage"), QJsonValue::Null},
                        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-12T10:00:00+08:00")},
                        {QStringLiteral("startedAt"), QStringLiteral("2026-08-12T10:00:01+08:00")},
                        {QStringLiteral("completedAt"), QJsonValue::Null},
                        {QStringLiteral("durationMs"), QJsonValue::Null}};
    if (status == QStringLiteral("SUCCEEDED")) {
        object.insert(QStringLiteral("plateNumber"), QStringLiteral("京A12345"));
        object.insert(QStringLiteral("completedAt"), QStringLiteral("2026-08-12T10:00:02+08:00"));
        object.insert(QStringLiteral("durationMs"), 1000);
    } else if (status == QStringLiteral("FAILED")) {
        object.insert(QStringLiteral("errorCode"), QStringLiteral("MODEL_ERROR"));
        object.insert(QStringLiteral("errorMessage"), QStringLiteral("识别失败"));
        object.insert(QStringLiteral("completedAt"), QStringLiteral("2026-08-12T10:00:02+08:00"));
        object.insert(QStringLiteral("durationMs"), 1000);
    }
    return object;
}

QJsonObject JsonCodecsTest::accessListData()
{
    return {{QStringLiteral("id"), 5},
            {QStringLiteral("listType"), QStringLiteral("WHITE")},
            {QStringLiteral("plateNumber"), QStringLiteral("京A12345")},
            {QStringLiteral("remark"), QString()},
            {QStringLiteral("createdBy"), QStringLiteral("operator")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-08-12T11:00:00+08:00")}};
}

void JsonCodecsTest::encodesLoginAndAccessListRequests()
{
    const auto login = JsonCodecs::encodeLoginRequest(
        QStringLiteral("user"), QStringLiteral("password"),
        QUuid(QStringLiteral("12345678-1234-4abc-8def-1234567890ab")));
    QVERIFY(login.isSuccess());
    const QJsonObject loginObject = QJsonDocument::fromJson(*login.value()).object();
    QCOMPARE(loginObject.keys().size(), 3);
    QCOMPARE(loginObject.value(QStringLiteral("clientId")).toString(),
             QStringLiteral("12345678-1234-4abc-8def-1234567890ab"));
    QVERIFY(!JsonCodecs::encodeLoginRequest(QString(), QStringLiteral("password"), QUuid::createUuid()).isSuccess());

    const auto access = JsonCodecs::encodeAccessListCreateRequest(
        AccessListType::Black, QStringLiteral("京A12345"), QString());
    QVERIFY(access.isSuccess());
    const QJsonObject accessObject = QJsonDocument::fromJson(*access.value()).object();
    QCOMPARE(accessObject.value(QStringLiteral("listType")).toString(), QStringLiteral("BLACK"));
    QVERIFY(accessObject.contains(QStringLiteral("remark")));
    QVERIFY(!JsonCodecs::encodeAccessListCreateRequest(
        AccessListType::White, QStringLiteral(" ab123 "), QString()).isSuccess());
}

void JsonCodecsTest::decodesValidLoginEnvelope()
{
    const auto result = JsonCodecs::decodeLoginEnvelope(compact(envelope(loginData())));
    QVERIFY2(result.isSuccess(), qPrintable(result.error() ? result.error()->message() : QString()));
    QVERIFY(result.value()->data().has_value());
    const SessionContext &session = *result.value()->data();
    QCOMPARE(session.displayName(), QStringLiteral("操作员"));
    QCOMPARE(session.mqttPort(), quint16(8883));
    QCOMPARE(session.expiresAt().offsetFromUtc(), 8 * 60 * 60);
    QCOMPARE(session.generation(), quint64(0));
}

void JsonCodecsTest::rejectsInvalidLoginContracts_data()
{
    QTest::addColumn<QString>("kind");
    QTest::newRow("unknown login field") << QStringLiteral("unknown-login");
    QTest::newRow("unknown mqtt field") << QStringLiteral("unknown-mqtt");
    QTest::newRow("different expiry") << QStringLiteral("expiry");
    QTest::newRow("utc timezone") << QStringLiteral("timezone");
    QTest::newRow("port zero") << QStringLiteral("port");
    QTest::newRow("empty credential") << QStringLiteral("credential");
}

void JsonCodecsTest::rejectsInvalidLoginContracts()
{
    QFETCH(QString, kind);
    QJsonObject data = loginData();
    QJsonObject mqtt = data.value(QStringLiteral("mqtt")).toObject();
    if (kind == QStringLiteral("unknown-login")) data.insert(QStringLiteral("extra"), 1);
    if (kind == QStringLiteral("unknown-mqtt")) mqtt.insert(QStringLiteral("extra"), 1);
    if (kind == QStringLiteral("expiry")) mqtt.insert(QStringLiteral("expiresAt"), QStringLiteral("2026-08-12T18:00:01+08:00"));
    if (kind == QStringLiteral("timezone")) data.insert(QStringLiteral("expiresAt"), QStringLiteral("2026-08-12T10:00:00Z"));
    if (kind == QStringLiteral("port")) mqtt.insert(QStringLiteral("port"), 0);
    if (kind == QStringLiteral("credential")) mqtt.insert(QStringLiteral("password"), QString());
    data.insert(QStringLiteral("mqtt"), mqtt);
    const auto result = JsonCodecs::decodeLoginEnvelope(compact(envelope(data)));
    QVERIFY(!result.isSuccess());
    QVERIFY(!result.value().has_value());
    QVERIFY(result.error().has_value());
}

void JsonCodecsTest::decodesRecognitionStates_data()
{
    QTest::addColumn<QString>("status");
    QTest::newRow("processing") << QStringLiteral("PROCESSING");
    QTest::newRow("succeeded") << QStringLiteral("SUCCEEDED");
    QTest::newRow("failed") << QStringLiteral("FAILED");
}

void JsonCodecsTest::decodesRecognitionStates()
{
    QFETCH(QString, status);
    const auto result = JsonCodecs::decodeRecognitionSnapshot(compact(recognitionData(status)));
    QVERIFY2(result.isSuccess(), qPrintable(result.error() ? result.error()->message() : QString()));
    QCOMPARE(result.value()->recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(result.value()->revision(), qint64(3));
}

void JsonCodecsTest::decodesRecognitionEnvelopeAndPage()
{
    const auto detail = JsonCodecs::decodeRecognitionEnvelope(
        compact(envelope(recognitionData(QStringLiteral("SUCCEEDED")))));
    QVERIFY(detail.isSuccess());
    QCOMPARE(detail.value()->data()->status(), RecognitionStatus::Succeeded);

    const QJsonObject page {{QStringLiteral("items"),
                             QJsonArray {recognitionData(QStringLiteral("PROCESSING")),
                                         recognitionData(QStringLiteral("FAILED"))}},
                            {QStringLiteral("page"), 2},
                            {QStringLiteral("pageSize"), 100},
                            {QStringLiteral("total"), 102}};
    const auto result = JsonCodecs::decodeRecognitionPageEnvelope(compact(envelope(page)));
    QVERIFY2(result.isSuccess(), qPrintable(result.error() ? result.error()->message() : QString()));
    QCOMPARE(result.value()->data()->items().size(), 2);
    QCOMPARE(result.value()->data()->page(), 2);
    QCOMPARE(result.value()->data()->total(), qint64(102));
}

void JsonCodecsTest::rejectsRecognitionNullabilityAndLimits()
{
    QJsonObject processing = recognitionData(QStringLiteral("PROCESSING"));
    processing.insert(QStringLiteral("durationMs"), 0);
    auto result = JsonCodecs::decodeRecognitionSnapshot(compact(processing));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error()->code(), ProtocolErrorCode::InvalidCombination);

    QJsonObject succeeded = recognitionData(QStringLiteral("SUCCEEDED"));
    succeeded.insert(QStringLiteral("plateNumber"), QStringLiteral("ab123"));
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(succeeded)).isSuccess());

    QJsonObject failed = recognitionData(QStringLiteral("FAILED"));
    failed.insert(QStringLiteral("errorMessage"), QString(513, QLatin1Char('x')));
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(failed)).isSuccess());
    failed = recognitionData(QStringLiteral("FAILED"));
    failed.insert(QStringLiteral("revision"), 9007199254740992.0);
    result = JsonCodecs::decodeRecognitionSnapshot(compact(failed));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error()->code(), ProtocolErrorCode::OutOfRange);

    QJsonObject invalid = recognitionData(QStringLiteral("UNKNOWN"));
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(invalid)).isSuccess());
    invalid = recognitionData(QStringLiteral("PROCESSING"));
    invalid.insert(QStringLiteral("schemaVersion"), 2);
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(invalid)).isSuccess());
    invalid = recognitionData(QStringLiteral("PROCESSING"));
    invalid.insert(QStringLiteral("capturedAt"), QStringLiteral("2026-08-12T02:00:00Z"));
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(invalid)).isSuccess());
    invalid = recognitionData(QStringLiteral("FAILED"));
    invalid.insert(QStringLiteral("errorCode"), QString());
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(invalid)).isSuccess());
}

void JsonCodecsTest::rejectsMissingNullWrongTypeAndUnknownFields()
{
    QJsonObject object = recognitionData(QStringLiteral("PROCESSING"));
    object.remove(QStringLiteral("plateNumber"));
    auto result = JsonCodecs::decodeRecognitionSnapshot(compact(object));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error()->code(), ProtocolErrorCode::MissingField);
    QCOMPARE(result.error()->fieldPath(), QStringLiteral("$.plateNumber"));

    object = recognitionData(QStringLiteral("PROCESSING"));
    object.insert(QStringLiteral("recognitionId"), QJsonValue::Null);
    result = JsonCodecs::decodeRecognitionSnapshot(compact(object));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error()->code(), ProtocolErrorCode::TypeMismatch);

    object = recognitionData(QStringLiteral("PROCESSING"));
    object.insert(QStringLiteral("revision"), 1.5);
    QVERIFY(!JsonCodecs::decodeRecognitionSnapshot(compact(object)).isSuccess());
    object = recognitionData(QStringLiteral("PROCESSING"));
    object.insert(QStringLiteral("extra"), QStringLiteral("secret-value"));
    result = JsonCodecs::decodeRecognitionSnapshot(compact(object));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error()->code(), ProtocolErrorCode::UnknownField);
    QVERIFY(!result.error()->message().contains(QStringLiteral("secret-value")));
}

void JsonCodecsTest::decodesAccessListAndPage()
{
    const auto record = JsonCodecs::decodeAccessListEnvelope(compact(envelope(accessListData())));
    QVERIFY(record.isSuccess());
    QCOMPARE(record.value()->data()->id(), qint64(5));
    QCOMPARE(record.value()->data()->listType(), AccessListType::White);

    const QJsonObject page {{QStringLiteral("items"), QJsonArray {accessListData()}},
                            {QStringLiteral("page"), 1},
                            {QStringLiteral("pageSize"), 100},
                            {QStringLiteral("total"), 1}};
    const auto result = JsonCodecs::decodeAccessListPageEnvelope(compact(envelope(page)));
    QVERIFY2(result.isSuccess(), qPrintable(result.error() ? result.error()->message() : QString()));
    QCOMPARE(result.value()->data()->items().size(), 1);
    QCOMPARE(result.value()->data()->pageSize(), 100);
}

void JsonCodecsTest::rejectsAccessListAndPaginationLimits()
{
    QJsonObject record = accessListData();
    record.insert(QStringLiteral("remark"), QString(201, QLatin1Char('x')));
    QVERIFY(!JsonCodecs::decodeAccessListEnvelope(compact(envelope(record))).isSuccess());
    record = accessListData();
    record.insert(QStringLiteral("createdBy"), QString());
    QVERIFY(!JsonCodecs::decodeAccessListEnvelope(compact(envelope(record))).isSuccess());
    record = accessListData();
    record.insert(QStringLiteral("plateNumber"), QStringLiteral("京 A12345"));
    QVERIFY(!JsonCodecs::decodeAccessListEnvelope(compact(envelope(record))).isSuccess());
    record = accessListData();
    record.insert(QStringLiteral("remark"), QJsonValue::Null);
    QVERIFY(!JsonCodecs::decodeAccessListEnvelope(compact(envelope(record))).isSuccess());
    record = accessListData();
    record.insert(QStringLiteral("listType"), QStringLiteral("GRAY"));
    QVERIFY(!JsonCodecs::decodeAccessListEnvelope(compact(envelope(record))).isSuccess());
    record = accessListData();
    record.insert(QStringLiteral("id"), 0);
    QVERIFY(!JsonCodecs::decodeAccessListEnvelope(compact(envelope(record))).isSuccess());
    record = accessListData();
    record.remove(QStringLiteral("plateNumber"));
    const auto missingPlate = JsonCodecs::decodeAccessListEnvelope(compact(envelope(record)));
    QVERIFY(!missingPlate.isSuccess());
    QCOMPARE(missingPlate.error()->code(), ProtocolErrorCode::MissingField);
    QCOMPARE(missingPlate.error()->fieldPath(), QStringLiteral("$.data.plateNumber"));

    QJsonObject page {{QStringLiteral("items"), QJsonArray {}},
                      {QStringLiteral("page"), 0},
                      {QStringLiteral("pageSize"), 99},
                      {QStringLiteral("total"), -1}};
    QVERIFY(!JsonCodecs::decodeAccessListPageEnvelope(compact(envelope(page))).isSuccess());
    page.insert(QStringLiteral("page"), 1);
    QVERIFY(!JsonCodecs::decodeAccessListPageEnvelope(compact(envelope(page))).isSuccess());
    page.insert(QStringLiteral("pageSize"), 100);
    QVERIFY(!JsonCodecs::decodeAccessListPageEnvelope(compact(envelope(page))).isSuccess());
    page.insert(QStringLiteral("total"), 9007199254740991.0);
    QVERIFY(JsonCodecs::decodeAccessListPageEnvelope(compact(envelope(page))).isSuccess());
    page.insert(QStringLiteral("total"), 1.5);
    QVERIFY(!JsonCodecs::decodeAccessListPageEnvelope(compact(envelope(page))).isSuccess());
}

void JsonCodecsTest::handlesSuccessFailureAndEmptyEnvelopes()
{
    const auto emptySuccess = JsonCodecs::decodeEmptyEnvelope(
        compact(envelope(QJsonValue::Null)));
    QVERIFY(emptySuccess.isSuccess());
    QVERIFY(emptySuccess.value()->data().has_value());

    const auto failure = JsonCodecs::decodeLoginEnvelope(
        compact(envelope(QJsonValue::Null, false)));
    QVERIFY(failure.isSuccess());
    QVERIFY(!failure.value()->success());
    QVERIFY(!failure.value()->data().has_value());

    QVERIFY(!JsonCodecs::decodeEmptyEnvelope(compact(envelope(QJsonObject()))).isSuccess());
    QVERIFY(!JsonCodecs::decodeLoginEnvelope(compact(envelope(QJsonValue::Null))).isSuccess());
    QVERIFY(!JsonCodecs::decodeLoginEnvelope(compact(envelope(loginData(), false))).isSuccess());

    QJsonObject missingData = envelope(QJsonValue::Null);
    missingData.remove(QStringLiteral("data"));
    QVERIFY(!JsonCodecs::decodeEmptyEnvelope(compact(missingData)).isSuccess());
    QJsonObject wrongSuccess = envelope(QJsonValue::Null);
    wrongSuccess.insert(QStringLiteral("success"), QStringLiteral("true"));
    QVERIFY(!JsonCodecs::decodeEmptyEnvelope(compact(wrongSuccess)).isSuccess());
    QJsonObject unknownEnvelope = envelope(QJsonValue::Null);
    unknownEnvelope.insert(QStringLiteral("extra"), 1);
    QVERIFY(!JsonCodecs::decodeEmptyEnvelope(compact(unknownEnvelope)).isSuccess());
}

void JsonCodecsTest::reportsMalformedJsonAndNestedItemPath()
{
    auto malformed = JsonCodecs::decodeRecognitionEnvelope(QByteArrayLiteral("{bad-json"));
    QVERIFY(!malformed.isSuccess());
    QCOMPARE(malformed.error()->code(), ProtocolErrorCode::MalformedJson);
    QVERIFY(!JsonCodecs::decodeRecognitionEnvelope(QByteArrayLiteral("[]")).isSuccess());

    QJsonObject invalidItem = recognitionData(QStringLiteral("PROCESSING"));
    invalidItem.remove(QStringLiteral("deviceId"));
    const QJsonObject page {{QStringLiteral("items"), QJsonArray {invalidItem}},
                            {QStringLiteral("page"), 1},
                            {QStringLiteral("pageSize"), 100},
                            {QStringLiteral("total"), 1}};
    const auto result = JsonCodecs::decodeRecognitionPageEnvelope(compact(envelope(page)));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error()->fieldPath(), QStringLiteral("$.data.items[0].deviceId"));
}

QTEST_MAIN(JsonCodecsTest)

#include "tst_json_codecs.moc"
