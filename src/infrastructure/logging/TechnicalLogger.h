#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>

#include <functional>

enum class TechnicalLogLevel
{
    Debug,
    Info,
    Warning,
    Error,
};

struct TechnicalLogEntry final
{
    TechnicalLogLevel level = TechnicalLogLevel::Info;
    QString module;
    QString errorCode;
    QString requestId;
    QString recognitionId;
    QJsonObject context;
};

class TechnicalLogger final
{
public:
    using Clock = std::function<QDateTime()>;

    TechnicalLogger();
    explicit TechnicalLogger(QString logDirectory, Clock clock = {});

    TechnicalLogger(const TechnicalLogger &) = delete;
    TechnicalLogger &operator=(const TechnicalLogger &) = delete;

    [[nodiscard]] bool write(const TechnicalLogEntry &entry) noexcept;

    [[nodiscard]] static QString defaultLogDirectory();

private:
    void cleanupExpiredLogs(const QDate &currentDate) noexcept;

    QString logDirectory_;
    Clock clock_;
    QMutex mutex_;
};
