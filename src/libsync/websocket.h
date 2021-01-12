#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <QWebSocket>

class TestWebSocket;

namespace OCC {

class AbstractWebSocket : public QObject
{
    Q_OBJECT

public:
    virtual void open(const QUrl &url) = 0;
    virtual void close() = 0;
    virtual qint64 sendTextMessage(const QString &message) = 0;

signals:
    void connected();
    void disconnected();
    void textMessageReceived(const QString &message);
};

class WebSocket : public AbstractWebSocket
{
    Q_OBJECT
public:
    WebSocket();

    void open(const QUrl &url) override;
    void close() override;
    qint64 sendTextMessage(const QString &message) override;

private:
    QWebSocket _webSocket;

    friend class ::TestWebSocket;
};
}

#endif // WEBSOCKET_H
