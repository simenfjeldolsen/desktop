#include <QTest>
#include <chrono>
#include <thread>

#include "websocket.h"

using namespace std::chrono_literals;

QSharedPointer<OCC::WebSocket>
createWebSocket()
{
    return QSharedPointer<OCC::WebSocket>(new OCC::WebSocket);
}

template <typename T>
void wait(T duration)
{
    std::this_thread::sleep_for(duration);
}

class TestWebSocket : public QObject
{
    Q_OBJECT

private slots:
    void testConnect()
    {
        auto connected = false;

        auto webSocket = createWebSocket();
        connect(webSocket.data(), &OCC::WebSocket::connected, [&]() {
            connected = true;
        });

        emit webSocket->_webSocket.connected();
        wait(4ms);

        QCOMPARE(connected, true);
    }

    void testDisconnect()
    {
        auto disconnected = false;

        auto webSocket = createWebSocket();
        connect(webSocket.data(), &OCC::WebSocket::disconnected, [&]() {
            disconnected = true;
        });

        emit webSocket->_webSocket.disconnected();
        wait(4ms);

        QCOMPARE(disconnected, true);
    }

    void testTextMessageReceived()
    {
        QString messageToSent = "Hello World!";
        QString messageReceived = "";

        auto webSocket = createWebSocket();
        connect(webSocket.data(), &OCC::WebSocket::textMessageReceived, [&](const QString &message) {
            messageReceived = message;
        });

        emit webSocket->_webSocket.textMessageReceived(messageToSent);
        wait(4ms);

        QCOMPARE(messageReceived, messageToSent);
    }
};

QTEST_GUILESS_MAIN(TestWebSocket)
#include "testwebsocket.moc"
