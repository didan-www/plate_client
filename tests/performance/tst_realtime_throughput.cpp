#include "domain/RecognitionSnapshot.h"
#include "domain/SessionContext.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"
#include "ui/models/RecognitionTableModel.h"

#include <QElapsedTimer>
#include <QTableView>
#include <QTimer>
#include <QtTest>

#include <optional>

namespace {
SessionContext session()
{
    return SessionContext(QStringLiteral("吞吐验收用户"),
                          QStringLiteral("access-secret"),
                          QDateTime::currentDateTime().addSecs(3600),
                          QStringLiteral("broker.test"),
                          8883,
                          true,
                          QStringLiteral("mqtt-user"),
                          QStringLiteral("mqtt-secret"),
                          QStringLiteral("plate/management/recognition-events"));
}

RecognitionSnapshot snapshot(int sequence)
{
    const QDateTime capturedAt = QDateTime::fromString(
        QStringLiteral("2026-08-25T10:00:00+08:00"), Qt::ISODate).addSecs(sequence - 1);
    return RecognitionSnapshot(1,
                               QStringLiteral("throughput-%1").arg(sequence),
                               1,
                               QStringLiteral("device-%1").arg((sequence % 10) + 1),
                               RecognitionStatus::Succeeded,
                               QStringLiteral("京A%1").arg(sequence, 5, 10, QLatin1Char('0')),
                               std::nullopt,
                               std::nullopt,
                               capturedAt,
                               capturedAt,
                               capturedAt.addMSecs(120),
                               120);
}
} // namespace

class RealtimeThroughputTest final : public QObject
{
    Q_OBJECT

private slots:
    void oneEventPerSecondKeepsModelBoundedAndEventLoopResponsive();
};

void RealtimeThroughputTest::oneEventPerSecondKeepsModelBoundedAndEventLoopResponsive()
{
    SessionService sessions;
    RealtimeRecognitionStore store(&sessions);
    RecognitionTableModel model(&store);
    QVERIFY(sessions.establishSession(session()));
    const quint64 generation = sessions.currentGeneration();

    QTableView view;
    view.setModel(&model);
    view.resize(900, 500);
    view.show();
    QTRY_VERIFY(view.isVisible());

    QElapsedTimer wallClock;
    wallClock.start();
    qint64 previousProbeMs = wallClock.elapsed();
    qint64 maximumProbeGapMs = 0;
    qint64 maximumApplyNs = 0;
    int probeCount = 0;
    int eventCount = 0;
    int maximumRows = 0;
    bool everyEventAccepted = true;

    QTimer responsivenessProbe;
    responsivenessProbe.setTimerType(Qt::PreciseTimer);
    responsivenessProbe.setInterval(25);
    connect(&responsivenessProbe, &QTimer::timeout, this, [&] {
        const qint64 nowMs = wallClock.elapsed();
        maximumProbeGapMs = qMax(maximumProbeGapMs, nowMs - previousProbeMs);
        previousProbeMs = nowMs;
        ++probeCount;
    });

    QTimer eventSource;
    eventSource.setTimerType(Qt::PreciseTimer);
    eventSource.setInterval(1000);
    connect(&eventSource, &QTimer::timeout, this, [&] {
        ++eventCount;
        QElapsedTimer applyTimer;
        applyTimer.start();
        everyEventAccepted = store.apply(generation, snapshot(eventCount))
                             && everyEventAccepted;
        maximumApplyNs = qMax(maximumApplyNs, applyTimer.nsecsElapsed());
        maximumRows = qMax(maximumRows, model.rowCount());
        if (eventCount == 101) {
            eventSource.stop();
            responsivenessProbe.stop();
        }
    });

    responsivenessProbe.start();
    eventSource.start();
    QTRY_COMPARE_WITH_TIMEOUT(eventCount, 101, 115000);

    QVERIFY(everyEventAccepted);
    QCOMPARE(maximumRows, 100);
    QCOMPARE(store.items().size(), 100);
    QCOMPARE(model.rowCount(), 100);
    QCOMPARE(view.model(), &model);
    QCOMPARE(store.items().constFirst().recognitionId(), QStringLiteral("throughput-101"));
    QCOMPARE(store.items().constLast().recognitionId(), QStringLiteral("throughput-2"));
    QVERIFY2(wallClock.elapsed() >= 100000,
             qPrintable(QStringLiteral("elapsed=%1ms").arg(wallClock.elapsed())));
    QVERIFY2(probeCount >= 2000,
             qPrintable(QStringLiteral("event-loop probes=%1").arg(probeCount)));
    QVERIFY2(maximumProbeGapMs < 500,
             qPrintable(QStringLiteral("maximum event-loop gap=%1ms")
                            .arg(maximumProbeGapMs)));
    QVERIFY2(maximumApplyNs < 100000000,
             qPrintable(QStringLiteral("maximum apply time=%1ns").arg(maximumApplyNs)));
}

QTEST_MAIN(RealtimeThroughputTest)

#include "tst_realtime_throughput.moc"
