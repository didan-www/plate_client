#pragma once

#include <QAbstractTableModel>

class RealtimeRecognitionStore;
class HistoryService;

class RecognitionTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class Mode
    {
        Realtime,
        History,
    };

    enum Column
    {
        SequenceColumn,
        DeviceColumn,
        PlateColumn,
        StatusColumn,
        CapturedAtColumn,
        ColumnCount,
    };

    enum HistoryColumn
    {
        HistoryRecognitionIdColumn,
        HistoryDeviceColumn,
        HistoryPlateColumn,
        HistoryStatusColumn,
        HistoryErrorCodeColumn,
        HistoryErrorMessageColumn,
        HistoryCapturedAtColumn,
        HistoryStartedAtColumn,
        HistoryCompletedAtColumn,
        HistoryDurationColumn,
        HistoryColumnCount,
    };

    explicit RecognitionTableModel(RealtimeRecognitionStore *store,
                                   QObject *parent = nullptr);
    explicit RecognitionTableModel(HistoryService *historyService,
                                   QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    [[nodiscard]] RealtimeRecognitionStore *store() const noexcept;
    [[nodiscard]] HistoryService *historyService() const noexcept;
    [[nodiscard]] Mode mode() const noexcept;

private:
    Mode mode_;
    RealtimeRecognitionStore *store_ = nullptr;
    HistoryService *historyService_ = nullptr;
};
