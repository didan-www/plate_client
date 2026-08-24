#include "ui/pages/AccessListPage.h"

#include "services/AccessListService.h"
#include "ui/models/AccessListTableModel.h"
#include "ui/widgets/AccessListCreateDialog.h"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QTabBar>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace {
QString listTypeName(AccessListType listType)
{
    return listType == AccessListType::White ? QStringLiteral("白名单")
                                             : QStringLiteral("黑名单");
}

bool confirmDelete(QWidget *parent,
                   AccessListType listType,
                   const QString &plateNumber)
{
    return QMessageBox::question(
               parent,
               QStringLiteral("确认删除"),
               QStringLiteral("确定从%1删除车牌 %2 吗？")
                   .arg(listTypeName(listType), plateNumber),
               QMessageBox::Yes | QMessageBox::No,
               QMessageBox::No)
           == QMessageBox::Yes;
}

QString serviceErrorMessage(const AccessListServiceError &error)
{
    switch (error.code()) {
    case AccessListServiceErrorCode::InvalidInput:
        return QStringLiteral("输入不符合名单规则。");
    case AccessListServiceErrorCode::Busy:
        return QStringLiteral("已有名单写操作正在进行。");
    case AccessListServiceErrorCode::Unauthorized:
        return QStringLiteral("当前会话已失效。");
    case AccessListServiceErrorCode::Forbidden:
        return QStringLiteral("当前用户无权执行此操作。");
    case AccessListServiceErrorCode::NotFound:
        return QStringLiteral("名单记录不存在。");
    case AccessListServiceErrorCode::ConflictWhite:
        return QStringLiteral("该车牌已存在于白名单。");
    case AccessListServiceErrorCode::ConflictBlack:
        return QStringLiteral("该车牌已存在于黑名单。");
    case AccessListServiceErrorCode::ClientRejected:
        return QStringLiteral("服务端拒绝了当前名单操作。");
    case AccessListServiceErrorCode::ServerError:
        return QStringLiteral("名单服务暂时不可用。");
    case AccessListServiceErrorCode::NetworkError:
        return QStringLiteral("无法连接名单服务。");
    case AccessListServiceErrorCode::TlsError:
        return QStringLiteral("名单服务的 TLS 连接失败。");
    case AccessListServiceErrorCode::Timeout:
        return QStringLiteral("名单请求超时。");
    case AccessListServiceErrorCode::ResponseTooLarge:
        return QStringLiteral("名单响应超过大小限制。");
    case AccessListServiceErrorCode::ProtocolError:
        return QStringLiteral("名单服务响应格式无效。");
    }
    return QStringLiteral("名单操作失败。");
}
} // namespace

AccessListPage::AccessListPage(AccessListService *service, QWidget *parent)
    : AccessListPage(service, {}, parent)
{
}

AccessListPage::AccessListPage(AccessListService *service,
                               DeleteConfirmer deleteConfirmer,
                               QWidget *parent)
    : QWidget(parent)
    , service_(service)
    , deleteConfirmer_(deleteConfirmer ? std::move(deleteConfirmer) : confirmDelete)
    , tableModel_(new AccessListTableModel(service, this))
    , tabs_(new QTabBar(this))
    , keywordEdit_(new QLineEdit(this))
    , searchButton_(new QPushButton(QStringLiteral("搜索"), this))
    , resetButton_(new QPushButton(QStringLiteral("重置"), this))
    , refreshButton_(new QPushButton(QStringLiteral("刷新"), this))
    , createButton_(new QPushButton(QStringLiteral("新增"), this))
    , deleteButton_(new QPushButton(QStringLiteral("删除"), this))
    , previousButton_(new QPushButton(QStringLiteral("上一页"), this))
    , nextButton_(new QPushButton(QStringLiteral("下一页"), this))
    , loadingLabel_(new QLabel(this))
    , errorLabel_(new QLabel(this))
    , statusLabel_(new QLabel(this))
    , totalLabel_(new QLabel(QStringLiteral("共 0 条"), this))
    , pageLabel_(new QLabel(QStringLiteral("第 1 页"), this))
    , tableView_(new QTableView(this))
{
    Q_ASSERT(service_);
    setObjectName(QStringLiteral("accessListPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *title = new QLabel(QStringLiteral("黑白名单管理"), this);
    title->setObjectName(QStringLiteral("accessListTitleLabel"));
    title->setFixedHeight(38);

    tabs_->setObjectName(QStringLiteral("accessListTabs"));
    tabs_->addTab(QStringLiteral("白名单"));
    tabs_->addTab(QStringLiteral("黑名单"));
    tabs_->setCurrentIndex(0);
    tabs_->setExpanding(false);

    keywordEdit_->setObjectName(QStringLiteral("accessListKeywordEdit"));
    keywordEdit_->setPlaceholderText(QStringLiteral("按车牌号搜索"));
    keywordEdit_->setClearButtonEnabled(true);
    keywordEdit_->setMaximumWidth(320);
    searchButton_->setObjectName(QStringLiteral("accessListSearchButton"));
    resetButton_->setObjectName(QStringLiteral("accessListResetButton"));
    refreshButton_->setObjectName(QStringLiteral("accessListRefreshButton"));
    createButton_->setObjectName(QStringLiteral("accessListCreateButton"));
    deleteButton_->setObjectName(QStringLiteral("accessListDeleteButton"));
    previousButton_->setObjectName(QStringLiteral("accessListPreviousButton"));
    nextButton_->setObjectName(QStringLiteral("accessListNextButton"));
    loadingLabel_->setObjectName(QStringLiteral("accessListLoadingLabel"));
    errorLabel_->setObjectName(QStringLiteral("accessListErrorLabel"));
    statusLabel_->setObjectName(QStringLiteral("accessListStatusLabel"));
    totalLabel_->setObjectName(QStringLiteral("accessListTotalLabel"));
    pageLabel_->setObjectName(QStringLiteral("accessListPageLabel"));
    tableView_->setObjectName(QStringLiteral("accessListTable"));

    for (QPushButton *button : {searchButton_, resetButton_, refreshButton_,
                                createButton_, deleteButton_, previousButton_, nextButton_}) {
        button->setFixedWidth(88);
    }

    auto *filterPanel = new QWidget(this);
    filterPanel->setObjectName(QStringLiteral("accessListFilterPanel"));
    auto *filterLayout = new QGridLayout(filterPanel);
    filterLayout->setContentsMargins(18, 14, 18, 14);
    filterLayout->setHorizontalSpacing(12);
    filterLayout->setVerticalSpacing(10);
    filterLayout->addWidget(new QLabel(QStringLiteral("车牌号"), filterPanel), 0, 0);
    filterLayout->addWidget(keywordEdit_, 0, 1);
    filterLayout->addWidget(searchButton_, 0, 2);
    filterLayout->addWidget(resetButton_, 0, 3);
    filterLayout->addWidget(refreshButton_, 0, 4);
    filterLayout->setColumnStretch(1, 1);

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addWidget(createButton_);
    actions->addWidget(deleteButton_);
    actions->addStretch();
    actions->addWidget(statusLabel_);

    loadingLabel_->setMinimumHeight(22);
    errorLabel_->setMinimumHeight(22);
    errorLabel_->setWordWrap(true);
    statusLabel_->setMinimumHeight(22);

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
    tableView_->setColumnWidth(AccessListTableModel::SequenceColumn, 76);
    tableView_->setColumnWidth(AccessListTableModel::PlateColumn, 180);
    tableView_->setColumnWidth(AccessListTableModel::RemarkColumn, 300);
    tableView_->setColumnWidth(AccessListTableModel::CreatedByColumn, 150);

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
    root->addWidget(title);
    root->addWidget(tabs_);
    root->addWidget(filterPanel);
    root->addLayout(actions);
    root->addWidget(loadingLabel_);
    root->addWidget(errorLabel_);
    root->addWidget(tableView_, 1);
    root->addLayout(pagination);

    connect(tabs_, &QTabBar::currentChanged,
            this, &AccessListPage::changeListType);
    connect(searchButton_, &QPushButton::clicked, this, &AccessListPage::search);
    connect(keywordEdit_, &QLineEdit::returnPressed, this, &AccessListPage::search);
    connect(resetButton_, &QPushButton::clicked, this, &AccessListPage::reset);
    connect(refreshButton_, &QPushButton::clicked, this, &AccessListPage::refresh);
    connect(createButton_, &QPushButton::clicked,
            this, &AccessListPage::openCreateDialog);
    connect(deleteButton_, &QPushButton::clicked,
            this, &AccessListPage::deleteSelected);
    connect(previousButton_, &QPushButton::clicked,
            service_, &AccessListService::previousPage);
    connect(nextButton_, &QPushButton::clicked,
            service_, &AccessListService::nextPage);
    connect(tableView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] { updateWriteActions(); });
    connect(service_, &AccessListService::resultsChanged,
            this, &AccessListPage::handleResultsChanged);
    connect(service_, &AccessListService::queryLoadingChanged, this,
            [this](bool loading) {
                if (loading && !pageActive_) {
                    service_->cancelQuery();
                    return;
                }
                if (loading) {
                    errorLabel_->clear();
                }
                loadingLabel_->setText(loading ? QStringLiteral("正在查询...")
                                               : QString());
            });
    connect(service_, &AccessListService::queryFailed, this,
            [this](const AccessListServiceError &error) {
                showError(serviceErrorMessage(error));
            });
    connect(service_, &AccessListService::writeBusyChanged,
            this, &AccessListPage::handleWriteBusyChanged);
    connect(service_, &AccessListService::writeCompleted,
            this, &AccessListPage::handleWriteCompleted);
    connect(service_, &AccessListService::writeFailed,
            this, &AccessListPage::handleWriteFailed);

    setStyleSheet(QStringLiteral(R"(
        QWidget#accessListPage { background: #F4F6F8; }
        QLabel#accessListTitleLabel {
            color: #1F2933;
            font-size: 24px;
            font-weight: bold;
        }
        QWidget#accessListFilterPanel {
            background: #FFFFFF;
            border: 1px solid #D7DEE5;
            border-radius: 4px;
        }
        QTabBar#accessListTabs::tab {
            min-width: 128px;
            min-height: 34px;
            background: #E6EDF3;
            color: #354656;
            border: 1px solid #CBD4DD;
            padding: 0 16px;
        }
        QTabBar#accessListTabs::tab:selected {
            background: #1F5F99;
            color: #FFFFFF;
        }
        QLabel#accessListLoadingLabel,
        QLabel#accessListStatusLabel { color: #1F5F99; }
        QLabel#accessListErrorLabel { color: #B42318; }
        QPushButton#accessListSearchButton,
        QPushButton#accessListRefreshButton,
        QPushButton#accessListCreateButton,
        QPushButton#accessListPreviousButton,
        QPushButton#accessListNextButton {
            color: #FFFFFF;
            background: #1F5F99;
            border: 0;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton#accessListDeleteButton {
            color: #FFFFFF;
            background: #B42318;
            border: 0;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton#accessListResetButton {
            color: #1F2933;
            background: #E6EDF3;
            border: 1px solid #CBD4DD;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton:disabled { background: #AEB9C3; color: #F4F6F8; }
        QTableView#accessListTable {
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

    loadingLabel_->setText(service_->isQueryLoading()
                               ? QStringLiteral("正在查询...")
                               : QString());
    updatePagination();
    updateWriteActions();
}

AccessListPage::~AccessListPage()
{
    service_->cancelQuery();
}

AccessListService *AccessListPage::service() const noexcept
{
    return service_;
}

AccessListTableModel *AccessListPage::tableModel() const noexcept
{
    return tableModel_;
}

AccessListType AccessListPage::currentListType() const noexcept
{
    return tabs_->currentIndex() == 1 ? AccessListType::Black : AccessListType::White;
}

AccessListCreateDialog *AccessListPage::activeCreateDialog() const noexcept
{
    return activeCreateDialog_.data();
}

void AccessListPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    pageActive_ = true;
    if (!refreshOnShow_) {
        return;
    }
    refreshOnShow_ = false;
    errorLabel_->clear();
    statusLabel_->clear();
    if (service_->lastSubmittedFilter()) {
        service_->refresh();
    } else {
        service_->search(currentListType(), {});
    }
}

void AccessListPage::hideEvent(QHideEvent *event)
{
    pageActive_ = false;
    service_->cancelQuery();
    refreshOnShow_ = true;
    QWidget::hideEvent(event);
}

void AccessListPage::changeListType(int tabIndex)
{
    if (tabIndex < 0 || tabIndex > 1) {
        return;
    }
    keywordEdit_->clear();
    tableModel_->setListType(currentListType());
    errorLabel_->clear();
    statusLabel_->clear();
    pendingDeleteRefreshFilter_.reset();
    service_->search(currentListType(), {});
}

void AccessListPage::search()
{
    errorLabel_->clear();
    statusLabel_->clear();
    pendingDeleteRefreshFilter_.reset();
    service_->search(currentListType(), keywordEdit_->text());
}

void AccessListPage::reset()
{
    keywordEdit_->clear();
    search();
}

void AccessListPage::refresh()
{
    errorLabel_->clear();
    statusLabel_->clear();
    pendingDeleteRefreshFilter_.reset();
    if (service_->lastSubmittedFilter()) {
        service_->refresh();
    } else {
        service_->search(currentListType(), {});
    }
}

void AccessListPage::openCreateDialog()
{
    if (service_->isWriteBusy()) {
        showError(QStringLiteral("已有名单写操作正在进行。"));
        return;
    }
    if (activeCreateDialog_) {
        activeCreateDialog_->raise();
        activeCreateDialog_->activateWindow();
        return;
    }

    auto *dialog = new AccessListCreateDialog(currentListType(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    activeCreateDialog_ = dialog;
    connect(dialog, &AccessListCreateDialog::submitRequested, this,
            [this, dialog](const QString &plateNumber, const QString &remark) {
                errorLabel_->clear();
                statusLabel_->clear();
                if (service_->create(dialog->listType(), plateNumber, remark)) {
                    dialog->setSubmitting(true);
                }
            });
    connect(dialog, &QObject::destroyed, this, [this] {
        activeCreateDialog_.clear();
    });
    dialog->show();
}

void AccessListPage::deleteSelected()
{
    if (service_->isWriteBusy()) {
        showError(QStringLiteral("已有名单写操作正在进行。"));
        return;
    }
    const QModelIndexList rows = tableView_->selectionModel()->selectedRows();
    if (rows.size() != 1) {
        showError(QStringLiteral("请先选择一条名单记录。"));
        return;
    }
    const auto record = tableModel_->recordAt(rows.constFirst().row());
    if (!record || !deleteConfirmer_(this, record->listType(), record->plateNumber())) {
        return;
    }
    errorLabel_->clear();
    statusLabel_->clear();
    service_->remove(*record);
}

void AccessListPage::handleResultsChanged()
{
    updatePagination();
    updateWriteActions();
    if (!pendingDeleteRefreshFilter_) {
        return;
    }
    const auto expected = *pendingDeleteRefreshFilter_;
    pendingDeleteRefreshFilter_.reset();
    const auto &submitted = service_->lastSubmittedFilter();
    const auto &page = service_->lastSuccessfulPage();
    if (submitted && *submitted == expected && page && page->page() == expected.page()
        && page->items().isEmpty() && page->page() > 1) {
        service_->previousPage();
    }
}

void AccessListPage::handleWriteBusyChanged(bool busy)
{
    if (activeCreateDialog_) {
        activeCreateDialog_->setSubmitting(busy);
    }
    updateWriteActions();
}

void AccessListPage::handleWriteCompleted(const AccessListWriteResult &result)
{
    if (result.operation() == AccessListWriteOperation::Create) {
        switch (result.disposition()) {
        case AccessListWriteDisposition::Applied:
        case AccessListWriteDisposition::AlreadyAbsent:
            if (activeCreateDialog_) {
                activeCreateDialog_->accept();
            }
            showStatus(QStringLiteral("新增已生效。"));
            break;
        case AccessListWriteDisposition::NotApplied:
            if (activeCreateDialog_) {
                const QString message = result.serverRecord()
                                            ? QStringLiteral("该车牌已存在于%1，新增未生效。")
                                                  .arg(listTypeName(
                                                      result.serverRecord()->listType()))
                                            : QStringLiteral("新增未生效。");
                activeCreateDialog_->showError(message);
            }
            break;
        case AccessListWriteDisposition::Unknown:
            if (activeCreateDialog_) {
                activeCreateDialog_->showError(QStringLiteral("操作结果无法确认。"));
            }
            break;
        }
        return;
    }

    switch (result.disposition()) {
    case AccessListWriteDisposition::Applied:
        pendingDeleteRefreshFilter_ = service_->lastSubmittedFilter();
        showStatus(QStringLiteral("删除已生效。"));
        break;
    case AccessListWriteDisposition::AlreadyAbsent:
        pendingDeleteRefreshFilter_ = service_->lastSubmittedFilter();
        showStatus(QStringLiteral("记录已不存在，已刷新。"));
        break;
    case AccessListWriteDisposition::NotApplied:
        showError(QStringLiteral("删除未生效。"));
        break;
    case AccessListWriteDisposition::Unknown:
        showError(QStringLiteral("操作结果无法确认。"));
        break;
    }
}

void AccessListPage::handleWriteFailed(const AccessListServiceError &error)
{
    const QString message = serviceErrorMessage(error);
    if (activeCreateDialog_) {
        activeCreateDialog_->setSubmitting(false);
        activeCreateDialog_->showError(message);
    } else {
        showError(message);
    }
}

void AccessListPage::updatePagination()
{
    const auto &page = service_->lastSuccessfulPage();
    totalLabel_->setText(QStringLiteral("共 %1 条").arg(page ? page->total() : 0));
    pageLabel_->setText(QStringLiteral("第 %1 页").arg(page ? page->page() : 1));
    previousButton_->setEnabled(service_->canGoPrevious());
    nextButton_->setEnabled(service_->canGoNext());
}

void AccessListPage::updateWriteActions()
{
    const bool busy = service_->isWriteBusy();
    createButton_->setEnabled(!busy);
    deleteButton_->setEnabled(!busy
                              && tableView_->selectionModel()->selectedRows().size() == 1);
}

void AccessListPage::showError(const QString &message)
{
    statusLabel_->clear();
    errorLabel_->setText(message);
}

void AccessListPage::showStatus(const QString &message)
{
    errorLabel_->clear();
    statusLabel_->setText(message);
}
