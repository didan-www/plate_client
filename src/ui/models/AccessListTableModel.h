#pragma once

#include "domain/AccessListRecord.h"

#include <QAbstractTableModel>

#include <optional>

class AccessListService;

class AccessListTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        SequenceColumn,
        PlateColumn,
        RemarkColumn,
        CreatedByColumn,
        CreatedAtColumn,
        ColumnCount,
    };

    explicit AccessListTableModel(AccessListService *service,
                                  QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    [[nodiscard]] AccessListService *service() const noexcept;
    [[nodiscard]] AccessListType listType() const noexcept;
    void setListType(AccessListType listType);
    [[nodiscard]] std::optional<AccessListRecord> recordAt(int row) const;

private:
    [[nodiscard]] bool pageMatchesListType() const;

    AccessListService *service_;
    AccessListType listType_ = AccessListType::White;
};
