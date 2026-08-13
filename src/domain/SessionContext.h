#pragma once

#include <QDateTime>
#include <QString>
#include <QtTypes>

#include <utility>

class SessionContext final
{
public:
    SessionContext(QString displayName,
                   QString accessToken,
                   QDateTime expiresAt,
                   QString mqttHost,
                   quint16 mqttPort,
                   bool mqttTls,
                   QString mqttUsername,
                   QString mqttPassword,
                   QString mqttEventTopic,
                   quint64 generation = 0)
        : displayName_(std::move(displayName))
        , accessToken_(std::move(accessToken))
        , expiresAt_(std::move(expiresAt))
        , mqttHost_(std::move(mqttHost))
        , mqttPort_(mqttPort)
        , mqttTls_(mqttTls)
        , mqttUsername_(std::move(mqttUsername))
        , mqttPassword_(std::move(mqttPassword))
        , mqttEventTopic_(std::move(mqttEventTopic))
        , generation_(generation)
    {
    }

    SessionContext(const SessionContext &) = delete;
    SessionContext &operator=(const SessionContext &) = delete;

    SessionContext(SessionContext &&other) noexcept
        : displayName_(std::move(other.displayName_))
        , accessToken_(std::move(other.accessToken_))
        , expiresAt_(std::move(other.expiresAt_))
        , mqttHost_(std::move(other.mqttHost_))
        , mqttPort_(other.mqttPort_)
        , mqttTls_(other.mqttTls_)
        , mqttUsername_(std::move(other.mqttUsername_))
        , mqttPassword_(std::move(other.mqttPassword_))
        , mqttEventTopic_(std::move(other.mqttEventTopic_))
        , generation_(other.generation_)
    {
        other.clearSensitiveData();
        other.generation_ = 0;
    }

    SessionContext &operator=(SessionContext &&other) noexcept
    {
        if (this != &other) {
            clearSensitiveData();
            displayName_ = std::move(other.displayName_);
            accessToken_ = std::move(other.accessToken_);
            expiresAt_ = std::move(other.expiresAt_);
            mqttHost_ = std::move(other.mqttHost_);
            mqttPort_ = other.mqttPort_;
            mqttTls_ = other.mqttTls_;
            mqttUsername_ = std::move(other.mqttUsername_);
            mqttPassword_ = std::move(other.mqttPassword_);
            mqttEventTopic_ = std::move(other.mqttEventTopic_);
            generation_ = other.generation_;
            other.clearSensitiveData();
            other.generation_ = 0;
        }
        return *this;
    }

    ~SessionContext()
    {
        clearSensitiveData();
    }

    [[nodiscard]] const QString &displayName() const noexcept { return displayName_; }
    [[nodiscard]] const QString &accessToken() const noexcept { return accessToken_; }
    [[nodiscard]] const QDateTime &expiresAt() const noexcept { return expiresAt_; }
    [[nodiscard]] const QString &mqttHost() const noexcept { return mqttHost_; }
    [[nodiscard]] quint16 mqttPort() const noexcept { return mqttPort_; }
    [[nodiscard]] bool mqttTls() const noexcept { return mqttTls_; }
    [[nodiscard]] const QString &mqttUsername() const noexcept { return mqttUsername_; }
    [[nodiscard]] const QString &mqttPassword() const noexcept { return mqttPassword_; }
    [[nodiscard]] const QString &mqttEventTopic() const noexcept { return mqttEventTopic_; }
    [[nodiscard]] quint64 generation() const noexcept { return generation_; }

private:
    friend class SessionService;

    static void clearString(QString &value) noexcept
    {
        value.fill(QChar::Null);
        value.clear();
    }

    void clearSensitiveData() noexcept
    {
        clearString(accessToken_);
        clearString(mqttUsername_);
        clearString(mqttPassword_);
    }

    QString displayName_;
    QString accessToken_;
    QDateTime expiresAt_;
    QString mqttHost_;
    quint16 mqttPort_;
    bool mqttTls_;
    QString mqttUsername_;
    QString mqttPassword_;
    QString mqttEventTopic_;
    quint64 generation_;
};
