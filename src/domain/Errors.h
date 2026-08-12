#pragma once

#include <QString>

#include <optional>
#include <utility>

enum class ProtocolErrorCode
{
    MalformedJson,
    RootTypeMismatch,
    MissingField,
    UnknownField,
    TypeMismatch,
    OutOfRange,
    InvalidValue,
    InvalidCombination,
};

class ProtocolError final
{
public:
    ProtocolError(ProtocolErrorCode code, QString fieldPath, QString message)
        : code_(code)
        , fieldPath_(std::move(fieldPath))
        , message_(std::move(message))
    {
    }

    [[nodiscard]] ProtocolErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &fieldPath() const noexcept { return fieldPath_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }

private:
    ProtocolErrorCode code_;
    QString fieldPath_;
    QString message_;
};

template<typename T>
class ProtocolResult final
{
public:
    [[nodiscard]] static ProtocolResult success(T value)
    {
        return ProtocolResult(std::move(value));
    }

    [[nodiscard]] static ProtocolResult failure(ProtocolError error)
    {
        return ProtocolResult(std::move(error));
    }

    [[nodiscard]] bool isSuccess() const noexcept { return value_.has_value(); }
    [[nodiscard]] const std::optional<T> &value() const noexcept { return value_; }
    [[nodiscard]] const std::optional<ProtocolError> &error() const noexcept { return error_; }

private:
    explicit ProtocolResult(T value)
        : value_(std::move(value))
    {
    }

    explicit ProtocolResult(ProtocolError error)
        : error_(std::move(error))
    {
    }

    std::optional<T> value_;
    std::optional<ProtocolError> error_;
};
