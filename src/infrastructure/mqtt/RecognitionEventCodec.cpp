#include "infrastructure/mqtt/RecognitionEventCodec.h"

#include "domain/Errors.h"
#include "infrastructure/http/JsonCodecs.h"
#include "infrastructure/logging/TechnicalLogger.h"

#include <QJsonObject>

namespace {
QString logErrorCode(ProtocolErrorCode code)
{
    switch (code) {
    case ProtocolErrorCode::MalformedJson:
        return QStringLiteral("MQTT_EVENT_MALFORMED_JSON");
    case ProtocolErrorCode::RootTypeMismatch:
        return QStringLiteral("MQTT_EVENT_ROOT_TYPE_MISMATCH");
    case ProtocolErrorCode::MissingField:
        return QStringLiteral("MQTT_EVENT_MISSING_FIELD");
    case ProtocolErrorCode::UnknownField:
        return QStringLiteral("MQTT_EVENT_UNKNOWN_FIELD");
    case ProtocolErrorCode::TypeMismatch:
        return QStringLiteral("MQTT_EVENT_TYPE_MISMATCH");
    case ProtocolErrorCode::OutOfRange:
        return QStringLiteral("MQTT_EVENT_OUT_OF_RANGE");
    case ProtocolErrorCode::InvalidValue:
        return QStringLiteral("MQTT_EVENT_INVALID_VALUE");
    case ProtocolErrorCode::InvalidCombination:
        return QStringLiteral("MQTT_EVENT_INVALID_COMBINATION");
    }
    return QStringLiteral("MQTT_EVENT_INVALID");
}
} // namespace

RecognitionEventCodec::RecognitionEventCodec(TechnicalLogger *logger)
    : logger_(logger)
{
    Q_ASSERT(logger_);
}

std::optional<RecognitionSnapshot> RecognitionEventCodec::decode(
    const QByteArray &payload) const
{
    auto result = JsonCodecs::decodeRecognitionSnapshot(payload);
    if (result.isSuccess()) {
        return result.takeValue();
    }

    const ProtocolError &error = *result.error();
    TechnicalLogEntry entry;
    entry.level = TechnicalLogLevel::Warning;
    entry.module = QStringLiteral("mqtt.event_codec");
    entry.errorCode = logErrorCode(error.code());
    entry.context = QJsonObject {
        {QStringLiteral("fieldPath"), error.fieldPath()},
        {QStringLiteral("payloadBytes"), static_cast<qint64>(payload.size())},
    };
    (void)logger_->write(entry);
    return std::nullopt;
}
