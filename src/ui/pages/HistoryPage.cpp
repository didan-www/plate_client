#include "ui/pages/HistoryPage.h"

#include "services/CsvDownloadService.h"
#include "services/HistoryService.h"
#include "ui/models/RecognitionTableModel.h"

#include <QAbstractItemView>
#include <QDateTimeEdit>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTableView>
#include <QTimeZone>
#include <QVBoxLayout>

#include <utility>

namespace {
QString selectExportPath(QWidget *parent, const QString &defaultFileName)
{
    QFileDialog dialog(parent, QStringLiteral("导出 CSV"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(QStringLiteral("CSV 文件 (*.csv)"));
    dialog.setDefaultSuffix(QStringLiteral("csv"));
    dialog.selectFile(defaultFileName);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return {};
    }
    return dialog.selectedFiles().constFirst();
}

QString defaultExportFileName()
{
    const QString timestamp = QDateTime::currentDateTimeUtc()
                                  .toOffsetFromUtc(8 * 60 * 60)
                                  .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QStringLiteral("车牌识别记录_%1.csv").arg(timestamp);
}
} // namespace

HistoryPage::HistoryPage(HistoryService *historyService, QWidget *parent)
    : HistoryPage(historyService, nullptr, {}, parent)
{
}

HistoryPage::HistoryPage(HistoryService *historyService,
                         CsvDownloadService *csvDownloadService,
                         SavePathSelector savePathSelector,
                         QWidget *parent)
    : QWidget(parent)
    , historyService_(historyService)
    , csvDownloadService_(csvDownloadService)
    , savePathSelector_(savePathSelector ? std::move(savePathSelector) : selectExportPath)
    , tableModel_(new RecognitionTableModel(historyService, this))
    , startTimeEdit_(new QDateTimeEdit(this))
    , endTimeEdit_(new QDateTimeEdit(this))
    , deviceEdit_(new QLineEdit(this))
    , searchButton_(new QPushButton(QStringLiteral("搜索"), this))
    , resetButton_(new QPushButton(QStringLiteral("重置"), this))
    , previousButton_(new QPushButton(QStringLiteral("上一页"), this))
    , nextButton_(new QPushButton(QStringLiteral("下一页"), this))
    , loadingLabel_(new QLabel(this))
    , errorLabel_(new QLabel(this))
    , totalLabel_(new QLabel(QStringLiteral("共 0 条"), this))
    , pageLabel_(new QLabel(QStringLiteral("第 1 页"), this))
    , tableView_(new QTableView(this))
{
    Q_ASSERT(historyService_);
    setObjectName(QStringLiteral("historyPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *title = new QLabel(QStringLiteral("日志记录"), this);
    title->setObjectName(QStringLiteral("historyTitleLabel"));
    title->setFixedHeight(38);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    if (csvDownloadService_) {
        exportStatusLabel_ = new QLabel(this);
        exportStatusLabel_->setObjectName(QStringLiteral("historyExportStatusLabel"));
        exportButton_ = new QPushButton(QStringLiteral("导出 CSV"), this);
        exportButton_->setObjectName(QStringLiteral("historyExportButton"));
        exportButton_->setFixedWidth(108);
        headerLayout->addWidget(exportStatusLabel_);
        headerLayout->addWidget(exportButton_);
    }

    startTimeEdit_->setObjectName(QStringLiteral("historyStartTimeEdit"));
    endTimeEdit_->setObjectName(QStringLiteral("historyEndTimeEdit"));
    deviceEdit_->setObjectName(QStringLiteral("historyDeviceEdit"));
    searchButton_->setObjectName(QStringLiteral("historySearchButton"));
    resetButton_->setObjectName(QStringLiteral("historyResetButton"));
    previousButton_->setObjectName(QStringLiteral("historyPreviousButton"));
    nextButton_->setObjectName(QStringLiteral("historyNextButton"));
    loadingLabel_->setObjectName(QStringLiteral("historyLoadingLabel"));
    errorLabel_->setObjectName(QStringLiteral("historyErrorLabel"));
    totalLabel_->setObjectName(QStringLiteral("historyTotalLabel"));
    pageLabel_->setObjectName(QStringLiteral("historyPageLabel"));
    tableView_->setObjectName(QStringLiteral("historyTable"));

    for (QDateTimeEdit *edit : {startTimeEdit_, endTimeEdit_}) {
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        edit->setTimeZone(QTimeZone::fromSecondsAheadOfUtc(8 * 60 * 60));
        edit->setCalendarPopup(true);
        edit->setMinimumWidth(190);
    }
    deviceEdit_->setPlaceholderText(QStringLiteral("全部设备"));
    deviceEdit_->setClearButtonEnabled(true);
    deviceEdit_->setMaximumWidth(260);
    searchButton_->setFixedWidth(88);
    resetButton_->setFixedWidth(88);
    previousButton_->setFixedWidth(88);
    nextButton_->setFixedWidth(88);

    auto *filterPanel = new QWidget(this);
    filterPanel->setObjectName(QStringLiteral("historyFilterPanel"));
    auto *filterLayout = new QGridLayout(filterPanel);
    filterLayout->setContentsMargins(18, 14, 18, 14);
    filterLayout->setHorizontalSpacing(12);
    filterLayout->setVerticalSpacing(10);
    filterLayout->addWidget(new QLabel(QStringLiteral("开始时间"), filterPanel), 0, 0);
    filterLayout->addWidget(startTimeEdit_, 0, 1);
    filterLayout->addWidget(new QLabel(QStringLiteral("结束时间"), filterPanel), 0, 2);
    filterLayout->addWidget(endTimeEdit_, 0, 3);
    filterLayout->addWidget(new QLabel(QStringLiteral("设备"), filterPanel), 0, 4);
    filterLayout->addWidget(deviceEdit_, 0, 5);
    filterLayout->addWidget(searchButton_, 0, 6);
    filterLayout->addWidget(resetButton_, 0, 7);
    filterLayout->setColumnStretch(5, 1);

    loadingLabel_->setMinimumHeight(22);
    errorLabel_->setMinimumHeight(22);
    errorLabel_->setWordWrap(true);

    tableView_->setModel(tableModel_);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setAlternatingRowColors(true);
    tableView_->setShowGrid(false);
    tableView_->setWordWrap(false);
    tableView_->setTextElideMode(Qt::ElideRight);
    tableView_->verticalHeader()->setVisible(false);
    tableView_->verticalHeader()->setDefaultSectionSize(34);
    tableView_->horizontalHeader()->setMinimumSectionSize(88);
    tableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tableView_->horizontalHeader()->setStretchLastSection(true);

    auto *pagination = new QHBoxLayout;
    pagination->setContentsMargins(0, 0, 0, 0);
    pagination->addWidget(totalLabel_);
    pagination->addStretch();
    pagination->addWidget(pageLabel_);
    pagination->addWidget(previousButton_);
    pagination->addWidget(nextButton_);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 20, 28, 24);
    root->setSpacing(12);
    root->addLayout(headerLayout);
    root->addWidget(filterPanel);
    root->addWidget(loadingLabel_);
    root->addWidget(errorLabel_);
    root->addWidget(tableView_, 1);
    root->addLayout(pagination);

    connect(searchButton_, &QPushButton::clicked, this, &HistoryPage::search);
    connect(resetButton_, &QPushButton::clicked, this, &HistoryPage::reset);
    connect(deviceEdit_, &QLineEdit::returnPressed, this, &HistoryPage::search);
    connect(previousButton_, &QPushButton::clicked,
            historyService_, &HistoryService::previousPage);
    connect(nextButton_, &QPushButton::clicked,
            historyService_, &HistoryService::nextPage);
    connect(historyService_, &HistoryService::resultsChanged,
            this, [this] {
                updatePagination();
                updateExportState();
            });
    connect(historyService_, &HistoryService::loadingChanged,
            this, &HistoryPage::updateLoading);
    connect(historyService_, &HistoryService::queryFailed,
            this, &HistoryPage::showError);
    if (csvDownloadService_) {
        connect(exportButton_, &QPushButton::clicked, this, &HistoryPage::exportCsv);
        connect(csvDownloadService_, &CsvDownloadService::downloadingChanged,
                this, &HistoryPage::updateDownloading);
        connect(csvDownloadService_, &CsvDownloadService::downloadSucceeded,
                this, &HistoryPage::showExportSuccess);
        connect(csvDownloadService_, &CsvDownloadService::downloadFailed,
                this, &HistoryPage::showError);
    }

    setStyleSheet(QStringLiteral(R"(
        QWidget#historyPage { background: #F4F6F8; }
        QLabel#historyTitleLabel {
            color: #1F2933;
            font-size: 24px;
            font-weight: bold;
        }
        QWidget#historyFilterPanel {
            background: #FFFFFF;
            border: 1px solid #D7DEE5;
            border-radius: 4px;
        }
        QLabel#historyLoadingLabel { color: #1F5F99; }
        QLabel#historyExportStatusLabel { color: #1F5F99; }
        QLabel#historyErrorLabel { color: #B42318; }
        QPushButton#historySearchButton,
        QPushButton#historyExportButton,
        QPushButton#historyPreviousButton,
        QPushButton#historyNextButton {
            color: #FFFFFF;
            background: #1F5F99;
            border: 0;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton#historyResetButton {
            color: #1F2933;
            background: #E6EDF3;
            border: 1px solid #CBD4DD;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton:disabled { background: #AEB9C3; color: #F4F6F8; }
        QTableView#historyTable {
            background: #FFFFFF;
            alternate-background-color: #F7F9FB;
            border: 1px solid #D7DEE5;
            color: #1F2933;
            selection-background-color: #DCEAF5;
            selection-color: #1F2933;
        }
        QHeaderView::section {
            background: #E6EDF3;
            color: #354656;
            border: 0;
            border-right: 1px solid #D2DAE2;
            padding: 7px;
            font-weight: bold;
        }
    )"));

    applyDefaultFilter();
    updateLoading(historyService_->isLoading());
    updatePagination();
    updateExportState();
}

HistoryPage::~HistoryPage()
{
    historyService_->cancelActive();
}

HistoryService *HistoryPage::historyService() const noexcept
{
    return historyService_;
}

CsvDownloadService *HistoryPage::csvDownloadService() const noexcept
{
    return csvDownloadService_;
}

RecognitionTableModel *HistoryPage::tableModel() const noexcept
{
    return tableModel_;
}

void HistoryPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (refreshOnShow_) {
        refreshOnShow_ = false;
        errorLabel_->clear();
        historyService_->refresh();
    }
}

void HistoryPage::hideEvent(QHideEvent *event)
{
    historyService_->cancelActive();
    refreshOnShow_ = true;
    QWidget::hideEvent(event);
}

void HistoryPage::search()
{
    errorLabel_->clear();
    historyService_->search(startTimeEdit_->dateTime(),
                            endTimeEdit_->dateTime(),
                            deviceEdit_->text());
}

void HistoryPage::reset()
{
    deviceEdit_->clear();
    errorLabel_->clear();
    historyService_->reset();
    const auto &submitted = historyService_->lastSubmittedFilter();
    if (submitted) {
        startTimeEdit_->setDateTime(submitted->startTime());
        endTimeEdit_->setDateTime(submitted->endTime());
    }
}

void HistoryPage::exportCsv()
{
    if (!csvDownloadService_ || csvDownloadService_->isDownloading()
        || !historyService_->lastSubmittedFilter()) {
        return;
    }
    const QString targetPath = savePathSelector_(this, defaultExportFileName());
    if (targetPath.isEmpty()) {
        return;
    }
    errorLabel_->clear();
    exportStatusLabel_->clear();
    if (!csvDownloadService_->download(*historyService_->lastSubmittedFilter(),
                                       targetPath)) {
        showError(QStringLiteral("CSV 导出无法启动。"));
    }
}

void HistoryPage::updateLoading(bool loading)
{
    if (loading) {
        errorLabel_->clear();
    }
    loadingLabel_->setText(loading ? QStringLiteral("正在查询...") : QString());
    updateExportState();
}

void HistoryPage::updateDownloading(bool downloading)
{
    if (exportStatusLabel_) {
        exportStatusLabel_->setText(downloading ? QStringLiteral("正在导出...") : QString());
    }
    updateExportState();
}

void HistoryPage::showExportSuccess(const QString &targetPath)
{
    Q_UNUSED(targetPath)
    errorLabel_->clear();
    if (exportStatusLabel_) {
        exportStatusLabel_->setText(QStringLiteral("CSV 已保存。"));
    }
}

void HistoryPage::showError(const QString &message)
{
    errorLabel_->setText(message);
}

void HistoryPage::updatePagination()
{
    const auto &page = historyService_->lastSuccessfulPage();
    totalLabel_->setText(QStringLiteral("共 %1 条").arg(page ? page->total() : 0));
    pageLabel_->setText(QStringLiteral("第 %1 页").arg(page ? page->page() : 1));
    previousButton_->setEnabled(historyService_->canGoPrevious());
    nextButton_->setEnabled(historyService_->canGoNext());
}

void HistoryPage::updateExportState()
{
    if (!exportButton_) {
        return;
    }
    exportButton_->setEnabled(historyService_->lastSubmittedFilter().has_value()
                              && !csvDownloadService_->isDownloading());
}

void HistoryPage::applyDefaultFilter()
{
    const HistoryFilter filter = historyService_->defaultFilter();
    startTimeEdit_->setDateTime(filter.startTime());
    endTimeEdit_->setDateTime(filter.endTime());
}
