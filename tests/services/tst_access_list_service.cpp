#include "domain/SessionContext.h"
#include "infrastructure/http/AccessListApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/AccessListService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QtTest>

#include <chrono>
#include <memory>

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

QJsonObject jsonRecord(qint64 id,
                       const QString &listType = QStringLiteral("WHITE"),
                       const QString &plate = QStringLiteral("京A12345"),
                       const QString &remark = QStringLiteral("教学车辆"))
{
    return QJsonObject {{QStringLiteral("id"), id},
                        {QStringLiteral("listType"), listType},
                        {QStringLiteral("plateNumber"), plate},
                        {QStringLiteral("remark"), remark},
                        {QStringLiteral("createdBy"), QStringLiteral("测试操作员")},
                        {QStringLiteral("createdAt"),
                         QStringLiteral("2026-08-24T10:00:00.001+08:00")}};
}

AccessListRecord domainRecord(qint64 id,
                              AccessListType type = AccessListType::White,
                              const QString &plate = QStringLiteral("京A12345"))
{
    return AccessListRecord(id,
                            type,
                            plate,
                            QStringLiteral("教学车辆"),
                            QStringLiteral("测试操作员"),
                            QDateTime::fromString(
                                QStringLiteral("2026-08-24T10:00:00.001+08:00"),
                                Qt::ISODateWithMs));
}

QByteArray envelope(const QJsonValue &data,
                    bool success = true,
                    const QString &code = QStringLiteral("OK"))
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), success},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"),
                              success ? QString() : QStringLiteral("名单请求失败")},
                             {QStringLiteral("requestId"), QStringLiteral("access-request")},
                             {QStringLiteral("data"), data},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray pageEnvelope(int page,
                        const QJsonArray &items = {},
                        qint64 total = 0)
{
    return envelope(QJsonObject {{QStringLiteral("items"), items},
                                 {QStringLiteral("page"), page},
                                 {QStringLiteral("pageSize"), 100},
                                 {QStringLiteral("total"), total}});
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

AccessListWriteResult writeResultFrom(const QSignalSpy &spy)
{
    return *static_cast<const AccessListWriteResult *>(spy.first().first().constData());
}

AccessListServiceError serviceErrorFrom(const QSignalSpy &spy, qsizetype index = 0)
{
    return *static_cast<const AccessListServiceError *>(spy.at(index).first().constData());
}

std::optional<AccessListWriteResult> runCreateTimeoutVerification(
    const MockHttpResponse &verification)
{
    MockHttpServer server;
    if (!server.listen()) {
        return std::nullopt;
    }
    server.enqueueResponse(jsonResponse(200, envelope(jsonRecord(1)), 100));
    server.enqueueResponse(verification);
    SessionService sessions;
    if (!sessions.establishSession(session())) {
        return std::nullopt;
    }
    QNetworkAccessManager manager;
    HttpClient transport(&manager,
                         [&sessions](const RequestContext &context) {
                             return sessions.isCurrent(context);
                         },
                         [](HttpRequestKind kind) {
                             return kind == HttpRequestKind::AccessListWrite
                                        ? std::chrono::milliseconds(15)
                                        : std::chrono::milliseconds(500);
                         });
    AccessListApiClient api(server.url(), &transport, &sessions);
    AccessListService service(&api, &sessions);
    QSignalSpy completed(&service, &AccessListService::writeCompleted);
    if (!service.create(AccessListType::White,
                        QStringLiteral(" 京a12345 "),
                        QStringLiteral("教学车辆"))
        || !completed.wait(1000) || server.requests().size() != 2
        || server.requests().at(0).method != QByteArrayLiteral("POST")
        || server.requests().at(1).method != QByteArrayLiteral("GET")) {
        return std::nullopt;
    }
    return writeResultFrom(completed);
}

std::optional<AccessListWriteResult> runDeleteTimeoutVerification(
    const MockHttpResponse &verification)
{
    MockHttpServer server;
    if (!server.listen()) {
        return std::nullopt;
    }
    server.enqueueResponse(jsonResponse(200, envelope(QJsonValue::Null), 100));
    server.enqueueResponse(verification);
    SessionService sessions;
    if (!sessions.establishSession(session())) {
        return std::nullopt;
    }
    QNetworkAccessManager manager;
    HttpClient transport(&manager,
                         [&sessions](const RequestContext &context) {
                             return sessions.isCurrent(context);
                         },
                         [](HttpRequestKind kind) {
                             return kind == HttpRequestKind::AccessListWrite
                                        ? std::chrono::milliseconds(15)
                                        : std::chrono::milliseconds(500);
                         });
    AccessListApiClient api(server.url(), &transport, &sessions);
    AccessListService service(&api, &sessions);
    QSignalSpy completed(&service, &AccessListService::writeCompleted);
    if (!service.remove(domainRecord(7)) || !completed.wait(1000)
        || server.requests().size() != 2
        || server.requests().at(0).method != QByteArrayLiteral("DELETE")
        || server.requests().at(1).method != QByteArrayLiteral("GET")) {
        return std::nullopt;
    }
    return writeResultFrom(completed);
}
} // namespace

class AccessListServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizesExactUnicodeBoundaries();
    void newestQueryWinsAndPreservesLastPage();
    void queryRunsAlongsideSingleBusyWrite();
    void mapsStableConflictsWithoutRefreshing();
    void createTimeoutUsesFixedThreeStateMatrix();
    void deleteTimeoutAndDirectNotFoundUseFixedMatrix();
    void cancellationSessionAndDestructionAreSilent();
    void synchronousSignalDeletionIsSafe();
};

void AccessListServiceTest::initTestCase()
{
    qRegisterMetaType<AccessListWriteResult>();
    qRegisterMetaType<AccessListServiceError>();
}

void AccessListServiceTest::normalizesExactUnicodeBoundaries()
{
    QCOMPARE(AccessListService::normalizePlateNumber(
                 QString(QChar(0x0085)) + QStringLiteral("京a12345") + QChar(0x3000)),
             std::optional<QString>(QStringLiteral("京A12345")));
    QVERIFY(!AccessListService::normalizePlateNumber(QStringLiteral("京 A12345")));
    QVERIFY(!AccessListService::normalizeKeyword(QStringLiteral("A\tB")));
    QCOMPARE(AccessListService::normalizeKeyword(QStringLiteral(" \t ")),
             std::optional<QString>(QString()));

    const QString astral = QString::fromUcs4(U"\U0001F680");
    QVERIFY(AccessListService::normalizePlateNumber(astral.repeated(16)));
    QVERIFY(!AccessListService::normalizePlateNumber(astral.repeated(17)));
    QVERIFY(AccessListService::isValidRemark(astral.repeated(200)));
    QVERIFY(!AccessListService::isValidRemark(astral.repeated(201)));
}

void AccessListServiceTest::newestQueryWinsAndPreservesLastPage()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        200, pageEnvelope(1, QJsonArray {jsonRecord(1)}, 1), 150));
    server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(1,
                     QJsonArray {jsonRecord(2,
                                            QStringLiteral("BLACK"),
                                            QStringLiteral("京B22222"))},
                     1)));
    server.enqueueResponse(jsonResponse(
        503, envelope(QJsonValue::Null, false, QStringLiteral("DATABASE_UNAVAILABLE"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    AccessListService service(&api, &sessions);
    QSignalSpy changed(&service, &AccessListService::resultsChanged);
    QSignalSpy failed(&service, &AccessListService::queryFailed);

    QVERIFY(service.search(AccessListType::White, QStringLiteral(" old ")));
    QTRY_COMPARE(server.requests().size(), 1);
    QVERIFY(service.search(AccessListType::Black, QStringLiteral(" b ")));
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(service.items().first().id(), qint64(2));
    QCOMPARE(service.lastSubmittedFilter()->listType(), AccessListType::Black);
    QCOMPARE(service.lastSubmittedFilter()->keyword(), QStringLiteral("B"));
    QTest::qWait(180);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(failed.count(), 0);

    service.refresh();
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(service.items().first().id(), qint64(2));
    QCOMPARE(changed.count(), 1);
}

void AccessListServiceTest::queryRunsAlongsideSingleBusyWrite()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(jsonRecord(1)), 1000));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1, QJsonArray {jsonRecord(1)}, 1)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    AccessListService service(&api, &sessions);
    QSignalSpy writeCompleted(&service, &AccessListService::writeCompleted);
    QSignalSpy writeFailed(&service, &AccessListService::writeFailed);
    QSignalSpy changed(&service, &AccessListService::resultsChanged);

    QVERIFY(service.create(AccessListType::White,
                           QStringLiteral("京a12345"),
                           QStringLiteral("教学车辆")));
    QTRY_COMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().method, QByteArrayLiteral("POST"));
    QVERIFY(service.isWriteBusy());
    QVERIFY(!service.create(AccessListType::Black,
                            QStringLiteral("京B22222"),
                            QString()));
    QCOMPARE(writeFailed.count(), 1);
    QCOMPARE(serviceErrorFrom(writeFailed).code(), AccessListServiceErrorCode::Busy);
    QVERIFY(service.search(AccessListType::White, {}));
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(service.isWriteBusy());
    QTRY_COMPARE(writeCompleted.count(), 1);
    QCOMPARE(writeResultFrom(writeCompleted).disposition(),
             AccessListWriteDisposition::Applied);
    QTRY_COMPARE(changed.count(), 2);
    QVERIFY(!service.isWriteBusy());
    QTRY_COMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().at(0).method, QByteArrayLiteral("POST"));
    QCOMPARE(server.requests().at(1).method, QByteArrayLiteral("GET"));
    QCOMPARE(server.requests().at(2).method, QByteArrayLiteral("GET"));
}

void AccessListServiceTest::mapsStableConflictsWithoutRefreshing()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(
        200, pageEnvelope(1, QJsonArray {jsonRecord(1)}, 1)));
    server.enqueueResponse(jsonResponse(
        409,
        envelope(QJsonValue::Null,
                 false,
                 QStringLiteral("ACCESS_LIST_CONFLICT_WHITE"))));
    server.enqueueResponse(jsonResponse(
        409,
        envelope(QJsonValue::Null,
                 false,
                 QStringLiteral("ACCESS_LIST_CONFLICT_BLACK"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    AccessListService service(&api, &sessions);
    QSignalSpy changed(&service, &AccessListService::resultsChanged);
    QSignalSpy failed(&service, &AccessListService::writeFailed);

    QVERIFY(service.search(AccessListType::White, {}));
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(service.items().first().id(), qint64(1));

    QVERIFY(service.create(AccessListType::White,
                           QStringLiteral("京A12345"),
                           QStringLiteral("教学车辆")));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(serviceErrorFrom(failed).code(),
             AccessListServiceErrorCode::ConflictWhite);
    QVERIFY(service.create(AccessListType::White,
                           QStringLiteral("京B22222"),
                           QStringLiteral("教学车辆")));
    QTRY_COMPARE(failed.count(), 2);
    QCOMPARE(serviceErrorFrom(failed, 1).code(),
             AccessListServiceErrorCode::ConflictBlack);

    QCOMPARE(changed.count(), 1);
    QCOMPARE(service.items().first().id(), qint64(1));
    QCOMPARE(server.requests().size(), 3);
}

void AccessListServiceTest::createTimeoutUsesFixedThreeStateMatrix()
{
    const auto applied = runCreateTimeoutVerification(
        jsonResponse(200, envelope(jsonRecord(8))));
    QVERIFY(applied);
    QCOMPARE(applied->disposition(), AccessListWriteDisposition::Applied);
    QVERIFY(applied->serverRecord());

    const auto missing = runCreateTimeoutVerification(
        jsonResponse(404,
                     envelope(QJsonValue::Null,
                              false,
                              QStringLiteral("ACCESS_LIST_NOT_FOUND"))));
    QVERIFY(missing);
    QCOMPARE(missing->disposition(), AccessListWriteDisposition::NotApplied);

    const auto otherType = runCreateTimeoutVerification(
        jsonResponse(200,
                     envelope(jsonRecord(9, QStringLiteral("BLACK")))));
    QVERIFY(otherType);
    QCOMPARE(otherType->disposition(), AccessListWriteDisposition::NotApplied);
    QCOMPARE(otherType->serverRecord()->listType(), AccessListType::Black);

    const auto unknown = runCreateTimeoutVerification(
        jsonResponse(503,
                     envelope(QJsonValue::Null,
                              false,
                              QStringLiteral("DATABASE_UNAVAILABLE"))));
    QVERIFY(unknown);
    QCOMPARE(unknown->disposition(), AccessListWriteDisposition::Unknown);
}

void AccessListServiceTest::deleteTimeoutAndDirectNotFoundUseFixedMatrix()
{
    const auto missing = runDeleteTimeoutVerification(
        jsonResponse(404,
                     envelope(QJsonValue::Null,
                              false,
                              QStringLiteral("ACCESS_LIST_NOT_FOUND"))));
    QVERIFY(missing);
    QCOMPARE(missing->disposition(), AccessListWriteDisposition::Applied);

    const auto sameId = runDeleteTimeoutVerification(
        jsonResponse(200, envelope(jsonRecord(7))));
    QVERIFY(sameId);
    QCOMPARE(sameId->disposition(), AccessListWriteDisposition::NotApplied);

    const auto differentId = runDeleteTimeoutVerification(
        jsonResponse(200, envelope(jsonRecord(8))));
    QVERIFY(differentId);
    QCOMPARE(differentId->disposition(), AccessListWriteDisposition::Applied);
    QCOMPARE(differentId->serverRecord()->id(), qint64(8));

    const auto unknown = runDeleteTimeoutVerification(
        jsonResponse(403,
                     envelope(QJsonValue::Null, false, QStringLiteral("FORBIDDEN"))));
    QVERIFY(unknown);
    QCOMPARE(unknown->disposition(), AccessListWriteDisposition::Unknown);

    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1)));
    server.enqueueResponse(jsonResponse(
        404,
        envelope(QJsonValue::Null,
                 false,
                 QStringLiteral("ACCESS_LIST_NOT_FOUND"))));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    AccessListService service(&api, &sessions);
    QSignalSpy changed(&service, &AccessListService::resultsChanged);
    QSignalSpy completed(&service, &AccessListService::writeCompleted);
    QVERIFY(service.search(AccessListType::White, {}));
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(service.remove(domainRecord(7)));
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(writeResultFrom(completed).disposition(),
             AccessListWriteDisposition::AlreadyAbsent);
    QTRY_COMPARE(changed.count(), 2);
    QCOMPARE(server.requests().size(), 3);
}

void AccessListServiceTest::cancellationSessionAndDestructionAreSilent()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, envelope(jsonRecord(1)), 100));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(1), 100));
    server.enqueueResponse(jsonResponse(200, envelope(jsonRecord(2)), 100));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    AccessListApiClient api(server.url(), &transport, &sessions);
    auto service = std::make_unique<AccessListService>(&api, &sessions);
    QSignalSpy completed(service.get(), &AccessListService::writeCompleted);
    QSignalSpy writeFailed(service.get(), &AccessListService::writeFailed);
    QSignalSpy queryFailed(service.get(), &AccessListService::queryFailed);

    QVERIFY(service->create(AccessListType::White,
                            QStringLiteral("京A12345"),
                            QStringLiteral("教学车辆")));
    QTRY_COMPARE(server.requests().size(), 1);
    service->cancelWrite();
    QVERIFY(!service->isWriteBusy());
    QTest::qWait(120);
    QCOMPARE(completed.count(), 0);
    QCOMPARE(writeFailed.count(), 0);

    QVERIFY(service->search(AccessListType::White, {}));
    QTRY_VERIFY(service->hasActiveQuery());
    QTRY_COMPARE(server.requests().size(), 2);
    sessions.clear(SessionInvalidationReason::LoggedOut);
    QVERIFY(!service->hasActiveQuery());
    QTest::qWait(120);
    QCOMPARE(queryFailed.count(), 0);

    QVERIFY(sessions.establishSession(session()));
    QVERIFY(service->create(AccessListType::White,
                            QStringLiteral("京A12345"),
                            QStringLiteral("教学车辆")));
    QTRY_COMPARE(server.requests().size(), 3);
    service.reset();
    QTest::qWait(120);
    QVERIFY(true);
}

void AccessListServiceTest::synchronousSignalDeletionIsSafe()
{
    {
        MockHttpServer server;
        QVERIFY(server.listen());
        SessionService sessions;
        QVERIFY(sessions.establishSession(session()));
        HttpClient transport([&sessions](const RequestContext &context) {
            return sessions.isCurrent(context);
        });
        AccessListApiClient api(server.url(), &transport, &sessions);
        QPointer<AccessListService> service = new AccessListService(&api, &sessions);
        connect(service.data(), &AccessListService::writeBusyChanged, service.data(),
                [service](bool busy) mutable {
                    if (busy) {
                        delete service.data();
                    }
                });

        QVERIFY(!service->create(AccessListType::White,
                                 QStringLiteral("京A12345"),
                                 QStringLiteral("教学车辆")));
        QVERIFY(service.isNull());
        QCOMPARE(server.requests().size(), 0);
    }

    {
        MockHttpServer server;
        QVERIFY(server.listen());
        server.enqueueResponse(jsonResponse(200, pageEnvelope(1)));
        SessionService sessions;
        QVERIFY(sessions.establishSession(session()));
        HttpClient transport([&sessions](const RequestContext &context) {
            return sessions.isCurrent(context);
        });
        AccessListApiClient api(server.url(), &transport, &sessions);
        QPointer<AccessListService> service = new AccessListService(&api, &sessions);
        QObject observer;
        bool resultsPublished = false;
        connect(service.data(), &AccessListService::resultsChanged,
                &observer, [&resultsPublished] { resultsPublished = true; });
        connect(service.data(), &AccessListService::queryLoadingChanged, service.data(),
                [service](bool loading) mutable {
                    if (!loading) {
                        delete service.data();
                    }
                });

        QVERIFY(service->search(AccessListType::White, {}));
        QTRY_VERIFY(service.isNull());
        QVERIFY(!resultsPublished);
    }

    {
        MockHttpServer server;
        QVERIFY(server.listen());
        server.enqueueResponse(jsonResponse(200, envelope(jsonRecord(1))));
        SessionService sessions;
        QVERIFY(sessions.establishSession(session()));
        HttpClient transport([&sessions](const RequestContext &context) {
            return sessions.isCurrent(context);
        });
        AccessListApiClient api(server.url(), &transport, &sessions);
        QPointer<AccessListService> service = new AccessListService(&api, &sessions);
        QObject observer;
        bool completionPublished = false;
        connect(service.data(), &AccessListService::writeCompleted,
                &observer, [&completionPublished] { completionPublished = true; });
        connect(service.data(), &AccessListService::writeBusyChanged, service.data(),
                [service](bool busy) mutable {
                    if (!busy) {
                        delete service.data();
                    }
                });

        QVERIFY(service->create(AccessListType::White,
                                QStringLiteral("京A12345"),
                                QStringLiteral("教学车辆")));
        QTRY_VERIFY(service.isNull());
        QVERIFY(!completionPublished);
    }

    {
        MockHttpServer server;
        QVERIFY(server.listen());
        server.enqueueResponse(jsonResponse(200, pageEnvelope(1, QJsonArray {jsonRecord(1)}, 1)));
        SessionService sessions;
        QVERIFY(sessions.establishSession(session()));
        HttpClient transport([&sessions](const RequestContext &context) {
            return sessions.isCurrent(context);
        });
        AccessListApiClient api(server.url(), &transport, &sessions);
        QPointer<AccessListService> service = new AccessListService(&api, &sessions);
        QVERIFY(service->search(AccessListType::White, {}));
        QTRY_COMPARE(service->items().size(), 1);
        connect(service.data(), &AccessListService::resultsChanged, service.data(),
                [service]() mutable { delete service.data(); });

        sessions.clear(SessionInvalidationReason::LoggedOut);
        QVERIFY(service.isNull());
    }
}

QTEST_MAIN(AccessListServiceTest)

#include "tst_access_list_service.moc"
