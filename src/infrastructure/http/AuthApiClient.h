#pragma once

#include "domain/SessionContext.h"
#include "infrastructure/http/AuthError.h"
#include "services/SessionService.h"

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QUuid>

#include <optional>

class HttpClient;
class HttpRequestHandle;

class LoginRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit LoginRequestHandle(QObject *parent = nullptr);
    ~LoginRequestHandle() override;

    void cancel();
    [[nodiscard]] const std::optional<SessionSummary> &summary() const noexcept;

signals:
    void succeeded();
    void failed(const AuthError &error);
    void finished();

private:
    friend class AuthApiClient;
    void completeFailure(AuthError error);

    QPointer<HttpRequestHandle> transportHandle_;
    std::optional<SessionSummary> summary_;
    bool completed_ = false;
};

class LogoutRequestHandle final : public QObject
{
    Q_OBJECT

public:
    explicit LogoutRequestHandle(QObject *parent = nullptr);
    ~LogoutRequestHandle() override;

    void cancel();

signals:
    void succeeded();
    void failed(const AuthError &error);
    void finished();

private:
    friend class AuthApiClient;
    void completeFailure(AuthError error);

    QPointer<HttpRequestHandle> transportHandle_;
    bool completed_ = false;
};

class AuthApiClient final : public QObject
{
    Q_OBJECT

public:
    AuthApiClient(QUrl loginBaseUrl,
                  QUuid clientId,
                  HttpClient *httpClient,
                  SessionService *sessionService,
                  QObject *parent = nullptr);

    [[nodiscard]] LoginRequestHandle *login(const QString &username, const QString &password);
    [[nodiscard]] LogoutRequestHandle *logout(QString accessToken,
                                               quint64 sessionGeneration);

private:
    [[nodiscard]] QUrl endpoint(const QString &path) const;

    QUrl loginBaseUrl_;
    QUuid clientId_;
    HttpClient *httpClient_;
    SessionService *sessionService_;
    quint64 requestGeneration_ = 0;
    quint64 currentLoginRequestGeneration_ = 0;
};
