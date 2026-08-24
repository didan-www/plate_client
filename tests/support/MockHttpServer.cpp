#include "support/MockHttpServer.h"

#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>
#include <utility>

namespace {
struct IncomingRequest final
{
    QByteArray bytes;
    qsizetype headerEnd = -1;
    qint64 contentLength = 0;
    bool completed = false;
};

QByteArray normalizedName(const QByteArray &name)
{
    return name.trimmed().toLower();
}

MockHttpRequest parseRequest(const QByteArray &bytes, qsizetype headerEnd)
{
    MockHttpRequest request;
    const QList<QByteArray> lines = bytes.left(headerEnd).split('\n');
    if (!lines.isEmpty()) {
        const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
        if (requestLine.size() >= 2) {
            request.method = requestLine.at(0);
            request.target = requestLine.at(1);
        }
    }
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0) {
            request.headers.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
        }
    }
    request.body = bytes.mid(headerEnd + 4);
    return request;
}
} // namespace

QByteArray MockHttpRequest::header(const QByteArray &name) const
{
    const QByteArray expected = normalizedName(name);
    for (const auto &header : headers) {
        if (normalizedName(header.first) == expected) {
            return header.second;
        }
    }
    return {};
}

MockHttpServer::MockHttpServer(QObject *parent)
    : QObject(parent)
{
    connect(&server_, &QTcpServer::newConnection, this, &MockHttpServer::acceptConnection);
}

bool MockHttpServer::listen()
{
    return server_.listen(QHostAddress::LocalHost, 0);
}

QUrl MockHttpServer::url(const QString &path) const
{
    QUrl result;
    result.setScheme(QStringLiteral("http"));
    result.setHost(QStringLiteral("127.0.0.1"));
    result.setPort(server_.serverPort());
    result.setPath(path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path);
    return result;
}

void MockHttpServer::enqueueResponse(MockHttpResponse response)
{
    responses_.append(std::move(response));
}

const QList<MockHttpRequest> &MockHttpServer::requests() const noexcept
{
    return requests_;
}

void MockHttpServer::acceptConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket *socket = server_.nextPendingConnection();
        socket->setParent(this);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto incoming = std::make_shared<IncomingRequest>();
        const auto consumeAvailable = [this, socket, incoming] {
            if (incoming->completed) {
                return;
            }
            incoming->bytes.append(socket->readAll());
            if (incoming->headerEnd < 0) {
                incoming->headerEnd = incoming->bytes.indexOf("\r\n\r\n");
                if (incoming->headerEnd < 0) {
                    return;
                }
                const MockHttpRequest partial = parseRequest(incoming->bytes, incoming->headerEnd);
                bool valid = false;
                const qint64 length = partial.header(QByteArrayLiteral("Content-Length"))
                                          .toLongLong(&valid);
                incoming->contentLength = valid ? length : 0;
            }
            const qint64 bodyBytes = incoming->bytes.size() - incoming->headerEnd - 4;
            if (bodyBytes < incoming->contentLength) {
                return;
            }
            MockHttpRequest request = parseRequest(incoming->bytes, incoming->headerEnd);
            if (request.body.size() > incoming->contentLength) {
                request.body.truncate(incoming->contentLength);
            }
            requests_.append(std::move(request));
            incoming->completed = true;
            emit requestReceived();
            consumeRequest(socket);
        };
        connect(socket, &QTcpSocket::readyRead, socket, consumeAvailable);
        consumeAvailable();
        QTimer::singleShot(0, socket, consumeAvailable);
    }
}

void MockHttpServer::consumeRequest(QTcpSocket *socket)
{
    MockHttpResponse response;
    if (!responses_.isEmpty()) {
        response = responses_.takeFirst();
    }
    sendResponse(socket, std::move(response));
}

void MockHttpServer::sendResponse(QTcpSocket *socket, MockHttpResponse response)
{
    QByteArray payload;
    for (const QByteArray &chunk : response.chunks) {
        payload.append(chunk);
    }
    const qint64 declaredLength = response.declaredContentLength.value_or(payload.size());
    QByteArray head = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(response.statusCode)
                      + ' ' + response.reasonPhrase + QByteArrayLiteral("\r\n");
    bool hasLength = false;
    bool hasConnection = false;
    for (const auto &header : response.headers) {
        if (normalizedName(header.first) == QByteArrayLiteral("content-length")) {
            hasLength = true;
        }
        if (normalizedName(header.first) == QByteArrayLiteral("connection")) {
            hasConnection = true;
        }
        head += header.first + QByteArrayLiteral(": ") + header.second + QByteArrayLiteral("\r\n");
    }
    if (!hasLength && !response.omitContentLength) {
        head += QByteArrayLiteral("Content-Length: ") + QByteArray::number(declaredLength)
                + QByteArrayLiteral("\r\n");
    }
    if (!hasConnection) {
        head += QByteArrayLiteral("Connection: close\r\n");
    }
    head += QByteArrayLiteral("\r\n");

    auto chunks = std::make_shared<QList<QByteArray>>(std::move(response.chunks));
    auto index = std::make_shared<int>(0);
    const int disconnectAfterChunks = response.disconnectAfterChunks;
    const int chunkDelayMs = response.chunkDelayMs;
    QPointer<QTcpSocket> guardedSocket(socket);
    QTimer::singleShot(response.initialDelayMs, socket,
                       [guardedSocket, head, chunks, index, disconnectAfterChunks,
                        chunkDelayMs]() mutable {
        if (!guardedSocket) {
            return;
        }
        guardedSocket->write(head);
        guardedSocket->flush();
        auto sendNext = std::make_shared<std::function<void()>>();
        *sendNext = [guardedSocket, chunks, index, disconnectAfterChunks,
                     chunkDelayMs, sendNext]() mutable {
            if (!guardedSocket) {
                return;
            }
            if (disconnectAfterChunks >= 0 && *index >= disconnectAfterChunks) {
                *sendNext = {};
                guardedSocket->disconnectFromHost();
                return;
            }
            if (*index >= chunks->size()) {
                *sendNext = {};
                guardedSocket->disconnectFromHost();
                return;
            }
            guardedSocket->write(chunks->at(*index));
            guardedSocket->flush();
            ++*index;
            QTimer::singleShot(chunkDelayMs, guardedSocket, *sendNext);
        };
        (*sendNext)();
    });
}
