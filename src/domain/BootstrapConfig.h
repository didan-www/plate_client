#pragma once

#include <QUrl>
#include <QUuid>

#include <utility>

class BootstrapConfigLoader;

class BootstrapConfig final
{
public:
    [[nodiscard]] const QUuid &clientId() const noexcept
    {
        return clientId_;
    }

    [[nodiscard]] const QUrl &loginBaseUrl() const noexcept
    {
        return loginBaseUrl_;
    }

    [[nodiscard]] bool allowInsecureTransport() const noexcept
    {
        return allowInsecureTransport_;
    }

private:
    friend class BootstrapConfigLoader;

    BootstrapConfig(QUuid clientId, QUrl loginBaseUrl, bool allowInsecureTransport)
        : clientId_(std::move(clientId))
        , loginBaseUrl_(std::move(loginBaseUrl))
        , allowInsecureTransport_(allowInsecureTransport)
    {
    }

    QUuid clientId_;
    QUrl loginBaseUrl_;
    bool allowInsecureTransport_ = false;
};
