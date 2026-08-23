#pragma once

#include <QMainWindow>

class QByteArray;
class ImageLoadService;
class HistoryPage;
class HistoryService;
class NavigationSidebar;
class QShowEvent;
class QStackedWidget;
class RealtimePage;
class RealtimeRecognitionStore;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &displayName, QWidget *parent = nullptr);
    MainWindow(const QString &displayName,
               RealtimeRecognitionStore *realtimeStore,
               ImageLoadService *imageLoadService,
               QWidget *parent = nullptr);
    MainWindow(const QString &displayName,
               RealtimeRecognitionStore *realtimeStore,
               ImageLoadService *imageLoadService,
               HistoryService *historyService,
               QWidget *parent = nullptr);

    [[nodiscard]] NavigationSidebar *sidebar() const noexcept;
    [[nodiscard]] QStackedWidget *pageStack() const noexcept;
    [[nodiscard]] RealtimePage *realtimePage() const noexcept;
    [[nodiscard]] HistoryPage *historyPage() const noexcept;

signals:
    void logoutRequested();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void showEvent(QShowEvent *event) override;

private:
    NavigationSidebar *sidebar_;
    QStackedWidget *pageStack_;
    RealtimePage *realtimePage_ = nullptr;
    HistoryPage *historyPage_ = nullptr;
    bool firstShow_ = true;
};
