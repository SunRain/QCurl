#ifndef TST_QCNETWORKNATIVEDIAGNOSTICS_H
#define TST_QCNETWORKNATIVEDIAGNOSTICS_H

#include <QObject>

class QUrl;
namespace QCurl {
class QCNetworkRequest;
}

class tst_QCNetworkNativeDiagnostics final : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkNativeDiagnostics() = default;

private Q_SLOTS:
    void transferResults_data();
    void transferResults();
    void tlsFailures_data();
    void tlsFailures();
    void deviceReadFailure_data();
    void deviceReadFailure();
    void deviceWriteFailure_data();
    void deviceWriteFailure();
    void retryResult_data();
    void retryResult();
    void retryRestoreFailure();
    void cancelResult_data();
    void cancelResult();
    void cancelDuringBackoff_data();
    void cancelDuringBackoff();
    void cacheFallback_data();
    void cacheFallback();
    void mockAfterNetworkFailure();
    void copyDiagnosticBeforeDestruction_data();
    void copyDiagnosticBeforeDestruction();

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkNativeDiagnostics)
    static QCurl::QCNetworkRequest retryRequest(const QUrl &url, int delayMs = 0);
};

#endif
