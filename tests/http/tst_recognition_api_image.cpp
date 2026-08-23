#include "domain/SessionContext.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/http/RecognitionApiClient.h"
#include "services/SessionService.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
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

MockHttpResponse imageResponse(QByteArray contentType,
                               QByteArray body = QByteArrayLiteral("image-bytes"))
{
    MockHttpResponse response;
    if (!contentType.isNull()) {
        response.headers.append({QByteArrayLiteral("Content-Type"),
                                 std::move(contentType)});
    }
    response.chunks.append(std::move(body));
    return response;
}

MockHttpResponse errorResponse(int status, QByteArray body)
{
    MockHttpResponse response;
    response.statusCode = status;
    response.reasonPhrase = QByteArrayLiteral("Error");
    response.headers.append({QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")});
    response.chunks.append(std::move(body));
    return response;
}

RecognitionApiError errorFrom(const QSignalSpy &spy)
{
    return *static_cast<const RecognitionApiError *>(spy.first().first().constData());
}
} // namespace

class RecognitionApiImageTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsBearerGetToEncodedFixedPath();
    void acceptsOnlyExactCanonicalContentTypes();
    void mapsStrictErrorsOversizeAndUnauthorized();
};

void RecognitionApiImageTest::initTestCase()
{
    qRegisterMetaType<RecognitionApiError>();
}

void RecognitionApiImageTest::sendsBearerGetToEncodedFixedPath()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(imageResponse(QByteArrayLiteral("image/png")));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(QStringLiteral("/gateway/")),
                                &transport,
                                &sessions);
    const QString id = QStringLiteral("rec/../?x% y");
    auto *handle = client.getImage(id, sessions.currentGeneration(), 47);
    QSignalSpy success(handle, &RecognitionImageRequestHandle::succeeded);
    QSignalSpy failure(handle, &RecognitionImageRequestHandle::failed);
    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QCOMPARE(handle->imageBytes(), QByteArrayLiteral("image-bytes"));
    QCOMPARE(handle->declaredFormat(), QByteArrayLiteral("png"));
    QCOMPARE(handle->context().requestGeneration(), quint64(47));
    QCOMPARE(handle->context().recognitionId(), std::optional<QString>(id));
    QTRY_COMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().target,
             QByteArrayLiteral("/gateway/api/v1/recognitions/rec%2F..%2F%3Fx%25%20y/image"));
    QCOMPARE(server.requests().first().header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer access-secret"));
    QCOMPARE(server.requests().first().header(QByteArrayLiteral("Accept")),
             QByteArrayLiteral("image/jpeg, image/png"));
}

void RecognitionApiImageTest::acceptsOnlyExactCanonicalContentTypes()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(imageResponse(QByteArrayLiteral("IMAGE/JPEG")));
    server.enqueueResponse(imageResponse(QByteArrayLiteral(" image/png ")));
    server.enqueueResponse(imageResponse(QByteArrayLiteral("image/png; charset=binary")));
    server.enqueueResponse(imageResponse(QByteArray()));
    server.enqueueResponse(imageResponse(QByteArrayLiteral("application/octet-stream")));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(), &transport, &sessions);

    for (int index = 0; index < 5; ++index) {
        auto *handle = client.getImage(QStringLiteral("rec-%1").arg(index),
                                       sessions.currentGeneration(),
                                       static_cast<quint64>(index + 1));
        QSignalSpy success(handle, &RecognitionImageRequestHandle::succeeded);
        QSignalSpy failure(handle, &RecognitionImageRequestHandle::failed);
        if (index < 2) {
            QVERIFY(success.wait());
            QCOMPARE(failure.count(), 0);
            QCOMPARE(handle->declaredFormat(),
                     index == 0 ? QByteArrayLiteral("jpeg")
                                : QByteArrayLiteral("png"));
        } else {
            QVERIFY(failure.wait());
            QCOMPARE(success.count(), 0);
            QCOMPARE(errorFrom(failure).code(),
                     RecognitionApiErrorCode::InvalidContentType);
        }
    }
    QCOMPARE(server.requests().size(), 5);
}

void RecognitionApiImageTest::mapsStrictErrorsOversizeAndUnauthorized()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(errorResponse(503, failureEnvelope(QStringLiteral("TEMPORARY"))));
    server.enqueueResponse(errorResponse(503, QByteArrayLiteral("{bad")));
    MockHttpResponse oversized = imageResponse(QByteArrayLiteral("image/png"), {});
    oversized.declaredContentLength = HttpRequestPolicy::imageMaximumBytes + 1;
    server.enqueueResponse(oversized);
    server.enqueueResponse(errorResponse(401, failureEnvelope(QStringLiteral("UNAUTHORIZED"))));
    SessionService sessions;
    QVERIFY(sessions.establishSession(session()));
    HttpClient transport([&sessions](const RequestContext &context) {
        return sessions.isCurrent(context);
    });
    RecognitionApiClient client(server.url(), &transport, &sessions);

    auto *serverFailure = client.getImage(QStringLiteral("rec-1"),
                                          sessions.currentGeneration(), 1);
    QSignalSpy serverSpy(serverFailure, &RecognitionImageRequestHandle::failed);
    QVERIFY(serverSpy.wait());
    QCOMPARE(errorFrom(serverSpy).code(), RecognitionApiErrorCode::ServerError);
    QCOMPARE(errorFrom(serverSpy).serverCode(), QStringLiteral("TEMPORARY"));

    auto *malformed = client.getImage(QStringLiteral("rec-2"),
                                      sessions.currentGeneration(), 2);
    QSignalSpy malformedSpy(malformed, &RecognitionImageRequestHandle::failed);
    QVERIFY(malformedSpy.wait());
    QCOMPARE(errorFrom(malformedSpy).code(), RecognitionApiErrorCode::ProtocolError);

    auto *tooLarge = client.getImage(QStringLiteral("rec-3"),
                                     sessions.currentGeneration(), 3);
    QSignalSpy tooLargeSpy(tooLarge, &RecognitionImageRequestHandle::failed);
    QVERIFY(tooLargeSpy.wait());
    QCOMPARE(errorFrom(tooLargeSpy).code(), RecognitionApiErrorCode::ResponseTooLarge);

    const quint64 generation = sessions.currentGeneration();
    auto *unauthorized = client.getImage(QStringLiteral("rec-4"), generation, 4);
    QSignalSpy unauthorizedSpy(unauthorized, &RecognitionImageRequestHandle::failed);
    QSignalSpy invalidated(&sessions, &SessionService::sessionInvalidated);
    QVERIFY(unauthorizedSpy.wait());
    QCOMPARE(errorFrom(unauthorizedSpy).code(), RecognitionApiErrorCode::Unauthorized);
    QCOMPARE(invalidated.count(), 1);
    QVERIFY(!sessions.hasActiveSession());
    QCOMPARE(server.requests().size(), 4);
}

QTEST_MAIN(RecognitionApiImageTest)

#include "tst_recognition_api_image.moc"
