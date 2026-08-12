#pragma once

#include <QString>
#include <QVariantMap>
#include <QtTypes>

#include <optional>
#include <utility>

class RequestContext final
{
public:
    RequestContext(quint64 sessionGeneration,
                   quint64 requestGeneration,
                   QString operation,
                   std::optional<QString> recognitionId = std::nullopt,
                   QVariantMap filterSnapshot = {})
        : sessionGeneration_(sessionGeneration)
        , requestGeneration_(requestGeneration)
        , operation_(std::move(operation))
        , recognitionId_(std::move(recognitionId))
        , filterSnapshot_(std::move(filterSnapshot))
    {
    }

    [[nodiscard]] quint64 sessionGeneration() const noexcept { return sessionGeneration_; }
    [[nodiscard]] quint64 requestGeneration() const noexcept { return requestGeneration_; }
    [[nodiscard]] const QString &operation() const noexcept { return operation_; }
    [[nodiscard]] const std::optional<QString> &recognitionId() const noexcept
    {
        return recognitionId_;
    }
    [[nodiscard]] const QVariantMap &filterSnapshot() const noexcept { return filterSnapshot_; }

private:
    quint64 sessionGeneration_;
    quint64 requestGeneration_;
    QString operation_;
    std::optional<QString> recognitionId_;
    QVariantMap filterSnapshot_;
};
