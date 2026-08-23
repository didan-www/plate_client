#include "ui/models/RecognitionTableModel.h"

#include "services/RealtimeRecognitionStore.h"

#include <QDateTime>

namespace {
QString statusText(RecognitionStatus status)
{
    switch (status) {
    case RecognitionStatus::Processing:
        return QStringLiteral("识别中");
    case RecognitionStatus::Succeeded:
        return QStringLiteral("识别成功");
    case RecognitionStatus::Failed:
        return QStringLiteral("识别失败");
    }
    return {};
}
} // namespace

RecognitionTableModel::RecognitionTableModel(RealtimeRecognitionStore *store,
                                             QObject *parent)
    : QAbstractTableModel(parent)
    , store_(store)
{
    Q_ASSERT(store_);
    connect(store_, &RealtimeRecognitionStore::itemsChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

int RecognitionTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(store_->items().size());
}

int RecognitionTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RecognitionTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= ColumnCount) {
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    const RecognitionSnapshot &snapshot = store_->items().at(index.row());
    switch (index.column()) {
    case SequenceColumn:
        return index.row() + 1;
    case DeviceColumn:
        return snapshot.deviceId();
    case PlateColumn:
        return snapshot.plateNumber().value_or(QStringLiteral("--"));
    case StatusColumn:
        return statusText(snapshot.status());
    case CapturedAtColumn:
        return snapshot.capturedAt().toString(Qt::ISODate);
    default:
        return {};
    }
}

QVariant RecognitionTableModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole
        || section < 0 || section >= ColumnCount) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    static const QStringList headers {
        QStringLiteral("序号"),
        QStringLiteral("设备"),
        QStringLiteral("车牌号"),
        QStringLiteral("状态"),
        QStringLiteral("时间"),
    };
    return headers.at(section);
}

RealtimeRecognitionStore *RecognitionTableModel::store() const noexcept
{
    return store_;
}
