#pragma once

#include "domain/BootstrapConfig.h"

#include <QString>

#include <optional>

class BootstrapConfigLoadResult final
{
public:
    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] const std::optional<BootstrapConfig> &config() const noexcept;
    [[nodiscard]] const QString &errorMessage() const noexcept;

private:
    friend class BootstrapConfigLoader;

    explicit BootstrapConfigLoadResult(BootstrapConfig config);
    explicit BootstrapConfigLoadResult(QString errorMessage);

    std::optional<BootstrapConfig> config_;
    QString errorMessage_;
};

class BootstrapConfigLoader final
{
public:
    [[nodiscard]] static BootstrapConfigLoadResult loadFromApplicationDirectory();
    [[nodiscard]] static BootstrapConfigLoadResult loadFromDirectory(const QString &directoryPath);
    [[nodiscard]] static BootstrapConfigLoadResult loadFromFile(const QString &filePath);
};
