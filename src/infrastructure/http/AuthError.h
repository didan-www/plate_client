#pragma once

#include "domain/Errors.h"

#include <QString>

#include <optional>
#include <utility>

enum class AuthErrorCode
{
    InvalidRequest,
    Unauthorized,
    Forbidden,
    ClientRejected,
    ServerError,
    NetworkError,
    TlsError,
    Timeout,
    Cancelled,
    StaleResponse,
    ProtocolError,
};

class AuthError final
{
public:
    AuthError(AuthErrorCode code,
              QString message,
              QString serviceCode = {},
              QString requestId = {},
              std::optional<int> httpStatus = std::nullopt,
              std::optional<ProtocolError> protocolError = std::nullopt)
        : code_(code)
        , message_(std::move(message))
        , serviceCode_(std::move(serviceCode))
        , requestId_(std::move(requestId))
        , httpStatus_(httpStatus)
        , protocolError_(std::move(protocolError))
    {
    }

    [[nodiscard]] AuthErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const QString &serviceCode() const noexcept { return serviceCode_; }
    [[nodiscard]] const QString &requestId() const noexcept { return requestId_; }
    [[nodiscard]] const std::optional<int> &httpStatus() const noexcept { return httpStatus_; }
    [[nodiscard]] const std::optional<ProtocolError> &protocolError() const noexcept
    {
        return protocolError_;
    }

private:
    AuthErrorCode code_;
    QString message_;
    QString serviceCode_;
    QString requestId_;
    std::optional<int> httpStatus_;
    std::optional<ProtocolError> protocolError_;
};

Q_DECLARE_METATYPE(AuthError)
