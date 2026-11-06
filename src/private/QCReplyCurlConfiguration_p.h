/**
 * @file
 * @brief 封装请求计划、非 TLS 选项规则与原生借用存储。
 */

#ifndef QCREPLYCURLCONFIGURATION_P_H
#define QCREPLYCURLCONFIGURATION_P_H

#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkRequestPolicy_p.h"
#include "private/QCRequestPipeline_p.h"
#include "private/QCTlsConfiguration_p.h"

#include <QList>
#include <QString>

#include <optional>

namespace QCurl {

class QCCurlHandleManager;

namespace Internal {

/// 普通日志不进入公开 capabilityWarnings；同段诊断仍按产生顺序发布。
enum class ReplyCurlWarningKind { LogOnly, Capability };

struct ReplyCurlWarning
{
    ReplyCurlWarningKind kind = ReplyCurlWarningKind::Capability;
    QString message;
};

/// 一次配置段的有序诊断与首次 fatal，不保存执行对象或原生句柄。
struct ReplyCurlConfigurationResult
{
    QList<ReplyCurlWarning> warnings;
    std::optional<QString> failure;
};

/**
 * @brief 拥有请求计划、共同策略结果与原生配置行为。
 *
 * 计划构造后只读；本对象不可复制或移动。shared transfer holder 让计划、字节和列表
 * 保活至 easy cleanup 之后，配置段只借用当次 easy，不保存其他 module 或执行对象。
 */
class Q_DECL_HIDDEN QCReplyCurlConfiguration final
{
public:
    explicit QCReplyCurlConfiguration(CurlPlan plan);
    Q_DISABLE_COPY_MOVE(QCReplyCurlConfiguration)

    /// 返回 capture/digest 和执行所用的同一份只读计划。
    [[nodiscard]] const CurlPlan &plan() const noexcept { return m_plan; }
    [[nodiscard]] ReplyCurlConfigurationResult validationResult() const;
    /// 只在 validationResult 成功后借用，存储与 transfer holder 同寿命。
    [[nodiscard]] const RequestPolicy &requestPolicy() const noexcept
    {
        Q_ASSERT(m_policy.policy);
        return *m_policy.policy;
    }

    /// URL 与 FOLLOWLOCATION 分段，留出原 PRIVATE 注册插点。
    [[nodiscard]] ReplyCurlConfigurationResult applyUrl(CURL *easy) const;
    [[nodiscard]] ReplyCurlConfigurationResult applyNetworkPath(CURL *easy);
    /// method 与 body 分段，设备 READ 注册仍位于两者之间。
    [[nodiscard]] ReplyCurlConfigurationResult applyMethod(CURL *easy) const;
    [[nodiscard]] ReplyCurlConfigurationResult applyTransferBody(CURL *easy,
                                                                 qint64 bodySourceSize) const;
    /// header list 仍由现有 easy 守卫拥有，覆盖规则已由纯值策略决定。
    [[nodiscard]] ReplyCurlConfigurationResult applyHeadersAndLimits(QCCurlHandleManager &handle);
    /// 完成认证、range、proxy endpoint/credentials，交回原 proxy TLS 插点的窄事实。
    [[nodiscard]] ReplyCurlConfigurationResult applyAuthenticationAndProxy(
        CURL *easy, std::optional<ProxyTlsInput> &proxyTls);
    [[nodiscard]] ReplyCurlConfigurationResult applyHttpVersion(CURL *easy) const;
    [[nodiscard]] ReplyCurlConfigurationResult applyTimeouts(CURL *easy) const;

    /// 仅在尚无 Cookie 文件配置时继承；执行前可再次借用 manager 的当前值。
    void inheritCookieFile(int mode, const QString &path);
    /// 供 share adapter 判断文件与共享 store 是否同时启用，不暴露路径或模式字段。
    [[nodiscard]] bool usesCookieFile() const noexcept
    {
        return m_cookieMode != 0 && !m_cookiePathBytes.isEmpty();
    }
    [[nodiscard]] ReplyCurlConfigurationResult applyCookieFile(CURL *easy) const;
    [[nodiscard]] ReplyCurlConfigurationResult applyHttpStateFiles(CURL *easy,
                                                                   const QString &hstsPath,
                                                                   const QString &altSvcPath);
    /// 在原成功终态位置执行 FLUSH；失败只产生既有 capability warning。
    [[nodiscard]] ReplyCurlConfigurationResult flushCookieJar(CURL *easy) const;
    /// mock 重复发布既有 encoding 冲突告警，和真实配置使用同一判断与文本。
    [[nodiscard]] ReplyCurlConfigurationResult mockConflictWarnings() const;
    /// callback adapter 复用 required 的首次失败语义，不自行解释选项策略。
    [[nodiscard]] static ReplyCurlConfigurationResult classifyRequiredOption(CURLcode code,
                                                                             const char *optionName);

private:
    [[nodiscard]] static bool required(CURLcode code,
                                       const char *optionName,
                                       ReplyCurlConfigurationResult &result);
    [[nodiscard]] static bool optional(CURLcode code,
                                       const char *optionName,
                                       ReplyCurlConfigurationResult &result);
    static void warn(ReplyCurlConfigurationResult &result, const QString &message);

    template<typename T>
    [[nodiscard]] static bool requiredOption(CURL *easy,
                                             CurlOptions::Option option,
                                             T value,
                                             ReplyCurlConfigurationResult &result)
    {
        return required(CurlOptions::setWithTestHook(easy, option, value), option.name, result);
    }

    template<typename T>
    static bool optionalOption(CURL *easy,
                               CurlOptions::Option option,
                               T value,
                               ReplyCurlConfigurationResult &result)
    {
        return easy
               && optional(CurlOptions::setWithTestHook(easy, option, value), option.name, result);
    }

    [[nodiscard]] bool configureRedirects(CURL *easy, ReplyCurlConfigurationResult &result) const;
    void configureLocalNetworkBinding(CURL *easy, ReplyCurlConfigurationResult &result);
    void configureDnsTransport(CURL *easy, ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureSlist(CURL *easy,
                                      const QStringList &entries,
                                      CurlOptions::CurlSlistOwner &storage,
                                      CurlOptions::Option option,
                                      ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureProtocolList(CURL *easy,
                                             const QByteArray &protocols,
                                             CurlOptions::Option option,
                                             const QString &unsupportedMessage,
                                             ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureProtocolRestrictions(CURL *easy,
                                                     ReplyCurlConfigurationResult &result);
    void configureExpectContinueTimeout(CURL *easy, ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool appendRequestHeaders(QCCurlHandleManager &handle,
                                            ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureReferer(CURL *easy, ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureAcceptEncoding(CURL *easy,
                                               ReplyCurlConfigurationResult &result) const;
    void appendAcceptEncodingConflictWarning(ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureTransferLimits(CURL *easy,
                                               ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureHttpCredentials(CURL *easy,
                                                const RequestHttpCredentials &credentials,
                                                ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureHttpAuthentication(CURL *easy,
                                                   ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureSensitiveRedirectPolicy(CURL *easy,
                                                        ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureRange(CURL *easy, ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureProxyEndpoint(CURL *easy,
                                              const RequestProxyEndpoint &proxy,
                                              ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureProxyCredentials(CURL *easy,
                                                 const RequestProxyEndpoint &proxy,
                                                 ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureProxy(CURL *easy,
                                      std::optional<ProxyTlsInput> &proxyTls,
                                      ReplyCurlConfigurationResult &result) const;

    const CurlPlan m_plan;
    const RequestPolicyResult m_policy;
    int m_cookieMode = 0;
    QByteArray m_cookiePathBytes;
    QByteArray m_hstsCachePathBytes;
    QByteArray m_altSvcCachePathBytes;
    QByteArray m_interfaceBytes;
    QByteArray m_dnsServersBytes;
    QByteArray m_dohUrlBytes;
    CurlOptions::CurlSlistOwner m_resolveSlist;
    CurlOptions::CurlSlistOwner m_connectToSlist;
};

} // namespace Internal
} // namespace QCurl

#endif // QCREPLYCURLCONFIGURATION_P_H
