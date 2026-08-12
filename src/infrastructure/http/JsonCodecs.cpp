#include "infrastructure/http/JsonCodecs.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace {
constexpr qint64 maximumSafeJsonInteger = 9007199254740991LL;

ProtocolError error(ProtocolErrorCode code, const QString &path, const QString &message)
{
    return ProtocolError(code, path, message);
}

ProtocolError missingField(const QString &path)
{
    return error(ProtocolErrorCode::MissingField, path, QStringLiteral("协议缺少必填字段。"));
}

ProtocolError typeMismatch(const QString &path)
{
    return error(ProtocolErrorCode::TypeMismatch, path, QStringLiteral("协议字段类型不正确。"));
}

std::optional<ProtocolError> requireExactFields(const QJsonObject &object,
                                                const QSet<QString> &expected,
                                                const QString &path)
{
    for (const QString &key : expected) {
        if (!object.contains(key)) {
            return missingField(path + QLatin1Char('.') + key);
        }
    }
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!expected.contains(iterator.key())) {
            return error(ProtocolErrorCode::UnknownField,
                         path,
                         QStringLiteral("协议包含不支持的字段。"));
        }
    }
    return std::nullopt;
}

ProtocolResult<QJsonObject> decodeRootObject(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return ProtocolResult<QJsonObject>::failure(
            error(ProtocolErrorCode::MalformedJson, QStringLiteral("$"),
                  QStringLiteral("响应不是有效的 JSON。")));
    }
    if (!document.isObject()) {
        return ProtocolResult<QJsonObject>::failure(
            error(ProtocolErrorCode::RootTypeMismatch, QStringLiteral("$"),
                  QStringLiteral("JSON 根节点必须是对象。")));
    }
    return ProtocolResult<QJsonObject>::success(document.object());
}

ProtocolResult<QString> requiredString(const QJsonObject &object,
                                       const QString &key,
                                       const QString &path,
                                       bool allowEmpty)
{
    const QString fieldPath = path + QLatin1Char('.') + key;
    if (!object.contains(key)) {
        return ProtocolResult<QString>::failure(missingField(fieldPath));
    }
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return ProtocolResult<QString>::failure(typeMismatch(fieldPath));
    }
    const QString text = value.toString();
    if (!allowEmpty && text.isEmpty()) {
        return ProtocolResult<QString>::failure(
            error(ProtocolErrorCode::InvalidValue, fieldPath,
                  QStringLiteral("协议字符串字段不得为空。")));
    }
    return ProtocolResult<QString>::success(text);
}

ProtocolResult<bool> requiredBool(const QJsonObject &object,
                                  const QString &key,
                                  const QString &path)
{
    const QString fieldPath = path + QLatin1Char('.') + key;
    if (!object.contains(key)) {
        return ProtocolResult<bool>::failure(missingField(fieldPath));
    }
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        return ProtocolResult<bool>::failure(typeMismatch(fieldPath));
    }
    return ProtocolResult<bool>::success(value.toBool());
}

ProtocolResult<qint64> integer(const QJsonValue &value,
                               const QString &path,
                               qint64 minimum,
                               qint64 maximum)
{
    if (!value.isDouble()) {
        return ProtocolResult<qint64>::failure(typeMismatch(path));
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(minimum) || number > static_cast<double>(maximum)
        || std::abs(number) > static_cast<double>(maximumSafeJsonInteger)) {
        return ProtocolResult<qint64>::failure(
            error(ProtocolErrorCode::OutOfRange, path,
                  QStringLiteral("协议整数超出可无损表示的范围。")));
    }
    return ProtocolResult<qint64>::success(static_cast<qint64>(number));
}

ProtocolResult<qint64> requiredInteger(const QJsonObject &object,
                                       const QString &key,
                                       const QString &path,
                                       qint64 minimum,
                                       qint64 maximum)
{
    const QString fieldPath = path + QLatin1Char('.') + key;
    if (!object.contains(key)) {
        return ProtocolResult<qint64>::failure(missingField(fieldPath));
    }
    return integer(object.value(key), fieldPath, minimum, maximum);
}

ProtocolResult<QDateTime> beijingDateTime(const QString &text, const QString &path)
{
    static const QRegularExpression format(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?\+08:00$)"));
    if (!format.match(text).hasMatch()) {
        return ProtocolResult<QDateTime>::failure(
            error(ProtocolErrorCode::InvalidValue, path,
                  QStringLiteral("协议时间必须是带 +08:00 的 ISO 8601 时间。")));
    }
    const QDateTime value = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!value.isValid() || value.offsetFromUtc() != 8 * 60 * 60) {
        return ProtocolResult<QDateTime>::failure(
            error(ProtocolErrorCode::InvalidValue, path,
                  QStringLiteral("协议时间值无效。")));
    }
    return ProtocolResult<QDateTime>::success(value);
}

ProtocolResult<QDateTime> requiredDateTime(const QJsonObject &object,
                                           const QString &key,
                                           const QString &path)
{
    const ProtocolResult<QString> text = requiredString(object, key, path, false);
    if (!text.isSuccess()) {
        return ProtocolResult<QDateTime>::failure(*text.error());
    }
    return beijingDateTime(*text.value(), path + QLatin1Char('.') + key);
}

template<typename T, typename Parser>
ProtocolResult<std::optional<T>> nullable(const QJsonObject &object,
                                          const QString &key,
                                          const QString &path,
                                          Parser parser)
{
    const QString fieldPath = path + QLatin1Char('.') + key;
    if (!object.contains(key)) {
        return ProtocolResult<std::optional<T>>::failure(missingField(fieldPath));
    }
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        return ProtocolResult<std::optional<T>>::success(std::nullopt);
    }
    ProtocolResult<T> parsed = parser(value, fieldPath);
    if (!parsed.isSuccess()) {
        return ProtocolResult<std::optional<T>>::failure(*parsed.error());
    }
    return ProtocolResult<std::optional<T>>::success(std::move(*parsed.value()));
}

ProtocolResult<QString> stringValue(const QJsonValue &value,
                                    const QString &path,
                                    bool allowEmpty,
                                    int maximumLength = -1)
{
    if (!value.isString()) {
        return ProtocolResult<QString>::failure(typeMismatch(path));
    }
    const QString text = value.toString();
    if ((!allowEmpty && text.isEmpty()) || (maximumLength >= 0 && text.size() > maximumLength)) {
        return ProtocolResult<QString>::failure(
            error(ProtocolErrorCode::OutOfRange, path,
                  QStringLiteral("协议字符串长度超出允许范围。")));
    }
    return ProtocolResult<QString>::success(text);
}

bool isNormalizedPlate(const QString &plate)
{
    if (plate.size() < 1 || plate.size() > 16 || plate.trimmed() != plate) {
        return false;
    }
    for (const QChar character : plate) {
        if (character.isSpace()
            || (character >= QLatin1Char('a') && character <= QLatin1Char('z'))) {
            return false;
        }
    }
    return true;
}

ProtocolResult<QString> plateValue(const QJsonValue &value, const QString &path)
{
    ProtocolResult<QString> result = stringValue(value, path, false, 16);
    if (!result.isSuccess()) {
        return result;
    }
    if (!isNormalizedPlate(*result.value())) {
        return ProtocolResult<QString>::failure(
            error(ProtocolErrorCode::InvalidValue, path,
                  QStringLiteral("协议车牌号不是规范化值。")));
    }
    return result;
}

ProtocolResult<QString> requiredPlate(const QJsonObject &object,
                                      const QString &key,
                                      const QString &path)
{
    const QString fieldPath = path + QLatin1Char('.') + key;
    if (!object.contains(key)) {
        return ProtocolResult<QString>::failure(missingField(fieldPath));
    }
    return plateValue(object.value(key), fieldPath);
}

ProtocolResult<QDateTime> dateTimeValue(const QJsonValue &value, const QString &path)
{
    if (!value.isString()) {
        return ProtocolResult<QDateTime>::failure(typeMismatch(path));
    }
    return beijingDateTime(value.toString(), path);
}

ProtocolResult<qint64> nonNegativeIntegerValue(const QJsonValue &value, const QString &path)
{
    return integer(value, path, 0, maximumSafeJsonInteger);
}

ProtocolResult<SessionContext> parseSession(const QJsonObject &object, const QString &path)
{
    const QSet<QString> fields {QStringLiteral("displayName"), QStringLiteral("accessToken"),
                                QStringLiteral("expiresAt"), QStringLiteral("mqtt")};
    if (const auto fieldError = requireExactFields(object, fields, path)) {
        return ProtocolResult<SessionContext>::failure(*fieldError);
    }
    const auto displayName = requiredString(object, QStringLiteral("displayName"), path, false);
    const auto accessToken = requiredString(object, QStringLiteral("accessToken"), path, false);
    const auto expiresAt = requiredDateTime(object, QStringLiteral("expiresAt"), path);
    const QJsonValue mqttValue = object.value(QStringLiteral("mqtt"));
    if (!displayName.isSuccess()) return ProtocolResult<SessionContext>::failure(*displayName.error());
    if (!accessToken.isSuccess()) return ProtocolResult<SessionContext>::failure(*accessToken.error());
    if (!expiresAt.isSuccess()) return ProtocolResult<SessionContext>::failure(*expiresAt.error());
    if (!mqttValue.isObject()) return ProtocolResult<SessionContext>::failure(typeMismatch(path + QStringLiteral(".mqtt")));

    const QJsonObject mqtt = mqttValue.toObject();
    const QString mqttPath = path + QStringLiteral(".mqtt");
    const QSet<QString> mqttFields {QStringLiteral("host"), QStringLiteral("port"),
                                    QStringLiteral("tls"), QStringLiteral("username"),
                                    QStringLiteral("password"), QStringLiteral("expiresAt"),
                                    QStringLiteral("eventTopic")};
    if (const auto fieldError = requireExactFields(mqtt, mqttFields, mqttPath)) {
        return ProtocolResult<SessionContext>::failure(*fieldError);
    }
    const auto host = requiredString(mqtt, QStringLiteral("host"), mqttPath, false);
    const auto port = requiredInteger(mqtt, QStringLiteral("port"), mqttPath, 1, 65535);
    const auto tls = requiredBool(mqtt, QStringLiteral("tls"), mqttPath);
    const auto username = requiredString(mqtt, QStringLiteral("username"), mqttPath, false);
    const auto password = requiredString(mqtt, QStringLiteral("password"), mqttPath, false);
    const auto mqttExpiresAt = requiredDateTime(mqtt, QStringLiteral("expiresAt"), mqttPath);
    const auto topic = requiredString(mqtt, QStringLiteral("eventTopic"), mqttPath, false);
    if (!host.isSuccess()) return ProtocolResult<SessionContext>::failure(*host.error());
    if (!port.isSuccess()) return ProtocolResult<SessionContext>::failure(*port.error());
    if (!tls.isSuccess()) return ProtocolResult<SessionContext>::failure(*tls.error());
    if (!username.isSuccess()) return ProtocolResult<SessionContext>::failure(*username.error());
    if (!password.isSuccess()) return ProtocolResult<SessionContext>::failure(*password.error());
    if (!mqttExpiresAt.isSuccess()) return ProtocolResult<SessionContext>::failure(*mqttExpiresAt.error());
    if (!topic.isSuccess()) return ProtocolResult<SessionContext>::failure(*topic.error());
    if (*expiresAt.value() != *mqttExpiresAt.value()) {
        return ProtocolResult<SessionContext>::failure(
            error(ProtocolErrorCode::InvalidCombination, mqttPath + QStringLiteral(".expiresAt"),
                  QStringLiteral("登录与 MQTT 凭据有效期必须相同。")));
    }
    return ProtocolResult<SessionContext>::success(
        SessionContext(*displayName.value(), *accessToken.value(), *expiresAt.value(), *host.value(),
                       static_cast<quint16>(*port.value()), *tls.value(), *username.value(),
                       *password.value(), *topic.value(), 0));
}

ProtocolResult<RecognitionSnapshot> parseRecognition(const QJsonObject &object, const QString &path)
{
    const QSet<QString> fields {QStringLiteral("schemaVersion"), QStringLiteral("recognitionId"),
        QStringLiteral("revision"), QStringLiteral("deviceId"), QStringLiteral("status"),
        QStringLiteral("plateNumber"), QStringLiteral("errorCode"), QStringLiteral("errorMessage"),
        QStringLiteral("capturedAt"), QStringLiteral("startedAt"), QStringLiteral("completedAt"),
        QStringLiteral("durationMs")};
    if (const auto fieldError = requireExactFields(object, fields, path)) {
        return ProtocolResult<RecognitionSnapshot>::failure(*fieldError);
    }
    const auto schemaVersion = requiredInteger(object, QStringLiteral("schemaVersion"), path, 1, 1);
    const auto recognitionId = requiredString(object, QStringLiteral("recognitionId"), path, false);
    const auto revision = requiredInteger(object, QStringLiteral("revision"), path, 1, maximumSafeJsonInteger);
    const auto deviceId = requiredString(object, QStringLiteral("deviceId"), path, false);
    const auto statusText = requiredString(object, QStringLiteral("status"), path, false);
    const auto capturedAt = requiredDateTime(object, QStringLiteral("capturedAt"), path);
    const auto startedAt = requiredDateTime(object, QStringLiteral("startedAt"), path);
    if (!schemaVersion.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*schemaVersion.error());
    if (!recognitionId.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*recognitionId.error());
    if (!revision.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*revision.error());
    if (!deviceId.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*deviceId.error());
    if (!statusText.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*statusText.error());
    if (!capturedAt.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*capturedAt.error());
    if (!startedAt.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*startedAt.error());

    RecognitionStatus status;
    if (*statusText.value() == QStringLiteral("PROCESSING")) status = RecognitionStatus::Processing;
    else if (*statusText.value() == QStringLiteral("SUCCEEDED")) status = RecognitionStatus::Succeeded;
    else if (*statusText.value() == QStringLiteral("FAILED")) status = RecognitionStatus::Failed;
    else return ProtocolResult<RecognitionSnapshot>::failure(
        error(ProtocolErrorCode::InvalidValue, path + QStringLiteral(".status"),
              QStringLiteral("协议枚举值无效。")));

    auto plate = nullable<QString>(object, QStringLiteral("plateNumber"), path, plateValue);
    auto errorCode = nullable<QString>(object, QStringLiteral("errorCode"), path,
        [](const QJsonValue &value, const QString &fieldPath) { return stringValue(value, fieldPath, false); });
    auto errorMessage = nullable<QString>(object, QStringLiteral("errorMessage"), path,
        [](const QJsonValue &value, const QString &fieldPath) { return stringValue(value, fieldPath, false, 512); });
    auto completedAt = nullable<QDateTime>(object, QStringLiteral("completedAt"), path, dateTimeValue);
    auto duration = nullable<qint64>(object, QStringLiteral("durationMs"), path, nonNegativeIntegerValue);
    if (!plate.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*plate.error());
    if (!errorCode.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*errorCode.error());
    if (!errorMessage.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*errorMessage.error());
    if (!completedAt.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*completedAt.error());
    if (!duration.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*duration.error());

    const bool validCombination =
        (status == RecognitionStatus::Processing && !plate.value()->has_value()
         && !errorCode.value()->has_value() && !errorMessage.value()->has_value()
         && !completedAt.value()->has_value() && !duration.value()->has_value())
        || (status == RecognitionStatus::Succeeded && plate.value()->has_value()
            && !errorCode.value()->has_value() && !errorMessage.value()->has_value()
            && completedAt.value()->has_value() && duration.value()->has_value())
        || (status == RecognitionStatus::Failed && !plate.value()->has_value()
            && errorCode.value()->has_value() && errorMessage.value()->has_value()
            && completedAt.value()->has_value() && duration.value()->has_value());
    if (!validCombination) {
        return ProtocolResult<RecognitionSnapshot>::failure(
            error(ProtocolErrorCode::InvalidCombination, path,
                  QStringLiteral("识别状态与可空字段组合不一致。")));
    }
    return ProtocolResult<RecognitionSnapshot>::success(RecognitionSnapshot(
        static_cast<int>(*schemaVersion.value()), *recognitionId.value(), *revision.value(),
        *deviceId.value(), status, *plate.value(), *errorCode.value(), *errorMessage.value(),
        *capturedAt.value(), *startedAt.value(), *completedAt.value(), *duration.value()));
}

ProtocolResult<AccessListRecord> parseAccessList(const QJsonObject &object, const QString &path)
{
    const QSet<QString> fields {QStringLiteral("id"), QStringLiteral("listType"),
        QStringLiteral("plateNumber"), QStringLiteral("remark"), QStringLiteral("createdBy"),
        QStringLiteral("createdAt")};
    if (const auto fieldError = requireExactFields(object, fields, path)) {
        return ProtocolResult<AccessListRecord>::failure(*fieldError);
    }
    const auto id = requiredInteger(object, QStringLiteral("id"), path, 1, maximumSafeJsonInteger);
    const auto listTypeText = requiredString(object, QStringLiteral("listType"), path, false);
    const auto plate = requiredPlate(object, QStringLiteral("plateNumber"), path);
    const auto remark = requiredString(object, QStringLiteral("remark"), path, true);
    const auto createdBy = requiredString(object, QStringLiteral("createdBy"), path, false);
    const auto createdAt = requiredDateTime(object, QStringLiteral("createdAt"), path);
    if (!id.isSuccess()) return ProtocolResult<AccessListRecord>::failure(*id.error());
    if (!listTypeText.isSuccess()) return ProtocolResult<AccessListRecord>::failure(*listTypeText.error());
    if (!plate.isSuccess()) return ProtocolResult<AccessListRecord>::failure(*plate.error());
    if (!remark.isSuccess()) return ProtocolResult<AccessListRecord>::failure(*remark.error());
    if (remark.value()->size() > 200) return ProtocolResult<AccessListRecord>::failure(
        error(ProtocolErrorCode::OutOfRange, path + QStringLiteral(".remark"),
              QStringLiteral("名单备注超过 200 个字符。")));
    if (!createdBy.isSuccess()) return ProtocolResult<AccessListRecord>::failure(*createdBy.error());
    if (!createdAt.isSuccess()) return ProtocolResult<AccessListRecord>::failure(*createdAt.error());

    AccessListType listType;
    if (*listTypeText.value() == QStringLiteral("WHITE")) listType = AccessListType::White;
    else if (*listTypeText.value() == QStringLiteral("BLACK")) listType = AccessListType::Black;
    else return ProtocolResult<AccessListRecord>::failure(
        error(ProtocolErrorCode::InvalidValue, path + QStringLiteral(".listType"),
              QStringLiteral("协议枚举值无效。")));
    return ProtocolResult<AccessListRecord>::success(
        AccessListRecord(*id.value(), listType, *plate.value(), *remark.value(),
                         *createdBy.value(), *createdAt.value()));
}

template<typename T, typename ItemParser>
ProtocolResult<PageResult<T>> parsePage(const QJsonObject &object,
                                        const QString &path,
                                        ItemParser itemParser)
{
    const QSet<QString> fields {QStringLiteral("items"), QStringLiteral("page"),
                                QStringLiteral("pageSize"), QStringLiteral("total")};
    if (const auto fieldError = requireExactFields(object, fields, path)) {
        return ProtocolResult<PageResult<T>>::failure(*fieldError);
    }
    if (!object.value(QStringLiteral("items")).isArray()) {
        return ProtocolResult<PageResult<T>>::failure(typeMismatch(path + QStringLiteral(".items")));
    }
    const auto page = requiredInteger(object, QStringLiteral("page"), path, 1,
                                      std::numeric_limits<int>::max());
    const auto pageSize = requiredInteger(object, QStringLiteral("pageSize"), path, 100, 100);
    const auto total = requiredInteger(object, QStringLiteral("total"), path, 0,
                                       maximumSafeJsonInteger);
    if (!page.isSuccess()) return ProtocolResult<PageResult<T>>::failure(*page.error());
    if (!pageSize.isSuccess()) return ProtocolResult<PageResult<T>>::failure(*pageSize.error());
    if (!total.isSuccess()) return ProtocolResult<PageResult<T>>::failure(*total.error());

    QList<T> items;
    const QJsonArray array = object.value(QStringLiteral("items")).toArray();
    items.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            return ProtocolResult<PageResult<T>>::failure(
                typeMismatch(path + QStringLiteral(".items[%1]").arg(index)));
        }
        ProtocolResult<T> item = itemParser(array.at(index).toObject(),
                                            path + QStringLiteral(".items[%1]").arg(index));
        if (!item.isSuccess()) return ProtocolResult<PageResult<T>>::failure(*item.error());
        items.append(std::move(*item.value()));
    }
    return ProtocolResult<PageResult<T>>::success(
        PageResult<T>(std::move(items), static_cast<int>(*page.value()),
                      static_cast<int>(*pageSize.value()), *total.value()));
}

template<typename T, typename DataParser>
ProtocolResult<HttpEnvelope<T>> parseEnvelope(const QByteArray &json,
                                              DataParser dataParser,
                                              bool successDataMustBeNull)
{
    ProtocolResult<QJsonObject> root = decodeRootObject(json);
    if (!root.isSuccess()) return ProtocolResult<HttpEnvelope<T>>::failure(*root.error());
    const QJsonObject object = *root.value();
    const QSet<QString> fields {QStringLiteral("success"), QStringLiteral("code"),
        QStringLiteral("message"), QStringLiteral("requestId"), QStringLiteral("data")};
    if (const auto fieldError = requireExactFields(object, fields, QStringLiteral("$"))) {
        return ProtocolResult<HttpEnvelope<T>>::failure(*fieldError);
    }
    const auto success = requiredBool(object, QStringLiteral("success"), QStringLiteral("$"));
    const auto code = requiredString(object, QStringLiteral("code"), QStringLiteral("$"), true);
    const auto message = requiredString(object, QStringLiteral("message"), QStringLiteral("$"), true);
    const auto requestId = requiredString(object, QStringLiteral("requestId"), QStringLiteral("$"), true);
    if (!success.isSuccess()) return ProtocolResult<HttpEnvelope<T>>::failure(*success.error());
    if (!code.isSuccess()) return ProtocolResult<HttpEnvelope<T>>::failure(*code.error());
    if (!message.isSuccess()) return ProtocolResult<HttpEnvelope<T>>::failure(*message.error());
    if (!requestId.isSuccess()) return ProtocolResult<HttpEnvelope<T>>::failure(*requestId.error());

    const QJsonValue data = object.value(QStringLiteral("data"));
    if (!*success.value()) {
        if (!data.isNull()) return ProtocolResult<HttpEnvelope<T>>::failure(
            error(ProtocolErrorCode::InvalidCombination, QStringLiteral("$.data"),
                  QStringLiteral("失败信封的 data 必须为 null。")));
        return ProtocolResult<HttpEnvelope<T>>::success(
            HttpEnvelope<T>(false, *code.value(), *message.value(), *requestId.value(), std::nullopt));
    }
    if (successDataMustBeNull) {
        if (!data.isNull()) return ProtocolResult<HttpEnvelope<T>>::failure(
            error(ProtocolErrorCode::InvalidCombination, QStringLiteral("$.data"),
                  QStringLiteral("该成功响应的 data 必须为 null。")));
        ProtocolResult<T> emptyData = dataParser(QJsonObject(), QStringLiteral("$.data"));
        if (!emptyData.isSuccess()) {
            return ProtocolResult<HttpEnvelope<T>>::failure(*emptyData.error());
        }
        return ProtocolResult<HttpEnvelope<T>>::success(
            HttpEnvelope<T>(true, *code.value(), *message.value(), *requestId.value(),
                            std::move(*emptyData.value())));
    }
    if (!data.isObject()) return ProtocolResult<HttpEnvelope<T>>::failure(typeMismatch(QStringLiteral("$.data")));
    ProtocolResult<T> parsedData = dataParser(data.toObject(), QStringLiteral("$.data"));
    if (!parsedData.isSuccess()) return ProtocolResult<HttpEnvelope<T>>::failure(*parsedData.error());
    return ProtocolResult<HttpEnvelope<T>>::success(HttpEnvelope<T>(
        true, *code.value(), *message.value(), *requestId.value(), std::move(*parsedData.value())));
}

ProtocolResult<QByteArray> encodeJsonObject(const QJsonObject &object)
{
    return ProtocolResult<QByteArray>::success(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
} // namespace

ProtocolResult<QByteArray> JsonCodecs::encodeLoginRequest(const QString &username,
                                                          const QString &password,
                                                          const QUuid &clientId)
{
    if (username.isEmpty()) return ProtocolResult<QByteArray>::failure(
        error(ProtocolErrorCode::InvalidValue, QStringLiteral("$.username"),
              QStringLiteral("用户名不得为空。")));
    if (password.isEmpty()) return ProtocolResult<QByteArray>::failure(
        error(ProtocolErrorCode::InvalidValue, QStringLiteral("$.password"),
              QStringLiteral("密码不得为空。")));
    if (clientId.isNull()) return ProtocolResult<QByteArray>::failure(
        error(ProtocolErrorCode::InvalidValue, QStringLiteral("$.clientId"),
              QStringLiteral("客户端 ID 必须是有效 UUID。")));
    return encodeJsonObject({{QStringLiteral("username"), username},
                             {QStringLiteral("password"), password},
                             {QStringLiteral("clientId"), clientId.toString(QUuid::WithoutBraces)}});
}

ProtocolResult<QByteArray> JsonCodecs::encodeAccessListCreateRequest(AccessListType listType,
                                                                    const QString &plateNumber,
                                                                    const QString &remark)
{
    if (!isNormalizedPlate(plateNumber)) return ProtocolResult<QByteArray>::failure(
        error(ProtocolErrorCode::InvalidValue, QStringLiteral("$.plateNumber"),
              QStringLiteral("车牌号不是规范化值。")));
    if (remark.size() > 200) return ProtocolResult<QByteArray>::failure(
        error(ProtocolErrorCode::OutOfRange, QStringLiteral("$.remark"),
              QStringLiteral("名单备注超过 200 个字符。")));
    QString type;
    switch (listType) {
    case AccessListType::White:
        type = QStringLiteral("WHITE");
        break;
    case AccessListType::Black:
        type = QStringLiteral("BLACK");
        break;
    default:
        return ProtocolResult<QByteArray>::failure(
            error(ProtocolErrorCode::InvalidValue, QStringLiteral("$.listType"),
                  QStringLiteral("名单类型无效。")));
    }
    return encodeJsonObject({{QStringLiteral("listType"), type},
                             {QStringLiteral("plateNumber"), plateNumber},
                             {QStringLiteral("remark"), remark}});
}

ProtocolResult<RecognitionSnapshot> JsonCodecs::decodeRecognitionSnapshot(const QByteArray &json)
{
    ProtocolResult<QJsonObject> root = decodeRootObject(json);
    if (!root.isSuccess()) return ProtocolResult<RecognitionSnapshot>::failure(*root.error());
    return parseRecognition(*root.value(), QStringLiteral("$"));
}

ProtocolResult<HttpEnvelope<SessionContext>> JsonCodecs::decodeLoginEnvelope(const QByteArray &json)
{
    return parseEnvelope<SessionContext>(json, parseSession, false);
}

ProtocolResult<HttpEnvelope<RecognitionSnapshot>> JsonCodecs::decodeRecognitionEnvelope(
    const QByteArray &json)
{
    return parseEnvelope<RecognitionSnapshot>(json, parseRecognition, false);
}

ProtocolResult<HttpEnvelope<PageResult<RecognitionSnapshot>>>
JsonCodecs::decodeRecognitionPageEnvelope(const QByteArray &json)
{
    return parseEnvelope<PageResult<RecognitionSnapshot>>(
        json,
        [](const QJsonObject &object, const QString &path) {
            return parsePage<RecognitionSnapshot>(object, path, parseRecognition);
        },
        false);
}

ProtocolResult<HttpEnvelope<AccessListRecord>> JsonCodecs::decodeAccessListEnvelope(
    const QByteArray &json)
{
    return parseEnvelope<AccessListRecord>(json, parseAccessList, false);
}

ProtocolResult<HttpEnvelope<PageResult<AccessListRecord>>>
JsonCodecs::decodeAccessListPageEnvelope(const QByteArray &json)
{
    return parseEnvelope<PageResult<AccessListRecord>>(
        json,
        [](const QJsonObject &object, const QString &path) {
            return parsePage<AccessListRecord>(object, path, parseAccessList);
        },
        false);
}

ProtocolResult<HttpEnvelope<EmptyResponse>> JsonCodecs::decodeEmptyEnvelope(const QByteArray &json)
{
    return parseEnvelope<EmptyResponse>(
        json,
        [](const QJsonObject &, const QString &) {
            return ProtocolResult<EmptyResponse>::success(EmptyResponse {});
        },
        true);
}
