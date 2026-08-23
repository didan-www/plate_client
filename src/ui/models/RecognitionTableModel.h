#pragma once

#include <QAbstractTableModel>

class RealtimeRecognitionStore;

class RecognitionTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        SequenceColumn,
        DeviceColumn,
        PlateColumn,
        StatusColumn,
        CapturedAtColumn,
        ColumnCount,
    };

    explicit RecognitionTableModel(RealtimeRecognitionStore *store,
                                   QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    [[nodiscard]] RealtimeRecognitionStore *store() const noexcept;

private:
    RealtimeRecognitionStore *store_;
};
