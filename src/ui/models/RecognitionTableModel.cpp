#include "ui/models/RecognitionTableModel.h"

#include "services/RealtimeRecognitionStore.h"
#include "services/HistoryService.h"

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
    , mode_(Mode::Realtime)
    , store_(store)
{
    Q_ASSERT(store_);
    connect(store_, &RealtimeRecognitionStore::itemsChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

RecognitionTableModel::RecognitionTableModel(HistoryService *historyService,
                                             QObject *parent)
    : QAbstractTableModel(parent)
    , mode_(Mode::History)
    , historyService_(historyService)
{
    Q_ASSERT(historyService_);
    connect(historyService_, &HistoryService::resultsChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

int RecognitionTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return mode_ == Mode::Realtime
               ? static_cast<int>(store_->items().size())
               : static_cast<int>(historyService_->items().size());
}

int RecognitionTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return mode_ == Mode::Realtime ? static_cast<int>(ColumnCount)
                                   : static_cast<int>(HistoryColumnCount);
}

QVariant RecognitionTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= columnCount()) {
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    const RecognitionSnapshot &snapshot = mode_ == Mode::Realtime
                                              ? store_->items().at(index.row())
                                              : historyService_->items().at(index.row());
    if (mode_ == Mode::History) {
        switch (index.column()) {
        case HistoryRecognitionIdColumn:
            return snapshot.recognitionId();
        case HistoryDeviceColumn:
            return snapshot.deviceId();
        case HistoryPlateColumn:
            return snapshot.plateNumber().value_or(QStringLiteral("--"));
        case HistoryStatusColumn:
            return statusText(snapshot.status());
        case HistoryErrorCodeColumn:
            return snapshot.errorCode().value_or(QStringLiteral("--"));
        case HistoryErrorMessageColumn:
            return snapshot.errorMessage().value_or(QStringLiteral("--"));
        case HistoryCapturedAtColumn:
            return snapshot.capturedAt().toString(Qt::ISODateWithMs);
        case HistoryStartedAtColumn:
            return snapshot.startedAt().toString(Qt::ISODateWithMs);
        case HistoryCompletedAtColumn:
            return snapshot.completedAt()
                       ? snapshot.completedAt()->toString(Qt::ISODateWithMs)
                       : QStringLiteral("--");
        case HistoryDurationColumn:
            return snapshot.durationMs()
                       ? QString::number(*snapshot.durationMs())
                       : QStringLiteral("--");
        default:
            return {};
        }
    }
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
        || section < 0 || section >= columnCount()) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    if (mode_ == Mode::History) {
        static const QStringList historyHeaders {
            QStringLiteral("记录 ID"),
            QStringLiteral("设备"),
            QStringLiteral("车牌号"),
            QStringLiteral("状态"),
            QStringLiteral("错误码"),
            QStringLiteral("错误信息"),
            QStringLiteral("拍摄时间"),
            QStringLiteral("开始时间"),
            QStringLiteral("完成时间"),
            QStringLiteral("耗时"),
        };
        return historyHeaders.at(section);
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

HistoryService *RecognitionTableModel::historyService() const noexcept
{
    return historyService_;
}

RecognitionTableModel::Mode RecognitionTableModel::mode() const noexcept
{
    return mode_;
}
