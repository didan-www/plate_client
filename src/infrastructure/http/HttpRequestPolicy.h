#pragma once

#include <QByteArray>
#include <QString>

#include <chrono>

enum class HttpRequestKind
{
    Login,
    Logout,
    Heartbeat,
    Query,
    Image,
    AccessListWrite,
    Csv,
};

enum class HttpResponseMode
{
    JsonMemory,
    ImageMemory,
    CsvStream,
};

class HttpRequestPolicy final
{
public:
    static constexpr qint64 jsonMaximumBytes = 2LL * 1024LL * 1024LL;
    static constexpr qint64 imageMaximumBytes = 20LL * 1024LL * 1024LL;

    [[nodiscard]] static std::chrono::milliseconds timeout(HttpRequestKind kind) noexcept
    {
        using namespace std::chrono_literals;
        switch (kind) {
        case HttpRequestKind::Login:
            return 10s;
        case HttpRequestKind::Logout:
        case HttpRequestKind::Heartbeat:
            return 5s;
        case HttpRequestKind::Query:
        case HttpRequestKind::AccessListWrite:
            return 10s;
        case HttpRequestKind::Image:
            return 30s;
        case HttpRequestKind::Csv:
            return 120s;
        }
        return 10s;
    }

    [[nodiscard]] static qint64 maximumBufferedBytes(HttpResponseMode mode) noexcept
    {
        switch (mode) {
        case HttpResponseMode::JsonMemory:
            return jsonMaximumBytes;
        case HttpResponseMode::ImageMemory:
            return imageMaximumBytes;
        case HttpResponseMode::CsvStream:
            return 0;
        }
        return 0;
    }

    [[nodiscard]] static QByteArray acceptHeader(HttpResponseMode mode)
    {
        switch (mode) {
        case HttpResponseMode::JsonMemory:
            return QByteArrayLiteral("application/json");
        case HttpResponseMode::ImageMemory:
            return QByteArrayLiteral("image/jpeg, image/png");
        case HttpResponseMode::CsvStream:
            return QByteArrayLiteral("text/csv");
        }
        return {};
    }

private:
    HttpRequestPolicy() = delete;
};
