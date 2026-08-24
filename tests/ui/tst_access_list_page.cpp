#include "domain/SessionContext.h"
#include "infrastructure/http/AccessListApiClient.h"
#include "infrastructure/http/HttpClient.h"
#include "services/AccessListService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/models/AccessListTableModel.h"
#include "ui/pages/AccessListPage.h"
#include "ui/widgets/AccessListCreateDialog.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTableView>
#include <QUrlQuery>
#include <QtTest>

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

QJsonObject record(qint64 id,
                   const QString &plate = QStringLiteral("京A12345"),
                   const QString &listType = QStringLiteral("WHITE"),
                   const QString &remark = QStringLiteral("教学车辆"))
{
    return QJsonObject {{QStringLiteral("id"), id},
                        {QStringLiteral("listType"), listType},
                        {QStringLiteral("plateNumber"), plate},
                        {QStringLiteral("remark"), remark},
                        {QStringLiteral("createdBy"), QStringLiteral("operator")},
                        {QStringLiteral("createdAt"),
                         QStringLiteral("2026-08-24T10:20:30.000+08:00")}};
}

QByteArray envelope(const QJsonValue &data,
                    bool success = true,
                    const QString &code = QStringLiteral("OK"),
                    const QString &message = {})
{
    return QJsonDocument(QJsonObject {{QStringLiteral("success"), success},
                                      {QStringLiteral("code"), code},
                                      {QStringLiteral("message"), message},
                                      {QStringLiteral("requestId"),
                                       QStringLiteral("access-ui")},
                                      {QStringLiteral("data"), data}})
        .toJson(QJsonDocument::Compact);
}

QByteArray pageEnvelope(const QJsonArray &items, int page, qint64 total)
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

QString queryValue(const MockHttpRequest &request, const QString &name)
{
    const QUrl url(QStringLiteral("http://mock.test") + QString::fromLatin1(request.target));
    return QUrlQuery(url).queryItemValue(name);
}

struct Harness final
{
    explicit Harness(HttpClient::TimeoutResolver timeoutResolver = {})
    {
        if (!server.listen() || !sessions.establishSession(session())) {
            qFatal("Unable to initialize access-list page harness.");
        }
        transport = std::make_unique<HttpClient>(
            &network,
            [this](const RequestContext &context) { return sessions.isCurrent(context); },
            timeoutResolver,
            nullptr);
        api = std::make_unique<AccessListApiClient>(server.url(),
                                                    transport.get(),
                                                    &sessions);
        service = std::make_unique<AccessListService>(api.get(), &sessions);
    }

    ~Harness()
    {
        cleanup();
    }

    void cleanup()
    {
        if (!page) {
            return;
        }
        if (AccessListCreateDialog *dialog = page->activeCreateDialog()) {
            dialog->close();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        page->close();
        page.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void createPage(AccessListPage::DeleteConfirmer confirmer = {})
    {
        page = std::make_unique<AccessListPage>(service.get(), std::move(confirmer));
        page->setAttribute(Qt::WA_DontShowOnScreen);
        page->resize(1086, 724);
    }

    MockHttpServer server;
    SessionService sessions;
    QNetworkAccessManager network;
    std::unique_ptr<HttpClient> transport;
    std::unique_ptr<AccessListApiClient> api;
    std::unique_ptr<AccessListService> service;
    std::unique_ptr<AccessListPage> page;
};
} // namespace

class AccessListPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void firstShowSearchTabsPagingAndReshowUseServerResults();
    void createSuccessConflictAndBusyStateUseServiceResults();
    void timeoutVerificationShowsAppliedNotAppliedAndUnknown();
    void deleteConfirmationAlreadyAbsentAndEmptyPageFallback();
    void hiddenPageCancelsRefreshStartedByCompletedWrite();
    void layoutFitsSupportedSizes_data();
    void layoutFitsSupportedSizes();
};

void AccessListPageTest::firstShowSearchTabsPagingAndReshowUseServerResults()
{
    Harness harness;
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(1)}, 1, 201)));
    harness.createPage();
    harness.page->show();

    QTRY_COMPARE(harness.page->tableModel()->rowCount(), 1);
    QCOMPARE(harness.page->currentListType(), AccessListType::White);
    QCOMPARE(harness.page->tableModel()->columnCount(), 5);
    QCOMPARE(harness.page->tableModel()->data(
                 harness.page->tableModel()->index(0, AccessListTableModel::SequenceColumn)),
             QVariant(1));
    QCOMPARE(harness.page->tableModel()->data(
                 harness.page->tableModel()->index(0, AccessListTableModel::PlateColumn)),
             QVariant(QStringLiteral("京A12345")));
    QCOMPARE(harness.page->tableModel()->data(
                 harness.page->tableModel()->index(0, AccessListTableModel::CreatedAtColumn)),
             QVariant(QStringLiteral("2026-08-24 10:20:30")));
    QTRY_COMPARE(harness.server.requests().size(), 1);
    QCOMPARE(queryValue(harness.server.requests().first(), QStringLiteral("listType")),
             QStringLiteral("WHITE"));
    QCOMPARE(queryValue(harness.server.requests().first(), QStringLiteral("keyword")),
             QString());
    QCOMPARE(queryValue(harness.server.requests().first(), QStringLiteral("page")),
             QStringLiteral("1"));

    auto *keyword = harness.page->findChild<QLineEdit *>(
        QStringLiteral("accessListKeywordEdit"));
    auto *search = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListSearchButton"));
    auto *reset = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListResetButton"));
    auto *tabs = harness.page->findChild<QTabBar *>(QStringLiteral("accessListTabs"));
    auto *next = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListNextButton"));
    auto *previous = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListPreviousButton"));
    auto *refresh = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListRefreshButton"));
    QVERIFY(keyword && search && reset && tabs && next && previous && refresh);

    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(2, QStringLiteral("京A00001"))}, 1, 1), 120));
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(3, QStringLiteral("沪B00002"))}, 1, 1)));
    keyword->setText(QStringLiteral("京a"));
    search->click();
    QTRY_COMPARE(harness.server.requests().size(), 2);
    keyword->setText(QStringLiteral("沇b"));
    emit keyword->returnPressed();
    QTRY_COMPARE(harness.page->tableModel()->data(
                     harness.page->tableModel()->index(0, AccessListTableModel::PlateColumn))
                     .toString(),
                 QStringLiteral("沪B00002"));
    QTest::qWait(150);
    QCOMPARE(harness.page->tableModel()->data(
                 harness.page->tableModel()->index(0, AccessListTableModel::PlateColumn))
                 .toString(),
             QStringLiteral("沪B00002"));

    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(4,
                                        QStringLiteral("粤C00003"),
                                        QStringLiteral("BLACK"))},
                     1,
                     201)));
    tabs->setCurrentIndex(1);
    QTRY_COMPARE(harness.page->tableModel()->data(
                     harness.page->tableModel()->index(0, AccessListTableModel::PlateColumn))
                     .toString(),
                 QStringLiteral("粤C00003"));
    QCOMPARE(keyword->text(), QString());
    QCOMPARE(queryValue(harness.server.requests().last(), QStringLiteral("listType")),
             QStringLiteral("BLACK"));
    QVERIFY(next->isEnabled());

    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(5,
                                        QStringLiteral("粤C00004"),
                                        QStringLiteral("BLACK"))},
                     2,
                     201)));
    next->click();
    QTRY_COMPARE(harness.page->findChild<QLabel *>(
                     QStringLiteral("accessListPageLabel"))->text(),
                 QStringLiteral("第 2 页"));
    QVERIFY(previous->isEnabled());

    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(6,
                                        QStringLiteral("粤C00005"),
                                        QStringLiteral("BLACK"))},
                     2,
                     201)));
    refresh->click();
    QTRY_COMPARE(harness.page->tableModel()->data(
                     harness.page->tableModel()->index(0, AccessListTableModel::PlateColumn))
                     .toString(),
                 QStringLiteral("粤C00005"));
    QCOMPARE(queryValue(harness.server.requests().last(), QStringLiteral("page")),
             QStringLiteral("2"));

    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(7,
                                        QStringLiteral("粤C00006"),
                                        QStringLiteral("BLACK"))},
                     2,
                     201),
        100));
    refresh->click();
    QTRY_VERIFY(harness.service->hasActiveQuery());
    QTRY_COMPARE(harness.server.requests().size(), 7);
    harness.page->hide();
    QVERIFY(!harness.service->hasActiveQuery());
    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(8,
                                        QStringLiteral("粤C00007"),
                                        QStringLiteral("BLACK"))},
                     2,
                     201)));
    harness.page->show();
    QTRY_COMPARE(harness.page->tableModel()->data(
                     harness.page->tableModel()->index(0, AccessListTableModel::PlateColumn))
                     .toString(),
                 QStringLiteral("粤C00007"));

    keyword->setText(QStringLiteral("未提交条件"));
    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(9,
                                        QStringLiteral("粤C00008"),
                                        QStringLiteral("BLACK"))},
                     1,
                     1)));
    reset->click();
    QTRY_COMPARE(harness.page->findChild<QLabel *>(
                     QStringLiteral("accessListPageLabel"))->text(),
                 QStringLiteral("第 1 页"));
    QCOMPARE(keyword->text(), QString());
    QCOMPARE(queryValue(harness.server.requests().last(), QStringLiteral("listType")),
             QStringLiteral("BLACK"));
    QCOMPARE(queryValue(harness.server.requests().last(), QStringLiteral("keyword")),
             QString());
    QCOMPARE(queryValue(harness.server.requests().last(), QStringLiteral("page")),
             QStringLiteral("1"));
}

void AccessListPageTest::createSuccessConflictAndBusyStateUseServiceResults()
{
    Harness harness;
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(1)}, 1, 1)));
    harness.createPage();
    harness.page->show();
    QTRY_COMPARE(harness.page->tableModel()->rowCount(), 1);

    auto *createButton = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListCreateButton"));
    auto *searchButton = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListSearchButton"));
    QVERIFY(createButton && searchButton);
    std::optional<AccessListServiceErrorCode> writeError;
    std::optional<AccessListWriteDisposition> writeDisposition;
    connect(harness.service.get(), &AccessListService::writeFailed, this,
            [&writeError](const AccessListServiceError &error) {
                writeError = error.code();
            });
    connect(harness.service.get(), &AccessListService::writeCompleted, this,
            [&writeDisposition](const AccessListWriteResult &result) {
                writeDisposition = result.disposition();
            });
    createButton->click();
    QPointer<AccessListCreateDialog> dialog(harness.page->activeCreateDialog());
    QVERIFY(dialog);
    dialog->findChild<QLineEdit *>(QStringLiteral("accessListCreatePlateEdit"))
        ->setText(QStringLiteral(" 沪a54321 "));
    dialog->findChild<QPlainTextEdit *>(QStringLiteral("accessListCreateRemarkEdit"))
        ->setPlainText(QStringLiteral("新增车辆"));
    harness.server.enqueueResponse(jsonResponse(
        200,
        envelope(record(2,
                        QStringLiteral("沪A54321"),
                        QStringLiteral("WHITE"),
                        QStringLiteral("新增车辆")))));
    harness.server.enqueueResponse(jsonResponse(
        200,
        pageEnvelope(QJsonArray {record(2, QStringLiteral("沪A54321")), record(1)},
                     1,
                     2)));
    dialog->findChild<QPushButton *>(QStringLiteral("accessListCreateSubmitButton"))->click();
    QVERIFY(!createButton->isEnabled());
    QVERIFY(searchButton->isEnabled());
    QTRY_VERIFY(writeError || writeDisposition);
    QVERIFY2(!writeError,
             qPrintable(QStringLiteral("write error %1")
                            .arg(writeError ? static_cast<int>(*writeError) : -1)));
    QCOMPARE(*writeDisposition, AccessListWriteDisposition::Applied);
    QTRY_COMPARE(harness.server.requests().size(), 3);
    QTRY_VERIFY(!harness.service->isWriteBusy());
    QCOMPARE(harness.page->findChild<QLabel *>(
                 QStringLiteral("accessListStatusLabel"))->text(),
             QStringLiteral("新增已生效。"));
    QTRY_VERIFY(dialog.isNull());
    QTRY_COMPARE(harness.page->tableModel()->rowCount(), 2);
    QCOMPARE(harness.server.requests().at(1).method, QByteArrayLiteral("POST"));
    QCOMPARE(harness.server.requests().size(), 3);

    createButton->click();
    dialog = harness.page->activeCreateDialog();
    QVERIFY(dialog);
    dialog->findChild<QLineEdit *>(QStringLiteral("accessListCreatePlateEdit"))
        ->setText(QStringLiteral("京B22222"));
    harness.server.enqueueResponse(jsonResponse(
        409,
        envelope(QJsonValue::Null,
                 false,
                 QStringLiteral("ACCESS_LIST_CONFLICT_BLACK"),
                 QStringLiteral("车牌冲突"))));
    dialog->findChild<QPushButton *>(QStringLiteral("accessListCreateSubmitButton"))->click();
    QTRY_VERIFY(!dialog->isSubmitting());
    QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("accessListCreateErrorLabel"))
                ->text()
                .contains(QStringLiteral("黑名单")));
    QCOMPARE(harness.server.requests().size(), 4);
}

void AccessListPageTest::timeoutVerificationShowsAppliedNotAppliedAndUnknown()
{
    const auto timeoutResolver = [](HttpRequestKind kind) {
        return kind == HttpRequestKind::AccessListWrite
                   ? std::chrono::milliseconds(15)
                   : std::chrono::milliseconds(500);
    };

    auto runScenario = [&](const MockHttpResponse &verification,
                           const QString &expectedDialogError,
                           bool shouldClose) {
        Harness harness(timeoutResolver);
        harness.server.enqueueResponse(jsonResponse(
            200, pageEnvelope(QJsonArray {record(1)}, 1, 1)));
        harness.createPage();
        harness.page->show();
        QTRY_COMPARE(harness.page->tableModel()->rowCount(), 1);
        harness.page->findChild<QPushButton *>(QStringLiteral("accessListCreateButton"))
            ->click();
        QPointer<AccessListCreateDialog> dialog(harness.page->activeCreateDialog());
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>(QStringLiteral("accessListCreatePlateEdit"))
            ->setText(QStringLiteral("京A99999"));
        harness.server.enqueueResponse(jsonResponse(
            200, envelope(record(9, QStringLiteral("京A99999"))), 100));
        harness.server.enqueueResponse(verification);
        if (shouldClose) {
            harness.server.enqueueResponse(jsonResponse(
                200, pageEnvelope(QJsonArray {record(9, QStringLiteral("京A99999"))}, 1, 1)));
        }
        dialog->findChild<QPushButton *>(QStringLiteral("accessListCreateSubmitButton"))->click();
        QTRY_COMPARE(harness.server.requests().size(), shouldClose ? 4 : 3);
        if (shouldClose) {
            QTRY_VERIFY(dialog.isNull());
            QCOMPARE(harness.page->findChild<QLabel *>(
                         QStringLiteral("accessListStatusLabel"))->text(),
                     QStringLiteral("新增已生效。"));
        } else {
            QVERIFY(dialog);
            QTRY_VERIFY(!dialog->isSubmitting());
            QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("accessListCreateErrorLabel"))
                        ->text()
                        .contains(expectedDialogError));
            QCOMPARE(harness.page->tableModel()->rowCount(), 1);
        }
    };

    runScenario(jsonResponse(200,
                             envelope(record(9, QStringLiteral("京A99999")))),
                {},
                true);
    runScenario(jsonResponse(200,
                             envelope(record(9,
                                             QStringLiteral("京A99999"),
                                             QStringLiteral("BLACK")))),
                QStringLiteral("黑名单"),
                false);
    runScenario(jsonResponse(
                    503,
                    envelope(QJsonValue::Null,
                             false,
                             QStringLiteral("DATABASE_UNAVAILABLE"))),
                QStringLiteral("无法确认"),
                false);
}

void AccessListPageTest::deleteConfirmationAlreadyAbsentAndEmptyPageFallback()
{
    Harness harness;
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(101)}, 1, 101)));
    QString confirmedPlate;
    AccessListType confirmedType = AccessListType::Black;
    harness.createPage([&](QWidget *, AccessListType type, const QString &plate) {
        confirmedType = type;
        confirmedPlate = plate;
        return true;
    });
    harness.page->show();
    QTRY_COMPARE(harness.page->tableModel()->rowCount(), 1);

    auto *table = harness.page->findChild<QTableView *>(QStringLiteral("accessListTable"));
    auto *deleteButton = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListDeleteButton"));
    auto *next = harness.page->findChild<QPushButton *>(
        QStringLiteral("accessListNextButton"));
    QVERIFY(table && deleteButton && next);
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(101)}, 2, 101)));
    next->click();
    QTRY_COMPARE(harness.page->findChild<QLabel *>(
                     QStringLiteral("accessListPageLabel"))->text(),
                 QStringLiteral("第 2 页"));
    table->selectRow(0);
    QVERIFY(deleteButton->isEnabled());

    harness.server.enqueueResponse(jsonResponse(200, envelope(QJsonValue::Null), 60));
    harness.server.enqueueResponse(jsonResponse(200, pageEnvelope({}, 2, 100)));
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(1)}, 1, 100)));
    deleteButton->click();
    QCOMPARE(confirmedType, AccessListType::White);
    QCOMPARE(confirmedPlate, QStringLiteral("京A12345"));
    QCOMPARE(harness.page->tableModel()->rowCount(), 1);
    QTRY_COMPARE(harness.page->findChild<QLabel *>(
                     QStringLiteral("accessListPageLabel"))->text(),
                 QStringLiteral("第 1 页"));
    QCOMPARE(harness.server.requests().size(), 5);
    QCOMPARE(harness.server.requests().at(2).method, QByteArrayLiteral("DELETE"));

    table->selectRow(0);
    harness.server.enqueueResponse(jsonResponse(
        404,
        envelope(QJsonValue::Null,
                 false,
                 QStringLiteral("ACCESS_LIST_NOT_FOUND"))));
    harness.server.enqueueResponse(jsonResponse(200, pageEnvelope({}, 1, 0)));
    deleteButton->click();
    QTRY_COMPARE(harness.page->tableModel()->rowCount(), 0);
    QCOMPARE(harness.page->findChild<QLabel *>(
                 QStringLiteral("accessListStatusLabel"))->text(),
             QStringLiteral("记录已不存在，已刷新。"));
    QCOMPARE(harness.server.requests().size(), 7);
}

void AccessListPageTest::hiddenPageCancelsRefreshStartedByCompletedWrite()
{
    Harness harness;
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(1)}, 1, 1)));
    harness.createPage();
    harness.page->show();
    QTRY_COMPARE(harness.page->tableModel()->rowCount(), 1);
    harness.page->findChild<QPushButton *>(QStringLiteral("accessListCreateButton"))
        ->click();
    QPointer<AccessListCreateDialog> dialog(harness.page->activeCreateDialog());
    QVERIFY(dialog);
    dialog->findChild<QLineEdit *>(QStringLiteral("accessListCreatePlateEdit"))
        ->setText(QStringLiteral("京A88888"));
    harness.server.enqueueResponse(jsonResponse(
        200,
        envelope(record(8,
                        QStringLiteral("京A88888"),
                        QStringLiteral("WHITE"),
                        QString())),
        60));
    harness.server.enqueueResponse(jsonResponse(
        200, pageEnvelope(QJsonArray {record(8, QStringLiteral("京A88888"))}, 1, 1), 100));
    dialog->findChild<QPushButton *>(QStringLiteral("accessListCreateSubmitButton"))->click();
    QTRY_COMPARE(harness.server.requests().size(), 2);
    harness.page->hide();
    QTRY_VERIFY(!harness.service->isWriteBusy());
    QVERIFY(!harness.service->hasActiveQuery());
}

void AccessListPageTest::layoutFitsSupportedSizes_data()
{
    QTest::addColumn<QSize>("pageSize");
    QTest::newRow("1366x768") << QSize(1086, 724);
    QTest::newRow("1920x1080") << QSize(1640, 1036);
    QTest::newRow("3840x2160") << QSize(3560, 2116);
}

void AccessListPageTest::layoutFitsSupportedSizes()
{
    QFETCH(QSize, pageSize);
    Harness harness;
    harness.createPage();
    harness.page->resize(pageSize);
    harness.page->ensurePolished();
    QVERIFY(harness.page->layout()->activate());
    const QList<QPushButton *> buttons = harness.page->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        const QRect rect(button->mapTo(harness.page.get(), QPoint(0, 0)), button->size());
        QVERIFY(harness.page->rect().contains(rect));
        QVERIFY(button->fontMetrics().horizontalAdvance(button->text())
                <= button->contentsRect().width());
    }
}

QTEST_MAIN(AccessListPageTest)

#include "tst_access_list_page.moc"
