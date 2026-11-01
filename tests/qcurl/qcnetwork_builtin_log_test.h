#ifndef QCNETWORK_BUILTIN_LOG_TEST_H
#define QCNETWORK_BUILTIN_LOG_TEST_H

#include <QObject>

class tst_QCNetworkBuiltinLogRedaction : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkBuiltinLogRedaction() = default;

private Q_SLOTS:
    void libIdsTraceKeepsSafeHeaders_data();
    void libIdsTraceKeepsSafeHeaders();
    void builtinLogs_data();
    void builtinLogs();
    void redactsOpaqueQueries();
    void transportFailureKeepsSafeDiagnostics();
    void fullRedirectAndWireIdentity();
    void quotedRedirectUrlsKeepSecretsPrivate_data();
    void quotedRedirectUrlsKeepSecretsPrivate();
    void fullTransportFailureKeepsUrlField();
    void authenticationResendDoesNotReopenFull_data();
    void authenticationResendDoesNotReopenFull();
    void retryStartsNewFullAttempt_data();
    void retryStartsNewFullAttempt();
    void cookieTextKeepsCredentialsPrivate_data();
    void cookieTextKeepsCredentialsPrivate();
    void normalizedInitialTargetPreservesWireAndQuery_data();
    void normalizedInitialTargetPreservesWireAndQuery();

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkBuiltinLogRedaction)
};

#endif // QCNETWORK_BUILTIN_LOG_TEST_H
