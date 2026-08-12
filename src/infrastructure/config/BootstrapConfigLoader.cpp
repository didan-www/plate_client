#include "infrastructure/config/BootstrapConfigLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <utility>

namespace {

constexpr auto clientIdKey = "clientId";
constexpr auto loginBaseUrlKey = "loginBaseUrl";
constexpr auto allowInsecureTransportKey = "allowInsecureTransport";

bool isCanonicalUuid(const QString &value, QUuid *uuid)
{
    const QUuid parsed = QUuid::fromString(value);
    const QString canonical = parsed.toString(QUuid::WithoutBraces);
    if (canonical.compare(value, Qt::CaseInsensitive) != 0) {
        return false;
    }

    *uuid = parsed;
    return true;
}

bool isValidBaseUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
           && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasQuery()
           && !url.hasFragment();
}

} // namespace

BootstrapConfigLoadResult::BootstrapConfigLoadResult(BootstrapConfig config)
    : config_(std::move(config))
{
}

BootstrapConfigLoadResult::BootstrapConfigLoadResult(QString errorMessage)
    : errorMessage_(std::move(errorMessage))
{
}

bool BootstrapConfigLoadResult::isSuccess() const noexcept
{
    return config_.has_value();
}

const std::optional<BootstrapConfig> &BootstrapConfigLoadResult::config() const noexcept
{
    return config_;
}

const QString &BootstrapConfigLoadResult::errorMessage() const noexcept
{
    return errorMessage_;
}

BootstrapConfigLoadResult BootstrapConfigLoader::loadFromApplicationDirectory()
{
    return loadFromDirectory(QCoreApplication::applicationDirPath());
}

BootstrapConfigLoadResult BootstrapConfigLoader::loadFromDirectory(const QString &directoryPath)
{
    return loadFromFile(QDir(directoryPath).filePath(QStringLiteral("config/client.json")));
}

BootstrapConfigLoadResult BootstrapConfigLoader::loadFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return BootstrapConfigLoadResult(QStringLiteral("无法读取引导配置文件。"));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return BootstrapConfigLoadResult(QStringLiteral("引导配置不是有效的 JSON 对象。"));
    }

    const QJsonObject object = document.object();
    static const QSet<QString> allowedKeys = {
        QString::fromLatin1(clientIdKey),
        QString::fromLatin1(loginBaseUrlKey),
        QString::fromLatin1(allowInsecureTransportKey),
    };
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!allowedKeys.contains(iterator.key())) {
            return BootstrapConfigLoadResult(QStringLiteral("引导配置包含不支持的字段。"));
        }
    }

    const QJsonValue clientIdValue = object.value(QLatin1StringView(clientIdKey));
    const QJsonValue loginBaseUrlValue = object.value(QLatin1StringView(loginBaseUrlKey));
    const QJsonValue allowInsecureValue =
        object.value(QLatin1StringView(allowInsecureTransportKey));

    if (!clientIdValue.isString() || !loginBaseUrlValue.isString()) {
        return BootstrapConfigLoadResult(
            QStringLiteral("引导配置缺少必填字段或字段类型错误。"));
    }
    if (!allowInsecureValue.isUndefined() && !allowInsecureValue.isBool()) {
        return BootstrapConfigLoadResult(QStringLiteral("引导配置字段类型错误。"));
    }

    QUuid clientId;
    if (!isCanonicalUuid(clientIdValue.toString(), &clientId)) {
        return BootstrapConfigLoadResult(QStringLiteral("clientId 必须是规范的 UUID。"));
    }

    const QUrl loginBaseUrl(loginBaseUrlValue.toString(), QUrl::StrictMode);
    if (!isValidBaseUrl(loginBaseUrl)) {
        return BootstrapConfigLoadResult(
            QStringLiteral("loginBaseUrl 必须是合法的 HTTP 或 HTTPS 基址。"));
    }

    const bool allowInsecureTransport = allowInsecureValue.toBool(false);
    if (loginBaseUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
        && !allowInsecureTransport) {
        return BootstrapConfigLoadResult(
            QStringLiteral("明文 HTTP 必须通过 allowInsecureTransport 显式启用。"));
    }

    return BootstrapConfigLoadResult(
        BootstrapConfig(clientId, loginBaseUrl, allowInsecureTransport));
}
