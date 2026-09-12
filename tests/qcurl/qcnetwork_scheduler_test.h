// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORK_SCHEDULER_TEST_H
#define QCNETWORK_SCHEDULER_TEST_H

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_mock_test_support.h"

#include <QSignalSpy>
#include <QtTest>

class SchedulerHarness
{
public:
    SchedulerHarness()
    {
        QCurl::TestSupport::setMockHandler(m_manager, &m_mock);
        m_mock.setCaptureEnabled(true);
        m_mock.setGlobalDelay(80);
        m_manager.enableRequestScheduler(true);
    }

    bool configure(int concurrent = 1, int perHost = 2)
    {
        auto policy = m_manager.schedulerPolicy();
        policy.setMaxConcurrentRequests(concurrent);
        policy.setMaxRequestsPerHost(perHost);
        return m_manager.setSchedulerPolicy(policy);
    }

    QCurl::QCNetworkReply *get(
        const QUrl &url,
        QCurl::QCNetworkRequestPriority priority = QCurl::QCNetworkRequestPriority::Normal,
        const QCurl::QCNetworkLaneKey &lane      = QCurl::QCNetworkLaneKey::defaultLane(),
        const QByteArray &body                   = QByteArrayLiteral("OK"))
    {
        QCurl::QCNetworkRequest request(url);
        request.setPriority(priority);
        request.setLane(lane);
        m_mock.mockResponse(QCurl::HttpMethod::Get, url, body);
        return m_manager.get(request);
    }

    QCurl::QCNetworkReply *occupySlot()
    {
        return get(QUrl(QStringLiteral("http://blocker.test/")));
    }

    QCurl::QCNetworkAccessManager &manager() { return m_manager; }
    QCurl::QCNetworkMockHandler &mock() { return m_mock; }

private:
    QCurl::QCNetworkMockHandler m_mock;
    QCurl::QCNetworkAccessManager m_manager;
};

class tst_QCNetworkScheduler : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkScheduler() = default;

private Q_SLOTS:
    void testSchedulerEnabled();
    void testPriorityMetatypeContract();
    void testManagerLevelSchedulerPolicyConfiguresAdmission();
    void testUnknownLaneFailsClosed();
    void testInvalidSchedulerPriorityFailsClosed();
    void testStatistics();
    void testPriorityQueueOrdering();
    void testConcurrentRequestLimit();
    void testPerHostLimit();
    void testLaneReservationGating();
    void testWeightedFairnessByLane();
    void testPerHostHeadOfLineAvoidance();
    void testHostKeyUsesOrigin();
    void testReservationFallbackProgress();
    void testDeferUndefer();
    void testChangePriority();
    void testCommandReentryKeepsCommitResult();
    void testSchedulerCommandResultFailuresAreSideEffectFree();
    void testSchedulerCommandResultRejectsAffinityMismatch();
    void testCrossThreadSchedulerCommandsAreRejectedWithoutMutation();
    void testManagerCancelLaneRequestsReturnsStructuredFailClosedResult();
    void testCancelLaneRequests();
    void testQueueEmptySignal();
    void testRequestStartedRequiresExecuteDispatch();
    void testCancelAfterStartQueuedDoesNotStart();
    void testCancelFromAboutToStartSlotPreventsExecute();
    void testDirectCancelContract_data();
    void testDirectCancelContract();
    void testFinishedDeletionPreservesTerminalAccounting();
    void testRequestQueuedReplyDeletionStopsContinuation();
    void testRequestQueuedManagerDeletionStopsContinuation();
    void testUndeferRequestQueuedManagerDeletionStopsContinuation();
    void testCancelledManagerDeletionStopsContinuation();
    void testAboutToStartDestruction_data();
    void testAboutToStartDestruction();
    void testExecutionBoundaryDestruction_data();
    void testExecutionBoundaryDestruction();
    void testQueuedObserverUsesCapturedGuardAndValueSnapshot();
    void testQueuedObserverCannotVetoStart();
    void testSchedulerConstructionDoesNotStartThrottleTimer();
    void testBandwidthWindowUsesProgressDeltas();
    void testProgressTrackingAutoConnectionAndTeardown();
    void testAdmissionWakeupOnlyWhileGating();
    void testExpiredStartTicketCannotExecute();
    void testNoEventDispatcherRejectsAdmission();

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkScheduler)
};

#endif // QCNETWORK_SCHEDULER_TEST_H
