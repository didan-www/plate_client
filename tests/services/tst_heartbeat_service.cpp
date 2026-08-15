#include "domain/RequestContext.h"
#include "domain/SessionContext.h"
#include "infrastructure/http/HeartbeatApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/HeartbeatService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QSslError>
#include <QTimer>
#include <QtTest>

#include <chrono>

using namespace std::chrono_literals;

namespace {
QByteArray envelope(bool success = true,
                    const QString &code = QStringLiteral("OK"),
                    const QString &message = {})
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), success},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"), message},
                             {QStringLiteral("requestId"), QStringLiteral("heartbeat-request")},
                             {QStringLiteral("data"), QJsonValue::Null},
                         })
        .toJson(QJsonDocument::Compact);
}

MockHttpResponse jsonResponse(int status,
                              QByteArray body,
                              int initialDelayMs = 0)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = status >= 400 ? QByteArrayLiteral("Error")
                                           : QByteArrayLiteral("OK");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(std::move(body));
    response.initialDelayMs = initialDelayMs;
    return response;
}

MockHttpResponse disconnectedResponse()
{
    MockHttpResponse response = jsonResponse(200, QByteArrayLiteral("{"));
    response.declaredContentLength = 100;
    response.disconnectAfterChunks = 1;
    return response;
}

SessionContext session()
{
    return SessionContext(QStringLiteral("测试操作员"),
                          QStringLiteral("access-secret"),
                          QDateTime::currentDateTime().addSecs(3600),
                          QStringLiteral("broker.test"),
                          8883,
                          true,
                          QStringLiteral("mqtt-user"),
                          QStringLiteral("mqtt-secret"),
                          QStringLiteral("plate/events"));
}

HeartbeatError heartbeatErrorFrom(const QSignalSpy &spy, int index)
{
    return *static_cast<const HeartbeatError *>(spy.at(index).at(0).constData());
}

class TlsErrorReply final : public QNetworkReply
{
public:
    explicit TlsErrorReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit sslErrors({QSslError(QSslError::SelfSignedCertificate)});
        });
    }

    void abort() override
    {
        if (isFinished()) {
            return;
        }
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class TlsErrorNetworkManager final : public QNetworkAccessManager
{
protected:
    QNetworkReply *createRequest(Operation,
                                 const QNetworkRequest &request,
                                 QIODevice *) override
    {
        return new TlsErrorReply(request, this);
    }
};
} // namespace

class HeartbeatServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsImmediatelyAndPeriodicallyWithoutOverlap();
    void businessErrorsPreserveReachability();
    void countsOnlyConnectivityFailuresAndRecoversOnAnyHttpResponse();
    void countsTlsAndTimeoutFailures();
    void unauthorizedInvalidatesSessionAndStops();
    void sessionInvalidationCancelsInFlightRequest();
};

void HeartbeatServiceTest::initTestCase()
{
    qRegisterMetaType<HeartbeatError>();
    qRegisterMetaType<HttpConnectivityState>();
}

void HeartbeatServiceTest::sendsImmediatelyAndPeriodicallyWithoutOverlap()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(), 100));
    server.enqueueResponse(jsonResponse(200, envelope()));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    const QUuid clientId(QStringLiteral("12345678-1234-4abc-8def-1234567890ab"));
    HeartbeatApiClient api(server.url(), clientId, QStringLiteral("0.1.1"),
                           &transport, &sessions);
    HeartbeatService service(&api, &transport, &sessions, 40ms);

    QVERIFY(sessions.establishSession(session()));
    QCOMPARE(service.state(), HttpConnectivityState::Detecting);
    QTRY_COMPARE(server.requests().size(), 1);
    QTest::qWait(70);
    QCOMPARE(server.requests().size(), 1);
    QTRY_COMPARE(service.state(), HttpConnectivityState::Online);
    QTRY_VERIFY(server.requests().size() >= 2);

    const MockHttpRequest &request = server.requests().first();
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.target, QByteArrayLiteral("/api/v1/clients/heartbeat"));
    QCOMPARE(request.header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QStringLiteral("clientId")).toString(),
             QStringLiteral("12345678-1234-4abc-8def-1234567890ab"));
    QCOMPARE(body.value(QStringLiteral("appVersion")).toString(), QStringLiteral("0.1.1"));
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Heartbeat), 5s);
    sessions.clear();
}

void HeartbeatServiceTest::businessErrorsPreserveReachability()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(403,
                                        envelope(false,
                                                 QStringLiteral("FORBIDDEN"),
                                                 QStringLiteral("denied"))));
    server.enqueueResponse(jsonResponse(503,
                                        envelope(false,
                                                 QStringLiteral("UNAVAILABLE"),
                                                 QStringLiteral("later"))));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    HeartbeatApiClient api(server.url(), QUuid::createUuid(), QStringLiteral("0.1.1"),
                           &transport, &sessions);
    HeartbeatService service(&api, &transport, &sessions, 40ms);
    QSignalSpy failures(&service, &HeartbeatService::heartbeatFailed);

    QVERIFY(sessions.establishSession(session()));
    QTRY_COMPARE(failures.count(), 1);
    const HeartbeatError first = heartbeatErrorFrom(failures, 0);
    QCOMPARE(first.code(), HeartbeatErrorCode::ClientRejected);
    QCOMPARE(first.serverCode(), QStringLiteral("FORBIDDEN"));
    QCOMPARE(service.state(), HttpConnectivityState::Online);
    QCOMPARE(service.consecutiveFailureCount(), 0);

    QTRY_COMPARE(failures.count(), 2);
    const HeartbeatError second = heartbeatErrorFrom(failures, 1);
    QCOMPARE(second.code(), HeartbeatErrorCode::ServerError);
    QCOMPARE(second.serverCode(), QStringLiteral("UNAVAILABLE"));
    QCOMPARE(service.state(), HttpConnectivityState::Online);
    QCOMPARE(service.consecutiveFailureCount(), 0);
    sessions.clear();
}

void HeartbeatServiceTest::countsOnlyConnectivityFailuresAndRecoversOnAnyHttpResponse()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(disconnectedResponse());
    server.enqueueResponse(disconnectedResponse());
    server.enqueueResponse(disconnectedResponse());
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    HeartbeatApiClient api(server.url(), QUuid::createUuid(), QStringLiteral("0.1.1"),
                           &transport, &sessions);
    HeartbeatService service(&api, &transport, &sessions, 30ms);
    QVERIFY(sessions.establishSession(session()));

    QTRY_COMPARE(service.state(), HttpConnectivityState::Abnormal);
    QCOMPARE(service.consecutiveFailureCount(), 3);

    server.enqueueResponse(jsonResponse(404,
                                        envelope(false,
                                                 QStringLiteral("NOT_FOUND"),
                                                 QStringLiteral("missing"))));
    const quint64 generation = sessions.currentGeneration();
    HttpRequest query(HttpMethod::Get,
                      server.url(QStringLiteral("/api/v1/probe")),
                      HttpRequestKind::Query,
                      HttpResponseMode::JsonMemory,
                      RequestContext(generation, 99, QStringLiteral("test.probe")));
    HttpRequestHandle *queryHandle = transport.send(std::move(query));
    QSignalSpy queryFailure(queryHandle, &HttpRequestHandle::failed);
    QVERIFY(queryFailure.wait());
    QCOMPARE(service.state(), HttpConnectivityState::Online);
    QCOMPARE(service.consecutiveFailureCount(), 0);

    server.enqueueResponse(jsonResponse(200, QByteArrayLiteral("{bad")));
    QTRY_VERIFY(server.requests().size() >= 5);
    QTRY_COMPARE(service.state(), HttpConnectivityState::Online);
    QCOMPARE(service.consecutiveFailureCount(), 0);
    sessions.clear();
}

void HeartbeatServiceTest::countsTlsAndTimeoutFailures()
{
    SessionService tlsSessions;
    TlsErrorNetworkManager tlsManager;
    HttpClient tlsTransport(&tlsManager, {}, {}, nullptr);
    HeartbeatApiClient tlsApi(QUrl(QStringLiteral("https://example.test")),
                              QUuid::createUuid(), QStringLiteral("0.1.1"),
                              &tlsTransport, &tlsSessions);
    HeartbeatService tlsService(&tlsApi, &tlsTransport, &tlsSessions, 10s);
    QVERIFY(tlsSessions.establishSession(session()));
    QTRY_COMPARE(tlsService.consecutiveFailureCount(), 1);
    QCOMPARE(tlsService.state(), HttpConnectivityState::Detecting);
    tlsSessions.clear();

    MockHttpServer slowServer;
    QVERIFY(slowServer.listen());
    slowServer.enqueueResponse(jsonResponse(200, envelope(), 200));
    SessionService timeoutSessions;
    QNetworkAccessManager timeoutManager;
    HttpRequestKind observedKind = HttpRequestKind::Login;
    HttpClient timeoutTransport(
        &timeoutManager,
        [&timeoutSessions](const RequestContext &context) {
            return timeoutSessions.isCurrent(context);
        },
        [&observedKind](HttpRequestKind kind) {
            observedKind = kind;
            return 15ms;
        },
        nullptr);
    HeartbeatApiClient timeoutApi(slowServer.url(), QUuid::createUuid(),
                                  QStringLiteral("0.1.1"),
                                  &timeoutTransport, &timeoutSessions);
    HeartbeatService timeoutService(&timeoutApi, &timeoutTransport, &timeoutSessions, 10s);
    QVERIFY(timeoutSessions.establishSession(session()));
    QTRY_COMPARE(timeoutService.consecutiveFailureCount(), 1);
    QCOMPARE(observedKind, HttpRequestKind::Heartbeat);
    timeoutSessions.clear();
}

void HeartbeatServiceTest::unauthorizedInvalidatesSessionAndStops()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(401,
                                        envelope(false,
                                                 QStringLiteral("UNAUTHORIZED"),
                                                 QStringLiteral("expired"))));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    HeartbeatApiClient api(server.url(), QUuid::createUuid(), QStringLiteral("0.1.1"),
                           &transport, &sessions);
    HeartbeatService service(&api, &transport, &sessions, 10s);
    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);

    QVERIFY(sessions.establishSession(session()));
    QTRY_COMPARE(invalidated.count(), 1);
    QVERIFY(!sessions.hasActiveSession());
    QVERIFY(!service.isRunning());
    QVERIFY(!service.requestInFlight());
    QCOMPARE(service.consecutiveFailureCount(), 0);
}

void HeartbeatServiceTest::sessionInvalidationCancelsInFlightRequest()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(), 200));
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    HeartbeatApiClient api(server.url(), QUuid::createUuid(), QStringLiteral("0.1.1"),
                           &transport, &sessions);
    HeartbeatService service(&api, &transport, &sessions, 10s);
    QSignalSpy stateChanges(&service, &HeartbeatService::stateChanged);

    QVERIFY(sessions.establishSession(session()));
    QTRY_VERIFY(service.requestInFlight());
    sessions.clear();
    QVERIFY(!service.isRunning());
    QVERIFY(!service.requestInFlight());
    const int changesAfterStop = stateChanges.count();
    QTest::qWait(250);
    QCOMPARE(stateChanges.count(), changesAfterStop);
}

QTEST_MAIN(HeartbeatServiceTest)

#include "tst_heartbeat_service.moc"
