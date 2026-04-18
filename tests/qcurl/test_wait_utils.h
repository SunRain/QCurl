#ifndef QCURL_TEST_WAIT_UTILS_H
#define QCURL_TEST_WAIT_UTILS_H

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>

#include <type_traits>
#include <utility>

namespace QCurl::TestWaitUtils {

/**
 * @brief Wait until a predicate becomes true or times out.
 */
template <typename Predicate>
bool waitUntil(Predicate &&predicate, int timeoutMs, int stepMs = 25)
{
    if (predicate()) {
        return true;
    }

    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer stepTimer;
        stepTimer.setSingleShot(true);
        QObject::connect(&stepTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

        const int remainingMs = timeoutMs - static_cast<int>(timer.elapsed());
        stepTimer.start(qMax(1, qMin(stepMs, remainingMs)));
        loop.exec(QEventLoop::AllEvents);

        if (predicate()) {
            return true;
        }
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents);
    return predicate();
}

/**
 * @brief Wait until a signal spy has observed at least @p minCount emissions.
 */
inline bool waitForSpyCount(QSignalSpy &spy, int minCount, int timeoutMs, int stepMs = 25)
{
    return waitUntil([&spy, minCount]() { return spy.count() >= minCount; }, timeoutMs, stepMs);
}

/**
 * @brief Wait until a signal spy observes a new emission beyond @p previousCount.
 */
inline bool waitForSpyGrowth(QSignalSpy &spy, int previousCount, int timeoutMs, int stepMs = 25)
{
    return waitUntil([&spy, previousCount]() { return spy.count() > previousCount; },
                     timeoutMs,
                     stepMs);
}

/**
 * @brief Verify that no new signal is emitted during the timeout window.
 */
inline bool waitForNoAdditionalSignal(QSignalSpy &spy, int timeoutMs, int stepMs = 25)
{
    const int initialCount = spy.count();
    return !waitForSpyGrowth(spy, initialCount, timeoutMs, stepMs);
}

/**
 * @brief Wait until a sampled value remains unchanged for a quiet window.
 */
template <typename Reader>
bool waitForStableValue(Reader &&reader,
                        int quietMs,
                        int timeoutMs,
                        std::invoke_result_t<Reader> *stableValue = nullptr,
                        int stepMs = 25)
{
    using Value = std::invoke_result_t<Reader>;

    Value last = reader();

    QElapsedTimer totalTimer;
    totalTimer.start();

    QElapsedTimer quietTimer;
    quietTimer.start();

    while (totalTimer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer stepTimer;
        stepTimer.setSingleShot(true);
        QObject::connect(&stepTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

        const int remainingMs = timeoutMs - static_cast<int>(totalTimer.elapsed());
        stepTimer.start(qMax(1, qMin(stepMs, remainingMs)));
        loop.exec(QEventLoop::AllEvents);

        const Value current = reader();
        if (current != last) {
            last = current;
            quietTimer.restart();
            continue;
        }

        if (quietTimer.elapsed() >= quietMs) {
            if (stableValue) {
                *stableValue = last;
            }
            return true;
        }
    }

    const Value current = reader();
    if (current == last && quietTimer.elapsed() >= quietMs) {
        if (stableValue) {
            *stableValue = current;
        }
        return true;
    }

    return false;
}

} // namespace QCurl::TestWaitUtils

#endif // QCURL_TEST_WAIT_UTILS_H
