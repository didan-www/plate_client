#pragma once

#include "domain/AccessListRecord.h"

#include <QDialog>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class AccessListCreateDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AccessListCreateDialog(AccessListType listType,
                                    QWidget *parent = nullptr);

    [[nodiscard]] AccessListType listType() const noexcept;
    [[nodiscard]] QString plateNumber() const;
    [[nodiscard]] QString remark() const;
    [[nodiscard]] bool isSubmitting() const noexcept;

    void setSubmitting(bool submitting);
    void showError(const QString &message);

signals:
    void submitRequested(const QString &plateNumber, const QString &remark);

public slots:
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void submit();

    AccessListType listType_;
    QLineEdit *plateEdit_;
    QPlainTextEdit *remarkEdit_;
    QLabel *errorLabel_;
    QPushButton *submitButton_;
    QPushButton *cancelButton_;
    bool submitting_ = false;
};
