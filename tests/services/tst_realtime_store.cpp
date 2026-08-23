#include "domain/SessionContext.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"

#include <QSignalSpy>
#include <QtTest>

#include <optional>

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
                             RecognitionStatus status)
{
    const bool completed = status != RecognitionStatus::Processing;
    return RecognitionSnapshot(
        1,
        id,
        revision,
        QStringLiteral("device-1"),
        status,
        status == RecognitionStatus::Succeeded
            ? std::optional<QString>(QStringLiteral("京A12345")) : std::nullopt,
        status == RecognitionStatus::Failed
            ? std::optional<QString>(QStringLiteral("MODEL_ERROR")) : std::nullopt,
        status == RecognitionStatus::Failed
            ? std::optional<QString>(QStringLiteral("识别失败")) : std::nullopt,
        QDateTime::fromString(QStringLiteral("2026-08-12T10:00:00+08:00"), Qt::ISODate),
        QDateTime::fromString(QStringLiteral("2026-08-12T10:00:01+08:00"), Qt::ISODate),
        completed
            ? std::optional<QDateTime>(QDateTime::fromString(
                  QStringLiteral("2026-08-12T10:00:02+08:00"), Qt::ISODate))
            : std::nullopt,
        completed ? std::optional<qint64>(1000) : std::nullopt);
}
} // namespace

class RealtimeRecognitionStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void insertsNewIdsAndUpdatesInPlace();
    void ignoresDuplicateLowerAndTerminalRollback();
    void currentTracksLatestProcessingAndItsFinalResult();
    void retainsOnlyNewestHundredFirstSeenIds();
    void sessionReplacementAndLogoutClearAndRejectOldGeneration();
};

void RealtimeRecognitionStoreTest::insertsNewIdsAndUpdatesInPlace()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();

    QVERIFY(store.apply(generation, snapshot(QStringLiteral("rec-1"), 1,
                                             RecognitionStatus::Processing)));
    QVERIFY(store.apply(generation, snapshot(QStringLiteral("rec-2"), 1,
                                             RecognitionStatus::Succeeded)));
    QCOMPARE(store.items().size(), 2);
    QCOMPARE(store.items().at(0).recognitionId(), QStringLiteral("rec-2"));
    QCOMPARE(store.items().at(1).recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(store.current()->recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(store.current()->status(), RecognitionStatus::Processing);

    QVERIFY(store.apply(generation, snapshot(QStringLiteral("rec-1"), 2,
                                             RecognitionStatus::Succeeded)));
    QCOMPARE(store.items().size(), 2);
    QCOMPARE(store.items().at(1).recognitionId(), QStringLiteral("rec-1"));
    QCOMPARE(store.items().at(1).revision(), qint64(2));
    QCOMPARE(store.items().at(1).status(), RecognitionStatus::Succeeded);
}

void RealtimeRecognitionStoreTest::ignoresDuplicateLowerAndTerminalRollback()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();
    QVERIFY(store.apply(generation, snapshot(QStringLiteral("rec-1"), 3,
                                             RecognitionStatus::Succeeded)));
    QSignalSpy changes(&store, &RealtimeRecognitionStore::itemsChanged);

    QVERIFY(!store.apply(generation, snapshot(QStringLiteral("rec-1"), 3,
                                              RecognitionStatus::Succeeded)));
    QVERIFY(!store.apply(generation, snapshot(QStringLiteral("rec-1"), 2,
                                              RecognitionStatus::Processing)));
    QVERIFY(!store.apply(generation, snapshot(QStringLiteral("rec-1"), 4,
                                              RecognitionStatus::Processing)));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(store.items().constFirst().revision(), qint64(3));
    QCOMPARE(store.items().constFirst().status(), RecognitionStatus::Succeeded);
}

void RealtimeRecognitionStoreTest::currentTracksLatestProcessingAndItsFinalResult()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();

    QVERIFY(store.apply(generation, snapshot(QStringLiteral("old"), 1,
                                             RecognitionStatus::Processing)));
    QVERIFY(store.apply(generation, snapshot(QStringLiteral("current"), 1,
                                             RecognitionStatus::Processing)));
    QCOMPARE(store.current()->recognitionId(), QStringLiteral("current"));

    QVERIFY(store.apply(generation, snapshot(QStringLiteral("old"), 2,
                                             RecognitionStatus::Failed)));
    QCOMPARE(store.current()->recognitionId(), QStringLiteral("current"));
    QCOMPARE(store.current()->status(), RecognitionStatus::Processing);

    QVERIFY(store.apply(generation, snapshot(QStringLiteral("current"), 2,
                                             RecognitionStatus::Succeeded)));
    QCOMPARE(store.current()->recognitionId(), QStringLiteral("current"));
    QCOMPARE(store.current()->status(), RecognitionStatus::Succeeded);
    QCOMPARE(store.current()->revision(), qint64(2));
}

void RealtimeRecognitionStoreTest::retainsOnlyNewestHundredFirstSeenIds()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();

    for (int index = 1; index <= 101; ++index) {
        QVERIFY(store.apply(generation,
                            snapshot(QStringLiteral("rec-%1").arg(index), 1,
                                     RecognitionStatus::Succeeded)));
    }
    QCOMPARE(store.items().size(), 100);
    QCOMPARE(store.items().constFirst().recognitionId(), QStringLiteral("rec-101"));
    QCOMPARE(store.items().constLast().recognitionId(), QStringLiteral("rec-2"));
    for (const RecognitionSnapshot &item : store.items()) {
        QVERIFY(item.recognitionId() != QStringLiteral("rec-1"));
    }
}

void RealtimeRecognitionStoreTest::sessionReplacementAndLogoutClearAndRejectOldGeneration()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    QVERIFY(sessions.establishSession(session()));
    const quint64 oldGeneration = sessions.currentGeneration();
    QVERIFY(store.apply(oldGeneration, snapshot(QStringLiteral("old"), 1,
                                                RecognitionStatus::Processing)));
    QVERIFY(store.current().has_value());

    QVERIFY(sessions.establishSession(session()));
    const quint64 newGeneration = sessions.currentGeneration();
    QVERIFY(newGeneration != oldGeneration);
    QVERIFY(store.items().isEmpty());
    QVERIFY(!store.current().has_value());
    QVERIFY(!store.apply(oldGeneration, snapshot(QStringLiteral("stale"), 1,
                                                 RecognitionStatus::Processing)));
    QVERIFY(store.apply(newGeneration, snapshot(QStringLiteral("new"), 1,
                                                RecognitionStatus::Processing)));
    QCOMPARE(store.items().size(), 1);

    sessions.clear();
    QCOMPARE(store.activeSessionGeneration(), quint64(0));
    QVERIFY(store.items().isEmpty());
    QVERIFY(!store.current().has_value());
    QVERIFY(!store.apply(newGeneration, snapshot(QStringLiteral("late"), 2,
                                                 RecognitionStatus::Succeeded)));
}

QTEST_MAIN(RealtimeRecognitionStoreTest)

#include "tst_realtime_store.moc"
