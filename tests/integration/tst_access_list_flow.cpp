#include "app/AppController.h"
#include "domain/AccessListRecord.h"
#include "integration/MockPlateServer.h"
#include "services/AccessListService.h"
#include "ui/pages/AccessListPage.h"
#include "ui/windows/MainWindow.h"

#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {
AccessListRecord record(qint64 id,
                        AccessListType type,
                        const QString &plate,
                        const QString &remark = {})
{
    return AccessListRecord(
        id,
        type,
        plate,
        remark,
        QStringLiteral("operator"),
        QDateTime::fromString(QStringLiteral("2026-08-24T10:00:00.000+08:00"),
                              Qt::ISODateWithMs));
}

void login(IntegrationTestRuntime &runtime)
{
    QSignalSpy succeeded(runtime.controller(), &AppController::loginSucceeded);
    runtime.submitLogin();
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 1000);
    QVERIFY(runtime.mainWindow());
    QTRY_VERIFY(runtime.mainWindow()->isVisible());
    QTest::qWait(20);
    QSignalSpy initialResults(runtime.accessListService(),
                              &AccessListService::resultsChanged);
    runtime.mainWindow()->pageStack()->setCurrentWidget(
        runtime.mainWindow()->accessListPage());
    QTRY_VERIFY(runtime.mainWindow()->accessListPage()->isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(initialResults.count(), 1, 1000);
}

AccessListWriteResult resultAt(const QSignalSpy &spy, qsizetype index)
{
    return *static_cast<const AccessListWriteResult *>(spy.at(index).at(0).constData());
}

AccessListServiceError errorAt(const QSignalSpy &spy, qsizetype index)
{
    return *static_cast<const AccessListServiceError *>(spy.at(index).at(0).constData());
}
} // namespace

class AccessListFlowTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void queryCreateConflictAndDelete();
    void committedTimeoutsAreVerifiedExactlyOnce();
};

void AccessListFlowTest::initTestCase()
{
    qRegisterMetaType<AccessListWriteResult>();
    qRegisterMetaType<AccessListServiceError>();
}

void AccessListFlowTest::queryCreateConflictAndDelete()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    server.seedAccessList({record(1, AccessListType::White, QStringLiteral("京A12345")),
                           record(2, AccessListType::Black, QStringLiteral("沪B67890"))});
    QTemporaryDir temporaryRoot;
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    login(runtime);

    auto *service = runtime.accessListService();
    QSignalSpy queryFailed(service, &AccessListService::queryFailed);
    QSignalSpy resultsChanged(service, &AccessListService::resultsChanged);
    QVERIFY(service->search(AccessListType::White, QStringLiteral("A123")));
    QVERIFY(service->isQueryLoading());
    QTRY_COMPARE_WITH_TIMEOUT(resultsChanged.count(), 1, 1000);
    if (!queryFailed.isEmpty()) {
        QFAIL(qPrintable(errorAt(queryFailed, 0).message()));
    }
    QCOMPARE(service->items().size(), 1);
    QCOMPARE(service->items().constFirst().plateNumber(), QStringLiteral("京A12345"));

    QSignalSpy completed(service, &AccessListService::writeCompleted);
    QSignalSpy failed(service, &AccessListService::writeFailed);
    QVERIFY(service->create(AccessListType::White,
                            QStringLiteral(" 粤c12345 "),
                            QStringLiteral("访客")));
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(resultAt(completed, 0).disposition(), AccessListWriteDisposition::Applied);
    QCOMPARE(resultAt(completed, 0).plateNumber(), QStringLiteral("粤C12345"));
    QTRY_VERIFY_WITH_TIMEOUT(!service->isQueryLoading(), 1000);
    resultsChanged.clear();

    QVERIFY(service->create(AccessListType::Black,
                            QStringLiteral("粤C12345"),
                            QStringLiteral("冲突")));
    QTRY_VERIFY(failed.count() >= 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(errorAt(failed, 0).code(), AccessListServiceErrorCode::ConflictWhite);

    service->search(AccessListType::White, QStringLiteral("粤C12345"));
    QVERIFY(service->isQueryLoading());
    QTRY_COMPARE_WITH_TIMEOUT(resultsChanged.count(), 1, 1000);
    if (queryFailed.count() > 0) {
        QFAIL(qPrintable(errorAt(queryFailed, queryFailed.count() - 1).message()));
    }
    QCOMPARE(service->items().size(), 1);
    const AccessListRecord created = service->items().constFirst();
    QVERIFY(service->remove(created));
    QTRY_COMPARE(completed.count(), 2);
    QCOMPARE(resultAt(completed, 1).operation(), AccessListWriteOperation::Delete);
    QCOMPARE(resultAt(completed, 1).disposition(), AccessListWriteDisposition::Applied);
    QTRY_VERIFY(service->items().isEmpty());
}

void AccessListFlowTest::committedTimeoutsAreVerifiedExactlyOnce()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    server.seedAccessList({record(10, AccessListType::White, QStringLiteral("京D00010"))});
    QTemporaryDir temporaryRoot;
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    login(runtime);

    auto *service = runtime.accessListService();
    QSignalSpy queryFailed(service, &AccessListService::queryFailed);
    QSignalSpy resultsChanged(service, &AccessListService::resultsChanged);
    QSignalSpy completed(service, &AccessListService::writeCompleted);
    const int createsBefore = server.requestCount(QByteArrayLiteral("POST"),
                                                  QByteArrayLiteral("/api/v1/access-lists"));
    const int lookupsBefore = server.requestCount(QByteArrayLiteral("GET"),
                                                  QByteArrayLiteral("/api/v1/access-lists/lookup"));
    server.delayNextAccessListCreate(250);
    QVERIFY(service->create(AccessListType::Black,
                            QStringLiteral("苏E54321"),
                            QStringLiteral("超时核实")));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
    QCOMPARE(resultAt(completed, 0).operation(), AccessListWriteOperation::Create);
    QCOMPARE(resultAt(completed, 0).disposition(), AccessListWriteDisposition::Applied);
    QCOMPARE(server.requestCount(QByteArrayLiteral("POST"),
                                 QByteArrayLiteral("/api/v1/access-lists")),
             createsBefore + 1);
    QCOMPARE(server.requestCount(QByteArrayLiteral("GET"),
                                 QByteArrayLiteral("/api/v1/access-lists/lookup")),
             lookupsBefore + 1);
    QTRY_VERIFY_WITH_TIMEOUT(!service->isQueryLoading(), 1000);
    resultsChanged.clear();

    service->search(AccessListType::White, QStringLiteral("京D00010"));
    QVERIFY(service->isQueryLoading());
    QTRY_COMPARE_WITH_TIMEOUT(resultsChanged.count(), 1, 1000);
    if (!queryFailed.isEmpty()) {
        QFAIL(qPrintable(errorAt(queryFailed, 0).message()));
    }
    QCOMPARE(service->items().size(), 1);
    const AccessListRecord existing = service->items().constFirst();
    const int deletesBefore = server.requestCount(QByteArrayLiteral("DELETE"),
                                                  QByteArrayLiteral("/api/v1/access-lists/"));
    const int deleteLookupsBefore = server.requestCount(
        QByteArrayLiteral("GET"), QByteArrayLiteral("/api/v1/access-lists/lookup"));
    server.delayNextAccessListDelete(250);
    QVERIFY(service->remove(existing));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 1000);
    QCOMPARE(resultAt(completed, 1).operation(), AccessListWriteOperation::Delete);
    QCOMPARE(resultAt(completed, 1).disposition(), AccessListWriteDisposition::Applied);
    QCOMPARE(server.requestCount(QByteArrayLiteral("DELETE"),
                                 QByteArrayLiteral("/api/v1/access-lists/")),
             deletesBefore + 1);
    QCOMPARE(server.requestCount(QByteArrayLiteral("GET"),
                                 QByteArrayLiteral("/api/v1/access-lists/lookup")),
             deleteLookupsBefore + 1);
    QTest::qWait(300);
    QCOMPARE(completed.count(), 2);
}

QTEST_MAIN(AccessListFlowTest)

#include "tst_access_list_flow.moc"
