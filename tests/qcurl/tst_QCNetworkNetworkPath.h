#ifndef TST_QCNETWORKNETWORKPATH_H
#define TST_QCNETWORKNETWORKPATH_H

#include <QObject>

class TestQCNetworkNetworkPath : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkNetworkPath() = default;

private Q_SLOTS:
    void resolveOptionFailureDoesNotSend_data();
    void resolveOptionFailureDoesNotSend();
    void optionCombinationsMatchNative_data();
    void optionCombinationsMatchNative();
    void addressFamiliesMatchNative_data();
    void addressFamiliesMatchNative();
    void proxyFormsMatchNative_data();
    void proxyFormsMatchNative();
    void requiredPathOptionFailureDoesNotSend_data();
    void requiredPathOptionFailureDoesNotSend();
    void retryKeepsOriginalSnapshot_data();
    void retryKeepsOriginalSnapshot();
    void redirectMatchesEachHop_data();
    void redirectMatchesEachHop();
    void combinedTlsIdentityMatchesNative_data();
    void combinedTlsIdentityMatchesNative();
    void combinedWarmScopeMatchesNative_data();
    void combinedWarmScopeMatchesNative();
    void testDefaults();
    void testSettersAndGetters();
    void testInvalidInputs();
    void testConfigureCurlOptionsSmoke();
    void testSlistBuildFailureIsTerminal();
    void testHeaderAppendFailureIsTerminal();
    void testRequiredSetoptFailureIsTerminal();
    void testMinimumRuntimeGate();
    void testProtocolAllowlistCapabilityPolicy();
    void testCoreProtocolOptionsAreMandatory();
    void testCoreEntryRejectsNonHttpScheme();
    void testExplicitProtocolCannotExpandCore();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkNetworkPath)
};

#endif // TST_QCNETWORKNETWORKPATH_H
