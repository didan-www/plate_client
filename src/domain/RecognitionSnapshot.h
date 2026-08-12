#pragma once

#include <QDateTime>
#include <QString>
#include <QtTypes>

#include <optional>
#include <utility>

enum class RecognitionStatus
{
    Processing,
    Succeeded,
    Failed,
};

class RecognitionSnapshot final
{
public:
    RecognitionSnapshot(int schemaVersion,
                        QString recognitionId,
                        qint64 revision,
                        QString deviceId,
                        RecognitionStatus status,
                        std::optional<QString> plateNumber,
                        std::optional<QString> errorCode,
                        std::optional<QString> errorMessage,
                        QDateTime capturedAt,
                        QDateTime startedAt,
                        std::optional<QDateTime> completedAt,
                        std::optional<qint64> durationMs)
        : schemaVersion_(schemaVersion)
        , recognitionId_(std::move(recognitionId))
        , revision_(revision)
        , deviceId_(std::move(deviceId))
        , status_(status)
        , plateNumber_(std::move(plateNumber))
        , errorCode_(std::move(errorCode))
        , errorMessage_(std::move(errorMessage))
        , capturedAt_(std::move(capturedAt))
        , startedAt_(std::move(startedAt))
        , completedAt_(std::move(completedAt))
        , durationMs_(durationMs)
    {
    }

    [[nodiscard]] int schemaVersion() const noexcept { return schemaVersion_; }
    [[nodiscard]] const QString &recognitionId() const noexcept { return recognitionId_; }
    [[nodiscard]] qint64 revision() const noexcept { return revision_; }
    [[nodiscard]] const QString &deviceId() const noexcept { return deviceId_; }
    [[nodiscard]] RecognitionStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::optional<QString> &plateNumber() const noexcept { return plateNumber_; }
    [[nodiscard]] const std::optional<QString> &errorCode() const noexcept { return errorCode_; }
    [[nodiscard]] const std::optional<QString> &errorMessage() const noexcept { return errorMessage_; }
    [[nodiscard]] const QDateTime &capturedAt() const noexcept { return capturedAt_; }
    [[nodiscard]] const QDateTime &startedAt() const noexcept { return startedAt_; }
    [[nodiscard]] const std::optional<QDateTime> &completedAt() const noexcept { return completedAt_; }
    [[nodiscard]] const std::optional<qint64> &durationMs() const noexcept { return durationMs_; }

private:
    int schemaVersion_;
    QString recognitionId_;
    qint64 revision_;
    QString deviceId_;
    RecognitionStatus status_;
    std::optional<QString> plateNumber_;
    std::optional<QString> errorCode_;
    std::optional<QString> errorMessage_;
    QDateTime capturedAt_;
    QDateTime startedAt_;
    std::optional<QDateTime> completedAt_;
    std::optional<qint64> durationMs_;
};
