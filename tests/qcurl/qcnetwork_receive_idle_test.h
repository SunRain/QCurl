#ifndef QCNETWORK_RECEIVE_IDLE_TEST_H
#define QCNETWORK_RECEIVE_IDLE_TEST_H

#include <QObject>

class TestQCNetworkReceiveIdleTimeout final : public QObject
{
    Q_OBJECT
public:
    TestQCNetworkReceiveIdleTimeout() = default;
private Q_SLOTS:
    void configuration();
    void invalidConfiguration_data();
    void invalidConfiguration();
    void disabledAndMaximum_data();
    void disabledAndMaximum();
    void firstError_data();
    void firstError();
    void receiveIdleCost_data();
    void receiveIdleCost();
    void preparedSnapshot_data();
    void preparedSnapshot();
    void cacheFallbackClearsSource();
    void phases_data();
    void phases();
    void slowStreamAndExplicitTotal();
    void redirectAndReuse();
    void retryAfterAndTimeoutRetry();
    void cancelAndDestroy();
    void pauseAndBackpressure_data();
    void pauseAndBackpressure();
    void sendPauseDoesNotSuspendReceive();
    void uploadDoesNotRefreshReceive();
    void mockFirstResponseWait();
    void schedulerQueueAndCancel();
    void receiveHeaderSlowStream();
    void unsetTimeoutPreservesWaiting();
    void staleReuseReconnect();
    void callbackCancelAndDestroy();
    void pauseResumeThenStall();
    void replyMovedToOwnerThread();
    void mockRetryDiscardsLateAttempt();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkReceiveIdleTimeout)
};

#endif
