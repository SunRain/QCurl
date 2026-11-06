#ifndef TST_QCNETWORKPROXY_H
#define TST_QCNETWORKPROXY_H

#include "QCNetworkAccessManager.h"

#include <QObject>

class TestQCNetworkProxy final : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkProxy() = default;

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    void testProxyConfigBasic();
    void testProxyTypeValidation();
    void testProxyAuthentication();
    void testProxyWithSsl();
    void testProxyAppliedToCurlHandle();
    void testProxyEnvironmentDisabledByDefault();
    void explicitProxyPolicy_data();
    void explicitProxyPolicy();
    void httpsProxyTransport_data();
    void httpsProxyTransport();
    void testSslConfigValueSemantics();
    void testTimeoutConfigOptionalLifecycle();
    void testProxyTlsConfigBraceInitSetter();
    void testProxyTlsConfigOptionalLifecycle();
    void testProxyTlsUnsupportedFail();
    void testProxyTlsUnsupportedWarn();
    void testProxyConnectionFailed();
    void endpointTlsVerification_data();
    void endpointTlsVerification();
    void endpointTlsNegotiation_data();
    void endpointTlsNegotiation();
    void endpointClientCredentials_data();
    void endpointClientCredentials();
    void httpsProxyTlsIsolation_data();
    void httpsProxyTlsIsolation();
    void httpsProxyTlsNegotiation_data();
    void httpsProxyTlsNegotiation();
    void tlsWarningsPrecedeFatal_data();
    void tlsWarningsPrecedeFatal();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkProxy)
    QCurl::QCNetworkAccessManager *m_manager = nullptr;
    bool waitForReply(QCurl::QCNetworkReply *reply, int timeout = 5000);
};

#endif // TST_QCNETWORKPROXY_H
