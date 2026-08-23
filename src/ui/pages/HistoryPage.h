#pragma once

#include <QWidget>

class HistoryService;
class QDateTimeEdit;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableView;
class RecognitionTableModel;

class HistoryPage final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPage(HistoryService *historyService, QWidget *parent = nullptr);
    ~HistoryPage() override;

    [[nodiscard]] HistoryService *historyService() const noexcept;
    [[nodiscard]] RecognitionTableModel *tableModel() const noexcept;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void search();
    void reset();
    void updateLoading(bool loading);
    void showError(const QString &message);
    void updatePagination();
    void applyDefaultFilter();

    HistoryService *historyService_;
    RecognitionTableModel *tableModel_;
    QDateTimeEdit *startTimeEdit_;
    QDateTimeEdit *endTimeEdit_;
    QLineEdit *deviceEdit_;
    QPushButton *searchButton_;
    QPushButton *resetButton_;
    QPushButton *previousButton_;
    QPushButton *nextButton_;
    QLabel *loadingLabel_;
    QLabel *errorLabel_;
    QLabel *totalLabel_;
    QLabel *pageLabel_;
    QTableView *tableView_;
    bool refreshOnShow_ = true;
};
