#include "ui/models/AccessListTableModel.h"

#include "domain/AccessListRecord.h"
#include "services/AccessListService.h"

#include <QDateTime>

AccessListTableModel::AccessListTableModel(AccessListService *service, QObject *parent)
    : QAbstractTableModel(parent)
    , service_(service)
{
    Q_ASSERT(service_);
    connect(service_, &AccessListService::resultsChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

int AccessListTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !pageMatchesListType() ? 0 : service_->items().size();
}

int AccessListTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AccessListTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= service_->items().size()
        || index.column() < 0 || index.column() >= ColumnCount || !pageMatchesListType()) {
        return {};
    }
    if (role == Qt::TextAlignmentRole) {
        return index.column() == RemarkColumn
                   ? QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter)
                   : QVariant::fromValue(Qt::AlignCenter);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    const AccessListRecord &record = service_->items().at(index.row());
    switch (index.column()) {
    case SequenceColumn: {
        const auto &page = service_->lastSuccessfulPage();
        const int pageNumber = page ? page->page() : 1;
        return static_cast<qint64>(pageNumber - 1) * AccessListService::pageSize
               + index.row() + 1;
    }
    case PlateColumn:
        return record.plateNumber();
    case RemarkColumn:
        return record.remark();
    case CreatedByColumn:
        return record.createdBy();
    case CreatedAtColumn:
        return record.createdAt()
            .toOffsetFromUtc(8 * 60 * 60)
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    default:
        return {};
    }
}

QVariant AccessListTableModel::headerData(int section,
                                          Qt::Orientation orientation,
                                          int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
    case SequenceColumn:
        return QStringLiteral("序号");
    case PlateColumn:
        return QStringLiteral("车牌号");
    case RemarkColumn:
        return QStringLiteral("备注");
    case CreatedByColumn:
        return QStringLiteral("创建用户");
    case CreatedAtColumn:
        return QStringLiteral("创建时间");
    default:
        return {};
    }
}

AccessListService *AccessListTableModel::service() const noexcept
{
    return service_;
}

AccessListType AccessListTableModel::listType() const noexcept
{
    return listType_;
}

void AccessListTableModel::setListType(AccessListType listType)
{
    if (listType_ == listType) {
        return;
    }
    beginResetModel();
    listType_ = listType;
    endResetModel();
}

std::optional<AccessListRecord> AccessListTableModel::recordAt(int row) const
{
    if (row < 0 || row >= service_->items().size() || !pageMatchesListType()) {
        return std::nullopt;
    }
    return service_->items().at(row);
}

bool AccessListTableModel::pageMatchesListType() const
{
    return service_->items().isEmpty()
           || service_->items().constFirst().listType() == listType_;
}
