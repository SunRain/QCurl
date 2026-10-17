#ifndef TST_QCNETWORKRESOLVEOVERRIDE_H
#define TST_QCNETWORKRESOLVEOVERRIDE_H

#include <QObject>

class TestQCNetworkResolveOverride final : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkResolveOverride() = default;

private Q_SLOTS:
    void updatesAreAtomic_data();
    void updatesAreAtomic();
    void encodedHostsReachEndpoint_data();
    void encodedHostsReachEndpoint();
    void requestsAreIsolated_data();
    void requestsAreIsolated();
    void effectiveRulesReuseOnlyCompatibleDomains_data();
    void effectiveRulesReuseOnlyCompatibleDomains();
    void nativeSharedPreservesWarmState_data();
    void nativeSharedPreservesWarmState();
    void cookieSharingStaysPerManagerAcrossDomains_data();
    void cookieSharingStaysPerManagerAcrossDomains();
    void tlsVerifiesOriginalHost_data();
    void tlsVerifiesOriginalHost();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkResolveOverride)
};

#endif // TST_QCNETWORKRESOLVEOVERRIDE_H
