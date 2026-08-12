#pragma once

#include <QDateTime>
#include <QString>
#include <QtTypes>

#include <utility>

enum class AccessListType
{
    White,
    Black,
};

class AccessListRecord final
{
public:
    AccessListRecord(qint64 id,
                     AccessListType listType,
                     QString plateNumber,
                     QString remark,
                     QString createdBy,
                     QDateTime createdAt)
        : id_(id)
        , listType_(listType)
        , plateNumber_(std::move(plateNumber))
        , remark_(std::move(remark))
        , createdBy_(std::move(createdBy))
        , createdAt_(std::move(createdAt))
    {
    }

    [[nodiscard]] qint64 id() const noexcept { return id_; }
    [[nodiscard]] AccessListType listType() const noexcept { return listType_; }
    [[nodiscard]] const QString &plateNumber() const noexcept { return plateNumber_; }
    [[nodiscard]] const QString &remark() const noexcept { return remark_; }
    [[nodiscard]] const QString &createdBy() const noexcept { return createdBy_; }
    [[nodiscard]] const QDateTime &createdAt() const noexcept { return createdAt_; }

private:
    qint64 id_;
    AccessListType listType_;
    QString plateNumber_;
    QString remark_;
    QString createdBy_;
    QDateTime createdAt_;
};
