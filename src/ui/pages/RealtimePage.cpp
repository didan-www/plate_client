#include "ui/pages/RealtimePage.h"

#include "domain/ImageLoadResult.h"
#include "services/ImageLoadService.h"
#include "services/RealtimeRecognitionStore.h"
#include "ui/models/RecognitionTableModel.h"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>

namespace {
QLabel *createValueLabel(const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("--"), parent);
    label->setObjectName(objectName);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setMinimumHeight(28);
    return label;
}

QString failureText(const RecognitionSnapshot &snapshot)
{
    if (snapshot.status() != RecognitionStatus::Failed) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1：%2")
        .arg(snapshot.errorCode().value_or(QStringLiteral("UNKNOWN")),
             snapshot.errorMessage().value_or(QStringLiteral("识别失败")));
}
} // namespace

RealtimePage::RealtimePage(RealtimeRecognitionStore *store,
                           ImageLoadService *imageLoadService,
                           QWidget *parent)
    : QWidget(parent)
    , store_(store)
    , imageLoadService_(imageLoadService)
    , tableModel_(new RecognitionTableModel(store, this))
    , imageLabel_(new QLabel(this))
    , statusLabel_(createValueLabel(QStringLiteral("currentStatusLabel"), this))
    , plateLabel_(createValueLabel(QStringLiteral("currentPlateLabel"), this))
    , errorLabel_(createValueLabel(QStringLiteral("currentErrorLabel"), this))
    , capturedAtLabel_(createValueLabel(QStringLiteral("currentCapturedAtLabel"), this))
    , durationLabel_(createValueLabel(QStringLiteral("currentDurationLabel"), this))
    , retryButton_(new QPushButton(QStringLiteral("重试图片"), this))
    , tableView_(new QTableView(this))
{
    Q_ASSERT(store_);
    Q_ASSERT(imageLoadService_);
    setObjectName(QStringLiteral("realtimePage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *title = new QLabel(QStringLiteral("车牌识别系统"), this);
    title->setObjectName(QStringLiteral("realtimeTitleLabel"));
    title->setAlignment(Qt::AlignCenter);
    title->setFixedHeight(44);

    imageLabel_->setObjectName(QStringLiteral("currentImageLabel"));
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(360, 220);
    imageLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    retryButton_->setObjectName(QStringLiteral("imageRetryButton"));
    retryButton_->setFixedSize(112, 36);
    retryButton_->setVisible(false);

    auto *imagePanel = new QWidget(this);
    imagePanel->setObjectName(QStringLiteral("currentImagePanel"));
    auto *imageLayout = new QVBoxLayout(imagePanel);
    imageLayout->setContentsMargins(12, 12, 12, 12);
    imageLayout->setSpacing(10);
    imageLayout->addWidget(imageLabel_, 1);
    imageLayout->addWidget(retryButton_, 0, Qt::AlignHCenter);

    auto *detailsPanel = new QWidget(this);
    detailsPanel->setObjectName(QStringLiteral("currentDetailsPanel"));
    detailsPanel->setMinimumWidth(260);
    auto *detailsLayout = new QGridLayout(detailsPanel);
    detailsLayout->setContentsMargins(22, 18, 22, 18);
    detailsLayout->setHorizontalSpacing(16);
    detailsLayout->setVerticalSpacing(10);
    const QStringList captions {
        QStringLiteral("识别状态"),
        QStringLiteral("车牌结果"),
        QStringLiteral("错误信息"),
        QStringLiteral("识别时间"),
        QStringLiteral("识别耗时"),
    };
    const QList<QLabel *> values {
        statusLabel_, plateLabel_, errorLabel_, capturedAtLabel_, durationLabel_,
    };
    for (int row = 0; row < captions.size(); ++row) {
        auto *caption = new QLabel(captions.at(row), detailsPanel);
        caption->setObjectName(QStringLiteral("currentDetailCaption"));
        caption->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        detailsLayout->addWidget(caption, row, 0);
        detailsLayout->addWidget(values.at(row), row, 1);
    }
    detailsLayout->setColumnStretch(1, 1);

    auto *currentLayout = new QHBoxLayout;
    currentLayout->setContentsMargins(0, 0, 0, 0);
    currentLayout->setSpacing(18);
    currentLayout->addWidget(imagePanel, 3);
    currentLayout->addWidget(detailsPanel, 2);

    auto *tableTitle = new QLabel(QStringLiteral("实时识别记录"), this);
    tableTitle->setObjectName(QStringLiteral("realtimeTableTitle"));
    tableTitle->setFixedHeight(30);

    tableView_->setObjectName(QStringLiteral("realtimeTable"));
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
    tableView_->horizontalHeader()->setMinimumSectionSize(96);
    tableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tableView_->horizontalHeader()->setSectionResizeMode(
        RecognitionTableModel::SequenceColumn, QHeaderView::ResizeToContents);
    tableView_->setMinimumHeight(180);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 20, 28, 24);
    root->setSpacing(14);
    root->addWidget(title);
    root->addLayout(currentLayout, 3);
    root->addWidget(tableTitle);
    root->addWidget(tableView_, 2);

    connect(store_, &RealtimeRecognitionStore::currentChanged,
            this, &RealtimePage::refreshCurrent);
    connect(imageLoadService_, &ImageLoadService::resultReady,
            this, &RealtimePage::applyImageResult);
    connect(retryButton_, &QPushButton::clicked, this, &RealtimePage::retryImage);

    setStyleSheet(QStringLiteral(R"(
        QWidget#realtimePage { background: #F4F6F8; }
        QLabel#realtimeTitleLabel {
            color: #1F2933;
            font-size: 26px;
            font-weight: bold;
        }
        QWidget#currentImagePanel,
        QWidget#currentDetailsPanel {
            background: #FFFFFF;
            border: 1px solid #D7DEE5;
            border-radius: 4px;
        }
        QLabel#currentImageLabel {
            background: #E8EDF2;
            color: #667482;
            border: 1px solid #CBD4DD;
            font-size: 15px;
        }
        QLabel#currentStatusLabel {
            color: #1F5F99;
            font-size: 18px;
            font-weight: bold;
        }
        QLabel#currentDetailCaption {
            color: #667482;
            font-weight: bold;
        }
        QLabel#currentPlateLabel,
        QLabel#currentErrorLabel,
        QLabel#currentCapturedAtLabel,
        QLabel#currentDurationLabel { color: #1F2933; }
        QLabel#realtimeTableTitle {
            color: #1F2933;
            font-size: 17px;
            font-weight: bold;
        }
        QPushButton#imageRetryButton {
            color: #FFFFFF;
            background: #1F5F99;
            border: 0;
            border-radius: 4px;
        }
        QPushButton#imageRetryButton:hover { background: #174D7D; }
        QTableView#realtimeTable {
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

    refreshCurrent();
}

RealtimePage::~RealtimePage()
{
    imageLoadService_->cancel();
}

RealtimeRecognitionStore *RealtimePage::store() const noexcept
{
    return store_;
}

ImageLoadService *RealtimePage::imageLoadService() const noexcept
{
    return imageLoadService_;
}

RecognitionTableModel *RealtimePage::tableModel() const noexcept
{
    return tableModel_;
}

void RealtimePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateScaledPixmap();
}

void RealtimePage::refreshCurrent()
{
    const auto &current = store_->current();
    if (!current) {
        if (!currentRecognitionId_.isEmpty()) {
            imageLoadService_->cancel();
        }
        currentRecognitionId_.clear();
        sourcePixmap_ = {};
        clearImage(QStringLiteral("暂无车辆图片"));
        statusLabel_->setText(QStringLiteral("未启动识别"));
        plateLabel_->setText(QStringLiteral("--"));
        errorLabel_->setText(QStringLiteral("--"));
        capturedAtLabel_->setText(QStringLiteral("--"));
        durationLabel_->setText(QStringLiteral("--"));
        return;
    }

    const bool newCurrent = currentRecognitionId_ != current->recognitionId();
    if (newCurrent) {
        currentRecognitionId_ = current->recognitionId();
        sourcePixmap_ = {};
        clearImage(QStringLiteral("图片加载中..."));
        imageLoadService_->load(currentRecognitionId_, store_->activeSessionGeneration());
    }

    statusLabel_->setText(current->status() == RecognitionStatus::Processing
                              ? QStringLiteral("识别中")
                              : QStringLiteral("已识别完成"));
    plateLabel_->setText(current->plateNumber().value_or(QStringLiteral("--")));
    errorLabel_->setText(failureText(*current));
    errorLabel_->setStyleSheet(current->status() == RecognitionStatus::Failed
                                   ? QStringLiteral("color: #B42318;") : QString());
    capturedAtLabel_->setText(current->capturedAt().toString(Qt::ISODate));
    durationLabel_->setText(current->durationMs()
                                ? QStringLiteral("%1 ms").arg(*current->durationMs())
                                : QStringLiteral("--"));
}

void RealtimePage::applyImageResult(const ImageLoadResult &result)
{
    const auto &current = store_->current();
    if (!current || !result.context().recognitionId()
        || *result.context().recognitionId() != current->recognitionId()
        || currentRecognitionId_ != current->recognitionId()) {
        return;
    }

    Q_ASSERT(QThread::currentThread() == thread());
    if (result.isSuccess()) {
        sourcePixmap_ = QPixmap::fromImage(result.image());
        retryButton_->setVisible(false);
        updateScaledPixmap();
        return;
    }

    sourcePixmap_ = {};
    clearImage(QStringLiteral("图片加载失败"));
    retryButton_->setVisible(result.manualRetryAvailable());
}

void RealtimePage::retryImage()
{
    if (imageLoadService_->retry()) {
        sourcePixmap_ = {};
        clearImage(QStringLiteral("图片加载中..."));
    }
}

void RealtimePage::clearImage(const QString &placeholder)
{
    imageLabel_->clear();
    imageLabel_->setText(placeholder);
    retryButton_->setVisible(false);
}

void RealtimePage::updateScaledPixmap()
{
    if (sourcePixmap_.isNull() || imageLabel_->size().isEmpty()) {
        return;
    }
    imageLabel_->setText({});
    imageLabel_->setPixmap(sourcePixmap_.scaled(imageLabel_->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
}
