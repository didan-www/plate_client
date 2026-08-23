#include "domain/ImageLoadResult.h"
#include "domain/RecognitionSnapshot.h"
#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/ImageLoadService.h"
#include "services/RealtimeRecognitionStore.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"
#include "ui/models/RecognitionTableModel.h"
#include "ui/pages/RealtimePage.h"

#include <QBuffer>
#include <QGridLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QThreadPool>
#include <QtTest>

#include <chrono>
#include <memory>

namespace {
using namespace std::chrono_literals;

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
                             const QDateTime &capturedAt,
                             qint64 durationMs = 1250)
{
    const bool succeeded = status == RecognitionStatus::Succeeded;
    const bool failed = status == RecognitionStatus::Failed;
    return RecognitionSnapshot(1,
                               id,
                               revision,
                               QStringLiteral("device-%1").arg(id),
                               status,
                               succeeded
                                   ? std::optional<QString>(QStringLiteral("京A12345"))
                                   : std::nullopt,
                               failed ? std::optional<QString>(QStringLiteral("MODEL_ERROR"))
                                      : std::nullopt,
                               failed ? std::optional<QString>(QStringLiteral("模型识别失败"))
                                      : std::nullopt,
                               capturedAt,
                               capturedAt.addSecs(1),
                               status == RecognitionStatus::Processing
                                   ? std::nullopt
                                   : std::optional<QDateTime>(capturedAt.addSecs(2)),
                               status == RecognitionStatus::Processing
                                   ? std::nullopt
                                   : std::optional<qint64>(durationMs));
}

QByteArray pngBytes(const QColor &color)
{
    QImage image(QSize(40, 24), QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        qFatal("Failed to create realtime page test PNG.");
    }
    return bytes;
}

MockHttpResponse imageResponse(const QColor &color, int delayMs = 0)
{
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("image/png")});
    response.chunks.append(pngBytes(color));
    response.initialDelayMs = delayMs;
    return response;
}

MockHttpResponse errorResponse(int status)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = QByteArrayLiteral("Error");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(QJsonDocument(QJsonObject {
                                              {QStringLiteral("success"), false},
                                              {QStringLiteral("code"), QStringLiteral("IMAGE_FAILED")},
                                              {QStringLiteral("message"), QStringLiteral("图片失败")},
                                              {QStringLiteral("requestId"), QStringLiteral("image-request")},
                                              {QStringLiteral("data"), QJsonValue(QJsonValue::Null)},
                                          })
                               .toJson(QJsonDocument::Compact));
    return response;
}

QColor displayedColor(QLabel *imageLabel)
{
    const QPixmap pixmap = imageLabel->pixmap();
    if (pixmap.isNull()) {
        return {};
    }
    return pixmap.toImage().pixelColor(pixmap.width() / 2, pixmap.height() / 2);
}

class RealtimeRig final
{
public:
    RealtimeRig()
    {
        if (!server.listen()) {
            qFatal("Mock HTTP server failed to listen.");
        }
        pool.setMaxThreadCount(2);
        if (!sessions.establishSession(session())) {
            qFatal("Failed to establish realtime page test session.");
        }
        http = std::make_unique<HttpClient>(
            &manager,
            [this](const RequestContext &context) { return sessions.isCurrent(context); },
            HttpClient::TimeoutResolver {},
            nullptr);
        api = std::make_unique<RecognitionApiClient>(server.url(), http.get(), &sessions);
        images = std::make_unique<ImageLoadService>(api.get(),
                                                    &sessions,
                                                    &pool,
                                                    [](int) { return 1ms; });
        store = std::make_unique<RealtimeRecognitionStore>(&sessions);
    }

    ~RealtimeRig()
    {
        store.reset();
        images.reset();
        api.reset();
        http.reset();
        pool.waitForDone();
    }

    MockHttpServer server;
    SessionService sessions;
    QNetworkAccessManager manager;
    QThreadPool pool;
    std::unique_ptr<HttpClient> http;
    std::unique_ptr<RecognitionApiClient> api;
    std::unique_ptr<ImageLoadService> images;
    std::unique_ptr<RealtimeRecognitionStore> store;
};
} // namespace

class RealtimePageTest final : public QObject
{
    Q_OBJECT

private slots:
    void initialProcessingFinalAndOldFinalStates();
    void rejectsLateImagesAndClearsWithSession();
    void failureRetryDoesNotBlockFinalState();
    void destructionCancelsCurrentImageSeries();
    void layoutFitsSupportedSizes_data();
    void layoutFitsSupportedSizes();
};

void RealtimePageTest::initialProcessingFinalAndOldFinalStates()
{
    RealtimeRig rig;
    RealtimePage page(rig.store.get(), rig.images.get());
    page.resize(1086, 724);
    page.show();
    auto *status = page.findChild<QLabel *>(QStringLiteral("currentStatusLabel"));
    auto *plate = page.findChild<QLabel *>(QStringLiteral("currentPlateLabel"));
    auto *error = page.findChild<QLabel *>(QStringLiteral("currentErrorLabel"));
    auto *captured = page.findChild<QLabel *>(QStringLiteral("currentCapturedAtLabel"));
    auto *duration = page.findChild<QLabel *>(QStringLiteral("currentDurationLabel"));
    auto *image = page.findChild<QLabel *>(QStringLiteral("currentImageLabel"));
    QVERIFY(status && plate && error && captured && duration && image);
    QCOMPARE(status->text(), QStringLiteral("未启动识别"));
    QCOMPARE(image->text(), QStringLiteral("暂无车辆图片"));

    const QDateTime oldTime = QDateTime::fromString(
        QStringLiteral("2026-08-24T11:00:00+08:00"), Qt::ISODate);
    rig.server.enqueueResponse(imageResponse(Qt::green));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("old"), 1,
                                      RecognitionStatus::Processing, oldTime)));
    QCOMPARE(status->text(), QStringLiteral("识别中"));
    QCOMPARE(captured->text(), oldTime.toString(Qt::ISODate));
    QTRY_VERIFY(!image->pixmap().isNull());
    QVERIFY(displayedColor(image).green() > displayedColor(image).red());

    const QDateTime currentTime = oldTime.addSecs(10);
    rig.server.enqueueResponse(imageResponse(Qt::blue));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("current"), 1,
                                      RecognitionStatus::Processing, currentTime)));
    QTRY_VERIFY(displayedColor(image).blue() > displayedColor(image).red());
    const qint64 currentImageKey = image->pixmap().cacheKey();

    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("old"), 2,
                                      RecognitionStatus::Failed, oldTime, 900)));
    QCOMPARE(rig.store->current()->recognitionId(), QStringLiteral("current"));
    QCOMPARE(status->text(), QStringLiteral("识别中"));
    QCOMPARE(image->pixmap().cacheKey(), currentImageKey);

    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("current"), 2,
                                      RecognitionStatus::Succeeded, currentTime, 1250)));
    QCOMPARE(status->text(), QStringLiteral("已识别完成"));
    QCOMPARE(plate->text(), QStringLiteral("京A12345"));
    QCOMPARE(error->text(), QStringLiteral("--"));
    QCOMPARE(captured->text(), currentTime.toString(Qt::ISODate));
    QCOMPARE(duration->text(), QStringLiteral("1250 ms"));
    QCOMPARE(image->pixmap().cacheKey(), currentImageKey);
    QCOMPARE(page.tableModel()->rowCount(), 2);
}

void RealtimePageTest::rejectsLateImagesAndClearsWithSession()
{
    RealtimeRig rig;
    RealtimePage page(rig.store.get(), rig.images.get());
    page.resize(1086, 724);
    page.show();
    auto *image = page.findChild<QLabel *>(QStringLiteral("currentImageLabel"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("currentStatusLabel"));
    QVERIFY(image && status);
    const QDateTime capturedAt = QDateTime::fromString(
        QStringLiteral("2026-08-24T12:00:00+08:00"), Qt::ISODate);

    rig.server.enqueueResponse(imageResponse(Qt::red, 150));
    rig.server.enqueueResponse(imageResponse(Qt::blue));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("old"), 1,
                                      RecognitionStatus::Processing, capturedAt)));
    QTRY_COMPARE(rig.server.requests().size(), 1);
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("new"), 1,
                                      RecognitionStatus::Processing,
                                      capturedAt.addSecs(1))));
    QTRY_VERIFY(displayedColor(image).blue() > displayedColor(image).red());

    RequestContext staleContext(rig.sessions.currentGeneration(),
                                rig.images->requestGeneration(),
                                QStringLiteral("recognition.image"),
                                QStringLiteral("old"));
    emit rig.images->resultReady(ImageLoadResult::success(
        staleContext, QImage::fromData(pngBytes(Qt::red), "PNG"), 1));
    QVERIFY(displayedColor(image).blue() > displayedColor(image).red());
    QTest::qWait(180);
    QVERIFY(displayedColor(image).blue() > displayedColor(image).red());

    rig.server.enqueueResponse(imageResponse(Qt::green, 150));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("clear"), 1,
                                      RecognitionStatus::Processing,
                                      capturedAt.addSecs(2))));
    QTRY_COMPARE(rig.server.requests().size(), 3);
    rig.sessions.clear();
    QCOMPARE(status->text(), QStringLiteral("未启动识别"));
    QCOMPARE(image->text(), QStringLiteral("暂无车辆图片"));
    QVERIFY(image->pixmap().isNull());
    QVERIFY(rig.images->recognitionId().isEmpty());
    QVERIFY(!rig.images->requestInFlight());
}

void RealtimePageTest::failureRetryDoesNotBlockFinalState()
{
    RealtimeRig rig;
    RealtimePage page(rig.store.get(), rig.images.get());
    page.resize(1086, 724);
    page.show();
    auto *status = page.findChild<QLabel *>(QStringLiteral("currentStatusLabel"));
    auto *plate = page.findChild<QLabel *>(QStringLiteral("currentPlateLabel"));
    auto *duration = page.findChild<QLabel *>(QStringLiteral("currentDurationLabel"));
    auto *image = page.findChild<QLabel *>(QStringLiteral("currentImageLabel"));
    auto *retry = page.findChild<QPushButton *>(QStringLiteral("imageRetryButton"));
    QVERIFY(status && plate && duration && image && retry);
    const QDateTime capturedAt = QDateTime::fromString(
        QStringLiteral("2026-08-24T13:00:00+08:00"), Qt::ISODate);

    rig.server.enqueueResponse(errorResponse(501));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("retry"), 1,
                                      RecognitionStatus::Processing, capturedAt)));
    QTRY_VERIFY(retry->isVisible());
    QCOMPARE(image->text(), QStringLiteral("图片加载失败"));

    rig.server.enqueueResponse(imageResponse(Qt::green, 120));
    retry->click();
    QCOMPARE(image->text(), QStringLiteral("图片加载中..."));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("retry"), 2,
                                      RecognitionStatus::Succeeded, capturedAt, 777)));
    QCOMPARE(status->text(), QStringLiteral("已识别完成"));
    QCOMPARE(plate->text(), QStringLiteral("京A12345"));
    QCOMPARE(duration->text(), QStringLiteral("777 ms"));
    QTRY_VERIFY(!image->pixmap().isNull());
    QVERIFY(!retry->isVisible());
}

void RealtimePageTest::destructionCancelsCurrentImageSeries()
{
    RealtimeRig rig;
    auto page = std::make_unique<RealtimePage>(rig.store.get(), rig.images.get());
    page->show();
    const QDateTime capturedAt = QDateTime::fromString(
        QStringLiteral("2026-08-24T14:00:00+08:00"), Qt::ISODate);
    rig.server.enqueueResponse(imageResponse(Qt::red, 200));
    QVERIFY(rig.store->apply(rig.sessions.currentGeneration(),
                             snapshot(QStringLiteral("destroy"), 1,
                                      RecognitionStatus::Processing, capturedAt)));
    QTRY_VERIFY(rig.images->requestInFlight());
    QPointer<RealtimePage> guardedPage(page.get());
    page.reset();
    QVERIFY(guardedPage.isNull());
    QVERIFY(rig.images->recognitionId().isEmpty());
    QVERIFY(!rig.images->requestInFlight());
    QVERIFY(!rig.images->retryPending());
    QTest::qWait(220);
}

void RealtimePageTest::layoutFitsSupportedSizes_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::newRow("1366x768") << QSize(1366, 768);
    QTest::newRow("1920x1080") << QSize(1920, 1080);
    QTest::newRow("3840x2160") << QSize(3840, 2160);
}

void RealtimePageTest::layoutFitsSupportedSizes()
{
    QFETCH(QSize, windowSize);
    RealtimeRig rig;
    RealtimePage page(rig.store.get(), rig.images.get());
    page.resize(windowSize.width() - 280, windowSize.height() - 44);
    page.ensurePolished();
    QVERIFY(page.layout()->activate());

    auto *imagePanel = page.findChild<QWidget *>(QStringLiteral("currentImagePanel"));
    auto *detailsPanel = page.findChild<QWidget *>(QStringLiteral("currentDetailsPanel"));
    auto *table = page.findChild<QTableView *>(QStringLiteral("realtimeTable"));
    auto *title = page.findChild<QLabel *>(QStringLiteral("realtimeTitleLabel"));
    auto *retry = page.findChild<QPushButton *>(QStringLiteral("imageRetryButton"));
    QVERIFY(imagePanel && detailsPanel && table && title && retry);
    QVERIFY(imagePanel->layout()->activate());
    QVERIFY(detailsPanel->layout()->activate());
    QVERIFY(page.rect().contains(imagePanel->geometry()));
    QVERIFY(page.rect().contains(detailsPanel->geometry()));
    QVERIFY(page.rect().contains(table->geometry()));
    QVERIFY(page.rect().contains(title->geometry()));
    QVERIFY(!imagePanel->geometry().intersects(detailsPanel->geometry()));
    QVERIFY(imagePanel->geometry().bottom() < table->geometry().top());
    QCOMPARE(table->model()->columnCount(), 5);
    QCOMPARE(retry->size(), QSize(112, 36));
    QVERIFY(retry->fontMetrics().horizontalAdvance(retry->text()) + 24 <= retry->width());
    QVERIFY(title->fontMetrics().horizontalAdvance(title->text()) <= title->width());
    for (QLabel *label : page.findChildren<QLabel *>()) {
        const QRect mapped(label->mapTo(&page, QPoint(0, 0)), label->size());
        QVERIFY2(page.rect().contains(mapped), qPrintable(label->objectName()));
        if (!label->wordWrap() && !label->text().isEmpty()) {
            QVERIFY2(label->fontMetrics().horizontalAdvance(label->text()) <= label->width(),
                     qPrintable(label->text()));
        }
    }
    for (int column = 0; column < table->model()->columnCount(); ++column) {
        const QString header = table->model()
                                   ->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                                   .toString();
        QVERIFY(table->fontMetrics().horizontalAdvance(header) + 20
                <= table->horizontalHeader()->sectionSize(column));
    }
}

QTEST_MAIN(RealtimePageTest)

#include "tst_realtime_page.moc"
