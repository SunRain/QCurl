#include "PoolDemo.h"

#include "QCWebSocket.h"
#include "QCWebSocketPool.h"

#include <QDebug>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QThread>
#include <QTimer>

using namespace QCurl;

namespace {

/**
 * @brief 连接池 owner thread 中一次已解析的活动借用。
 *
 * socket 仅用于当前 owner-thread 调用链；leaseId 是归还连接的唯一凭据。
 */
struct AcquiredSocket
{
    QCWebSocket *socket              = nullptr;
    QCWebSocketPool::LeaseId leaseId = 0;
};

template<typename Result>
Result awaitFuture(QFuture<Result> future, int timeout = 15000)
{
    QFutureWatcher<Result> watcher;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    timer.start(timeout);
    if (!future.isFinished()) {
        loop.exec();
    }
    return future.isFinished() ? future.result() : Result{};
}

/**
 * @brief 获取纯值 lease，并在连接池 owner thread 解析临时借用指针。
 * @param pool 连接池实例。
 * @param url 目标 WebSocket URL。
 * @return 成功时同时包含借用指针和非零 lease id；失败时返回空值。
 */
AcquiredSocket acquireSocket(QCWebSocketPool &pool, const QUrl &url)
{
    const auto result = awaitFuture(pool.acquire(url));
    if (!result.isSuccess()) {
        qWarning() << "获取连接失败:" << result.error();
        return {};
    }

    QCWebSocket *socket = nullptr;
    if (pool.resolveLease(result.leaseId(), &socket) != QCWebSocketPool::LeaseResult::Success) {
        static_cast<void>(pool.release(result.leaseId()));
        qWarning() << "解析连接 lease 失败";
        return {};
    }
    return {socket, result.leaseId()};
}

/**
 * @brief 按 lease id 归还连接并报告同步归还失败。
 * @param pool 连接池实例。
 * @param acquired 当前活动借用；函数不依赖其中的裸指针归还连接。
 */
void releaseSocket(QCWebSocketPool &pool, const AcquiredSocket &acquired)
{
    if (pool.release(acquired.leaseId) != QCWebSocketPool::LeaseResult::Success) {
        qWarning() << "连接归还失败";
    }
}

} // namespace

PoolDemo::PoolDemo(QObject *parent)
    : QObject(parent)
{}

PoolDemo::~PoolDemo() {}

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
    const auto acquired1 = acquireSocket(pool, url);
    auto *socket1        = acquired1.socket;
    if (!socket1) {
        qWarning() << "❌ 获取连接失败";
        return;
    }

    if (!waitForConnection(socket1, 10000)) {
        qWarning() << "❌ 连接超时";
        releaseSocket(pool, acquired1);
        return;
    }

    qDebug() << "   ✅ 连接成功！socket 地址:" << static_cast<const void *>(socket1);
    qDebug() << "   - 状态:" << static_cast<int>(socket1->state());

    // 发送消息
    qDebug() << "";
    qDebug() << "3. 发送测试消息...";
    static_cast<void>(socket1->sendTextMessage("Hello from WebSocket Pool!"));
    QThread::msleep(500);

    // 归还连接
    qDebug() << "";
    qDebug() << "4. 归还连接到池中...";
    releaseSocket(pool, acquired1);
    qDebug() << "   ✅ 连接已归还（未关闭）";

    // 查看统计
    auto stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "5. 统计信息:";
    qDebug() << "   - 总连接数:" << stats.totalConnections();
    qDebug() << "   - 活跃连接:" << stats.activeConnections();
    qDebug() << "   - 空闲连接:" << stats.idleConnections();
    qDebug() << "   - 未命中次数:" << stats.missCount();

    // 再次获取（应复用）
    qDebug() << "";
    qDebug() << "6. 再次获取连接（应复用）...";
    const auto acquired2 = acquireSocket(pool, url);
    auto *socket2        = acquired2.socket;
    if (!socket2) {
        qWarning() << "❌ 获取连接失败";
        return;
    }

    qDebug() << "   ✅ 获取成功！socket 地址:" << static_cast<const void *>(socket2);
    qDebug() << "   - 连接复用:" << (socket1 == socket2 ? "是 ✅" : "否 ❌");
    qDebug() << "   - 状态:" << static_cast<int>(socket2->state());

    // 查看统计（应有命中记录）
    stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "7. 更新后的统计信息:";
    qDebug() << "   - 命中次数:" << stats.hitCount();
    qDebug() << "   - 未命中次数:" << stats.missCount();
    qDebug() << "   - 命中率:" << stats.hitRate() << "%";

    releaseSocket(pool, acquired2);

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
    const auto preWarmResult = awaitFuture(pool.preWarm(url, 5));
    if (!preWarmResult.isSuccess()) {
        qWarning() << "预热失败:" << preWarmResult.error();
        return;
    }

    qDebug() << "2. 等待连接建立（3 秒）...";
    QThread::sleep(3);

    auto stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "3. 预热后的统计信息:";
    qDebug() << "   - 总连接数:" << stats.totalConnections();
    qDebug() << "   - 空闲连接:" << stats.idleConnections();
    qDebug() << "   - 活跃连接:" << stats.activeConnections();

    qDebug() << "";
    qDebug() << "4. 获取连接（应直接从池中获取）...";
    const auto acquired = acquireSocket(pool, url);
    auto *socket        = acquired.socket;
    if (socket) {
        qDebug() << "   ✅ 立即获取到连接！";
        qDebug() << "   - 状态:" << static_cast<int>(socket->state());
        releaseSocket(pool, acquired);
    }

    stats = pool.statistics(url);
    qDebug() << "";
    qDebug() << "5. 最终统计:";
    qDebug() << "   - 命中率:" << stats.hitRate() << "%";

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
        const auto acquired = acquireSocket(pool, url);
        auto *socket        = acquired.socket;
        if (!socket) {
            qWarning() << "   第" << (i + 1) << "次获取失败";
            continue;
        }

        if (i == 0 && !waitForConnection(socket, 10000)) {
            qWarning() << "   第一次连接超时";
            releaseSocket(pool, acquired);
            break;
        }

        qDebug() << "   操作" << (i + 1) << "- socket:" << static_cast<const void *>(socket);
        QThread::msleep(100);
        releaseSocket(pool, acquired);
    }

    qDebug() << "";
    qDebug() << "2. 详细统计信息:";
    auto stats = pool.statistics(url);
    qDebug() << "   ┌─ 连接数统计";
    qDebug() << "   ├─ 总连接数:" << stats.totalConnections();
    qDebug() << "   ├─ 活跃连接:" << stats.activeConnections();
    qDebug() << "   └─ 空闲连接:" << stats.idleConnections();
    qDebug() << "";
    qDebug() << "   ┌─ 命中率统计";
    qDebug() << "   ├─ 命中次数:" << stats.hitCount();
    qDebug() << "   ├─ 未命中次数:" << stats.missCount();
    qDebug() << "   └─ 命中率:" << QString::number(stats.hitRate(), 'f', 2) << "%";

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
    const auto acquired1 = acquireSocket(pool, url1);
    auto *socket1        = acquired1.socket;
    if (!socket1 || !waitForConnection(socket1, 10000)) {
        qWarning() << "❌ URL1 连接失败";
        if (socket1) {
            releaseSocket(pool, acquired1);
        }
        return;
    }
    qDebug() << "   ✅ URL1 连接成功";

    qDebug() << "";
    qDebug() << "2. 获取 URL2 的连接...";
    const auto acquired2 = acquireSocket(pool, url2);
    auto *socket2        = acquired2.socket;
    if (!socket2 || !waitForConnection(socket2, 10000)) {
        qWarning() << "❌ URL2 连接失败";
        if (socket2) {
            releaseSocket(pool, acquired2);
        }
        releaseSocket(pool, acquired1);
        return;
    }
    qDebug() << "   ✅ URL2 连接成功";

    qDebug() << "";
    qDebug() << "3. 验证连接独立性:";
    qDebug() << "   - socket1 地址:" << static_cast<const void *>(socket1);
    qDebug() << "   - socket2 地址:" << static_cast<const void *>(socket2);
    qDebug() << "   - 是否相同:" << (socket1 == socket2 ? "是 ❌" : "否 ✅");

    qDebug() << "";
    qDebug() << "4. URL1 池统计:";
    auto stats1 = pool.statistics(url1);
    qDebug() << "   - 总连接数:" << stats1.totalConnections();
    qDebug() << "   - 活跃连接:" << stats1.activeConnections();

    qDebug() << "";
    qDebug() << "5. URL2 池统计:";
    auto stats2 = pool.statistics(url2);
    qDebug() << "   - 总连接数:" << stats2.totalConnections();
    qDebug() << "   - 活跃连接:" << stats2.activeConnections();

    qDebug() << "";
    qDebug() << "6. 全局统计（所有 URL）:";
    auto globalStats = pool.statistics();
    qDebug() << "   - 全局总连接数:" << globalStats.totalConnections();
    qDebug() << "   - 应等于 URL1 + URL2:"
             << (stats1.totalConnections() + stats2.totalConnections());

    releaseSocket(pool, acquired1);
    releaseSocket(pool, acquired2);

    qDebug() << "";
    qDebug() << "✅ 多 URL 管理演示完成！";
}
