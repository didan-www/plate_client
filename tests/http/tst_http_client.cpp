#include "infrastructure/http/HttpClient.h"
#include "support/MockHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QSslError>
#include <QTimer>
#include <QtTest>

#include <memory>

namespace {
MockHttpResponse jsonResponse(int statusCode = 200,
                              QByteArray body = QByteArrayLiteral("{\"success\":true}"))
{
    MockHttpResponse response;
    response.statusCode = statusCode;
    response.reasonPhrase = statusCode >= 400 ? QByteArrayLiteral("Error")
                                               : QByteArrayLiteral("OK");
    response.headers.append(
        {QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")});
    response.chunks.append(std::move(body));
    return response;
}

RequestContext context(quint64 session = 1, quint64 request = 1)
{
    return RequestContext(session, request, QStringLiteral("test.request"));
}

HttpRequest jsonGet(const QUrl &url,
                    RequestContext requestContext = context(),
                    HttpRequestKind kind = HttpRequestKind::Query)
{
    return HttpRequest(HttpMethod::Get, url, kind, HttpResponseMode::JsonMemory,
                       std::move(requestContext));
}

HttpError errorFrom(const QSignalSpy &spy)
{
    return *static_cast<const HttpError *>(spy.first().at(0).constData());
}

HttpResponse responseFrom(const QSignalSpy &spy, int argument = 0)
{
    return *static_cast<const HttpResponse *>(spy.first().at(argument).constData());
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
        if (finished_) {
            return;
        }
        finished_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *, qint64) override { return -1; }

private:
    bool finished_ = false;
};

class TlsErrorNetworkManager final : public QNetworkAccessManager
{
protected:
    QNetworkReply *createRequest(Operation,
                                 const QNetworkRequest &request,
                                 QIODevice *) override
    {
        return new TlsErrorReply(request, this);
    }
};

class ImmediateSuccessReply final : public QNetworkReply
{
public:
    explicit ImmediateSuccessReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setRawHeader(QByteArrayLiteral("Content-Length"), QByteArrayLiteral("0"));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit finished();
        });
    }

    void abort() override {}

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class RecordingNetworkManager final : public QNetworkAccessManager
{
public:
    [[nodiscard]] const QNetworkRequest &recordedRequest() const noexcept
    {
        return recordedRequest_;
    }

protected:
    QNetworkReply *createRequest(Operation,
                                 const QNetworkRequest &request,
                                 QIODevice *) override
    {
        recordedRequest_ = request;
        return new ImmediateSuccessReply(request, this);
    }

private:
    QNetworkRequest recordedRequest_;
};
} // namespace

class HttpClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mapsProductionPolicies();
    void sendsHeadersMethodBodyAndContext();
    void sendsDeleteRequestBody();
    void acceptsJsonAtExactLimit();
    void acceptsImageAtExactLimit();
    void forcesImageNetworkWithoutSavingCache();
    void rejectsDeclaredAndChunkedOversize();
    void streamsCsvWithoutBuffering();
    void preservesJsonErrorForCsvRequest();
    void distinguishesHttpAndParseErrors();
    void reportsOnlyObservedHttpResponses();
    void rejectsRedirectsWithoutFollowing();
    void distinguishesNetworkTlsTimeoutAndCancellation();
    void cancelAllUsesCallTimeSnapshot();
    void destroyingHandleStopsRequestWithoutCallback();
    void rejectsStaleAndOutOfOrderResponses();
};

void HttpClientTest::initTestCase()
{
    qRegisterMetaType<HttpError>();
    qRegisterMetaType<HttpResponse>();
}

void HttpClientTest::mapsProductionPolicies()
{
    using namespace std::chrono_literals;
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Login), 10s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Logout), 5s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Heartbeat), 5s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Query), 10s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Image), 30s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::AccessListWrite), 10s);
    QCOMPARE(HttpRequestPolicy::timeout(HttpRequestKind::Csv), 120s);
    QCOMPARE(HttpRequestPolicy::maximumBufferedBytes(HttpResponseMode::JsonMemory),
             2LL * 1024LL * 1024LL);
    QCOMPARE(HttpRequestPolicy::maximumBufferedBytes(HttpResponseMode::ImageMemory),
             20LL * 1024LL * 1024LL);
    QCOMPARE(HttpRequestPolicy::maximumBufferedBytes(HttpResponseMode::CsvStream), 0);
    const ProtocolError protocolError(ProtocolErrorCode::MissingField,
                                      QStringLiteral("$.data"),
                                      QStringLiteral("协议缺少必填字段。"));
    const HttpError mapped(HttpErrorCode::ParseFailure,
                           QStringLiteral("HTTP DTO 解析失败。"),
                           200,
                           std::nullopt,
                           protocolError);
    QVERIFY(mapped.protocolError().has_value());
    QCOMPARE(mapped.protocolError()->code(), ProtocolErrorCode::MissingField);
}

void HttpClientTest::sendsHeadersMethodBodyAndContext()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse());
    HttpClient client;
    const QByteArray requestBody = QByteArrayLiteral("{\"name\":\"value\"}");
    const RequestContext requestContext(7, 9, QStringLiteral("access.create"));
    HttpRequest request(HttpMethod::Post, server.url(QStringLiteral("/api/items")),
                              HttpRequestKind::AccessListWrite,
                              HttpResponseMode::JsonMemory, requestContext, requestBody,
                              QStringLiteral("secret-token"));
    HttpRequestHandle *handle = client.send(std::move(request));
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);
    QSignalSpy failure(handle, &HttpRequestHandle::failed);

    QVERIFY(success.wait());
    QCOMPARE(failure.count(), 0);
    QCOMPARE(server.requests().size(), 1);
    const MockHttpRequest &recorded = server.requests().first();
    QCOMPARE(recorded.method, QByteArrayLiteral("POST"));
    QCOMPARE(recorded.target, QByteArrayLiteral("/api/items"));
    QCOMPARE(recorded.header(QByteArrayLiteral("Authorization")),
             QByteArrayLiteral("Bearer secret-token"));
    QCOMPARE(recorded.header(QByteArrayLiteral("Accept")), QByteArrayLiteral("application/json"));
    QCOMPARE(recorded.header(QByteArrayLiteral("Content-Type")), QByteArrayLiteral("application/json"));
    QCOMPARE(recorded.body, requestBody);
    const HttpResponse response = responseFrom(success);
    QCOMPARE(response.statusCode(), 200);
    QCOMPARE(response.context().sessionGeneration(), quint64(7));
    QCOMPARE(response.context().requestGeneration(), quint64(9));
}

void HttpClientTest::sendsDeleteRequestBody()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse());
    HttpClient client;
    const QByteArray body = QByteArrayLiteral("{\"plateNumber\":\"京A12345\"}");
    HttpRequest request(HttpMethod::Delete, server.url(QStringLiteral("/api/items/5")),
                              HttpRequestKind::AccessListWrite,
                              HttpResponseMode::JsonMemory, context(), body);
    HttpRequestHandle *handle = client.send(std::move(request));
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);

    QVERIFY(success.wait());
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().method, QByteArrayLiteral("DELETE"));
    QCOMPARE(server.requests().first().target, QByteArrayLiteral("/api/items/5"));
    QCOMPARE(server.requests().first().body, body);
}

void HttpClientTest::acceptsJsonAtExactLimit()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    QByteArray body = QByteArrayLiteral("{\"value\":\"");
    body.append(QByteArray(HttpRequestPolicy::jsonMaximumBytes - body.size() - 2, 'x'));
    body.append(QByteArrayLiteral("\"}"));
    QCOMPARE(body.size(), HttpRequestPolicy::jsonMaximumBytes);
    server.enqueueResponse(jsonResponse(200, body));
    HttpClient client;
    HttpRequestHandle *handle = client.send(jsonGet(server.url()));
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);
    QSignalSpy failure(handle, &HttpRequestHandle::failed);

    QVERIFY(success.wait(10000));
    QCOMPARE(failure.count(), 0);
    QCOMPARE(responseFrom(success).body().size(), HttpRequestPolicy::jsonMaximumBytes);
}

void HttpClientTest::acceptsImageAtExactLimit()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse image;
    image.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("image/png")});
    image.chunks.append(QByteArray(HttpRequestPolicy::imageMaximumBytes, 'i'));
    server.enqueueResponse(std::move(image));
    HttpClient client;
    HttpRequest request(HttpMethod::Get, server.url(QStringLiteral("/image")),
                              HttpRequestKind::Image, HttpResponseMode::ImageMemory,
                              context());
    HttpRequestHandle *handle = client.send(std::move(request));
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);
    QSignalSpy failure(handle, &HttpRequestHandle::failed);

    QVERIFY(success.wait(10000));
    QCOMPARE(failure.count(), 0);
    QCOMPARE(responseFrom(success).body().size(), HttpRequestPolicy::imageMaximumBytes);
}

void HttpClientTest::forcesImageNetworkWithoutSavingCache()
{
    RecordingNetworkManager manager;
    HttpClient client(&manager, {}, {}, nullptr);
    HttpRequest request(HttpMethod::Get,
                        QUrl(QStringLiteral("https://example.test/image")),
                        HttpRequestKind::Image,
                        HttpResponseMode::ImageMemory,
                        context());
    HttpRequestHandle *handle = client.send(std::move(request));
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);
    QVERIFY(success.wait());

    QCOMPARE(manager.recordedRequest()
                 .attribute(QNetworkRequest::CacheLoadControlAttribute)
                 .toInt(),
             static_cast<int>(QNetworkRequest::AlwaysNetwork));
    QCOMPARE(manager.recordedRequest()
                 .attribute(QNetworkRequest::CacheSaveControlAttribute)
                 .toBool(),
             false);
    QVERIFY(manager.cache() == nullptr);
}

void HttpClientTest::rejectsDeclaredAndChunkedOversize()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse declared = jsonResponse();
    declared.declaredContentLength = HttpRequestPolicy::jsonMaximumBytes + 1;
    server.enqueueResponse(declared);
    HttpClient client;
    HttpRequestHandle *declaredHandle = client.send(jsonGet(server.url(QStringLiteral("/declared"))));
    QSignalSpy declaredFailure(declaredHandle, &HttpRequestHandle::failed);
    QSignalSpy declaredSuccess(declaredHandle, &HttpRequestHandle::succeeded);
    QVERIFY(declaredFailure.wait());
    QCOMPARE(declaredSuccess.count(), 0);
    QCOMPARE(errorFrom(declaredFailure).code(), HttpErrorCode::ResponseTooLarge);

    MockHttpResponse chunked = jsonResponse();
    chunked.omitContentLength = true;
    chunked.chunks = {QByteArray(1024 * 1024, 'x'),
                      QByteArray(1024 * 1024, 'y'), QByteArray(1, 'z')};
    server.enqueueResponse(chunked);
    HttpRequestHandle *chunkedHandle = client.send(jsonGet(server.url(QStringLiteral("/chunked"))));
    QSignalSpy chunkedFailure(chunkedHandle, &HttpRequestHandle::failed);
    QVERIFY(chunkedFailure.wait(10000));
    QCOMPARE(errorFrom(chunkedFailure).code(), HttpErrorCode::ResponseTooLarge);

    MockHttpResponse image;
    image.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("image/png")});
    image.declaredContentLength = HttpRequestPolicy::imageMaximumBytes + 1;
    server.enqueueResponse(image);
    HttpRequest imageRequest(HttpMethod::Get, server.url(QStringLiteral("/image")),
                                   HttpRequestKind::Image, HttpResponseMode::ImageMemory,
                                   context());
    HttpRequestHandle *imageHandle = client.send(std::move(imageRequest));
    QSignalSpy imageFailure(imageHandle, &HttpRequestHandle::failed);
    QVERIFY(imageFailure.wait());
    QCOMPARE(errorFrom(imageFailure).code(), HttpErrorCode::ResponseTooLarge);
}

void HttpClientTest::streamsCsvWithoutBuffering()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse csv;
    csv.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("text/csv")});
    csv.chunks = {QByteArrayLiteral("a,b\r\n"), QByteArrayLiteral("1,2\r\n")};
    csv.chunkDelayMs = 5;
    server.enqueueResponse(csv);
    HttpClient client;
    HttpRequest request(HttpMethod::Get, server.url(QStringLiteral("/export")),
                              HttpRequestKind::Csv, HttpResponseMode::CsvStream, context());
    HttpRequestHandle *handle = client.send(std::move(request));
    QSignalSpy chunks(handle, &HttpRequestHandle::dataChunkReceived);
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);

    QVERIFY(success.wait());
    QCOMPARE(chunks.count(), 2);
    QCOMPARE(chunks.at(0).at(0).toByteArray(), QByteArrayLiteral("a,b\r\n"));
    QCOMPARE(chunks.at(1).at(0).toByteArray(), QByteArrayLiteral("1,2\r\n"));
    QVERIFY(responseFrom(success).body().isEmpty());
    QCOMPARE(server.requests().first().header(QByteArrayLiteral("Accept")),
             QByteArrayLiteral("text/csv"));
    QVERIFY(server.requests().first().header(QByteArrayLiteral("Content-Type")).isEmpty());
}

void HttpClientTest::preservesJsonErrorForCsvRequest()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(403, QByteArrayLiteral("{\"success\":false}")));
    HttpClient client;
    HttpRequest request(HttpMethod::Get, server.url(QStringLiteral("/export")),
                              HttpRequestKind::Csv, HttpResponseMode::CsvStream, context());
    HttpRequestHandle *handle = client.send(std::move(request));
    QSignalSpy chunks(handle, &HttpRequestHandle::dataChunkReceived);
    QSignalSpy failure(handle, &HttpRequestHandle::failed);

    QVERIFY(failure.wait());
    QCOMPARE(chunks.count(), 0);
    QCOMPARE(errorFrom(failure).code(), HttpErrorCode::ClientError);
    QCOMPARE(responseFrom(failure, 1).body(), QByteArrayLiteral("{\"success\":false}"));
}

void HttpClientTest::distinguishesHttpAndParseErrors()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(404, QByteArrayLiteral("{\"success\":false}")));
    server.enqueueResponse(jsonResponse(503, QByteArrayLiteral("{\"success\":false}")));
    server.enqueueResponse(jsonResponse(200, QByteArrayLiteral("{bad")));
    MockHttpResponse wrongType = jsonResponse();
    wrongType.headers.clear();
    wrongType.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("text/plain")});
    server.enqueueResponse(wrongType);
    HttpClient client;

    const QList<HttpErrorCode> expected {HttpErrorCode::ClientError, HttpErrorCode::ServerError,
                                         HttpErrorCode::ParseFailure, HttpErrorCode::ParseFailure};
    for (qsizetype index = 0; index < expected.size(); ++index) {
        HttpRequestHandle *handle = client.send(
            jsonGet(server.url(QStringLiteral("/error/%1").arg(index))));
        QSignalSpy failure(handle, &HttpRequestHandle::failed);
        QVERIFY(failure.wait());
        QCOMPARE(errorFrom(failure).code(), expected.at(index));
        if (index < 2) {
            QVERIFY(!responseFrom(failure, 1).body().isEmpty());
            QCOMPARE(errorFrom(failure).httpStatus().value(), index == 0 ? 404 : 503);
        }
    }
}

void HttpClientTest::reportsOnlyObservedHttpResponses()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(jsonResponse(503, QByteArrayLiteral("{\"success\":false}")));
    server.enqueueResponse(jsonResponse(200, QByteArrayLiteral("{bad")));
    MockHttpResponse incomplete = jsonResponse();
    incomplete.declaredContentLength = 100;
    incomplete.chunks = {QByteArrayLiteral("{")};
    incomplete.disconnectAfterChunks = 1;
    server.enqueueResponse(incomplete);
    MockHttpResponse incompleteError = jsonResponse(503, QByteArrayLiteral("{"));
    incompleteError.declaredContentLength = 100;
    incompleteError.disconnectAfterChunks = 1;
    server.enqueueResponse(incompleteError);

    HttpClient client;
    QSignalSpy responses(&client, &HttpClient::responseReceived);

    HttpRequestHandle *serverError = client.send(jsonGet(server.url(QStringLiteral("/503"))));
    QSignalSpy firstFailure(serverError, &HttpRequestHandle::failed);
    QVERIFY(firstFailure.wait());
    QCOMPARE(responses.count(), 1);
    QCOMPARE(responses.first().at(0).toInt(), 503);
    QCOMPARE(responses.first().at(1).toULongLong(), quint64(1));

    HttpRequestHandle *parseError = client.send(jsonGet(server.url(QStringLiteral("/parse"))));
    QSignalSpy secondFailure(parseError, &HttpRequestHandle::failed);
    QVERIFY(secondFailure.wait());
    QCOMPARE(responses.count(), 2);
    QCOMPARE(responses.last().at(0).toInt(), 200);

    HttpRequestHandle *networkError = client.send(jsonGet(server.url(QStringLiteral("/partial"))));
    QSignalSpy thirdFailure(networkError, &HttpRequestHandle::failed);
    QVERIFY(thirdFailure.wait());
    QCOMPARE(errorFrom(thirdFailure).code(), HttpErrorCode::NetworkError);
    QCOMPARE(responses.count(), 2);

    HttpRequestHandle *incompleteServerError = client.send(
        jsonGet(server.url(QStringLiteral("/partial-503"))));
    QSignalSpy fourthFailure(incompleteServerError, &HttpRequestHandle::failed);
    QVERIFY(fourthFailure.wait());
    QCOMPARE(responses.count(), 2);

    MockHttpResponse oversized = jsonResponse();
    oversized.declaredContentLength = HttpRequestPolicy::jsonMaximumBytes + 1;
    server.enqueueResponse(oversized);
    HttpClient staleClient([](const RequestContext &) { return false; });
    QSignalSpy staleResponses(&staleClient, &HttpClient::responseReceived);
    HttpRequestHandle *staleOversized = staleClient.send(
        jsonGet(server.url(QStringLiteral("/stale-oversized"))));
    QSignalSpy staleFailure(staleOversized, &HttpRequestHandle::failed);
    QVERIFY(staleFailure.wait());
    QCOMPARE(staleResponses.count(), 0);
}

void HttpClientTest::rejectsRedirectsWithoutFollowing()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse redirect;
    redirect.statusCode = 302;
    redirect.reasonPhrase = QByteArrayLiteral("Found");
    redirect.headers.append({QByteArrayLiteral("Location"), server.url(QStringLiteral("/target")).toEncoded()});
    server.enqueueResponse(redirect);
    server.enqueueResponse(redirect);
    HttpClient client;
    HttpRequestHandle *handle = client.send(jsonGet(server.url(QStringLiteral("/redirect"))));
    QSignalSpy failure(handle, &HttpRequestHandle::failed);

    QVERIFY(failure.wait());
    QCOMPARE(errorFrom(failure).code(), HttpErrorCode::RedirectRejected);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().target, QByteArrayLiteral("/redirect"));

    HttpRequest csvRequest(HttpMethod::Get, server.url(QStringLiteral("/csv-redirect")),
                                 HttpRequestKind::Csv, HttpResponseMode::CsvStream,
                                 context());
    HttpRequestHandle *csvHandle = client.send(std::move(csvRequest));
    QSignalSpy csvFailure(csvHandle, &HttpRequestHandle::failed);
    QVERIFY(csvFailure.wait());
    QCOMPARE(errorFrom(csvFailure).code(), HttpErrorCode::RedirectRejected);
    QCOMPARE(server.requests().size(), 2);
}

void HttpClientTest::distinguishesNetworkTlsTimeoutAndCancellation()
{
    MockHttpServer disconnectedServer;
    QVERIFY(disconnectedServer.listen());
    MockHttpResponse disconnected = jsonResponse();
    disconnected.declaredContentLength = 100;
    disconnected.chunks = {QByteArrayLiteral("{")};
    disconnected.disconnectAfterChunks = 1;
    disconnectedServer.enqueueResponse(disconnected);
    HttpClient client;
    HttpRequestHandle *networkHandle = client.send(jsonGet(disconnectedServer.url()));
    QSignalSpy networkFailure(networkHandle, &HttpRequestHandle::failed);
    QVERIFY(networkFailure.wait());
    QCOMPARE(errorFrom(networkFailure).code(), HttpErrorCode::NetworkError);

    TlsErrorNetworkManager tlsManager;
    HttpClient tlsClient(&tlsManager, {}, {}, nullptr);
    HttpRequestHandle *tlsHandle = tlsClient.send(jsonGet(QUrl(QStringLiteral("https://example.test"))));
    QSignalSpy tlsFailure(tlsHandle, &HttpRequestHandle::failed);
    QVERIFY(tlsFailure.wait());
    QCOMPARE(errorFrom(tlsFailure).code(), HttpErrorCode::TlsError);

    MockHttpServer slowServer;
    QVERIFY(slowServer.listen());
    MockHttpResponse slow = jsonResponse();
    slow.initialDelayMs = 200;
    slowServer.enqueueResponse(slow);
    QNetworkAccessManager timeoutManager;
    HttpClient timeoutClient(&timeoutManager,
                             {},
                             [](HttpRequestKind) { return std::chrono::milliseconds(20); });
    HttpRequestHandle *timeoutHandle = timeoutClient.send(jsonGet(slowServer.url()));
    QSignalSpy timeoutFailure(timeoutHandle, &HttpRequestHandle::failed);
    QVERIFY(timeoutFailure.wait());
    QCOMPARE(errorFrom(timeoutFailure).code(), HttpErrorCode::Timeout);
    QTest::qWait(250);
    QCOMPARE(slowServer.requests().size(), 1);

    MockHttpServer cancelServer;
    QVERIFY(cancelServer.listen());
    MockHttpResponse cancelResponse = jsonResponse();
    cancelResponse.initialDelayMs = 200;
    cancelServer.enqueueResponse(cancelResponse);
    HttpRequestHandle *cancelHandle = client.send(jsonGet(cancelServer.url()));
    QSignalSpy cancelled(cancelHandle, &HttpRequestHandle::failed);
    QSignalSpy cancelledSuccess(cancelHandle, &HttpRequestHandle::succeeded);
    QSignalSpy cancelledFinished(cancelHandle, &HttpRequestHandle::finished);
    cancelHandle->cancel();
    cancelHandle->cancel();
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(cancelledFinished.count(), 1);
    QCOMPARE(cancelledSuccess.count(), 0);
    QCOMPARE(errorFrom(cancelled).code(), HttpErrorCode::Cancelled);
    QTest::qWait(50);
    QCOMPARE(cancelled.count(), 1);
}

void HttpClientTest::cancelAllUsesCallTimeSnapshot()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse delayed = jsonResponse();
    delayed.initialDelayMs = 200;
    server.enqueueResponse(delayed);
    server.enqueueResponse(jsonResponse());
    HttpClient client;

    HttpRequestHandle *oldRequest = client.send(jsonGet(server.url(QStringLiteral("/old"))));
    QSignalSpy oldFailure(oldRequest, &HttpRequestHandle::failed);
    QSignalSpy oldSuccess(oldRequest, &HttpRequestHandle::succeeded);
    client.cancelAll();
    QCOMPARE(oldFailure.count(), 1);
    QCOMPARE(errorFrom(oldFailure).code(), HttpErrorCode::Cancelled);
    QCOMPARE(oldSuccess.count(), 0);

    HttpRequestHandle *newRequest = client.send(jsonGet(server.url(QStringLiteral("/new"))));
    QSignalSpy newSuccess(newRequest, &HttpRequestHandle::succeeded);
    QSignalSpy newFailure(newRequest, &HttpRequestHandle::failed);
    QVERIFY(newSuccess.wait());
    QCOMPARE(newFailure.count(), 0);
    QCOMPARE(oldFailure.count(), 1);
}

void HttpClientTest::destroyingHandleStopsRequestWithoutCallback()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse slow = jsonResponse();
    slow.initialDelayMs = 200;
    server.enqueueResponse(slow);
    QNetworkAccessManager manager;
    HttpClient client(&manager,
                      {},
                      [](HttpRequestKind) { return std::chrono::milliseconds(30); });
    HttpRequestHandle *handle = client.send(jsonGet(server.url()));
    QSignalSpy success(handle, &HttpRequestHandle::succeeded);
    QSignalSpy failure(handle, &HttpRequestHandle::failed);
    QSignalSpy destroyed(handle, &QObject::destroyed);

    delete handle;
    QCOMPARE(destroyed.count(), 1);
    QTest::qWait(250);
    QCOMPARE(success.count(), 0);
    QCOMPARE(failure.count(), 0);
    QVERIFY(server.requests().size() <= 1);
}

void HttpClientTest::rejectsStaleAndOutOfOrderResponses()
{
    MockHttpServer server;
    QVERIFY(server.listen());
    MockHttpResponse first = jsonResponse(200, QByteArrayLiteral("{\"order\":1}"));
    first.initialDelayMs = 100;
    MockHttpResponse second = jsonResponse(200, QByteArrayLiteral("{\"order\":2}"));
    server.enqueueResponse(first);
    server.enqueueResponse(second);
    quint64 currentRequest = 1;
    HttpClient client([&currentRequest](const RequestContext &requestContext) {
        return requestContext.requestGeneration() == currentRequest;
    });

    HttpRequestHandle *oldHandle = client.send(
        jsonGet(server.url(QStringLiteral("/first")), context(1, 1)));
    QSignalSpy oldFailure(oldHandle, &HttpRequestHandle::failed);
    QSignalSpy oldSuccess(oldHandle, &HttpRequestHandle::succeeded);
    currentRequest = 2;
    HttpRequestHandle *newHandle = client.send(
        jsonGet(server.url(QStringLiteral("/second")), context(1, 2)));
    QSignalSpy newSuccess(newHandle, &HttpRequestHandle::succeeded);

    QVERIFY(newSuccess.wait());
    QVERIFY(oldFailure.wait());
    QCOMPARE(oldSuccess.count(), 0);
    QCOMPARE(errorFrom(oldFailure).code(), HttpErrorCode::StaleResponse);
    QCOMPARE(responseFrom(newSuccess).body(), QByteArrayLiteral("{\"order\":2}"));
}

QTEST_MAIN(HttpClientTest)

#include "tst_http_client.moc"
