#ifndef POOLDEMO_H
#define POOLDEMO_H

#include <QObject>
#include <QUrl>

namespace QCurl {
class QCWebSocketPool;
class QCWebSocket;
}

/**
 * @brief WebSocket 连接池演示类
 * 
 * 展示 QCWebSocketPool 的各种使用场景：
 * - 基本使用（获取、释放、复用）
 * - 预热连接
 * - 统计信息
 * - 多 URL 管理
 * 
 */
class PoolDemo : public QObject
{
    Q_OBJECT

public:
    explicit PoolDemo(QObject *parent = nullptr);
    ~PoolDemo();

public slots:
    /**
     * @brief 演示 1：基本使用
     * 
     * 展示连接池的基本操作：
     * - 获取连接
     * - 发送消息
     * - 归还连接
     * - 再次获取（验证连接复用）
     */
    void demoBasicUsage();

    /**
     * @brief 演示 2：预热连接
     * 
     * 展示如何预先建立连接，减少首次请求延迟：
     * - 预热 5 个连接
     * - 查看统计信息
     */
    void demoPreWarm();

    /**
     * @brief 演示 3：统计信息
     * 
     * 展示如何获取和分析连接池统计信息：
     * - 总连接数、活跃连接、空闲连接
     * - 命中次数、未命中次数、命中率
     */
    void demoStatistics();

    /**
     * @brief 演示 4：多 URL 管理
     * 
     * 展示连接池如何管理多个 URL：
     * - 不同 URL 独立池
     * - 全局统计信息
     */
    void demoMultipleUrls();

private:
    bool waitForConnection(QCurl::QCWebSocket *socket, int timeout = 5000);
    void printSeparator(const QString &title);
};

#endif // POOLDEMO_H
