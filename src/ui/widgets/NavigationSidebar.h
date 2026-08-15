#pragma once

#include <QWidget>

class ConnectionStatusIndicator;

class NavigationSidebar final : public QWidget
{
    Q_OBJECT

public:
    enum PageIndex {
        RealtimePage = 0,
        HistoryPage = 1,
        AccessListPage = 2,
    };
    Q_ENUM(PageIndex)

    NavigationSidebar(const QString &displayName,
                      const QString &version,
                      const QString &buildDate,
                      QWidget *parent = nullptr);

    [[nodiscard]] ConnectionStatusIndicator *httpStatusIndicator() const noexcept;
    [[nodiscard]] ConnectionStatusIndicator *mqttStatusIndicator() const noexcept;

signals:
    void pageRequested(int pageIndex);
    void logoutRequested();

private:
    ConnectionStatusIndicator *httpStatusIndicator_;
    ConnectionStatusIndicator *mqttStatusIndicator_;
};
