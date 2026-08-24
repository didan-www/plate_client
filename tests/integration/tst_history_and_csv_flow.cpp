#include "app/AppController.h"
#include "domain/RecognitionSnapshot.h"
#include "integration/MockPlateServer.h"
#include "services/CsvDownloadService.h"
#include "services/HistoryService.h"
#include "ui/windows/MainWindow.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>

namespace {
constexpr auto firstId = "11111111-1111-4111-8111-111111111111";
constexpr auto secondId = "22222222-2222-4222-8222-222222222222";

void login(IntegrationTestRuntime &runtime)
{
    QSignalSpy succeeded(runtime.controller(), &AppController::loginSucceeded);
    runtime.submitLogin();
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.count(), 1, 1000);
    QVERIFY(runtime.mainWindow());
    QTRY_VERIFY(runtime.mainWindow()->isVisible());
    QTest::qWait(20);
}

QDateTime timestamp(const QString &value)
{
    return QDateTime::fromString(value, Qt::ISODateWithMs);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
} // namespace

class HistoryAndCsvFlowTest final : public QObject
{
    Q_OBJECT

private slots:
    void historyPaginatesAndNewestRequestWins();
    void csvUsesFixedFilterAndSavesAtomically();
};

void HistoryAndCsvFlowTest::historyPaginatesAndNewestRequestWins()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    server.setHistoryPage(1,
                          {MockPlateServer::succeededSnapshot(QString::fromLatin1(firstId))},
                          101);
    server.setHistoryPage(2,
                          {MockPlateServer::failedSnapshot(QString::fromLatin1(secondId))},
                          101);
    QTemporaryDir temporaryRoot;
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    login(runtime);

    auto *history = runtime.historyService();
    const QDateTime start = timestamp(QStringLiteral("2026-08-01T00:00:00.000+08:00"));
    const QDateTime end = timestamp(QStringLiteral("2026-09-01T00:00:00.000+08:00"));
    history->search(start, end, QStringLiteral("device-001"));
    QTRY_COMPARE(history->items().size(), 1);
    QCOMPARE(history->items().constFirst().recognitionId(), QString::fromLatin1(firstId));
    QVERIFY(history->canGoNext());
    QVERIFY(history->nextPage());
    QTRY_COMPARE(history->lastSubmittedFilter()->page(), 2);
    QTRY_COMPARE(history->items().constFirst().recognitionId(), QString::fromLatin1(secondId));
    QVERIFY(history->canGoPrevious());

    server.setHistoryPage(1,
                          {MockPlateServer::succeededSnapshot(QString::fromLatin1(firstId))},
                          1);
    server.setHistoryDelay(1, 100);
    history->search(start, end, QStringLiteral("old-device"));
    QTRY_VERIFY(history->hasActiveRequest());
    server.setHistoryPage(1,
                          {MockPlateServer::failedSnapshot(QString::fromLatin1(secondId))},
                          1);
    server.setHistoryDelay(1, 0);
    history->search(start.addDays(1), end, QStringLiteral("new-device"));
    QTRY_COMPARE(history->lastSubmittedFilter()->deviceId(), QStringLiteral("new-device"));
    QTRY_COMPARE(history->items().constFirst().recognitionId(), QString::fromLatin1(secondId));
    QTest::qWait(120);
    QCOMPARE(history->lastSubmittedFilter()->startTime(), start.addDays(1));
    QCOMPARE(history->items().constFirst().recognitionId(), QString::fromLatin1(secondId));
}

void HistoryAndCsvFlowTest::csvUsesFixedFilterAndSavesAtomically()
{
    MockPlateServer server;
    QVERIFY(server.listen());
    server.setHistoryPage(1,
                          {MockPlateServer::succeededSnapshot(QString::fromLatin1(firstId))},
                          1);
    QTemporaryDir temporaryRoot;
    IntegrationTestRuntime runtime(&server, temporaryRoot.path());
    QVERIFY(runtime.start());
    login(runtime);

    const QDateTime start = timestamp(QStringLiteral("2026-08-01T00:00:00.000+08:00"));
    const QDateTime end = timestamp(QStringLiteral("2026-09-01T00:00:00.000+08:00"));
    runtime.historyService()->search(start, end, QStringLiteral("device +中文%"));
    QTRY_VERIFY(runtime.historyService()->lastSubmittedFilter().has_value());

    const QString exportRoot = QDir(temporaryRoot.path()).filePath(QStringLiteral("exports"));
    QVERIFY(QDir().mkpath(exportRoot));
    const QString target = QDir(exportRoot).filePath(QStringLiteral("history.csv"));
    QSignalSpy succeeded(runtime.csvDownloadService(),
                         &CsvDownloadService::downloadSucceeded);
    QSignalSpy failed(runtime.csvDownloadService(), &CsvDownloadService::downloadFailed);
    QVERIFY(runtime.csvDownloadService()->download(
        *runtime.historyService()->lastSubmittedFilter(), target));
    QTRY_COMPARE(succeeded.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(readAll(target), MockPlateServer::fixedCsv());

    const auto exportIterator = std::find_if(
        server.requests().crbegin(), server.requests().crend(),
        [](const MockHttpRequest &request) {
            return request.method == QByteArrayLiteral("GET")
                   && request.target.startsWith(
                       QByteArrayLiteral("/api/v1/recognitions/export?"));
        });
    QVERIFY(exportIterator != server.requests().crend());
    const MockHttpRequest exportRequest = *exportIterator;
    const QUrl exportTarget = QUrl::fromEncoded(exportRequest.target);
    const QUrlQuery exportQuery(exportTarget);
    QCOMPARE(exportTarget.path(), QStringLiteral("/api/v1/recognitions/export"));
    QCOMPARE(exportQuery.queryItemValue(QStringLiteral("deviceId"), QUrl::FullyDecoded),
             QStringLiteral("device +中文%"));
    QVERIFY(!exportQuery.hasQueryItem(QStringLiteral("page")));
    QVERIFY(!exportQuery.hasQueryItem(QStringLiteral("pageSize")));

    server.setCsvFailure(true);
    QFile existing(target);
    QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(existing.write(QByteArrayLiteral("existing")), qint64(8));
    existing.close();
    QVERIFY(runtime.csvDownloadService()->download(
        *runtime.historyService()->lastSubmittedFilter(), target));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(readAll(target), QByteArrayLiteral("existing"));
    QCOMPARE(QDir(exportRoot).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList {QStringLiteral("history.csv")});
}

QTEST_MAIN(HistoryAndCsvFlowTest)

#include "tst_history_and_csv_flow.moc"
