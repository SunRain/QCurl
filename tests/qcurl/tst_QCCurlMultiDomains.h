#ifndef TST_QCCURLMULTIDOMAINS_H
#define TST_QCCURLMULTIDOMAINS_H

#include "QCNetworkAccessManager.h"

#include <QObject>

class ResolveTestServer;

class TestQCCurlMultiDomains final : public QObject
{
    Q_OBJECT

public:
    TestQCCurlMultiDomains() = default;

private Q_SLOTS:
    void shareTransitions_data();
    void shareTransitions();
    void limitsAndAdmissionSnapshots();
    void idleDomainsAreBoundedAndAgeWithoutAdmission();
    void requiredResourceFailureDoesNotSend_data();
    void requiredResourceFailureDoesNotSend();
    void retryPinsDomainUntilTerminalState_data();
    void retryPinsDomainUntilTerminalState();
    void managerAddressReuseDoesNotInheritRetiredShare();
    void reclamationPreservesCookieStoreAndNativeDomain();
    void persistentConnectionSurvivesIsolatedDomainReclamation();

private:
    Q_DISABLE_COPY_MOVE(TestQCCurlMultiDomains)

    static QCurl::QCNetworkRequest requestFor(const ResolveTestServer &server,
                                              const QString &address);
    static QCurl::QCNetworkAccessManager::ShareHandleConfig sharing(bool cookies, bool ssl);
    static void verifyCookieSharing(QCurl::QCNetworkAccessManager &manager,
                                    ResolveTestServer &server,
                                    bool expected);
};

#endif // TST_QCCURLMULTIDOMAINS_H
