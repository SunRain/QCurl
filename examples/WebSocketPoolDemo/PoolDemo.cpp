#include "PoolDemo.h"
#include "QCWebSocketPool.h"
#include "QCWebSocket.h"
#include <QDebug>
#include <QThread>
#include <QEventLoop>
#include <QTimer>

using namespace QCurl;

PoolDemo::PoolDemo(QObject *parent)
    : QObject(parent)
{
}

PoolDemo::~PoolDemo()
{
}

bool PoolDemo::waitForConnection(QCWebSocket *socket, int timeout)
{
    if (!socket) {
        return false;
    }

    if (socket->state() == QCWebSocket::State::Connected) {
        return true;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(socket, &QCWebSocket::connected, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeout);
    loop.exec();

    return socket->state() == QCWebSocket::State::Connected;
}

void PoolDemo::printSeparator(const QString &title)
{
    qDebug() << "";
    qDebug() << "==========================================";
    qDebug() << title;
    qDebug() << "==========================================";
}

void PoolDemo::demoBasicUsage()
{
    printSeparator("演示 1：基本使用");

    QCWebSocketPool pool;
    QUrl url("wss://echo.websocket.org");

    qDebug() << "1. 创建连接池（使用默认配置）";
    qDebug() << "   - maxPoolSize: 10";
    qDebug() << "   - maxIdleTime: 300 秒";
    qDebug() << "   - enableKeepAlive: true";
    qDebug() << "";

    // 第一次获取
    qDebug() << "2. 第一次获取连接...";
    auto *socket1 = pool.acquire(url);
    if (!socket1) {
        qWarning() << "❌ 获取连接失败";
        return;
    }

    if (!waitForConnection(socket1, 10000)) {
        qWarning() << "❌ 连接超时";
        pool.release(socket1);
        return;
    }

    qDebug() << "   ✅ 连接成功！socket 地址:" << (void*)socket1;
    qDebug() << "   - 状态:" << (int)socket1->state();
    
    // 发送消息
    qDebug() << "";
    qDebug() << "3. 发送测试消息...";
    socket1->sendTextMessage("Hello from WebSocket Pool!");
    QThread::msleep(500);

    // 归还连接
    qDebug() << "";
    qDebug() << "4. 归还连接到池中...";
    pool.release(socket1);
    qDebug() << "   ✅ 连接已归还（未关闭）";

    // 查看统计
    auto stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "5. 统计信息:";
    qDebug() << "   - 总连接数:" << stats.totalConnections;
    qDebug() << "   - 活跃连接:" << stats.activeConnections;
    qDebug() << "   - 空闲连接:" << stats.idleConnections;
    qDebug() << "   - 未命中次数:" << stats.missCount;

    // 再次获取（应复用）
    qDebug() << "";
    qDebug() << "6. 再次获取连接（应复用）...";
    auto *socket2 = pool.acquire(url);
    if (!socket2) {
        qWarning() << "❌ 获取连接失败";
        return;
    }

    qDebug() << "   ✅ 获取成功！socket 地址:" << (void*)socket2;
    qDebug() << "   - 连接复用:" << (socket1 == socket2 ? "是 ✅" : "否 ❌");
    qDebug() << "   - 状态:" << (int)socket2->state();

    // 查看统计（应有命中记录）
    stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "7. 更新后的统计信息:";
    qDebug() << "   - 命中次数:" << stats.hitCount;
    qDebug() << "   - 未命中次数:" << stats.missCount;
    qDebug() << "   - 命中率:" << stats.hitRate << "%";

    pool.release(socket2);

    qDebug() << "";
    qDebug() << "✅ 基本使用演示完成！";
}

void PoolDemo::demoPreWarm()
{
    printSeparator("演示 2：预热连接");

    QCWebSocketPool pool;
    QUrl url("wss://echo.websocket.org");

    qDebug() << "预热连接可以提前建立连接，减少首次请求延迟";
    qDebug() << "";

    qDebug() << "1. 开始预热 5 个连接...";
    pool.preWarm(url, 5);

    qDebug() << "2. 等待连接建立（3 秒）...";
    QThread::sleep(3);

    auto stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "3. 预热后的统计信息:";
    qDebug() << "   - 总连接数:" << stats.totalConnections;
    qDebug() << "   - 空闲连接:" << stats.idleConnections;
    qDebug() << "   - 活跃连接:" << stats.activeConnections;

    qDebug() << "";
    qDebug() << "4. 获取连接（应直接从池中获取）...";
    auto *socket = pool.acquire(url);
    if (socket) {
        qDebug() << "   ✅ 立即获取到连接！";
        qDebug() << "   - 状态:" << (int)socket->state();
        pool.release(socket);
    }

    stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "5. 最终统计:";
    qDebug() << "   - 命中率:" << stats.hitRate << "%";

    qDebug() << "";
    qDebug() << "✅ 预热连接演示完成！";
}

void PoolDemo::demoStatistics()
{
    printSeparator("演示 3：统计信息");

    QCWebSocketPool pool;
    QUrl url("wss://echo.websocket.org");

    qDebug() << "执行多次操作，观察统计信息变化...";
    qDebug() << "";

    qDebug() << "1. 执行 10 次获取-释放操作:";
    for (int i = 0; i < 10; ++i) {
        auto *socket = pool.acquire(url);
        if (!socket) {
            qWarning() << "   第" << (i+1) << "次获取失败";
            continue;
        }

        if (i == 0 && !waitForConnection(socket, 10000)) {
            qWarning() << "   第一次连接超时";
            pool.release(socket);
            break;
        }

        qDebug() << "   操作" << (i+1) << "- socket:" << (void*)socket;
        QThread::msleep(100);
        pool.release(socket);
    }

    qDebug() << "";
    qDebug() << "2. 详细统计信息:";
    auto stats = pool.statistics(url);
    qDebug() << "   ┌─ 连接数统计";
    qDebug() << "   ├─ 总连接数:" << stats.totalConnections;
    qDebug() << "   ├─ 活跃连接:" << stats.activeConnections;
    qDebug() << "   └─ 空闲连接:" << stats.idleConnections;
    qDebug() << "";
    qDebug() << "   ┌─ 命中率统计";
    qDebug() << "   ├─ 命中次数:" << stats.hitCount;
    qDebug() << "   ├─ 未命中次数:" << stats.missCount;
    qDebug() << "   └─ 命中率:" << QString::number(stats.hitRate, 'f', 2) << "%";

    qDebug() << "";
    qDebug() << "✅ 统计信息演示完成！";
}

void PoolDemo::demoMultipleUrls()
{
    printSeparator("演示 4：多 URL 管理");

    QCWebSocketPool pool;
    QUrl url1("wss://echo.websocket.org");
    QUrl url2("wss://echo.websocket.org/echo");

    qDebug() << "连接池为每个 URL 维护独立的连接池";
    qDebug() << "";

    qDebug() << "1. 获取 URL1 的连接...";
    auto *socket1 = pool.acquire(url1);
    if (!socket1 || !waitForConnection(socket1, 10000)) {
        qWarning() << "❌ URL1 连接失败";
        if (socket1) pool.release(socket1);
        return;
    }
    qDebug() << "   ✅ URL1 连接成功";

    qDebug() << "";
    qDebug() << "2. 获取 URL2 的连接...";
    auto *socket2 = pool.acquire(url2);
    if (!socket2 || !waitForConnection(socket2, 10000)) {
        qWarning() << "❌ URL2 连接失败";
        if (socket2) pool.release(socket2);
        pool.release(socket1);
        return;
    }
    qDebug() << "   ✅ URL2 连接成功";

    qDebug() << "";
    qDebug() << "3. 验证连接独立性:";
    qDebug() << "   - socket1 地址:" << (void*)socket1;
    qDebug() << "   - socket2 地址:" << (void*)socket2;
    qDebug() << "   - 是否相同:" << (socket1 == socket2 ? "是 ❌" : "否 ✅");

    qDebug() << "";
    qDebug() << "4. URL1 池统计:";
    auto stats1 = pool.statistics(url1);
    qDebug() << "   - 总连接数:" << stats1.totalConnections;
    qDebug() << "   - 活跃连接:" << stats1.activeConnections;

    qDebug() << "";
    qDebug() << "5. URL2 池统计:";
    auto stats2 = pool.statistics(url2);
    qDebug() << "   - 总连接数:" << stats2.totalConnections;
    qDebug() << "   - 活跃连接:" << stats2.activeConnections;

    qDebug() << "";
    qDebug() << "6. 全局统计（所有 URL）:";
    auto globalStats = pool.statistics();
    qDebug() << "   - 全局总连接数:" << globalStats.totalConnections;
    qDebug() << "   - 应等于 URL1 + URL2:" << (stats1.totalConnections + stats2.totalConnections);

    pool.release(socket1);
    pool.release(socket2);

    qDebug() << "";
    qDebug() << "✅ 多 URL 管理演示完成！";
}
