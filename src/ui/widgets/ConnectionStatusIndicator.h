#pragma once

#include "services/HttpConnectivityState.h"

#include <QWidget>

class QLabel;

class ConnectionStatusIndicator final : public QWidget
{
    Q_OBJECT

public:
    enum class VisualState {
        Neutral,
        Busy,
        Online,
        Error,
    };
    Q_ENUM(VisualState)

    explicit ConnectionStatusIndicator(const QString &title, QWidget *parent = nullptr);

    void setStatus(VisualState state, const QString &text);
    void setHttpConnectivityState(HttpConnectivityState state);
    [[nodiscard]] VisualState state() const noexcept;
    [[nodiscard]] QString statusText() const;

private:
    QLabel *dotLabel_;
    QLabel *statusLabel_;
    VisualState state_ = VisualState::Neutral;
};
