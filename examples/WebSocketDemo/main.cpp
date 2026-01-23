#include <QCWebSocket.h>
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QThread>
#include <QTimer>

using namespace QCurl;

/**
 * @brief WebSocket Echo 客户端示例程序
 *
 * 连接到公共 Echo 服务器 (wss://echo.websocket.org)，
 * 演示 QCWebSocket 的基本使用方法。
 *
 * 功能：
 * - 自动连接到 WebSocket 服务器
 * - 显示连接状态变化
 * - 接收用户输入并发送到服务器
 * - 显示服务器回显的消息
 * - 输入 "quit" 退出程序
 */

class WebSocketDemo : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketDemo(QObject *parent = nullptr)
        : QObject(parent)
        , m_socket(new QCWebSocket(QUrl("wss://echo.websocket.org"), this))
        , m_inputTimer(new QTimer(this))
    {
        setupConnections();
        setupInputTimer();
    }

public slots:
    void start()
    {
        printWelcome();
        m_socket->open();
    }

private slots:
    void onConnected()
    {
        qInfo() << "✅ WebSocket 连接成功！";
        qInfo() << "提示：输入消息后按回车发送（输入 'quit' 退出）";
        qInfo();

        // 启动输入定时器
        m_inputTimer->start();
    }

    void onDisconnected()
    {
        qInfo() << "❌ WebSocket 连接已断开";
        QCoreApplication::quit();
    }

    void onStateChanged(QCWebSocket::State state)
    {
        QString stateStr;
        switch (state) {
            case QCWebSocket::State::Unconnected:
                stateStr = "未连接";
                break;
            case QCWebSocket::State::Connecting:
                stateStr = "连接中...";
                break;
            case QCWebSocket::State::Connected:
                stateStr = "已连接";
                break;
            case QCWebSocket::State::Closing:
                stateStr = "关闭中...";
                break;
            case QCWebSocket::State::Closed:
                stateStr = "已关闭";
                break;
        }
        qDebug() << "📡 状态变化:" << stateStr;
    }

    void onTextMessageReceived(const QString &message) { qInfo() << "📩 收到消息:" << message; }

    void onBinaryMessageReceived(const QByteArray &data)
    {
        qInfo() << "📦 收到二进制数据:" << data.size() << "字节";
    }

    void onPongReceived(const QByteArray &payload) { qDebug() << "🏓 收到 Pong 响应:" << payload; }

    void onErrorOccurred(const QString &error) { qWarning() << "❌ 错误:" << error; }

    void onSslErrors(const QStringList &errors)
    {
        qWarning() << "🔒 SSL 错误:";
        for (const QString &err : errors) {
            qWarning() << "  -" << err;
        }
    }

    void checkInput()
    {
        // 检查是否有标准输入
        if (!QTextStream(stdin).atEnd()) {
            QString line = QTextStream(stdin).readLine().trimmed();

            if (line.isEmpty()) {
                return;
            }

            if (line.toLower() == "quit") {
                qInfo() << "👋 正在关闭连接...";
                m_socket->close(QCWebSocket::CloseCode::Normal, "User Quit");
                return;
            }

            if (line == "ping") {
                qInfo() << "🏓 发送 Ping...";
                m_socket->ping("Ping from QCurl Demo");
                return;
            }

            if (line == "help") {
                printHelp();
                return;
            }

            if (m_socket->state() != QCWebSocket::State::Connected) {
                qWarning() << "⚠️ 未连接，无法发送消息";
                return;
            }

            // 发送文本消息
            qInfo() << "📤 发送消息:" << line;
            qint64 sent = m_socket->sendTextMessage(line);
            if (sent < 0) {
                qWarning() << "❌ 发送失败";
            } else {
                qDebug() << "发送字节数:" << sent;
            }
        }
    }

private:
    void setupConnections()
    {
        connect(m_socket, &QCWebSocket::connected, this, &WebSocketDemo::onConnected);
        connect(m_socket, &QCWebSocket::disconnected, this, &WebSocketDemo::onDisconnected);
        connect(m_socket, &QCWebSocket::stateChanged, this, &WebSocketDemo::onStateChanged);
        connect(m_socket,
                &QCWebSocket::textMessageReceived,
                this,
                &WebSocketDemo::onTextMessageReceived);
        connect(m_socket,
                &QCWebSocket::binaryMessageReceived,
                this,
                &WebSocketDemo::onBinaryMessageReceived);
        connect(m_socket, &QCWebSocket::pongReceived, this, &WebSocketDemo::onPongReceived);
        connect(m_socket, &QCWebSocket::errorOccurred, this, &WebSocketDemo::onErrorOccurred);
        connect(m_socket, &QCWebSocket::sslErrors, this, &WebSocketDemo::onSslErrors);
    }

    void setupInputTimer()
    {
        m_inputTimer->setInterval(100); // 每 100ms 检查一次输入
        connect(m_inputTimer, &QTimer::timeout, this, &WebSocketDemo::checkInput);
    }

    void printWelcome()
    {
        qInfo() << "===========================================";
        qInfo() << "QCurl WebSocket Echo 客户端示例";
        qInfo() << "===========================================";
        qInfo() << "连接到: wss://echo.websocket.org";
        qInfo();
    }

    void printHelp()
    {
        qInfo() << "";
        qInfo() << "可用命令：";
        qInfo() << "  help  - 显示此帮助信息";
        qInfo() << "  ping  - 发送 Ping 帧";
        qInfo() << "  quit  - 退出程序";
        qInfo() << "";
        qInfo() << "输入任何其他文本将作为消息发送到服务器";
        qInfo() << "";
    }

private:
    QCWebSocket *m_socket;
    QTimer *m_inputTimer;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("WebSocketDemo");
    app.setApplicationVersion("2.3.0");

    // 创建并启动 WebSocket 演示
    WebSocketDemo demo;
    QTimer::singleShot(0, &demo, &WebSocketDemo::start);

    return app.exec();
}

#include "main.moc"
