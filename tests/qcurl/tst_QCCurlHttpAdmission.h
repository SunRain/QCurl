#ifndef TST_QCCURLHTTPADMISSION_H
#define TST_QCCURLHTTPADMISSION_H

#include <QObject>

class tst_QCCurlHttpAdmission final : public QObject
{
    Q_OBJECT

public:
    tst_QCCurlHttpAdmission() = default;

private Q_SLOTS:
    void cleanup();
    void acceptedReturnsTokenAndDetachedHandle();
    void rejectionReturnsSafeHandle_data();
    void rejectionReturnsSafeHandle();
    void callbackFlagsUseSharedState_data();
    void callbackFlagsUseSharedState();
    void explicitManagerControlsShare();
    void warningsStayInAdmissionResult();
    void wrongThreadAdmissionIsRejected();
    void tokenRemovalFromAnotherThreadIsQueued();
    void callbackRemovalPreservesStateUntilDetach();
    void stateOutlivesDestroyedObserver();
    void completionUsesReplyThread_data();
    void completionUsesReplyThread();
    void destroyedReplyDropsCompletion_data();
    void destroyedReplyDropsCompletion();
    void warningsPrecedeQueuedFailure();
    void callbackCancelAndAbort_data();
    void callbackCancelAndAbort();
    void replyDestroyedInCallback();
    void retryCountsOneLogicalRequest();
    void retryKeepsProgressCallbacksEnabled();
    void cancellationBeforeAdmissionDoesNotStartRequest();
    void acceptedRetryKeepsCompletionTokenCleared();

private:
    Q_DISABLE_COPY_MOVE(tst_QCCurlHttpAdmission)
};

#endif // TST_QCCURLHTTPADMISSION_H
