#pragma once

#include <QString>

#include <optional>
#include <utility>

struct EmptyResponse final
{
};

template<typename T>
class HttpEnvelope final
{
public:
    HttpEnvelope(bool success,
                 QString code,
                 QString message,
                 QString requestId,
                 std::optional<T> data)
        : success_(success)
        , code_(std::move(code))
        , message_(std::move(message))
        , requestId_(std::move(requestId))
        , data_(std::move(data))
    {
    }

    [[nodiscard]] bool success() const noexcept { return success_; }
    [[nodiscard]] const QString &code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const QString &requestId() const noexcept { return requestId_; }
    [[nodiscard]] const std::optional<T> &data() const noexcept { return data_; }

private:
    bool success_;
    QString code_;
    QString message_;
    QString requestId_;
    std::optional<T> data_;
};
