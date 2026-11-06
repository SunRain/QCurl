#include "QCCurlHandleManager.h"
#include "QCNetworkRequest.h"
#include "private/QCTlsConfiguration_p.h"

#include <QScopeGuard>
#include <QtTest>

using namespace QCurl;
using namespace QCurl::Internal;

namespace {

QCNetworkSslConfig endpointInput(const QByteArray &option, QCUnsupportedSecurityOptionPolicy policy)
{
    QCNetworkSslConfig config;
    config.setUnsupportedSecurityPolicy(policy);
    if (option == "CURLOPT_CAINFO") {
        config.setCaCertPath(QStringLiteral("ca-marker"));
    } else if (option == "CURLOPT_PINNEDPUBLICKEY") {
        config.setPinnedPublicKey(QStringLiteral("pin-marker"));
    } else if (option == "CURLOPT_SSLVERSION") {
        config.setMinTlsVersion(QCNetworkTlsVersion::Tls1_2);
    } else if (option == "CURLOPT_SSL_CIPHER_LIST") {
        config.setCipherList(QStringLiteral("cipher-marker"));
    } else if (option == "CURLOPT_TLS13_CIPHERS") {
        config.setTls13Ciphers(QStringLiteral("tls13-marker"));
    }
    config.setClientCertPath(QStringLiteral("cert-marker"));
    config.setClientKeyPath(QStringLiteral("key-marker"));
    config.setClientKeyPassword(QStringLiteral("password-marker"));
    return config;
}

ProxyTlsInput proxyInput(const QByteArray &option, QCUnsupportedSecurityOptionPolicy policy)
{
    QCNetworkProxyConfig::ProxyTlsConfig config;
    config.setUnsupportedSecurityPolicy(policy);
    if (option == "CURLOPT_PROXY_CAINFO") {
        config.setCaCertPath(QStringLiteral("proxy-ca-marker"));
    } else if (option == "CURLOPT_PROXY_SSLVERSION") {
        config.setMinTlsVersion(QCNetworkTlsVersion::Tls1_2);
    } else if (option == "CURLOPT_PROXY_SSL_CIPHER_LIST") {
        config.setCipherList(QStringLiteral("proxy-cipher-marker"));
    } else if (option == "CURLOPT_PROXY_TLS13_CIPHERS") {
        config.setTls13Ciphers(QStringLiteral("proxy-tls13-marker"));
    }
    return {QCNetworkProxyConfig::ProxyType::Https, CURLPROXY_HTTPS, config};
}

TlsConfigurationResult applyEndpoint(QCTlsConfiguration &configuration,
                                     CURL *easy,
                                     const QCNetworkSslConfig &config)
{
    QCNetworkRequest request(QUrl(QStringLiteral("https://example.test/")));
    request.setSslConfig(config);
    const auto compiled = compileRequestPolicy(request);
    return configuration.applyEndpoint(easy, compiled.policy->endpointTls, config);
}

void verifyFailure(const TlsConfigurationResult &result, const QByteArray &option, CURLcode code)
{
    QVERIFY(result.failure.has_value());
    QCOMPARE(result.failure->error, NetworkError::InvalidRequest);
    QCOMPARE(result.failure->nativeCode, code);
    QCOMPARE(result.failure->optionName, QString::fromLatin1(option));
    QVERIFY(result.failure->message.contains(QString::fromLatin1(option)));
    QVERIFY(!result.failure->message.contains(QStringLiteral("marker")));
}

} // namespace

class tst_QCTlsConfiguration final : public QObject
{
    Q_OBJECT

public:
    tst_QCTlsConfiguration() = default;

private Q_SLOTS:
    void optionPolicy_data();
    void optionPolicy();
    void unmappedMinimum_data();
    void unmappedMinimum();
    void warningsSurviveLaterFailure();
    void inapplicableProxy_data();
    void inapplicableProxy();

private:
    Q_DISABLE_COPY_MOVE(tst_QCTlsConfiguration)
};

void tst_QCTlsConfiguration::optionPolicy_data()
{
    QTest::addColumn<QByteArray>("option");
    QTest::addColumn<bool>("capabilityError");
    QTest::addColumn<bool>("warn");
    QTest::addColumn<bool>("required");
    for (const auto &option : {"CURLOPT_SSL_VERIFYPEER",
                               "CURLOPT_SSL_VERIFYHOST",
                               "CURLOPT_CAINFO",
                               "CURLOPT_SSLCERT",
                               "CURLOPT_SSLKEY",
                               "CURLOPT_KEYPASSWD",
                               "CURLOPT_PINNEDPUBLICKEY",
                               "CURLOPT_SSLVERSION",
                               "CURLOPT_SSL_CIPHER_LIST",
                               "CURLOPT_TLS13_CIPHERS",
                               "CURLOPT_PROXY_SSL_VERIFYPEER",
                               "CURLOPT_PROXY_SSL_VERIFYHOST",
                               "CURLOPT_PROXY_CAINFO",
                               "CURLOPT_PROXY_SSLVERSION",
                               "CURLOPT_PROXY_SSL_CIPHER_LIST",
                               "CURLOPT_PROXY_TLS13_CIPHERS"}) {
        const QByteArray name(option);
        const bool required = name == "CURLOPT_SSL_VERIFYPEER" || name == "CURLOPT_SSL_VERIFYHOST"
                              || name == "CURLOPT_CAINFO" || name == "CURLOPT_SSLCERT"
                              || name == "CURLOPT_SSLKEY" || name == "CURLOPT_KEYPASSWD";
        for (const bool capability : {true, false}) {
            for (const bool warn : {false, true}) {
                const QByteArray row = name + (capability ? "-capability" : "-argument")
                                       + (warn ? "-warn" : "-fail");
                QTest::newRow(row.constData()) << name << capability << warn << required;
            }
        }
    }
}

void tst_QCTlsConfiguration::optionPolicy()
{
    QFETCH(QByteArray, option);
    QFETCH(bool, capabilityError);
    QFETCH(bool, warn);
    QFETCH(bool, required);
    QCTlsConfiguration configuration;
    QCCurlHandleManager easy;
    QVERIFY(easy.isValid());
    const char *variable = capabilityError ? "QCURL_TEST_FORCE_CAPABILITY_ERROR"
                                           : "QCURL_TEST_FORCE_SETOPT_ERROR";
    qputenv(variable, option);
    const auto restore = qScopeGuard([variable]() { qunsetenv(variable); });
    const auto policy  = warn ? QCUnsupportedSecurityOptionPolicy::Warn
                              : QCUnsupportedSecurityOptionPolicy::Fail;
    const auto result  = option.startsWith("CURLOPT_PROXY_")
                             ? configuration.applyProxy(easy.handle(), proxyInput(option, policy))
                             : applyEndpoint(configuration,
                                             easy.handle(),
                                             endpointInput(option, policy));
    const bool shouldWarn = !required && warn && capabilityError;
    QCOMPARE(result.warnings.size(), shouldWarn ? 1 : 0);
    QCOMPARE(result.failure.has_value(), !shouldWarn);
    if (shouldWarn) {
        QVERIFY(result.warnings.first().contains(QString::fromLatin1(option)));
        QVERIFY(!result.warnings.first().contains(QStringLiteral("marker")));
    } else {
        verifyFailure(result,
                      option,
                      capabilityError ? CURLE_NOT_BUILT_IN : CURLE_BAD_FUNCTION_ARGUMENT);
    }
}

void tst_QCTlsConfiguration::unmappedMinimum_data()
{
    QTest::addColumn<bool>("proxy");
    QTest::addColumn<bool>("warn");
    QTest::addColumn<int>("version");
    for (const bool proxy : {false, true}) {
        for (const bool warn : {false, true}) {
            for (const int version : {int(QCNetworkTlsVersion::Default), 999}) {
                const QByteArray row = QByteArray(proxy ? "proxy" : "endpoint")
                                       + (warn ? "-warn-" : "-fail-") + QByteArray::number(version);
                QTest::newRow(row.constData()) << proxy << warn << version;
            }
        }
    }
}

void tst_QCTlsConfiguration::unmappedMinimum()
{
    QFETCH(bool, proxy);
    QFETCH(bool, warn);
    QFETCH(int, version);
    QCTlsConfiguration configuration;
    QCCurlHandleManager easy;
    QVERIFY(easy.isValid());
    const auto policy = warn ? QCUnsupportedSecurityOptionPolicy::Warn
                             : QCUnsupportedSecurityOptionPolicy::Fail;
    TlsConfigurationResult result;
    if (proxy) {
        auto input = proxyInput({}, policy);
        input.config->setMinTlsVersion(static_cast<QCNetworkTlsVersion>(version));
        result = configuration.applyProxy(easy.handle(), input);
    } else {
        QCNetworkSslConfig config;
        config.setMinTlsVersion(static_cast<QCNetworkTlsVersion>(version));
        config.setUnsupportedSecurityPolicy(policy);
        result = applyEndpoint(configuration, easy.handle(), config);
    }
    const QString message = QStringLiteral("不支持的 TLS 版本配置（%1）")
                                .arg(proxy ? QStringLiteral("proxy") : QStringLiteral("ssl"));
    QCOMPARE(result.warnings, warn ? QStringList{message} : QStringList{});
    QCOMPARE(result.failure.has_value(), !warn);
    if (!warn) {
        QCOMPARE(result.failure->nativeCode, CURLE_OK);
        QCOMPARE(result.failure->error, NetworkError::InvalidRequest);
        QCOMPARE(result.failure->message, message);
    }
}

void tst_QCTlsConfiguration::warningsSurviveLaterFailure()
{
    QCTlsConfiguration configuration;
    QCCurlHandleManager easy;
    QVERIFY(easy.isValid());
    auto config = endpointInput("CURLOPT_PINNEDPUBLICKEY", QCUnsupportedSecurityOptionPolicy::Warn);
    config.setMinTlsVersion(QCNetworkTlsVersion::Tls1_2);
    config.setCipherList(QStringLiteral("cipher-marker"));
    config.setTls13Ciphers(QStringLiteral("tls13-marker"));
    qputenv("QCURL_TEST_FORCE_CAPABILITY_ERROR",
            "CURLOPT_PINNEDPUBLICKEY,CURLOPT_SSLVERSION,CURLOPT_TLS13_CIPHERS");
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", "CURLOPT_SSL_CIPHER_LIST");
    const auto restore = qScopeGuard([]() {
        qunsetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
        qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR");
    });
    const auto result  = applyEndpoint(configuration, easy.handle(), config);
    QCOMPARE(result.warnings.size(), 2);
    QVERIFY(result.warnings[0].contains(QStringLiteral("CURLOPT_PINNEDPUBLICKEY")));
    QVERIFY(result.warnings[1].contains(QStringLiteral("CURLOPT_SSLVERSION")));
    verifyFailure(result, "CURLOPT_SSL_CIPHER_LIST", CURLE_BAD_FUNCTION_ARGUMENT);
    QVERIFY(!result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("marker")));
}

void tst_QCTlsConfiguration::inapplicableProxy_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("no-config") << 0;
    QTest::newRow("http-proxy") << 1;
    QTest::newRow("effective-http") << 2;
    QTest::newRow("no-proxy") << 3;
}

void tst_QCTlsConfiguration::inapplicableProxy()
{
    QFETCH(int, scenario);
    QCTlsConfiguration configuration;
    QCCurlHandleManager easy;
    auto input = proxyInput("CURLOPT_PROXY_CAINFO", QCUnsupportedSecurityOptionPolicy::Fail);
    if (scenario == 0) {
        input.config.reset();
    } else if (scenario == 2) {
        input.effectiveCurlProxyType = CURLPROXY_HTTP;
    } else {
        input.requestedType = scenario == 1 ? QCNetworkProxyConfig::ProxyType::Http
                                            : QCNetworkProxyConfig::ProxyType::None;
    }
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", "all");
    const auto restore = qScopeGuard([]() { qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR"); });
    const auto result  = configuration.applyProxy(easy.handle(), input);
    QVERIFY(!result.failure.has_value());
    QVERIFY(result.warnings.isEmpty());
}

QTEST_GUILESS_MAIN(tst_QCTlsConfiguration)
#include "tst_QCTlsConfiguration.moc"
