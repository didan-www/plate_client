#include "app/AppController.h"

#include "domain/RequestContext.h"
#include "infrastructure/http/AuthApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/windows/LoginWindow.h"
#include "ui/windows/MainWindow.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
QByteArray envelope(const QJsonValue &data)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("async-request")},
                             {QStringLiteral("data"), data},
                         })
        .toJson(QJsonDocument::Compact);
}

QJsonObject sessionData()
{
    const QString expiry = QDateTime::currentDateTime()
                               .addSecs(3600)
                               .toOffsetFromUtc(8 * 3600)
                               .toString(Qt::ISODate);
    return QJsonObject {
        {QStringLiteral("displayName"), QStringLiteral("测试操作员")},
        {QStringLiteral("accessToken"), QStringLiteral("async-token")},
        {QStringLiteral("expiresAt"), expiry},
        {QStringLiteral("mqtt"),
         QJsonObject {{QStringLiteral("host"), QStringLiteral("broker.test")},
                      {QStringLiteral("port"), 8883},
                      {QStringLiteral("tls"), true},
                      {QStringLiteral("username"), QStringLiteral("mqtt-user")},
                      {QStringLiteral("password"), QStringLiteral("mqtt-secret")},
                      {QStringLiteral("expiresAt"), expiry},
                      {QStringLiteral("eventTopic"), QStringLiteral("plate/events")}}},
    };
}

MockHttpResponse response(QByteArray body, int delayMs = 0)
{
    MockHttpResponse result;
    result.headers.append({QByteArrayLiteral("Content-Type"),
                           QByteArrayLiteral("application/json")});
    result.chunks.append(std::move(body));
    result.initialDelayMs = delayMs;
    return result;
}
} // namespace

class AsyncDestructionTest final : public QObject
{
    Q_OBJECT

private slots:
    void delayedHttpAndQueuedUiCallbacksAreRejectedAfterLogout();
};

void AsyncDestructionTest::delayedHttpAndQueuedUiCallbacksAreRejectedAfterLogout()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(response(envelope(sessionData())));
    server.enqueueResponse(response(envelope(QJsonObject {}), 250));
    server.enqueueResponse(response(envelope(QJsonValue::Null), 250));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AuthApiClient auth(server.url(), QUuid::createUuid(), &transport, &sessions);
    AppController::TerminationActions actions;
    actions.cancelAsync = [&transport] { transport.cancelAll(); };
    AppController controller(&auth, &sessions, &settings,
                             [](const QString &displayName) {
                                 return new MainWindow(displayName);
                             },
                             std::move(actions));
    controller.start();
    LoginWindow *login = controller.loginWindow();
    login->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->setText(
        QStringLiteral("operator"));
    login->findChild<QLineEdit *>(QStringLiteral("passwordEdit"))->setText(
        QStringLiteral("secret"));
    login->findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    QTRY_VERIFY(controller.authenticatedWindow());
    auto *window = qobject_cast<MainWindow *>(controller.authenticatedWindow());
    QVERIFY(window);
    QPointer<MainWindow> guardedWindow(window);
    const quint64 generation = sessions.currentGeneration();

    HttpRequest request(HttpMethod::Get,
                        server.url(QStringLiteral("/api/v1/delayed")),
                        HttpRequestKind::Query,
                        HttpResponseMode::JsonMemory,
                        RequestContext(generation, 1, QStringLiteral("test.delayed")),
                        {},
                        QStringLiteral("async-token"));
    HttpRequestHandle *handle = transport.send(std::move(request));
    int uiUpdateCount = 0;
    connect(handle, &HttpRequestHandle::succeeded, window,
            [&sessions, &uiUpdateCount](const HttpResponse &httpResponse) {
                if (sessions.isCurrent(httpResponse.context())) {
                    ++uiUpdateCount;
                }
            });
    connect(handle, &HttpRequestHandle::failed, window,
            [&sessions, &uiUpdateCount](const HttpError &, const HttpResponse &httpResponse) {
                if (sessions.isCurrent(httpResponse.context())) {
                    ++uiUpdateCount;
                }
            });
    QTimer::singleShot(100, window, [&uiUpdateCount] { ++uiUpdateCount; });
    QTRY_COMPARE(server.requests().size(), 2);

    QElapsedTimer elapsed;
    elapsed.start();
    window->logoutRequested();

    QVERIFY(elapsed.elapsed() < 100);
    QVERIFY(controller.loginWindow());
    QVERIFY(controller.loginWindow()->isVisible());
    QTRY_VERIFY(guardedWindow.isNull());
    QTest::qWait(350);
    QCOMPARE(uiUpdateCount, 0);
    QTRY_COMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().at(2).target, QByteArrayLiteral("/api/v1/auth/logout"));
}

QTEST_MAIN(AsyncDestructionTest)

#include "tst_async_destruction.moc"
