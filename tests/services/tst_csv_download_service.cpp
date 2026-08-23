#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/CsvDownloadService.h"
#include "services/HistoryService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
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

QByteArray csvPrefix()
{
    return QByteArray::fromHex("EFBBBF")
           + QStringLiteral("记录ID,设备,车牌号,状态,错误码,错误信息,拍摄时间,开始时间,完成时间,耗时(ms)\r\n")
                 .toUtf8();
}

QByteArray csvBody()
{
    return csvPrefix()
           + QStringLiteral("rec-1,device-1,京A12345,SUCCEEDED,,,2026-08-24T09:00:00.001+08:00,2026-08-24T09:00:01.002+08:00,2026-08-24T09:00:02.003+08:00,1001\r\n")
                 .toUtf8();
}

MockHttpResponse csvResponse(QByteArray contentType =
                                 QByteArrayLiteral("text/csv; charset=utf-8"),
                             int initialDelayMs = 0)
{
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             std::move(contentType)});
    const QByteArray body = csvBody();
    response.chunks = {body.left(2), body.mid(2, 31), body.mid(33)};
    response.initialDelayMs = initialDelayMs;
    response.chunkDelayMs = 5;
    return response;
}

QDateTime time(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

HistoryFilter filter()
{
    const QDateTime start = time(QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    return HistoryFilter(start, start.addDays(1), QStringLiteral(" device-1 "), 7);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QStringList directoryEntries(const QString &path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                    | QDir::NoDotAndDotDot,
                                QDir::Name);
}
} // namespace

class CsvDownloadServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void streamsExactBytesAndRejectsConcurrentDownload();
    void failuresPreserveExistingTargetAndLeaveNoTemporaryFile();
    void explicitAndSessionCancellationAreAtomic();
    void downloadingChangedCanSynchronouslyCancelOrDeleteService();
    void terminalStateCallbackCanSynchronouslyDeleteService();
};

void CsvDownloadServiceTest::streamsExactBytesAndRejectsConcurrentDownload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(csvResponse());
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    CsvDownloadService service(&api, &sessions);
    const QString target = directory.filePath(QStringLiteral("records.csv"));
    const QStringList before = directoryEntries(directory.path());
    QSignalSpy states(&service, &CsvDownloadService::downloadingChanged);
    QSignalSpy success(&service, &CsvDownloadService::downloadSucceeded);
    QSignalSpy failure(&service, &CsvDownloadService::downloadFailed);

    QVERIFY(service.download(filter(), target));
    QVERIFY(service.isDownloading());
    QVERIFY(service.hasActiveRequest());
    QVERIFY(!service.download(filter(), directory.filePath(QStringLiteral("other.csv"))));
    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QCOMPARE(states.count(), 2);
    QCOMPARE(states.at(0).at(0).toBool(), true);
    QCOMPARE(states.at(1).at(0).toBool(), false);
    QVERIFY(!service.isDownloading());
    QVERIFY(!service.hasActiveRequest());
    QCOMPARE(readAll(target), csvBody());
    QStringList expected = before;
    expected.append(QStringLiteral("records.csv"));
    expected.sort();
    QCOMPARE(directoryEntries(directory.path()), expected);

    QTRY_COMPARE(server.requests().size(), 1);
    QVERIFY(!server.requests().first().target.contains("page="));
    QVERIFY(!server.requests().first().target.contains("pageSize="));
}

void CsvDownloadServiceTest::failuresPreserveExistingTargetAndLeaveNoTemporaryFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString existing = directory.filePath(QStringLiteral("existing.csv"));
    QFile original(existing);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("original-bytes"), qint64(14));
    original.close();

    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(csvResponse(QByteArrayLiteral("application/octet-stream")));
    MockHttpResponse invalid = csvResponse();
    invalid.chunks = {QByteArrayLiteral("not-a-csv")};
    server.enqueueResponse(std::move(invalid));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    CsvDownloadService service(&api, &sessions);
    QSignalSpy failure(&service, &CsvDownloadService::downloadFailed);

    QVERIFY(service.download(filter(), existing));
    QVERIFY(failure.wait());
    QCOMPARE(readAll(existing), QByteArrayLiteral("original-bytes"));
    QCOMPARE(directoryEntries(directory.path()), QStringList {QStringLiteral("existing.csv")});

    const QString missing = directory.filePath(QStringLiteral("missing.csv"));
    QVERIFY(service.download(filter(), missing));
    QTRY_COMPARE(failure.count(), 2);
    QVERIFY(!QFileInfo::exists(missing));
    QCOMPARE(directoryEntries(directory.path()), QStringList {QStringLiteral("existing.csv")});

    const QString readOnlyTarget = directory.filePath(QStringLiteral("readonly.csv"));
    QFile readOnly(readOnlyTarget);
    QVERIFY(readOnly.open(QIODevice::WriteOnly));
    QCOMPARE(readOnly.write("read-only-original"), qint64(18));
    readOnly.close();
    QVERIFY(QFile::setPermissions(readOnlyTarget,
                                  QFileDevice::ReadOwner | QFileDevice::ReadGroup
                                      | QFileDevice::ReadOther));
    const int requestCountBeforeReadOnlyTarget = server.requests().size();
    QVERIFY(!service.download(filter(), readOnlyTarget));
    QCOMPARE(failure.count(), 3);
    QCOMPARE(server.requests().size(), requestCountBeforeReadOnlyTarget);
    QCOMPARE(readAll(readOnlyTarget), QByteArrayLiteral("read-only-original"));
    QCOMPARE(directoryEntries(directory.path()),
             QStringList({QStringLiteral("existing.csv"),
                          QStringLiteral("readonly.csv")}));
    QVERIFY(QFile::setPermissions(readOnlyTarget,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QString directoryTarget = directory.filePath(QStringLiteral("target-directory"));
    QVERIFY(QDir().mkpath(directoryTarget));
    const int requestCountBeforeDirectoryTarget = server.requests().size();
    QVERIFY(!service.download(filter(), directoryTarget));
    QCOMPARE(failure.count(), 4);
    QCOMPARE(server.requests().size(), requestCountBeforeDirectoryTarget);
    QVERIFY(QFileInfo(directoryTarget).isDir());
    QCOMPARE(directoryEntries(directory.path()),
             QStringList({QStringLiteral("existing.csv"),
                          QStringLiteral("readonly.csv"),
                          QStringLiteral("target-directory")}));

    const int requestCount = server.requests().size();
    QVERIFY(!service.download(filter(),
                              directory.filePath(QStringLiteral("missing/records.csv"))));
    QCOMPARE(failure.count(), 5);
    QCOMPARE(server.requests().size(), requestCount);
}

void CsvDownloadServiceTest::explicitAndSessionCancellationAreAtomic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(csvResponse({}, 100));
    server.enqueueResponse(csvResponse({}, 100));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    CsvDownloadService service(&api, &sessions);
    QSignalSpy failure(&service, &CsvDownloadService::downloadFailed);

    const QString explicitTarget = directory.filePath(QStringLiteral("cancelled.csv"));
    QVERIFY(service.download(filter(), explicitTarget));
    service.cancelActive();
    QVERIFY(!service.isDownloading());
    QVERIFY(!service.hasActiveRequest());
    QTest::qWait(130);
    QCOMPARE(failure.count(), 0);
    QVERIFY(!QFileInfo::exists(explicitTarget));
    QVERIFY(directoryEntries(directory.path()).isEmpty());

    const QString invalidatedTarget = directory.filePath(QStringLiteral("invalidated.csv"));
    QVERIFY(service.download(filter(), invalidatedTarget));
    sessions.clear(SessionInvalidationReason::LoggedOut);
    QVERIFY(!service.isDownloading());
    QVERIFY(!service.hasActiveRequest());
    QTest::qWait(130);
    QCOMPARE(failure.count(), 0);
    QVERIFY(!QFileInfo::exists(invalidatedTarget));
    QVERIFY(directoryEntries(directory.path()).isEmpty());
}

void CsvDownloadServiceTest::downloadingChangedCanSynchronouslyCancelOrDeleteService()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(csvResponse({}, 100));
    server.enqueueResponse(csvResponse({}, 100));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);

    CsvDownloadService cancelled(&api, &sessions);
    QSignalSpy cancelledStates(&cancelled, &CsvDownloadService::downloadingChanged);
    QSignalSpy cancelledSuccess(&cancelled, &CsvDownloadService::downloadSucceeded);
    QSignalSpy cancelledFailure(&cancelled, &CsvDownloadService::downloadFailed);
    connect(&cancelled, &CsvDownloadService::downloadingChanged,
            &cancelled, [&cancelled](bool downloading) {
                if (downloading) {
                    cancelled.cancelActive();
                }
            });
    const QString cancelledTarget = directory.filePath(QStringLiteral("reentrant-cancel.csv"));
    QVERIFY(cancelled.download(filter(), cancelledTarget));
    QCOMPARE(cancelledStates.count(), 2);
    QVERIFY(!cancelled.isDownloading());
    QVERIFY(!cancelled.hasActiveRequest());
    QTest::qWait(130);
    QCOMPARE(cancelledSuccess.count(), 0);
    QCOMPARE(cancelledFailure.count(), 0);
    QVERIFY(!QFileInfo::exists(cancelledTarget));
    QVERIFY(directoryEntries(directory.path()).isEmpty());

    auto *deleted = new CsvDownloadService(&api, &sessions);
    QPointer<CsvDownloadService> guarded(deleted);
    int terminalSignals = 0;
    connect(deleted, &CsvDownloadService::downloadSucceeded,
            this, [&terminalSignals] { ++terminalSignals; });
    connect(deleted, &CsvDownloadService::downloadFailed,
            this, [&terminalSignals] { ++terminalSignals; });
    connect(deleted, &CsvDownloadService::downloadingChanged,
            this, [deleted](bool downloading) {
                if (downloading) {
                    delete deleted;
                }
            });
    const QString deletedTarget = directory.filePath(QStringLiteral("reentrant-delete.csv"));
    QVERIFY(deleted->download(filter(), deletedTarget));
    QVERIFY(guarded.isNull());
    QTest::qWait(130);
    QCOMPARE(terminalSignals, 0);
    QVERIFY(!QFileInfo::exists(deletedTarget));
    QVERIFY(directoryEntries(directory.path()).isEmpty());
}

void CsvDownloadServiceTest::terminalStateCallbackCanSynchronouslyDeleteService()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(csvResponse());
    server.enqueueResponse(csvResponse(QByteArrayLiteral("application/octet-stream")));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);

    const QString successfulTarget = directory.filePath(QStringLiteral("successful.csv"));
    auto *successful = new CsvDownloadService(&api, &sessions);
    QPointer<CsvDownloadService> guardedSuccessful(successful);
    int successfulTerminalSignals = 0;
    connect(successful, &CsvDownloadService::downloadSucceeded,
            this, [&successfulTerminalSignals] { ++successfulTerminalSignals; });
    connect(successful, &CsvDownloadService::downloadFailed,
            this, [&successfulTerminalSignals] { ++successfulTerminalSignals; });
    connect(successful, &CsvDownloadService::downloadingChanged,
            this, [successful](bool downloading) {
                if (!downloading) {
                    delete successful;
                }
            });
    QVERIFY(successful->download(filter(), successfulTarget));
    QTRY_VERIFY(guardedSuccessful.isNull());
    QCOMPARE(successfulTerminalSignals, 0);
    QCOMPARE(readAll(successfulTarget), csvBody());

    const QString failedTarget = directory.filePath(QStringLiteral("preserved.csv"));
    QFile original(failedTarget);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("preserved"), qint64(9));
    original.close();
    auto *failed = new CsvDownloadService(&api, &sessions);
    QPointer<CsvDownloadService> guardedFailed(failed);
    int failedTerminalSignals = 0;
    connect(failed, &CsvDownloadService::downloadSucceeded,
            this, [&failedTerminalSignals] { ++failedTerminalSignals; });
    connect(failed, &CsvDownloadService::downloadFailed,
            this, [&failedTerminalSignals] { ++failedTerminalSignals; });
    connect(failed, &CsvDownloadService::downloadingChanged,
            this, [failed](bool downloading) {
                if (!downloading) {
                    delete failed;
                }
            });
    QVERIFY(failed->download(filter(), failedTarget));
    QTRY_VERIFY(guardedFailed.isNull());
    QCOMPARE(failedTerminalSignals, 0);
    QCOMPARE(readAll(failedTarget), QByteArrayLiteral("preserved"));
    QCOMPARE(directoryEntries(directory.path()),
             QStringList({QStringLiteral("preserved.csv"),
                          QStringLiteral("successful.csv")}));
}

QTEST_MAIN(CsvDownloadServiceTest)

#include "tst_csv_download_service.moc"
