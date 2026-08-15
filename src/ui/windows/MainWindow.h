#pragma once

#include <QMainWindow>

class QByteArray;
class NavigationSidebar;
class QShowEvent;
class QStackedWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &displayName, QWidget *parent = nullptr);

    [[nodiscard]] NavigationSidebar *sidebar() const noexcept;
    [[nodiscard]] QStackedWidget *pageStack() const noexcept;

signals:
    void logoutRequested();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void showEvent(QShowEvent *event) override;

private:
    NavigationSidebar *sidebar_;
    QStackedWidget *pageStack_;
    bool firstShow_ = true;
};
