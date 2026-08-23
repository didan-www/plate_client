#include "domain/ImageLoadResult.h"
#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/ImageLoadService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QThreadPool>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <limits>
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

QByteArray imageBytes(const char *format,
                      const QSize &size = QSize(16, 12),
                      const QColor &color = Qt::green)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, format)) {
        qFatal("Failed to create image fixture.");
    }
    return bytes;
}

MockHttpResponse imageResponse(QByteArray body, QByteArray contentType)
{
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             std::move(contentType)});
    response.chunks.append(std::move(body));
    return response;
}

ImageLoadResult resultFrom(const QSignalSpy &spy)
{
    return *static_cast<const ImageLoadResult *>(spy.first().first().constData());
}

class DecodeRig final
{
public:
    explicit DecodeRig(ImageLoadService::DecodeObserver observer = {})
    {
        if (!server.listen()) {
            qFatal("Mock HTTP server failed to listen.");
        }
        pool.setMaxThreadCount(2);
        http = std::make_unique<HttpClient>(
            &manager,
            [this](const RequestContext &context) { return sessions.isCurrent(context); },
            HttpClient::TimeoutResolver {});
        api = std::make_unique<RecognitionApiClient>(server.url(), http.get(), &sessions);
        service = std::make_unique<ImageLoadService>(
            api.get(),
            &sessions,
            &pool,
            [](int) { return 1ms; },
            std::move(observer));
        if (!sessions.establishSession(session())) {
            qFatal("Failed to establish test session.");
        }
    }

    ~DecodeRig()
    {
        service.reset();
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
    std::unique_ptr<ImageLoadService> service;
};
} // namespace

class ImageDecodeLimitsTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void enforcesDimensionAndPixelBoundaries();
    void decodesPngAndJpegInWorkerAndCreatesPixmapOnlyOnUiThread();
    void rejectsInvalidMismatchedAndOversizedImagesWithoutRetry();
    void dropsLateDecodeFromOldSeries();
    void destructionDropsRunningDecodeResult();
};

void ImageDecodeLimitsTest::initTestCase()
{
    qRegisterMetaType<ImageLoadResult>();
}

void ImageDecodeLimitsTest::enforcesDimensionAndPixelBoundaries()
{
    QVERIFY(ImageLoadLimits::accepts(QSize(8192, 1)));
    QVERIFY(!ImageLoadLimits::accepts(QSize(8193, 1)));
    QVERIFY(ImageLoadLimits::accepts(QSize(8000, 5000)));
    QVERIFY(!ImageLoadLimits::accepts(QSize(8000, 5001)));
    QVERIFY(ImageLoadLimits::acceptsPixelCount(40'000'000, 1));
    QVERIFY(!ImageLoadLimits::acceptsPixelCount(40'000'001, 1));
    QVERIFY(!ImageLoadLimits::acceptsPixelCount(
        std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::max()));
    QVERIFY(!ImageLoadLimits::accepts(QSize()));
    QVERIFY(!ImageLoadLimits::accepts(QSize(-1, 5)));
}

void ImageDecodeLimitsTest::decodesPngAndJpegInWorkerAndCreatesPixmapOnlyOnUiThread()
{
    QMutex mutex;
    QList<QThread *> decodeThreads;
    DecodeRig rig([&mutex, &decodeThreads] {
        QMutexLocker locker(&mutex);
        decodeThreads.append(QThread::currentThread());
    });
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);

    const QList<QPair<QByteArray, QByteArray>> fixtures {
        {imageBytes("PNG"), QByteArrayLiteral("image/png")},
        {imageBytes("JPG"), QByteArrayLiteral("image/jpeg")},
    };
    for (qsizetype index = 0; index < fixtures.size(); ++index) {
        rig.server.enqueueResponse(imageResponse(fixtures.at(index).first,
                                                 fixtures.at(index).second));
        rig.service->load(QStringLiteral("rec-format-%1").arg(index),
                          rig.sessions.currentGeneration());
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
        const ImageLoadResult result = resultFrom(results);
        QVERIFY(result.isSuccess());
        QCOMPARE(result.image().size(), QSize(16, 12));
        QCOMPARE(QThread::currentThread(), qApp->thread());
        const QPixmap pixmap = QPixmap::fromImage(result.image());
        QVERIFY(!pixmap.isNull());
        results.clear();
    }

    QMutexLocker locker(&mutex);
    QCOMPARE(decodeThreads.size(), 2);
    for (QThread *thread : decodeThreads) {
        QVERIFY(thread != qApp->thread());
    }
}

void ImageDecodeLimitsTest::rejectsInvalidMismatchedAndOversizedImagesWithoutRetry()
{
    DecodeRig rig;
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);

    rig.server.enqueueResponse(imageResponse(imageBytes("PNG"),
                                             QByteArrayLiteral("image/jpeg")));
    rig.service->load(QStringLiteral("rec-mismatch"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QCOMPARE(resultFrom(results).error()->code(), ImageLoadErrorCode::FormatMismatch);
    QCOMPARE(resultFrom(results).attempts(), 1);
    results.clear();

    rig.server.enqueueResponse(imageResponse(QByteArrayLiteral("not-an-image"),
                                             QByteArrayLiteral("image/png")));
    rig.service->load(QStringLiteral("rec-invalid"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QCOMPARE(resultFrom(results).error()->code(), ImageLoadErrorCode::UnsupportedFormat);
    QCOMPARE(resultFrom(results).attempts(), 1);
    results.clear();

    rig.server.enqueueResponse(imageResponse(imageBytes("PNG", QSize(8193, 1)),
                                             QByteArrayLiteral("image/png")));
    rig.service->load(QStringLiteral("rec-wide"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QCOMPARE(resultFrom(results).error()->code(), ImageLoadErrorCode::DimensionsExceeded);
    QCOMPARE(resultFrom(results).attempts(), 1);
    results.clear();

    rig.server.enqueueResponse(imageResponse(imageBytes("PNG", QSize(8192, 1)),
                                             QByteArrayLiteral("image/png")));
    rig.service->load(QStringLiteral("rec-boundary"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QVERIFY(resultFrom(results).isSuccess());
    QCOMPARE(resultFrom(results).image().size(), QSize(8192, 1));
    QCOMPARE(rig.server.requests().size(), 4);
}

void ImageDecodeLimitsTest::dropsLateDecodeFromOldSeries()
{
    std::atomic<int> decodeCalls {0};
    QSemaphore firstEntered;
    QSemaphore releaseFirst;
    DecodeRig rig([&] {
        if (++decodeCalls == 1) {
            firstEntered.release();
            releaseFirst.acquire();
        }
    });
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);
    rig.server.enqueueResponse(imageResponse(imageBytes("PNG", QSize(32, 24), Qt::red),
                                             QByteArrayLiteral("image/png")));
    rig.server.enqueueResponse(imageResponse(imageBytes("PNG", QSize(40, 30), Qt::blue),
                                             QByteArrayLiteral("image/png")));

    rig.service->load(QStringLiteral("rec-old"), rig.sessions.currentGeneration());
    QTRY_VERIFY_WITH_TIMEOUT(firstEntered.available() > 0, 2000);
    QVERIFY(firstEntered.tryAcquire());
    rig.service->load(QStringLiteral("rec-new"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QCOMPARE(resultFrom(results).context().recognitionId(),
             std::optional<QString>(QStringLiteral("rec-new")));
    QCOMPARE(resultFrom(results).image().size(), QSize(40, 30));
    releaseFirst.release();
    rig.pool.waitForDone(2000);
    QTest::qWait(30);
    QCOMPARE(results.count(), 1);
}

void ImageDecodeLimitsTest::destructionDropsRunningDecodeResult()
{
    QSemaphore entered;
    QSemaphore release;
    DecodeRig rig([&] {
        entered.release();
        release.acquire();
    });
    int callbacks = 0;
    QObject receiver;
    QObject::connect(rig.service.get(), &ImageLoadService::resultReady,
                     &receiver, [&callbacks] { ++callbacks; });
    rig.server.enqueueResponse(imageResponse(imageBytes("PNG"),
                                             QByteArrayLiteral("image/png")));
    rig.service->load(QStringLiteral("rec-destroy"), rig.sessions.currentGeneration());
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, 2000);
    QVERIFY(entered.tryAcquire());
    rig.service.reset();
    release.release();
    QVERIFY(rig.pool.waitForDone(2000));
    QTest::qWait(30);
    QCOMPARE(callbacks, 0);
}

QTEST_MAIN(ImageDecodeLimitsTest)

#include "tst_image_decode_limits.moc"
