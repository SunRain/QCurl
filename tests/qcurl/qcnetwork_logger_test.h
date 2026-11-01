#ifndef QCNETWORK_LOGGER_TEST_H
#define QCNETWORK_LOGGER_TEST_H

#include "QCNetworkAccessManager.h"

#include <QObject>

using namespace QCurl;

class TestQCNetworkLogger : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkLogger() = default;

private Q_SLOTS:
    void init();
    void cleanup();

    void testLogEntryDefaults();
    void testLogEntryAccessorsNormalizeUtc();
    void testLogEntryCopyAndMove();
    void testConvenienceOverloadForwards();
    void testDefaultLoggerFiltersAndStoresEntries();
    void testDefaultLoggerReportsSuccessfulAndFilteredOutput();
    void testDefaultLoggerRejectsInvalidFileConfigWithoutMutation();
    void testDefaultLoggerReportsOpenFailure();
    void testDefaultLoggerReportsBackupRemovalFailure();
    void testDefaultLoggerReportsCurrentLogRemovalFailure();
    void testDefaultLoggerReportsRotationRenameFailure();
    void testDefaultLoggerReportsWriteFailure();
    void testDefaultLoggerCallbackAndClear();
    void testDefaultLoggerCustomCallbackReentrant();
    void testDefaultLoggerQtMessageHandlerReentrant();
    void testDefaultLoggerFileOutput();
    void testSetAndGetLogger();
    void testLoggerHandleCopyMoveAndBorrowLifetime();
    void testLoggerNullptr();
    void testReplyRetainsLoggerSnapshotUntilDestroyed();
    void testDebugTraceFlag();
    void testDebugTraceRedaction();
    void testDebugTraceSignedUrlRedaction();
    void testTraceInitialTargetAndConnect();
    void testTraceSensitiveHeaderChunks();
    void testTraceBudgetAndTermination();
    void testTraceTextCredentialsAndFragments();
    void testTraceQuotedUrlRecordChunks_data();
    void testTraceQuotedUrlRecordChunks();
    void testRedactedUrlTextPreservesLineBoundaries();
    void testTraceAmbiguousHeadersFailClosed();
    void testTraceBlockPhaseAndStoppedStream();
    void testTraceCredentialRecordChunks();
    void testTraceNormalizedTargetMatching();
    void testTraceCallbacksShareRedactionAndStop();
    void testLibIdsSensitiveHeaders();
    void testLibIdsStrictDecoration();
    void testLibIdsRejectedDecoration();
    void testLibIdsLineAssociation();
    void testLibIdsFragmentConflict();
    void testLibIdsTargetAndConnect();
    void testLibIdsBudgetsAndTruncation();
    void testLibIdsAggregateBudget();
    void testTraceSourceIsolation();
    void testTraceInternalSwitchStaysClosed();
    void testTraceUnknownSourceClosesTarget();
    void testLibIdsProductionCallbacks();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkLogger)

    QCNetworkAccessManager *m_manager = nullptr;
};

#endif // QCNETWORK_LOGGER_TEST_H
