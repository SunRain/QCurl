#ifndef PERFORMANCETEST_H
#define PERFORMANCETEST_H

#include <QObject>

namespace QCurl {
class QCWebSocket;
}

/**
 * @brief WebSocket 连接池性能测试类
 * 
 * 对比有无连接池的性能差异：
 * - 连接建立时间
 * - 高频短消息吞吐量
 * - TLS 握手次数
 * 
 */
class PerformanceTest : public QObject
{
    Q_OBJECT

public:
    explicit PerformanceTest(QObject *parent = nullptr);
    ~PerformanceTest();

public slots:
    /**
     * @brief 测试 1：连接建立时间对比
     * 
     * 对比无连接池和有连接池的连接建立时间：
     * - 无连接池：每次都需要 TLS 握手（~2000ms）
     * - 有连接池：复用连接（~10ms）
     * 
     * 预期性能提升：-99%
     */
    void testConnectionTime();

    /**
     * @brief 测试 2：高频短消息吞吐量对比
     * 
     * 对比发送 100 条短消息的耗时：
     * - 无连接池：每次建立连接 + 发送 + 关闭（~200s）
     * - 有连接池：复用连接发送（~2s）
     * 
     * 预期性能提升：+100x
     */
    void testThroughput();

    /**
     * @brief 测试 3：TLS 握手次数统计
     * 
     * 说明 TLS 握手次数的差异：
     * - 无连接池：100 次请求 = 100 次握手
     * - 有连接池：100 次请求 = 1 次握手（首次）
     * 
     * 预期优化：-99%
     */
    void testTlsHandshakes();

    /**
     * @brief 运行所有性能测试
     */
    void runAllTests();

private:
    bool waitForConnection(QCurl::QCWebSocket *socket, int timeout = 5000);
    void printSeparator(const QString &title);
    void printPerformanceResult(const QString &metric, qint64 without, qint64 with, const QString &unit = "ms");
};

#endif // PERFORMANCETEST_H
