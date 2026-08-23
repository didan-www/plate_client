#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/HistoryService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

namespace {
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

QJsonObject snapshot(const QString &id)
{
    return QJsonObject {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("recognitionId"), id},
        {QStringLiteral("revision"), 2},
        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
        {QStringLiteral("status"), QStringLiteral("SUCCEEDED")},
        {QStringLiteral("plateNumber"), QStringLiteral("京A12345")},
        {QStringLiteral("errorCode"), QJsonValue::Null},
        {QStringLiteral("errorMessage"), QJsonValue::Null},
        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-24T09:00:00.001+08:00")},
        {QStringLiteral("startedAt"), QStringLiteral("2026-08-24T09:00:01.002+08:00")},
        {QStringLiteral("completedAt"), QStringLiteral("2026-08-24T09:00:02.003+08:00")},
        {QStringLiteral("durationMs"), 1001},
    };
}

QByteArray pageEnvelope(const QString &id, int page, qint64 total)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("history-request")},
                             {QStringLiteral("data"),
                              QJsonObject {
                                  {QStringLiteral("items"),
                                   id.isEmpty() ? QJsonArray {}
                                                : QJsonArray {snapshot(id)}},
                                  {QStringLiteral("page"), page},
                                  {QStringLiteral("pageSize"), 100},
                                  {QStringLiteral("total"), total},
                              }},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray failureEnvelope()
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), false},
                             {QStringLiteral("code"), QStringLiteral("TEMPORARY")},
                             {QStringLiteral("message"), QStringLiteral("服务暂不可用")},
                             {QStringLiteral("requestId"), QStringLiteral("history-error")},
                             {QStringLiteral("data"), QJsonValue::Null},
                         })
        .toJson(QJsonDocument::Compact);
}

MockHttpResponse jsonResponse(int status, QByteArray body, int delayMs = 0)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = status >= 400 ? QByteArrayLiteral("Error")
                                           : QByteArrayLiteral("OK");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(std::move(body));
    response.initialDelayMs = delayMs;
    return response;
}

QDateTime time(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}
} // namespace

class HistoryServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultBeijingDayAndResetImmediatelyQuery();
    void storesSuccessfulPagePaginatesAndPreservesOnFailure();
    void newestImmutableFilterWinsAndCancellationIsSilent();
    void cancelSessionInvalidationAndDestructionAreSafe();
};

void HistoryServiceTest::defaultBeijingDayAndResetImmediatelyQuery()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope({}, 1, 0)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime now = time(QStringLiteral("2026-08-24T15:16:17.321Z"));
    HistoryService service(&api, &sessions, [now] { return now; });

    const HistoryFilter defaults = service.defaultFilter();
    QCOMPARE(defaults.startTime().toString(Qt::ISODateWithMs),
             QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    QCOMPARE(defaults.endTime().toString(Qt::ISODateWithMs),
             QStringLiteral("2026-08-25T00:00:00.000+08:00"));
    QCOMPARE(defaults.page(), 1);
    QVERIFY(defaults.deviceId().isEmpty());

    QSignalSpy changed(&service, &HistoryService::resultsChanged);
    service.reset();
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(service.lastSubmittedFilter());
    QCOMPARE(*service.lastSubmittedFilter(), defaults);
    QVERIFY(service.lastSuccessfulPage());
    QCOMPARE(service.lastSuccessfulPage()->total(), qint64(0));
    QTRY_COMPARE(server.requests().size(), 1);
    const QUrl url(QStringLiteral("http://mock.test")
                   + QString::fromLatin1(server.requests().first().target));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("startTime")),
             QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    QCOMPARE(query.queryItemValue(QStringLiteral("endTime")),
             QStringLiteral("2026-08-25T00:00:00.000+08:00"));
}

void HistoryServiceTest::storesSuccessfulPagePaginatesAndPreservesOnFailure()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("page-1"), 1, 201)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("page-2"), 2, 201)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("page-3"), 3, 201)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("page-2-back"), 2, 201)));
    server.enqueueResponse(jsonResponse(503, failureEnvelope()));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    HistoryService service(&api, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    QSignalSpy changed(&service, &HistoryService::resultsChanged);
    QSignalSpy failed(&service, &HistoryService::queryFailed);

    service.search(start, start.addDays(1), QStringLiteral("  device-A  "));
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("page-1"));
    QVERIFY(!service.canGoPrevious());
    QVERIFY(service.canGoNext());
    QVERIFY(service.nextPage());
    QTRY_COMPARE(changed.count(), 2);
    QCOMPARE(service.lastSuccessfulPage()->page(), 2);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("page-2"));
    QVERIFY(service.canGoPrevious());
    QVERIFY(service.canGoNext());
    QCOMPARE(service.lastSubmittedFilter()->deviceId(), QStringLiteral("device-A"));

    QVERIFY(service.nextPage());
    QTRY_COMPARE(changed.count(), 3);
    QCOMPARE(service.lastSuccessfulPage()->page(), 3);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("page-3"));
    QVERIFY(service.canGoPrevious());
    QVERIFY(!service.canGoNext());
    QVERIFY(service.previousPage());
    QTRY_COMPARE(changed.count(), 4);
    QCOMPARE(service.lastSuccessfulPage()->page(), 2);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("page-2-back"));

    service.refresh();
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("服务暂不可用"));
    QCOMPARE(service.lastSuccessfulPage()->page(), 2);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("page-2-back"));
    QCOMPARE(changed.count(), 4);
    QVERIFY(!service.isLoading());
}

void HistoryServiceTest::newestImmutableFilterWinsAndCancellationIsSilent()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200,
                                        pageEnvelope(QStringLiteral("old"), 1, 1),
                                        150));
    server.enqueueResponse(jsonResponse(200,
                                        pageEnvelope(QStringLiteral("new"), 1, 1)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    HistoryService service(&api, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    QSignalSpy changed(&service, &HistoryService::resultsChanged);
    QSignalSpy failed(&service, &HistoryService::queryFailed);

    service.search(start, start.addDays(1), QStringLiteral("old-device"));
    QTRY_COMPARE(server.requests().size(), 1);
    service.search(start.addDays(-1), start, QStringLiteral(" new-device "));
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("new"));
    QCOMPARE(service.lastSubmittedFilter()->deviceId(), QStringLiteral("new-device"));
    QCOMPARE(service.lastSubmittedFilter()->startTime(), start.addDays(-1));
    QTest::qWait(180);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(service.items().first().recognitionId(), QStringLiteral("new"));
}

void HistoryServiceTest::cancelSessionInvalidationAndDestructionAreSafe()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("cancel"), 1, 1), 100));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("logout"), 1, 1), 100));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("destroy"), 1, 1), 100));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));

    auto service = std::make_unique<HistoryService>(&api, &sessions);
    QSignalSpy changed(service.get(), &HistoryService::resultsChanged);
    QSignalSpy failed(service.get(), &HistoryService::queryFailed);
    service->search(start, start.addDays(1), {});
    QTRY_VERIFY(service->hasActiveRequest());
    service->cancelActive();
    QVERIFY(!service->hasActiveRequest());
    QVERIFY(!service->isLoading());
    QTest::qWait(120);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(failed.count(), 0);

    service->search(start, start.addDays(1), {});
    QTRY_VERIFY(service->hasActiveRequest());
    sessions.clear(SessionInvalidationReason::LoggedOut);
    QVERIFY(!service->hasActiveRequest());
    QVERIFY(service->items().isEmpty());
    QTest::qWait(120);
    QCOMPARE(failed.count(), 0);

    QVERIFY(sessions.establishSession(session()));
    service->search(start, start.addDays(1), {});
    QTRY_VERIFY(service->hasActiveRequest());
    service.reset();
    QTest::qWait(120);
    QVERIFY(true);
}

QTEST_MAIN(HistoryServiceTest)

#include "tst_history_service.moc"
