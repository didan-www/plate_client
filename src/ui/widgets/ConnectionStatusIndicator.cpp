#include "ui/widgets/ConnectionStatusIndicator.h"

#include <QHBoxLayout>
#include <QLabel>

namespace {
QString stateColor(ConnectionStatusIndicator::VisualState state)
{
    switch (state) {
    case ConnectionStatusIndicator::VisualState::Busy:
        return QStringLiteral("#C77D16");
    case ConnectionStatusIndicator::VisualState::Online:
        return QStringLiteral("#287D4A");
    case ConnectionStatusIndicator::VisualState::Error:
        return QStringLiteral("#B42318");
    case ConnectionStatusIndicator::VisualState::Neutral:
        return QStringLiteral("#7A8895");
    }
    return QStringLiteral("#7A8895");
}
} // namespace

ConnectionStatusIndicator::ConnectionStatusIndicator(const QString &title, QWidget *parent)
    : QWidget(parent)
    , dotLabel_(new QLabel(this))
    , statusLabel_(new QLabel(this))
{
    setFixedHeight(28);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("statusTitleLabel"));
    dotLabel_->setObjectName(QStringLiteral("statusDotLabel"));
    dotLabel_->setFixedSize(10, 10);
    statusLabel_->setObjectName(QStringLiteral("statusValueLabel"));
    statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(titleLabel);
    layout->addStretch();
    layout->addWidget(dotLabel_);
    layout->addWidget(statusLabel_);

    setStatus(VisualState::Neutral, QStringLiteral("未连接"));
}

void ConnectionStatusIndicator::setStatus(VisualState state, const QString &text)
{
    state_ = state;
    statusLabel_->setText(text);
    const QString color = stateColor(state);
    dotLabel_->setStyleSheet(
        QStringLiteral("background-color: %1; border-radius: 5px;").arg(color));
    statusLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    setAccessibleDescription(text);
}

void ConnectionStatusIndicator::setHttpConnectivityState(HttpConnectivityState state)
{
    switch (state) {
    case HttpConnectivityState::Detecting:
        setStatus(VisualState::Busy, QStringLiteral("检测中"));
        break;
    case HttpConnectivityState::Online:
        setStatus(VisualState::Online, QStringLiteral("在线"));
        break;
    case HttpConnectivityState::Abnormal:
        setStatus(VisualState::Error, QStringLiteral("异常"));
        break;
    }
}

ConnectionStatusIndicator::VisualState ConnectionStatusIndicator::state() const noexcept
{
    return state_;
}

QString ConnectionStatusIndicator::statusText() const
{
    return statusLabel_->text();
}
