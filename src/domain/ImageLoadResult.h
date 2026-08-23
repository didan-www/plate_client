#pragma once

#include "domain/RequestContext.h"

#include <QImage>
#include <QMetaType>
#include <QSize>
#include <QString>

#include <optional>
#include <utility>

enum class ImageLoadErrorCode
{
    InvalidRequest,
    Unauthorized,
    Forbidden,
    NotFound,
    ClientRejected,
    ServerError,
    NetworkError,
    TlsError,
    Timeout,
    Cancelled,
    StaleResponse,
    ResponseTooLarge,
    InvalidContentType,
    UnsupportedFormat,
    FormatMismatch,
    InvalidDimensions,
    DimensionsExceeded,
    DecodeFailed,
    ProtocolError,
};

class ImageLoadError final
{
public:
    ImageLoadError(ImageLoadErrorCode code,
                   QString message,
                   std::optional<int> httpStatus = std::nullopt)
        : code_(code)
        , message_(std::move(message))
        , httpStatus_(httpStatus)
    {
    }

    [[nodiscard]] ImageLoadErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString &message() const noexcept { return message_; }
    [[nodiscard]] const std::optional<int> &httpStatus() const noexcept { return httpStatus_; }

private:
    ImageLoadErrorCode code_;
    QString message_;
    std::optional<int> httpStatus_;
};

class ImageLoadLimits final
{
public:
    static constexpr int maximumDimension = 8192;
    static constexpr qint64 maximumPixels = 40'000'000;

    [[nodiscard]] static bool accepts(const QSize &size) noexcept
    {
        if (!size.isValid() || size.width() <= 0 || size.height() <= 0
            || size.width() > maximumDimension || size.height() > maximumDimension) {
            return false;
        }
        return acceptsPixelCount(size.width(), size.height());
    }

    [[nodiscard]] static bool acceptsPixelCount(qint64 width, qint64 height) noexcept
    {
        if (width <= 0 || height <= 0 || width > maximumPixels / height) {
            return false;
        }
        return width * height <= maximumPixels;
    }
};

class ImageLoadResult final
{
public:
    [[nodiscard]] static ImageLoadResult success(RequestContext context,
                                                 QImage image,
                                                 int attempts)
    {
        return ImageLoadResult(std::move(context), std::move(image), std::nullopt,
                               attempts, false);
    }

    [[nodiscard]] static ImageLoadResult failure(RequestContext context,
                                                 ImageLoadError error,
                                                 int attempts,
                                                 bool manualRetryAvailable)
    {
        return ImageLoadResult(std::move(context), {}, std::move(error), attempts,
                               manualRetryAvailable);
    }

    [[nodiscard]] bool isSuccess() const noexcept { return !image_.isNull() && !error_; }
    [[nodiscard]] const RequestContext &context() const noexcept { return context_; }
    [[nodiscard]] const QImage &image() const noexcept { return image_; }
    [[nodiscard]] const std::optional<ImageLoadError> &error() const noexcept { return error_; }
    [[nodiscard]] int attempts() const noexcept { return attempts_; }
    [[nodiscard]] bool manualRetryAvailable() const noexcept
    {
        return manualRetryAvailable_;
    }

private:
    ImageLoadResult(RequestContext context,
                    QImage image,
                    std::optional<ImageLoadError> error,
                    int attempts,
                    bool manualRetryAvailable)
        : context_(std::move(context))
        , image_(std::move(image))
        , error_(std::move(error))
        , attempts_(attempts)
        , manualRetryAvailable_(manualRetryAvailable)
    {
    }

    RequestContext context_;
    QImage image_;
    std::optional<ImageLoadError> error_;
    int attempts_;
    bool manualRetryAvailable_;
};

Q_DECLARE_METATYPE(ImageLoadError)
Q_DECLARE_METATYPE(ImageLoadResult)
