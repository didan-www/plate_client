#include "domain/RecognitionSnapshot.h"
#include "domain/SessionContext.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/HistoryService.h"
#include "services/SessionService.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "ui/PlateNumberFormatter.h"
#include "ui/models/RecognitionTableModel.h"

#include <QSignalSpy>
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

RecognitionSnapshot snapshot(const QString &id,
                             qint64 revision,
                             RecognitionStatus status,
                             const QString &device,
                             const QDateTime &capturedAt)
{
    const bool succeeded = status == RecognitionStatus::Succeeded;
    const bool failed = status == RecognitionStatus::Failed;
    return RecognitionSnapshot(1,
                               id,
                               revision,
                               device,
                               status,
                               succeeded
                                   ? std::optional<QString>(QStringLiteral("陕U806RG"))
                                   : std::nullopt,
                               failed ? std::optional<QString>(QStringLiteral("MODEL_ERROR"))
                                      : std::nullopt,
                               failed ? std::optional<QString>(QStringLiteral("模型失败"))
                                      : std::nullopt,
                               capturedAt,
                               capturedAt.addSecs(1),
                               status == RecognitionStatus::Processing
                                   ? std::nullopt
                                   : std::optional<QDateTime>(capturedAt.addSecs(2)),
                               status == RecognitionStatus::Processing
                                   ? std::nullopt
                                   : std::optional<qint64>(1200));
}
} // namespace

class RecognitionTableModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void formatsOnlySupportedRecognitionPlateValues();
    void exposesFixedReadOnlyFiveColumnContract();
    void mapsStoreItemsAndCapturedTime();
    void followsUpdatesAndStoreLimitWithoutCopyingItems();
    void exposesHistoryTenColumnContractWithoutBusinessCopy();
};

void RecognitionTableModelTest::formatsOnlySupportedRecognitionPlateValues()
{
    QCOMPARE(formatPlateNumberForDisplay(QStringLiteral("陕U806RG")),
             QStringLiteral("陕U·806RG"));
    QCOMPARE(formatPlateNumberForDisplay(QStringLiteral("京A12345")),
             QStringLiteral("京A·12345"));
    QCOMPARE(formatPlateNumberForDisplay(QStringLiteral("京A·12345")),
             QStringLiteral("京A·12345"));
    QCOMPARE(formatPlateNumberForDisplay(QStringLiteral("京112345")),
             QStringLiteral("京112345"));
    QCOMPARE(formatPlateNumberForDisplay(QStringLiteral("TEST123")),
             QStringLiteral("TEST123"));
    QCOMPARE(formatPlateNumberForDisplay(QStringLiteral("--")), QStringLiteral("--"));
    QCOMPARE(formatPlateNumberForDisplay(QString()), QString());
}

void RecognitionTableModelTest::exposesFixedReadOnlyFiveColumnContract()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    RecognitionTableModel model(&store);

    QCOMPARE(model.store(), &store);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 5);
    const QStringList expectedHeaders {
        QStringLiteral("序号"),
        QStringLiteral("设备"),
        QStringLiteral("车牌号"),
        QStringLiteral("状态"),
        QStringLiteral("时间"),
    };
    for (int column = 0; column < expectedHeaders.size(); ++column) {
        QCOMPARE(model.headerData(column, Qt::Horizontal).toString(),
                 expectedHeaders.at(column));
    }
    QVERIFY(!model.index(0, 0).isValid());
}

void RecognitionTableModelTest::exposesHistoryTenColumnContractWithoutBusinessCopy()
{
    SessionService sessions;
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient api(QUrl(QStringLiteral("https://example.test")),
                             &transport,
                             &sessions);
    HistoryService history(&api, &sessions);
    RecognitionTableModel model(&history);

    QCOMPARE(model.mode(), RecognitionTableModel::Mode::History);
    QCOMPARE(model.historyService(), &history);
    QVERIFY(model.store() == nullptr);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 10);
    const QStringList expectedHeaders {
        QStringLiteral("记录 ID"), QStringLiteral("设备"), QStringLiteral("车牌号"),
        QStringLiteral("状态"), QStringLiteral("错误码"), QStringLiteral("错误信息"),
        QStringLiteral("拍摄时间"), QStringLiteral("开始时间"),
        QStringLiteral("完成时间"), QStringLiteral("耗时"),
    };
    for (int column = 0; column < expectedHeaders.size(); ++column) {
        QCOMPARE(model.headerData(column, Qt::Horizontal).toString(),
                 expectedHeaders.at(column));
    }
}

void RecognitionTableModelTest::mapsStoreItemsAndCapturedTime()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    RecognitionTableModel model(&store);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();
    const QDateTime processingTime = QDateTime::fromString(
        QStringLiteral("2026-08-24T09:10:11+08:00"), Qt::ISODate);
    const QDateTime successTime = processingTime.addSecs(30);
    const QDateTime failureTime = successTime.addSecs(30);

    QVERIFY(store.apply(generation,
                        snapshot(QStringLiteral("processing"), 1,
                                 RecognitionStatus::Processing,
                                 QStringLiteral("device-processing"), processingTime)));
    QVERIFY(store.apply(generation,
                        snapshot(QStringLiteral("success"), 1,
                                 RecognitionStatus::Succeeded,
                                 QStringLiteral("device-success"), successTime)));
    QVERIFY(store.apply(generation,
                        snapshot(QStringLiteral("failure"), 1,
                                 RecognitionStatus::Failed,
                                 QStringLiteral("device-failure"), failureTime)));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::SequenceColumn)).toInt(), 1);
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::DeviceColumn)).toString(),
             QStringLiteral("device-failure"));
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::PlateColumn)).toString(),
             QStringLiteral("--"));
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::StatusColumn)).toString(),
             QStringLiteral("识别失败"));
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::CapturedAtColumn)).toString(),
             failureTime.toString(Qt::ISODate));
    QCOMPARE(model.data(model.index(1, RecognitionTableModel::PlateColumn)).toString(),
             QStringLiteral("陕U·806RG"));
    QCOMPARE(model.data(model.index(1, RecognitionTableModel::StatusColumn)).toString(),
             QStringLiteral("识别成功"));
    QCOMPARE(model.data(model.index(2, RecognitionTableModel::StatusColumn)).toString(),
             QStringLiteral("识别中"));
    QVERIFY(!model.flags(model.index(0, 0)).testFlag(Qt::ItemIsEditable));
    QCOMPARE(model.data(model.index(0, 0), Qt::TextAlignmentRole).toInt(),
             static_cast<int>(Qt::AlignCenter));
}

void RecognitionTableModelTest::followsUpdatesAndStoreLimitWithoutCopyingItems()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    RecognitionTableModel model(&store);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();
    const QDateTime capturedAt = QDateTime::fromString(
        QStringLiteral("2026-08-24T10:00:00+08:00"), Qt::ISODate);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);

    for (int index = 1; index <= 101; ++index) {
        QVERIFY(store.apply(generation,
                            snapshot(QStringLiteral("rec-%1").arg(index), 1,
                                     RecognitionStatus::Processing,
                                     QStringLiteral("device-%1").arg(index),
                                     capturedAt.addSecs(index))));
    }
    QCOMPARE(model.rowCount(), 100);
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::DeviceColumn)).toString(),
             QStringLiteral("device-101"));
    QCOMPARE(model.data(model.index(99, RecognitionTableModel::DeviceColumn)).toString(),
             QStringLiteral("device-2"));
    QCOMPARE(resets.count(), 101);

    QVERIFY(store.apply(generation,
                        snapshot(QStringLiteral("rec-101"), 2,
                                 RecognitionStatus::Succeeded,
                                 QStringLiteral("device-101"),
                                 capturedAt.addSecs(101))));
    QCOMPARE(model.rowCount(), 100);
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::PlateColumn)).toString(),
             QStringLiteral("陕U·806RG"));
    QCOMPARE(model.data(model.index(0, RecognitionTableModel::StatusColumn)).toString(),
             QStringLiteral("识别成功"));
}

QTEST_MAIN(RecognitionTableModelTest)

#include "tst_recognition_table_model.moc"
