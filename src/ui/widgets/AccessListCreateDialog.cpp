#include "ui/widgets/AccessListCreateDialog.h"

#include "services/AccessListService.h"

#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString listTypeName(AccessListType listType)
{
    return listType == AccessListType::White ? QStringLiteral("白名单")
                                             : QStringLiteral("黑名单");
}
} // namespace

AccessListCreateDialog::AccessListCreateDialog(AccessListType listType, QWidget *parent)
    : QDialog(parent)
    , listType_(listType)
    , plateEdit_(new QLineEdit(this))
    , remarkEdit_(new QPlainTextEdit(this))
    , errorLabel_(new QLabel(this))
    , submitButton_(new QPushButton(QStringLiteral("新增"), this))
    , cancelButton_(new QPushButton(QStringLiteral("取消"), this))
{
    setObjectName(QStringLiteral("accessListCreateDialog"));
    setWindowTitle(QStringLiteral("新增%1记录").arg(listTypeName(listType_)));
    setModal(true);
    setMinimumWidth(440);

    auto *typeLabel = new QLabel(listTypeName(listType_), this);
    typeLabel->setObjectName(QStringLiteral("accessListCreateTypeLabel"));
    plateEdit_->setObjectName(QStringLiteral("accessListCreatePlateEdit"));
    plateEdit_->setPlaceholderText(QStringLiteral("请输入车牌号"));
    plateEdit_->setClearButtonEnabled(true);
    remarkEdit_->setObjectName(QStringLiteral("accessListCreateRemarkEdit"));
    remarkEdit_->setPlaceholderText(QStringLiteral("可选，最多 200 个 Unicode 字符"));
    remarkEdit_->setFixedHeight(96);
    errorLabel_->setObjectName(QStringLiteral("accessListCreateErrorLabel"));
    errorLabel_->setMinimumHeight(22);
    errorLabel_->setWordWrap(true);
    submitButton_->setObjectName(QStringLiteral("accessListCreateSubmitButton"));
    cancelButton_->setObjectName(QStringLiteral("accessListCreateCancelButton"));
    submitButton_->setDefault(true);
    submitButton_->setFixedWidth(96);
    cancelButton_->setFixedWidth(96);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(14);
    form->addRow(QStringLiteral("名单类型"), typeLabel);
    form->addRow(QStringLiteral("车牌号"), plateEdit_);
    form->addRow(QStringLiteral("备注"), remarkEdit_);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(cancelButton_);
    actions->addWidget(submitButton_);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(12);
    root->addLayout(form);
    root->addWidget(errorLabel_);
    root->addLayout(actions);

    connect(submitButton_, &QPushButton::clicked,
            this, &AccessListCreateDialog::submit);
    connect(cancelButton_, &QPushButton::clicked,
            this, &AccessListCreateDialog::reject);
    connect(plateEdit_, &QLineEdit::returnPressed,
            this, &AccessListCreateDialog::submit);

    setStyleSheet(QStringLiteral(R"(
        QDialog#accessListCreateDialog { background: #F4F6F8; }
        QLabel#accessListCreateTypeLabel { color: #1F5F99; font-weight: bold; }
        QLabel#accessListCreateErrorLabel { color: #B42318; }
        QPushButton#accessListCreateSubmitButton {
            color: #FFFFFF;
            background: #1F5F99;
            border: 0;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton#accessListCreateCancelButton {
            color: #1F2933;
            background: #E6EDF3;
            border: 1px solid #CBD4DD;
            border-radius: 4px;
            min-height: 34px;
        }
        QPushButton:disabled { background: #AEB9C3; color: #F4F6F8; }
    )"));
}

AccessListType AccessListCreateDialog::listType() const noexcept
{
    return listType_;
}

QString AccessListCreateDialog::plateNumber() const
{
    return plateEdit_->text();
}

QString AccessListCreateDialog::remark() const
{
    return remarkEdit_->toPlainText();
}

bool AccessListCreateDialog::isSubmitting() const noexcept
{
    return submitting_;
}

void AccessListCreateDialog::setSubmitting(bool submitting)
{
    if (submitting_ == submitting) {
        return;
    }
    submitting_ = submitting;
    plateEdit_->setEnabled(!submitting_);
    remarkEdit_->setEnabled(!submitting_);
    submitButton_->setEnabled(!submitting_);
    cancelButton_->setEnabled(!submitting_);
    submitButton_->setText(submitting_ ? QStringLiteral("提交中...")
                                       : QStringLiteral("新增"));
}

void AccessListCreateDialog::showError(const QString &message)
{
    errorLabel_->setText(message);
}

void AccessListCreateDialog::reject()
{
    if (!submitting_) {
        QDialog::reject();
    }
}

void AccessListCreateDialog::closeEvent(QCloseEvent *event)
{
    if (submitting_) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void AccessListCreateDialog::submit()
{
    if (submitting_) {
        return;
    }
    errorLabel_->clear();
    if (!AccessListService::normalizePlateNumber(plateEdit_->text())) {
        showError(QStringLiteral("车牌号需为 1 至 16 个 Unicode 字符，且不能包含内部空白。"));
        return;
    }
    if (!AccessListService::isValidRemark(remarkEdit_->toPlainText())) {
        showError(QStringLiteral("备注不能超过 200 个 Unicode 字符。"));
        return;
    }
    emit submitRequested(plateEdit_->text(), remarkEdit_->toPlainText());
}
