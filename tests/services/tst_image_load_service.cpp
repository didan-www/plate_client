#include "domain/ImageLoadResult.h"
#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/ImageLoadService.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QSslError>
#include <QTimer>
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

QByteArray pngBytes(const QColor &color = Qt::red, const QSize &size = QSize(8, 6))
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        qFatal("Failed to create PNG test image.");
    }
    return bytes;
}

QByteArray failureEnvelope(const QString &code = QStringLiteral("FAILED"))
{
    return QJsonDocument(QJsonObject {
                             {QStringLiteral("success"), false},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("message"), QStringLiteral("请求失败")},
                             {QStringLiteral("requestId"), QStringLiteral("request-image")},
                             {QStringLiteral("data"), QJsonValue(QJsonValue::Null)},
                         })
        .toJson(QJsonDocument::Compact);
}

MockHttpResponse imageResponse(QByteArray body,
                               QByteArray contentType = QByteArrayLiteral("image/png"),
                               int delayMs = 0)
{
    MockHttpResponse response;
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             std::move(contentType)});
    response.chunks.append(std::move(body));
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
    response.chunks.append(failureEnvelope(QStringLiteral("HTTP_%1").arg(status)));
    return response;
}

ImageLoadResult resultFrom(const QSignalSpy &spy)
{
    return *static_cast<const ImageLoadResult *>(spy.first().first().constData());
}

class TlsErrorReply final : public QNetworkReply
{
public:
    explicit TlsErrorReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit sslErrors({QSslError(QSslError::SelfSignedCertificate)});
        });
    }

    void abort() override
    {
        if (isFinished()) {
            return;
        }
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class TlsErrorNetworkManager final : public QNetworkAccessManager
{
public:
    [[nodiscard]] int requestCount() const noexcept { return requestCount_; }

protected:
    QNetworkReply *createRequest(Operation,
                                 const QNetworkRequest &request,
                                 QIODevice *) override
    {
        ++requestCount_;
        return new TlsErrorReply(request, this);
    }

private:
    int requestCount_ = 0;
};

class ImageRig final
{
public:
    explicit ImageRig(HttpClient::TimeoutResolver timeoutResolver = {},
                      ImageLoadService::RetryDelayResolver retryResolver =
                          [](int) { return 1ms; },
                      ImageLoadService::DecodeObserver observer = {})
    {
        if (!server.listen()) {
            qFatal("Mock HTTP server failed to listen.");
        }
        pool.setMaxThreadCount(2);
        http = std::make_unique<HttpClient>(
            &manager,
            [this](const RequestContext &context) { return sessions.isCurrent(context); },
            std::move(timeoutResolver));
        api = std::make_unique<RecognitionApiClient>(server.url(), http.get(), &sessions);
        service = std::make_unique<ImageLoadService>(api.get(),
                                                     &sessions,
                                                     &pool,
                                                     std::move(retryResolver),
                                                     std::move(observer));
    }

    ~ImageRig()
    {
        service.reset();
        api.reset();
        http.reset();
        pool.waitForDone();
    }

    void start()
    {
        if (!sessions.establishSession(session())) {
            qFatal("Failed to establish test session.");
        }
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

class ImageLoadServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void retriesOnlySelectedServerStatuses();
    void retriesNetworkAndTimeoutFailures();
    void doesNotRetryOtherServerOrContentErrors();
    void doesNotRetryTlsErrors();
    void manualRetryStartsFreshThreeAttemptSeries();
    void newSeriesAndExplicitCancelDropOldReplies();
    void sessionInvalidationAndDestructionCancelPendingWork();
};

void ImageLoadServiceTest::initTestCase()
{
    qRegisterMetaType<ImageLoadResult>();
}

void ImageLoadServiceTest::retriesOnlySelectedServerStatuses()
{
    ImageRig rig;
    rig.start();
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);
    const QList<int> statuses {500, 502, 503, 504};
    int expectedRequests = 0;
    for (const int status : statuses) {
        rig.server.enqueueResponse(errorResponse(status));
        rig.server.enqueueResponse(imageResponse(pngBytes()));
        rig.service->load(QStringLiteral("rec-%1").arg(status),
                          rig.sessions.currentGeneration());
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
        const ImageLoadResult result = resultFrom(results);
        QVERIFY(result.isSuccess());
        QCOMPARE(result.attempts(), 2);
        QCOMPARE(result.context().recognitionId(),
                 std::optional<QString>(QStringLiteral("rec-%1").arg(status)));
        expectedRequests += 2;
        QCOMPARE(rig.server.requests().size(), expectedRequests);
        results.clear();
    }
}

void ImageLoadServiceTest::retriesNetworkAndTimeoutFailures()
{
    ImageRig rig([](HttpRequestKind kind) {
        return kind == HttpRequestKind::Image ? 50ms : 10s;
    });
    rig.start();
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);

    MockHttpResponse disconnected = imageResponse(QByteArrayLiteral("partial"));
    disconnected.declaredContentLength = 100;
    disconnected.disconnectAfterChunks = 1;
    rig.server.enqueueResponse(disconnected);
    rig.server.enqueueResponse(imageResponse(pngBytes(Qt::green)));
    rig.service->load(QStringLiteral("rec-network"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QVERIFY(resultFrom(results).isSuccess());
    QCOMPARE(resultFrom(results).attempts(), 2);
    results.clear();

    rig.server.enqueueResponse(imageResponse(pngBytes(), QByteArrayLiteral("image/png"), 150));
    rig.server.enqueueResponse(imageResponse(pngBytes(Qt::blue)));
    rig.service->load(QStringLiteral("rec-timeout"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    QVERIFY(resultFrom(results).isSuccess());
    QCOMPARE(resultFrom(results).attempts(), 2);
    QCOMPARE(rig.server.requests().size(), 4);
}

void ImageLoadServiceTest::doesNotRetryOtherServerOrContentErrors()
{
    ImageRig rig;
    rig.start();
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);
    int expectedRequests = 0;
    for (const int status : {404, 501, 505}) {
        rig.server.enqueueResponse(errorResponse(status));
        rig.service->load(QStringLiteral("rec-%1").arg(status),
                          rig.sessions.currentGeneration());
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 1000);
        const ImageLoadResult result = resultFrom(results);
        QVERIFY(!result.isSuccess());
        QCOMPARE(result.error()->code(),
                 status == 404 ? ImageLoadErrorCode::NotFound
                               : ImageLoadErrorCode::ServerError);
        QCOMPARE(result.attempts(), 1);
        QVERIFY(result.manualRetryAvailable());
        ++expectedRequests;
        QTest::qWait(20);
        QCOMPARE(rig.server.requests().size(), expectedRequests);
        results.clear();
    }

    rig.server.enqueueResponse(imageResponse(pngBytes(),
                                             QByteArrayLiteral("image/png; charset=binary")));
    rig.service->load(QStringLiteral("rec-mime"), rig.sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 1000);
    QCOMPARE(resultFrom(results).error()->code(),
             ImageLoadErrorCode::InvalidContentType);
    QCOMPARE(resultFrom(results).attempts(), 1);
    QTest::qWait(20);
    QCOMPARE(rig.server.requests().size(), expectedRequests + 1);
}

void ImageLoadServiceTest::doesNotRetryTlsErrors()
{
    TlsErrorNetworkManager manager;
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport(&manager,
                         [&sessions](const RequestContext &context) {
                             return sessions.isCurrent(context);
                         },
                         {});
    RecognitionApiClient api(QUrl(QStringLiteral("https://example.test")),
                             &transport,
                             &sessions);
    QThreadPool pool;
    ImageLoadService service(&api,
                             &sessions,
                             &pool,
                             [](int) { return 1ms; });
    QSignalSpy results(&service, &ImageLoadService::resultReady);
    service.load(QStringLiteral("rec-tls"), sessions.currentGeneration());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 1000);
    QCOMPARE(resultFrom(results).error()->code(), ImageLoadErrorCode::TlsError);
    QCOMPARE(resultFrom(results).attempts(), 1);
    QTest::qWait(20);
    QCOMPARE(manager.requestCount(), 1);
}

void ImageLoadServiceTest::manualRetryStartsFreshThreeAttemptSeries()
{
    ImageRig rig;
    rig.start();
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);
    rig.server.enqueueResponse(errorResponse(501));
    rig.service->load(QStringLiteral("rec-manual"), rig.sessions.currentGeneration());
    QTRY_COMPARE(results.count(), 1);
    const quint64 firstGeneration = resultFrom(results).context().requestGeneration();
    QVERIFY(resultFrom(results).manualRetryAvailable());
    results.clear();

    rig.server.enqueueResponse(errorResponse(500));
    rig.server.enqueueResponse(errorResponse(502));
    rig.server.enqueueResponse(imageResponse(pngBytes()));
    QVERIFY(rig.service->retry());
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 2000);
    const ImageLoadResult result = resultFrom(results);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.attempts(), 3);
    QVERIFY(result.context().requestGeneration() > firstGeneration);
    QCOMPARE(rig.server.requests().size(), 4);
    QVERIFY(!rig.service->manualRetryAvailable());
}

void ImageLoadServiceTest::newSeriesAndExplicitCancelDropOldReplies()
{
    ImageRig rig;
    rig.start();
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);
    rig.server.enqueueResponse(imageResponse(pngBytes(Qt::red),
                                             QByteArrayLiteral("image/png"), 120));
    rig.server.enqueueResponse(imageResponse(pngBytes(Qt::blue)));
    rig.service->load(QStringLiteral("rec-old"), rig.sessions.currentGeneration());
    QTRY_COMPARE(rig.server.requests().size(), 1);
    const quint64 oldGeneration = rig.service->requestGeneration();
    rig.service->load(QStringLiteral("rec-new"), rig.sessions.currentGeneration());
    QVERIFY(rig.service->requestGeneration() > oldGeneration);
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 1000);
    QCOMPARE(resultFrom(results).context().recognitionId(),
             std::optional<QString>(QStringLiteral("rec-new")));
    QTest::qWait(150);
    QCOMPARE(results.count(), 1);
    QCOMPARE(rig.server.requests().size(), 2);

    rig.server.enqueueResponse(imageResponse(pngBytes(), QByteArrayLiteral("image/png"), 100));
    rig.service->load(QStringLiteral("rec-cancel"), rig.sessions.currentGeneration());
    QTRY_COMPARE(rig.server.requests().size(), 3);
    rig.service->cancel();
    QTest::qWait(130);
    QCOMPARE(results.count(), 1);
    QVERIFY(!rig.service->requestInFlight());
    QVERIFY(!rig.service->retryPending());
}

void ImageLoadServiceTest::sessionInvalidationAndDestructionCancelPendingWork()
{
    ImageRig rig({}, [](int) { return 100ms; });
    rig.start();
    QSignalSpy results(rig.service.get(), &ImageLoadService::resultReady);
    rig.server.enqueueResponse(errorResponse(503));
    rig.service->load(QStringLiteral("rec-session"), rig.sessions.currentGeneration());
    QTRY_VERIFY(rig.service->retryPending());
    rig.sessions.clear();
    QTest::qWait(130);
    QCOMPARE(rig.server.requests().size(), 1);
    QCOMPARE(results.count(), 0);
    QVERIFY(!rig.service->retryPending());

    QVERIFY(rig.sessions.establishSession(session()));
    rig.server.enqueueResponse(imageResponse(pngBytes(), QByteArrayLiteral("image/png"), 100));
    int callbacks = 0;
    QObject receiver;
    QObject::connect(rig.service.get(), &ImageLoadService::resultReady,
                     &receiver, [&callbacks] { ++callbacks; });
    rig.service->load(QStringLiteral("rec-destroy"), rig.sessions.currentGeneration());
    QTRY_COMPARE(rig.server.requests().size(), 2);
    rig.service.reset();
    QTest::qWait(130);
    QCOMPARE(callbacks, 0);
}

QTEST_MAIN(ImageLoadServiceTest)

#include "tst_image_load_service.moc"
