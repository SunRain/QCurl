/**
 * @file
 * @brief 编译异步与阻塞请求共用的纯值策略，不采样运行环境或调用原生接口。
 */
#ifndef QCNETWORKREQUESTPOLICY_P_H
#define QCNETWORKREQUESTPOLICY_P_H

#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"

#include <QByteArray>
#include <QString>

#include <curl/curl.h>
#include <optional>

namespace QCurl {
class QCNetworkRequest;

namespace Internal {

/// 已解析的初始白名单；关闭重定向时不产生重定向覆盖。
struct RequestProtocolPolicy
{
    QByteArray initial;
    std::optional<QByteArray> redirect;
};

/// 仅保留开启重定向后需要应用的控制项，零上限仍是有效限制。
struct RequestRedirectPolicy
{
    bool follow = false;
    std::optional<long> maximum;
    std::optional<long> post;
    bool autoReferer = false;
};

/// 实际生效的认证凭据；raw Authorization 覆盖时不生成本记录。
struct RequestHttpCredentials
{
    QByteArray user;
    QByteArray password;
    long method = 0;
};

/// raw header 优先级处理后的配置意图及敏感头重定向授权。
struct RequestHeaderPolicy
{
    std::optional<QByteArray> referer;
    std::optional<QByteArray> acceptEncoding;
    std::optional<RequestHttpCredentials> credentials;
    bool unrestrictedAuth = false;
};

/// 纯诊断事实，不负责选择日志通道或发布时机。
struct RequestPolicyDiagnostics
{
    // 诊断事实由执行器在原有插点发布；mock 仅消费既有的头冲突提示。
    bool refererConflict  = false;
    bool encodingConflict = false;
    bool basicOverHttp    = false;
};

/// 已校验的显式代理；字符串由 adapter 的既有 backing 持有。
struct RequestProxyEndpoint
{
    QCNetworkProxyConfig::ProxyType requestedType = QCNetworkProxyConfig::ProxyType::Http;
    long type                                     = CURLPROXY_HTTP;
    QByteArray host;
    long port = 0;
    std::optional<QByteArray> user;
    std::optional<QByteArray> password;
    bool negotiateHttpAuth = false;
};

/// 两执行器共同支持的 endpoint TLS 子集，不包含高级 TLS 策略。
struct RequestEndpointTlsPolicy
{
    bool verifyPeer = true;
    bool verifyHost = true;
    // 空路径沿用 TLS backend 默认信任源，不产生 CAINFO 覆盖。
    QString caCertPath;
};

/// 已按原生 long 校验的毫秒值，空值表示沿用原生默认。
struct RequestTimeoutPolicy
{
    std::optional<long> connectMilliseconds;
    std::optional<long> totalMilliseconds;
};

/// 有效 range 与按 curl_off_t 无损校验的正数限速。
struct RequestTransferPolicy
{
    std::optional<QByteArray> range;
    std::optional<curl_off_t> downloadBytesPerSecond;
    std::optional<curl_off_t> uploadBytesPerSecond;
};

/// 区分隐式默认与调用方显式选择，能力判断留在原执行检查点。
struct HttpVersionIntent
{
    QCNetworkHttpVersion requested = QCNetworkHttpVersion::Http1_1;
    bool explicitVersion           = false;
};

/// adapter 在 HTTP 检查点采样的能力事实，不保留 probe 或环境引用。
struct HttpVersionFacts
{
    int runtimeVersion = 0;
    bool hasHttp3      = false;
    bool requireHttp3  = false;
};

/// HTTP 原生版本覆盖、拒绝或既有允许降级的单一决策。
struct HttpVersionDecision
{
    std::optional<long> nativeVersion;
    QString failure;
    bool downgraded = false;
};

/// 可绑定到原生插点的独立值结果，不持有 Request、body 或执行对象。
struct RequestPolicy
{
    QByteArray url;
    RequestProtocolPolicy protocols;
    RequestRedirectPolicy redirect;
    RequestHeaderPolicy headers;
    // 无 endpoint 明确表示直连；执行器须禁用环境代理。
    std::optional<RequestProxyEndpoint> proxy;
    RequestEndpointTlsPolicy endpointTls;
    RequestTimeoutPolicy timeout;
    RequestTransferPolicy transfer;
    HttpVersionIntent httpVersion;
};

/// 编译事务的完整结果；失败不暴露可应用的部分策略。
struct RequestPolicyResult
{
    // 只有校验完整通过才提供可应用的策略，失败结果不携带部分配置。
    std::optional<RequestPolicy> policy;
    QString failure;
    RequestPolicyDiagnostics diagnostics;
};

/// 按 URL/协议、代理、连接超时、总超时、其余生效数值的顺序返回首错。
[[nodiscard]] Q_DECL_HIDDEN RequestPolicyResult
compileRequestPolicy(const QCNetworkRequest &request);
/// 只解释调用方在原检查点采集的能力事实，不读取环境或保存 probe。
[[nodiscard]] Q_DECL_HIDDEN HttpVersionDecision
decideHttpVersionPolicy(const HttpVersionIntent &intent, const HttpVersionFacts &facts);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREQUESTPOLICY_P_H
