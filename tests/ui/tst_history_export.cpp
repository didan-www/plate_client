#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/CsvDownloadService.h"
#include "services/HistoryService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/models/RecognitionTableModel.h"
#include "ui/pages/HistoryPage.h"

#include <QDateTimeEdit>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
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

QByteArray pageEnvelope(const QString &id, qint64 total = 1)
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), true},
                             {QStringLiteral("code"), QStringLiteral("OK")},
                             {QStringLiteral("message"), QString()},
                             {QStringLiteral("requestId"), QStringLiteral("history-export-ui")},
                             {QStringLiteral("data"),
                              QJsonObject {{QStringLiteral("items"),
                                            QJsonArray {snapshot(id)}},
                                           {QStringLiteral("page"), 1},
                                           {QStringLiteral("pageSize"), 100},
                                           {QStringLiteral("total"), total}}},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray csvPrefix()
{
    return QByteArray::fromHex("EFBBBF")
           + QStringLiteral("记录ID,设备,车牌号,状态,错误码,错误信息,拍摄时间,开始时间,完成时间,耗时(ms)\r\n")
                 .toUtf8();
}

QByteArray csvBody()
{
    return csvPrefix() + QByteArrayLiteral("rec-1,device-1,,,,,,,,\r\n");
}

MockHttpResponse response(QByteArray contentType,
                          QList<QByteArray> chunks,
                          int initialDelayMs = 0,
                          int chunkDelayMs = 0)
{
    MockHttpResponse result;
    result.headers.append({QByteArrayLiteral("Content-Type"),
                           std::move(contentType)});
    result.chunks = std::move(chunks);
    result.initialDelayMs = initialDelayMs;
    result.chunkDelayMs = chunkDelayMs;
    return result;
}

MockHttpResponse jsonResponse(QByteArray body)
{
    return response(QByteArrayLiteral("application/json"), {std::move(body)});
}

MockHttpResponse csvResponse(int initialDelayMs = 0)
{
    const QByteArray body = csvBody();
    return response(QByteArrayLiteral("text/csv; charset=utf-8"),
                    {body.left(2), body.mid(2, 19), body.mid(21)},
                    initialDelayMs,
                    15);
}

QDateTime time(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

QString queryValue(const MockHttpRequest &request, const QString &name)
{
    const QUrl url(QStringLiteral("http://mock.test") + QString::fromLatin1(request.target));
    return QUrlQuery(url).queryItemValue(name);
}
} // namespace

class HistoryExportTest final : public QObject
{
    Q_OBJECT

private slots:
    void exportsLastSubmittedFilterAndContinuesWhileHidden();
    void cancelledDialogAndFailureDoNotChangeHistoryTable();
};

void HistoryExportTest::exportsLastSubmittedFilterAndContinuesWhileHidden()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(pageEnvelope(QStringLiteral("initial"))));
    server.enqueueResponse(jsonResponse(pageEnvelope(QStringLiteral("submitted"), 201)));
    server.enqueueResponse(csvResponse(80));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime now = time(QStringLiteral("2026-08-24T04:00:00.000Z"));
    HistoryService history(&api, &sessions, [now] { return now; });
    CsvDownloadService csv(&api, &sessions);
    QString offeredName;
    const QString target = directory.filePath(QStringLiteral("records.csv"));
    HistoryPage page(&history,
                     &csv,
                     [&offeredName, &target](QWidget *, const QString &defaultName) {
                         offeredName = defaultName;
                         return target;
                     });
    page.setAttribute(Qt::WA_DontShowOnScreen);
    page.show();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("initial"));

    auto *start = page.findChild<QDateTimeEdit *>(QStringLiteral("historyStartTimeEdit"));
    auto *end = page.findChild<QDateTimeEdit *>(QStringLiteral("historyEndTimeEdit"));
    auto *device = page.findChild<QLineEdit *>(QStringLiteral("historyDeviceEdit"));
    auto *search = page.findChild<QPushButton *>(QStringLiteral("historySearchButton"));
    auto *next = page.findChild<QPushButton *>(QStringLiteral("historyNextButton"));
    auto *exportButton = page.findChild<QPushButton *>(QStringLiteral("historyExportButton"));
    QVERIFY(start && end && device && search && next && exportButton);
    start->setDateTime(time(QStringLiteral("2026-08-20T00:00:00.000+08:00")));
    end->setDateTime(time(QStringLiteral("2026-08-21T00:00:00.000+08:00")));
    device->setText(QStringLiteral(" submitted-device "));
    search->click();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("submitted"));

    start->setDateTime(time(QStringLiteral("2026-08-22T00:00:00.000+08:00")));
    device->setText(QStringLiteral("unsaved-device"));
    QSignalSpy success(&csv, &CsvDownloadService::downloadSucceeded);
    exportButton->click();
    QVERIFY(QRegularExpression(QStringLiteral("^车牌识别记录_\\d{8}_\\d{6}\\.csv$"))
                .match(offeredName)
                .hasMatch());
    QVERIFY(csv.isDownloading());
    QVERIFY(!exportButton->isEnabled());
    QVERIFY(search->isEnabled());
    QVERIFY(next->isEnabled());
    QTRY_COMPARE(server.requests().size(), 3);
    QCOMPARE(queryValue(server.requests().at(2), QStringLiteral("startTime")),
             QStringLiteral("2026-08-20T00:00:00.000+08:00"));
    QCOMPARE(queryValue(server.requests().at(2), QStringLiteral("endTime")),
             QStringLiteral("2026-08-21T00:00:00.000+08:00"));
    QCOMPARE(queryValue(server.requests().at(2), QStringLiteral("deviceId")),
             QStringLiteral("submitted-device"));
    QVERIFY(!server.requests().at(2).target.contains("page="));

    page.hide();
    QVERIFY(csv.isDownloading());
    QVERIFY(success.wait());
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), csvBody());
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
             QStringLiteral("submitted"));
}

void HistoryExportTest::cancelledDialogAndFailureDoNotChangeHistoryTable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(pageEnvelope(QStringLiteral("preserved"))));
    server.enqueueResponse(response(QByteArrayLiteral("application/octet-stream"),
                                    {csvBody()}));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    HistoryService history(&api, &sessions);
    CsvDownloadService csv(&api, &sessions);
    QString selectedPath;
    HistoryPage page(&history,
                     &csv,
                     [&selectedPath](QWidget *, const QString &) { return selectedPath; });
    page.setAttribute(Qt::WA_DontShowOnScreen);
    page.show();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("preserved"));
    auto *exportButton = page.findChild<QPushButton *>(QStringLiteral("historyExportButton"));
    QVERIFY(exportButton);

    exportButton->click();
    QTest::qWait(20);
    QCOMPARE(server.requests().size(), 1);
    QVERIFY(!csv.isDownloading());

    selectedPath = directory.filePath(QStringLiteral("failed.csv"));
    exportButton->click();
    QTRY_VERIFY(!page.findChild<QLabel *>(QStringLiteral("historyErrorLabel"))->text().isEmpty());
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
             QStringLiteral("preserved"));
    QVERIFY(!QFileInfo::exists(selectedPath));
}

QTEST_MAIN(HistoryExportTest)

#include "tst_history_export.moc"
