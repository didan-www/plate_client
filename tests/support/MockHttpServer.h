#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPair>
#include <QTcpServer>
#include <QUrl>

#include <optional>

class QTcpSocket;

struct MockHttpResponse final
{
    int statusCode = 200;
    QByteArray reasonPhrase = QByteArrayLiteral("OK");
    QList<QPair<QByteArray, QByteArray>> headers;
    QList<QByteArray> chunks;
    int initialDelayMs = 0;
    int chunkDelayMs = 0;
    std::optional<qint64> declaredContentLength;
    bool omitContentLength = false;
    int disconnectAfterChunks = -1;
};

struct MockHttpRequest final
{
    QByteArray method;
    QByteArray target;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;

    [[nodiscard]] QByteArray header(const QByteArray &name) const;
};

class MockHttpServer final : public QObject
{
    Q_OBJECT

public:
    explicit MockHttpServer(QObject *parent = nullptr);

    [[nodiscard]] bool listen();
    [[nodiscard]] QUrl url(const QString &path = QStringLiteral("/")) const;
    void enqueueResponse(MockHttpResponse response);
    [[nodiscard]] const QList<MockHttpRequest> &requests() const noexcept;

signals:
    void requestReceived();

private:
    void acceptConnection();
    void consumeRequest(QTcpSocket *socket);
    void sendResponse(QTcpSocket *socket, MockHttpResponse response);

    QTcpServer server_;
    QList<MockHttpResponse> responses_;
    QList<MockHttpRequest> requests_;
};
