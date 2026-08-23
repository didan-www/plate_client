#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/HistoryService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/models/RecognitionTableModel.h"
#include "ui/pages/HistoryPage.h"

#include <QDateTimeEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
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

QJsonObject snapshot(const QString &id, const QString &status = QStringLiteral("FAILED"))
{
    const bool failed = status == QStringLiteral("FAILED");
    return QJsonObject {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("recognitionId"), id},
        {QStringLiteral("revision"), 2},
        {QStringLiteral("deviceId"), QStringLiteral("device-1")},
        {QStringLiteral("status"), status},
        {QStringLiteral("plateNumber"),
         failed ? QJsonValue::Null : QJsonValue(QStringLiteral("京A12345"))},
        {QStringLiteral("errorCode"),
         failed ? QJsonValue(QStringLiteral("MODEL_ERROR")) : QJsonValue::Null},
        {QStringLiteral("errorMessage"),
         failed ? QJsonValue(QStringLiteral("模型识别失败")) : QJsonValue::Null},
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
                             {QStringLiteral("requestId"), QStringLiteral("history-ui")},
                             {QStringLiteral("data"),
                              QJsonObject {{QStringLiteral("items"),
                                            QJsonArray {snapshot(id)}},
                                           {QStringLiteral("page"), page},
                                           {QStringLiteral("pageSize"), 100},
                                           {QStringLiteral("total"), total}}},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray failureEnvelope()
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), false},
                             {QStringLiteral("code"), QStringLiteral("TEMPORARY")},
                             {QStringLiteral("message"), QStringLiteral("历史服务暂不可用")},
                             {QStringLiteral("requestId"), QStringLiteral("history-ui-error")},
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

QString queryValue(const MockHttpRequest &request, const QString &name)
{
    const QUrl url(QStringLiteral("http://mock.test") + QString::fromLatin1(request.target));
    return QUrlQuery(url).queryItemValue(name);
}
} // namespace

class HistoryPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void firstShowUsesDefaultDayAndRendersTenColumns();
    void searchResetAndPagingUseOnlySubmittedFilter();
    void hideCancelsAndReshowRefreshesWhileFailurePreservesTable();
    void layoutFitsSupportedSizes_data();
    void layoutFitsSupportedSizes();
};

void HistoryPageTest::firstShowUsesDefaultDayAndRendersTenColumns()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("rec-first"), 1, 201)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime now = time(QStringLiteral("2026-08-24T04:00:00.123Z"));
    HistoryService service(&api, &sessions, [now] { return now; });
    HistoryPage page(&service);
    page.setAttribute(Qt::WA_DontShowOnScreen);
    page.resize(1366 - 280, 768 - 44);
    page.show();

    QTRY_COMPARE(page.tableModel()->rowCount(), 1);
    QCOMPARE(page.historyService(), &service);
    QCOMPARE(page.findChild<QDateTimeEdit *>(QStringLiteral("historyStartTimeEdit"))
                 ->timeZone().offsetFromUtc(now),
             8 * 60 * 60);
    QCOMPARE(page.tableModel()->mode(), RecognitionTableModel::Mode::History);
    QCOMPARE(page.tableModel()->historyService(), &service);
    QCOMPARE(page.tableModel()->columnCount(), 10);
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(
                 0, RecognitionTableModel::HistoryRecognitionIdColumn)).toString(),
             QStringLiteral("rec-first"));
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(
                 0, RecognitionTableModel::HistoryErrorCodeColumn)).toString(),
             QStringLiteral("MODEL_ERROR"));
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(
                 0, RecognitionTableModel::HistoryErrorMessageColumn)).toString(),
             QStringLiteral("模型识别失败"));
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(
                 0, RecognitionTableModel::HistoryDurationColumn)).toString(),
             QStringLiteral("1001"));
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("historyTotalLabel"))->text(),
             QStringLiteral("共 201 条"));
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("historyPageLabel"))->text(),
             QStringLiteral("第 1 页"));
    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("historyPreviousButton"))->isEnabled());
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("historyNextButton"))->isEnabled());
    QVERIFY(page.findChildren<QPushButton *>(QStringLiteral("historyExportButton")).isEmpty());
    QTRY_COMPARE(server.requests().size(), 1);
    QCOMPARE(queryValue(server.requests().first(), QStringLiteral("startTime")),
             QStringLiteral("2026-08-24T00:00:00.000+08:00"));
    QCOMPARE(queryValue(server.requests().first(), QStringLiteral("endTime")),
             QStringLiteral("2026-08-25T00:00:00.000+08:00"));
}

void HistoryPageTest::searchResetAndPagingUseOnlySubmittedFilter()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("default"), 1, 1)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("search"), 1, 201)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("page-2"), 2, 201)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("reset"), 1, 1)));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    const QDateTime now = time(QStringLiteral("2026-08-24T04:00:00.000Z"));
    HistoryService service(&api, &sessions, [now] { return now; });
    HistoryPage page(&service);
    page.setAttribute(Qt::WA_DontShowOnScreen);
    page.show();
    QTRY_COMPARE(page.tableModel()->rowCount(), 1);

    auto *startEdit = page.findChild<QDateTimeEdit *>(QStringLiteral("historyStartTimeEdit"));
    auto *endEdit = page.findChild<QDateTimeEdit *>(QStringLiteral("historyEndTimeEdit"));
    auto *deviceEdit = page.findChild<QLineEdit *>(QStringLiteral("historyDeviceEdit"));
    auto *search = page.findChild<QPushButton *>(QStringLiteral("historySearchButton"));
    auto *reset = page.findChild<QPushButton *>(QStringLiteral("historyResetButton"));
    auto *next = page.findChild<QPushButton *>(QStringLiteral("historyNextButton"));
    QVERIFY(startEdit && endEdit && deviceEdit && search && reset && next);
    startEdit->setDateTime(time(QStringLiteral("2026-08-20T00:00:00.000+08:00")));
    endEdit->setDateTime(time(QStringLiteral("2026-08-21T00:00:00.000+08:00")));
    deviceEdit->setText(QStringLiteral("  device-submitted  "));
    search->click();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("search"));
    QTRY_COMPARE(server.requests().size(), 2);
    QCOMPARE(queryValue(server.requests().at(1), QStringLiteral("page")), QStringLiteral("1"));
    QCOMPARE(queryValue(server.requests().at(1), QStringLiteral("deviceId")),
             QStringLiteral("device-submitted"));

    deviceEdit->setText(QStringLiteral("unsaved-device"));
    next->click();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("page-2"));
    QTRY_COMPARE(server.requests().size(), 3);
    QCOMPARE(queryValue(server.requests().at(2), QStringLiteral("page")), QStringLiteral("2"));
    QCOMPARE(queryValue(server.requests().at(2), QStringLiteral("deviceId")),
             QStringLiteral("device-submitted"));

    reset->click();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("reset"));
    QTRY_COMPARE(server.requests().size(), 4);
    QCOMPARE(queryValue(server.requests().at(3), QStringLiteral("page")), QStringLiteral("1"));
    QVERIFY(queryValue(server.requests().at(3), QStringLiteral("deviceId")).isEmpty());
    QVERIFY(deviceEdit->text().isEmpty());
    QCOMPARE(startEdit->dateTime().date(), QDate(2026, 8, 24));
}

void HistoryPageTest::hideCancelsAndReshowRefreshesWhileFailurePreservesTable()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("initial"), 1, 1)));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("cancelled"), 1, 1), 150));
    server.enqueueResponse(jsonResponse(200, pageEnvelope(QStringLiteral("reshow"), 1, 1)));
    server.enqueueResponse(jsonResponse(503, failureEnvelope()));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    HistoryService service(&api, &sessions);
    HistoryPage page(&service);
    page.setAttribute(Qt::WA_DontShowOnScreen);
    page.show();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("initial"));

    page.findChild<QPushButton *>(QStringLiteral("historySearchButton"))->click();
    QTRY_VERIFY(service.hasActiveRequest());
    QTRY_COMPARE(server.requests().size(), 2);
    page.hide();
    QVERIFY(!service.hasActiveRequest());
    QTest::qWait(170);
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
             QStringLiteral("initial"));
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("historyErrorLabel"))->text().isEmpty());

    page.show();
    QTRY_COMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
                 QStringLiteral("reshow"));
    QTRY_COMPARE(server.requests().size(), 3);
    page.findChild<QPushButton *>(QStringLiteral("historySearchButton"))->click();
    QTRY_COMPARE(page.findChild<QLabel *>(QStringLiteral("historyErrorLabel"))->text(),
                 QStringLiteral("历史服务暂不可用"));
    QCOMPARE(page.tableModel()->data(page.tableModel()->index(0, 0)).toString(),
             QStringLiteral("reshow"));
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("historySearchButton"))->isEnabled());
}

void HistoryPageTest::layoutFitsSupportedSizes_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("1366x768-content") << QSize(1086, 724);
    QTest::newRow("1920x1080-content") << QSize(1640, 1036);
    QTest::newRow("4k-content") << QSize(3560, 2116);
}

void HistoryPageTest::layoutFitsSupportedSizes()
{
    QFETCH(QSize, size);
    MockHttpServer server;
    QVERIFY(server.listen());
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(server.url(), &transport, &sessions);
    HistoryService service(&api, &sessions);
    HistoryPage page(&service);
    page.resize(size);
    page.ensurePolished();
    QVERIFY(page.layout()->activate());
    const QList<QWidget *> controls {
        page.findChild<QDateTimeEdit *>(QStringLiteral("historyStartTimeEdit")),
        page.findChild<QDateTimeEdit *>(QStringLiteral("historyEndTimeEdit")),
        page.findChild<QLineEdit *>(QStringLiteral("historyDeviceEdit")),
        page.findChild<QPushButton *>(QStringLiteral("historySearchButton")),
        page.findChild<QPushButton *>(QStringLiteral("historyResetButton")),
        page.findChild<QPushButton *>(QStringLiteral("historyPreviousButton")),
        page.findChild<QPushButton *>(QStringLiteral("historyNextButton")),
        page.findChild<QTableView *>(QStringLiteral("historyTable")),
    };
    for (QWidget *control : controls) {
        QVERIFY(control);
        const QRect rectangle(control->mapTo(&page, QPoint(0, 0)), control->size());
        QVERIFY2(page.rect().contains(rectangle), qPrintable(control->objectName()));
    }
}

QTEST_MAIN(HistoryPageTest)

#include "tst_history_page.moc"
