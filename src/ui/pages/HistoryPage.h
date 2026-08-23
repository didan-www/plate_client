#pragma once

#include <QWidget>

class CsvDownloadService;
class HistoryService;
class QDateTimeEdit;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableView;
class RecognitionTableModel;

#include <functional>

class HistoryPage final : public QWidget
{
    Q_OBJECT

public:
    using SavePathSelector =
        std::function<QString(QWidget *parent, const QString &defaultFileName)>;

    explicit HistoryPage(HistoryService *historyService, QWidget *parent = nullptr);
    HistoryPage(HistoryService *historyService,
                CsvDownloadService *csvDownloadService,
                SavePathSelector savePathSelector = {},
                QWidget *parent = nullptr);
    ~HistoryPage() override;

    [[nodiscard]] HistoryService *historyService() const noexcept;
    [[nodiscard]] CsvDownloadService *csvDownloadService() const noexcept;
    [[nodiscard]] RecognitionTableModel *tableModel() const noexcept;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void search();
    void reset();
    void exportCsv();
    void updateLoading(bool loading);
    void updateDownloading(bool downloading);
    void showExportSuccess(const QString &targetPath);
    void showError(const QString &message);
    void updatePagination();
    void updateExportState();
    void applyDefaultFilter();

    HistoryService *historyService_;
    CsvDownloadService *csvDownloadService_;
    SavePathSelector savePathSelector_;
    RecognitionTableModel *tableModel_;
    QDateTimeEdit *startTimeEdit_;
    QDateTimeEdit *endTimeEdit_;
    QLineEdit *deviceEdit_;
    QPushButton *searchButton_;
    QPushButton *resetButton_;
    QPushButton *exportButton_ = nullptr;
    QPushButton *previousButton_;
    QPushButton *nextButton_;
    QLabel *loadingLabel_;
    QLabel *errorLabel_;
    QLabel *totalLabel_;
    QLabel *pageLabel_;
    QLabel *exportStatusLabel_ = nullptr;
    QTableView *tableView_;
    bool refreshOnShow_ = true;
};
