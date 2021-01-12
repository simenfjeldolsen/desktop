#include "websocket.h"

namespace OCC {

WebSocket::WebSocket()
{
    connect(&_webSocket, &QWebSocket::connected, this, &WebSocket::connected);
    connect(&_webSocket, &QWebSocket::disconnected, this, &WebSocket::disconnected);
    connect(&_webSocket, &QWebSocket::textMessageReceived, this, &WebSocket::textMessageReceived);
}

void WebSocket::open(const QUrl &url)
{
    _webSocket.open(url);
}

void WebSocket::close()
{
    _webSocket.close();
}

qint64 WebSocket::sendTextMessage(const QString &message)
{
    return _webSocket.sendTextMessage(message);
}
}
