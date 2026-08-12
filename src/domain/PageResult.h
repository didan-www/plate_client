#pragma once

#include <QList>
#include <QtTypes>

#include <utility>

template<typename T>
class PageResult final
{
public:
    PageResult(QList<T> items, int page, int pageSize, qint64 total)
        : items_(std::move(items))
        , page_(page)
        , pageSize_(pageSize)
        , total_(total)
    {
    }

    [[nodiscard]] const QList<T> &items() const noexcept { return items_; }
    [[nodiscard]] int page() const noexcept { return page_; }
    [[nodiscard]] int pageSize() const noexcept { return pageSize_; }
    [[nodiscard]] qint64 total() const noexcept { return total_; }

private:
    QList<T> items_;
    int page_;
    int pageSize_;
    qint64 total_;
};
