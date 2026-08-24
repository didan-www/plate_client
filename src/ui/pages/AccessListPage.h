#pragma once

#include "services/AccessListService.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <optional>

class AccessListCreateDialog;
class AccessListServiceError;
class AccessListTableModel;
class AccessListWriteResult;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTabBar;
class QTableView;

class AccessListPage final : public QWidget
{
    Q_OBJECT

public:
    using DeleteConfirmer =
        std::function<bool(QWidget *parent,
                           AccessListType listType,
                           const QString &plateNumber)>;

    explicit AccessListPage(AccessListService *service,
                            QWidget *parent = nullptr);
    AccessListPage(AccessListService *service,
                   DeleteConfirmer deleteConfirmer,
                   QWidget *parent = nullptr);
    ~AccessListPage() override;

    [[nodiscard]] AccessListService *service() const noexcept;
    [[nodiscard]] AccessListTableModel *tableModel() const noexcept;
    [[nodiscard]] AccessListType currentListType() const noexcept;
    [[nodiscard]] AccessListCreateDialog *activeCreateDialog() const noexcept;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void changeListType(int tabIndex);
    void search();
    void reset();
    void refresh();
    void openCreateDialog();
    void deleteSelected();
    void handleResultsChanged();
    void handleWriteBusyChanged(bool busy);
    void handleWriteCompleted(const AccessListWriteResult &result);
    void handleWriteFailed(const AccessListServiceError &error);
    void updatePagination();
    void updateWriteActions();
    void showError(const QString &message);
    void showStatus(const QString &message);

    AccessListService *service_;
    DeleteConfirmer deleteConfirmer_;
    AccessListTableModel *tableModel_;
    QTabBar *tabs_;
    QLineEdit *keywordEdit_;
    QPushButton *searchButton_;
    QPushButton *resetButton_;
    QPushButton *refreshButton_;
    QPushButton *createButton_;
    QPushButton *deleteButton_;
    QPushButton *previousButton_;
    QPushButton *nextButton_;
    QLabel *loadingLabel_;
    QLabel *errorLabel_;
    QLabel *statusLabel_;
    QLabel *totalLabel_;
    QLabel *pageLabel_;
    QTableView *tableView_;
    QPointer<AccessListCreateDialog> activeCreateDialog_;
    std::optional<AccessListFilter> pendingDeleteRefreshFilter_;
    bool refreshOnShow_ = true;
    bool pageActive_ = false;
};
