#include "infrastructure/logging/TechnicalLogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

#include <utility>

namespace {

constexpr auto redactedValue = "[REDACTED]";
constexpr auto omittedValue = "[OMITTED]";

QString levelName(TechnicalLogLevel level)
{
    switch (level) {
    case TechnicalLogLevel::Debug:
        return QStringLiteral("DEBUG");
    case TechnicalLogLevel::Info:
        return QStringLiteral("INFO");
    case TechnicalLogLevel::Warning:
        return QStringLiteral("WARNING");
    case TechnicalLogLevel::Error:
        return QStringLiteral("ERROR");
    }

    return QStringLiteral("INFO");
}

QString normalizedKey(QString key)
{
    key.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
    return key.toLower();
}

bool isSensitiveKey(const QString &key)
{
    const QString normalized = normalizedKey(key);
    return normalized.contains(QStringLiteral("authorization"))
           || normalized.contains(QStringLiteral("password"))
           || normalized.contains(QStringLiteral("token"));
}

bool isOmittedPayloadKey(const QString &key)
{
    const QString normalized = normalizedKey(key);
    const bool completeResponse =
        normalized == QStringLiteral("response")
        || (normalized.contains(QStringLiteral("response"))
            && (normalized.contains(QStringLiteral("body"))
                || normalized.contains(QStringLiteral("data"))
                || normalized.contains(QStringLiteral("content"))
                || normalized.contains(QStringLiteral("payload"))));
    const bool mqttPayload =
        normalized.contains(QStringLiteral("mqtt"))
        && (normalized.contains(QStringLiteral("payload"))
            || normalized.contains(QStringLiteral("message")));
    const bool imageContent =
        normalized == QStringLiteral("image")
        || (normalized.contains(QStringLiteral("image"))
            && (normalized.contains(QStringLiteral("bytes"))
                || normalized.contains(QStringLiteral("data"))
                || normalized.contains(QStringLiteral("content"))));
    return completeResponse || mqttPayload || imageContent;
}

QString sanitizeText(QString text)
{
    static const QList<QRegularExpression> keyValuePatterns = {
        QRegularExpression(QStringLiteral(R"((?i)\bBearer\s+[^\s,;]+)")),
        QRegularExpression(QStringLiteral(
            R"((?i)(Authorization\s*[:=]\s*)[^\r\n,;]+)")),
        QRegularExpression(QStringLiteral(
            R"((?i)((?:password|access[_-]?token|refresh[_-]?token|mqtt[_-]?password|token)\s*[:=]\s*)[^\s,;}&]+)")),
    };
    static const QRegularExpression jsonPattern(QStringLiteral(
        R"REGEX((?i)("(?:password|accessToken|refreshToken|mqttPassword|token)"\s*:\s*")[^"]*("))REGEX"));

    text.replace(keyValuePatterns.at(0), QString::fromLatin1(redactedValue));
    for (qsizetype index = 1; index < keyValuePatterns.size(); ++index) {
        text.replace(keyValuePatterns.at(index),
                     QStringLiteral("\\1%1").arg(QString::fromLatin1(redactedValue)));
    }
    text.replace(jsonPattern,
                 QStringLiteral("\\1%1\\2").arg(QString::fromLatin1(redactedValue)));
    return text;
}

QJsonValue sanitizeValue(const QJsonValue &value);

QJsonObject sanitizeObject(const QJsonObject &object)
{
    QJsonObject sanitized;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (isSensitiveKey(iterator.key())) {
            sanitized.insert(iterator.key(), QString::fromLatin1(redactedValue));
        } else if (isOmittedPayloadKey(iterator.key())) {
            sanitized.insert(iterator.key(), QString::fromLatin1(omittedValue));
        } else {
            sanitized.insert(iterator.key(), sanitizeValue(iterator.value()));
        }
    }
    return sanitized;
}

QJsonArray sanitizeArray(const QJsonArray &array)
{
    QJsonArray sanitized;
    for (const QJsonValue &value : array) {
        sanitized.append(sanitizeValue(value));
    }
    return sanitized;
}

QJsonValue sanitizeValue(const QJsonValue &value)
{
    if (value.isObject()) {
        return sanitizeObject(value.toObject());
    }
    if (value.isArray()) {
        return sanitizeArray(value.toArray());
    }
    if (value.isString()) {
        return sanitizeText(value.toString());
    }
    return value;
}

QString logFileName(const QDate &date)
{
    return QStringLiteral("plate-client-%1.log").arg(date.toString(Qt::ISODate));
}

} // namespace

TechnicalLogger::TechnicalLogger()
    : TechnicalLogger(defaultLogDirectory())
{
}

TechnicalLogger::TechnicalLogger(QString logDirectory, Clock clock)
    : logDirectory_(std::move(logDirectory))
    , clock_(std::move(clock))
{
    if (!clock_) {
        clock_ = [] { return QDateTime::currentDateTime(); };
    }

    const QDateTime now = clock_();
    QDir().mkpath(logDirectory_);
    cleanupExpiredLogs(now.date());
}

bool TechnicalLogger::write(const TechnicalLogEntry &entry) noexcept
{
    QMutexLocker locker(&mutex_);
    const QDateTime now = clock_();

    QJsonObject record {
        {QStringLiteral("timestamp"), now.toString(Qt::ISODateWithMs)},
        {QStringLiteral("level"), levelName(entry.level)},
        {QStringLiteral("module"), sanitizeText(entry.module)},
        {QStringLiteral("errorCode"), sanitizeText(entry.errorCode)},
        {QStringLiteral("requestId"), sanitizeText(entry.requestId)},
        {QStringLiteral("context"), sanitizeObject(entry.context)},
    };
    if (!entry.recognitionId.isEmpty()) {
        record.insert(QStringLiteral("recognitionId"), sanitizeText(entry.recognitionId));
    }

    QFile file(QDir(logDirectory_).filePath(logFileName(now.date())));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return false;
    }

    QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact);
    line.append('\n');
    return file.write(line) == line.size() && file.flush();
}

QString TechnicalLogger::defaultLogDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("logs"));
}

void TechnicalLogger::cleanupExpiredLogs(const QDate &currentDate) noexcept
{
    static const QRegularExpression filePattern(
        QStringLiteral(R"(^plate-client-(\d{4}-\d{2}-\d{2})\.log$)"));
    const QDate oldestRetainedDate = currentDate.addDays(-7);
    const QDir directory(logDirectory_);

    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo &file : files) {
        const QRegularExpressionMatch match = filePattern.match(file.fileName());
        if (!match.hasMatch()) {
            continue;
        }

        const QString dateText = match.captured(1);
        const QDate fileDate = QDate::fromString(dateText, Qt::ISODate);
        if (!fileDate.isValid() || fileDate.toString(Qt::ISODate) != dateText) {
            continue;
        }

        if (fileDate < oldestRetainedDate) {
            QFile::remove(file.absoluteFilePath());
        }
    }
}
