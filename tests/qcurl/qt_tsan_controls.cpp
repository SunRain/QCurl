#include <QCoreApplication>
#include <QLibraryInfo>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QWaitCondition>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

constexpr int kIterations = 10000;

int stdMutexControl()
{
    std::mutex mutex;
    int count            = 0;
    const auto increment = [&]() {
        for (int index = 0; index < kIterations; ++index) {
            const std::lock_guard<std::mutex> lock(mutex);
            ++count;
        }
    };
    std::thread first(increment);
    std::thread second(increment);
    first.join();
    second.join();
    return count == 2 * kIterations ? count : -1;
}

int qtMutexControl()
{
    QMutex mutex;
    int count            = 0;
    const auto increment = [&]() {
        for (int index = 0; index < kIterations; ++index) {
            const QMutexLocker<QMutex> lock(&mutex);
            ++count;
        }
    };
    std::thread first(increment);
    std::thread second(increment);
    first.join();
    second.join();
    return count == 2 * kIterations ? count : -1;
}

int qtWaitControl()
{
    QMutex mutex;
    QWaitCondition condition;
    bool ready  = false;
    int payload = 0;
    // 先持锁再启动生产者，确保消费者实际进入 wait()，而非只验证已就绪分支。
    QMutexLocker<QMutex> lock(&mutex);
    std::thread producer([&]() {
        const QMutexLocker<QMutex> lock(&mutex);
        payload = 42;
        ready   = true;
        condition.wakeOne();
    });
    int received = -1;
    while (!ready) {
        if (!condition.wait(&mutex, 5000)) {
            break;
        }
    }
    if (ready) {
        received = payload;
    }
    lock.unlock();
    producer.join();
    return received == 42 ? received : -1;
}

int qtQueuedControl(QCoreApplication &application)
{
    QObject receiver;
    int payload  = 0;
    int received = -1;
    std::thread producer([&]() {
        payload = 42;
        // 必须经队列发布数据；额外的应用锁会掩盖 Qt 投递路径缺少插桩的问题。
        QMetaObject::invokeMethod(
            &receiver,
            [&]() {
                received = payload;
                application.quit();
            },
            Qt::QueuedConnection);
    });
    QTimer::singleShot(5000, &application, &QCoreApplication::quit);
    application.exec();
    producer.join();
    return received == 42 ? received : -1;
}

void deliberateRaceWrite(volatile int *value,
                         std::atomic<int> *participants,
                         const std::atomic<bool> *start)
{
    participants->fetch_add(1, std::memory_order_relaxed);
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    // 负对照故意不加锁；必须由检测器定位这里的竞争并以 66 退出。
    for (int index = 0; index < kIterations; ++index) {
        *value = index;
    }
    // 两个线程写完之前都不退出，避免线程启动/退出的库内同步掩盖负对照。
    participants->fetch_add(1, std::memory_order_relaxed);
    while (participants->load(std::memory_order_relaxed) != 4) {
        std::this_thread::yield();
    }
}

void deliberateRaceControl()
{
    volatile int value = 0;
    std::atomic<int> participants{0};
    std::atomic<bool> start{false};
    std::thread first(deliberateRaceWrite, &value, &participants, &start);
    std::thread second(deliberateRaceWrite, &value, &participants, &start);
    while (participants.load(std::memory_order_relaxed) != 2) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    first.join();
    second.join();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "缺少固定对照场景参数\n");
        return 2;
    }
    int value = -1;
    if (std::strcmp(argv[1], "std-mutex") == 0) {
        value = stdMutexControl();
    } else if (std::strcmp(argv[1], "qt-mutex") == 0) {
        value = qtMutexControl();
    } else if (std::strcmp(argv[1], "qt-wait") == 0) {
        value = qtWaitControl();
    } else if (std::strcmp(argv[1], "qt-queued") == 0) {
        value = qtQueuedControl(application);
    } else if (std::strcmp(argv[1], "deliberate-race") == 0) {
        deliberateRaceControl();
        std::fprintf(stderr, "故意竞争未被 TSan 中止\n");
        return 0;
    }
    if (value < 0) {
        std::fprintf(stderr, "CONTROL FAIL %s\n", argv[1]);
        return 1;
    }
    std::printf("CONTROL PASS %s value=%d Qt=%s\n", argv[1], value, qVersion());
    std::printf("CONTROL COMPILER %s\nCONTROL QT_BUILD %s\n", __VERSION__, QLibraryInfo::build());
    return 0;
}
