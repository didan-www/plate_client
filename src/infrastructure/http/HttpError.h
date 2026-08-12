#pragma once

#include "domain/Errors.h"

#include <QString>

#include <optional>
#include <utility>

enum class HttpErrorCode
{
    ClientError,
    ServerError,
    NetworkError,
    TlsError,
    Timeout,
    Cancelled,
    ResponseTooLarge,
    ParseFailure,
    RedirectRejected,
    StaleResponse,
};

class HttpError final
{
public:
    HttpError(HttpErrorCode code,
              QString message,
              std::optional<int> httpStatus = std::nullopt,
              std::optional<int> networkError = std::nullopt,
              std::optional<ProtocolError> protocolError = std::nullopt)
        : code_(code)
        , message_(std::move(message))
        , httpStatus_(httpStatus)
        , networkError_(networkError)
        , protocolError_(std::move(protocolError))
    {
    }

    [[nodiscard]] HttpErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const std::optional<int> &httpStatus() const noexcept { return httpStatus_; }
    [[nodiscard]] const std::optional<int> &networkError() const noexcept { return networkError_; }
    [[nodiscard]] const std::optional<ProtocolError> &protocolError() const noexcept
    {
        return protocolError_;
    }

private:
    HttpErrorCode code_;
    QString message_;
    std::optional<int> httpStatus_;
    std::optional<int> networkError_;
    std::optional<ProtocolError> protocolError_;
};
