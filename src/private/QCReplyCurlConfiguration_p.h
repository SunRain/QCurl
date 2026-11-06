/**
 * @file
 * @brief 封装请求计划、非 TLS 选项规则与原生借用存储。
 */

#ifndef QCREPLYCURLCONFIGURATION_P_H
#define QCREPLYCURLCONFIGURATION_P_H

#include "private/QCCurlOptionAdapter_p.h"
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

struct ReplyCurlHeaderFacts
{
    bool hasExplicitAuthorization  = false;
    bool hasExplicitReferer        = false;
    bool hasExplicitAcceptEncoding = false;
    bool hasSensitive              = false;
};

/**
 * @brief 拥有请求计划与非 TLS 配置行为。
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

    /// URL 与 FOLLOWLOCATION 分段，留出原 PRIVATE 注册插点。
    [[nodiscard]] ReplyCurlConfigurationResult applyUrl(CURL *easy) const;
    [[nodiscard]] ReplyCurlConfigurationResult applyNetworkPath(CURL *easy);
    /// method 与 body 分段，设备 READ 注册仍位于两者之间。
    [[nodiscard]] ReplyCurlConfigurationResult applyMethod(CURL *easy) const;
    [[nodiscard]] ReplyCurlConfigurationResult applyTransferBody(CURL *easy,
                                                                 qint64 bodySourceSize) const;
    /// header list 仍由现有 easy 守卫拥有；输出仅为后续认证段需要的头事实。
    [[nodiscard]] ReplyCurlConfigurationResult applyHeadersAndLimits(QCCurlHandleManager &handle,
                                                                     ReplyCurlHeaderFacts &facts);
    /// 完成认证、range、proxy endpoint/credentials，交回原 proxy TLS 插点的窄事实。
    [[nodiscard]] ReplyCurlConfigurationResult applyAuthenticationAndProxy(
        CURL *easy, const ReplyCurlHeaderFacts &facts, std::optional<ProxyTlsInput> &proxyTls);
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

    void configureRedirects(CURL *easy, ReplyCurlConfigurationResult &result) const;
    void configureLocalNetworkBinding(CURL *easy, ReplyCurlConfigurationResult &result);
    void configureDnsTransport(CURL *easy, ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureSlist(CURL *easy,
                                      const QStringList &entries,
                                      CurlOptions::CurlSlistOwner &storage,
                                      CurlOptions::Option option,
                                      ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureProtocolList(CURL *easy,
                                             const QStringList &protocols,
                                             QByteArray &storage,
                                             CurlOptions::Option option,
                                             const QString &unsupportedMessage,
                                             ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureProtocolRestrictions(CURL *easy,
                                                     ReplyCurlConfigurationResult &result);
    void configureExpectContinueTimeout(CURL *easy, ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool appendRequestHeaders(QCCurlHandleManager &handle,
                                            ReplyCurlHeaderFacts &facts,
                                            ReplyCurlConfigurationResult &result) const;
    void configureReferer(CURL *easy,
                          const ReplyCurlHeaderFacts &facts,
                          ReplyCurlConfigurationResult &result);
    void configureAcceptEncoding(CURL *easy,
                                 const ReplyCurlHeaderFacts &facts,
                                 ReplyCurlConfigurationResult &result);
    void appendAcceptEncodingConflictWarning(const ReplyCurlHeaderFacts &facts,
                                             ReplyCurlConfigurationResult &result) const;
    void configureTransferLimits(CURL *easy, ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureHttpCredentials(CURL *easy,
                                                const QCNetworkHttpAuthConfig &config,
                                                ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureHttpAuthentication(CURL *easy,
                                                   bool hasExplicitAuthorization,
                                                   bool &hasSensitiveHeader,
                                                   bool &wantsUnrestrictedHeaders,
                                                   ReplyCurlConfigurationResult &result);
    void configureSensitiveRedirectPolicy(CURL *easy,
                                          bool hasSensitiveHeader,
                                          bool wantsUnrestrictedHeaders,
                                          ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureRange(CURL *easy, ReplyCurlConfigurationResult &result) const;
    [[nodiscard]] bool configureProxyEndpoint(CURL *easy,
                                              const QCNetworkProxyConfig &config,
                                              long &curlProxyType,
                                              ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureProxyCredentials(CURL *easy,
                                                 const QCNetworkProxyConfig &config,
                                                 ReplyCurlConfigurationResult &result);
    [[nodiscard]] bool configureProxy(CURL *easy,
                                      std::optional<ProxyTlsInput> &proxyTls,
                                      ReplyCurlConfigurationResult &result);

    const CurlPlan m_plan;
    int m_cookieMode = 0;
    QByteArray m_cookiePathBytes;
    QByteArray m_hstsCachePathBytes;
    QByteArray m_altSvcCachePathBytes;
    QByteArray m_proxyHostBytes;
    QByteArray m_proxyUserBytes;
    QByteArray m_proxyPasswordBytes;
    QByteArray m_httpAuthUserBytes;
    QByteArray m_httpAuthPasswordBytes;
    QByteArray m_refererBytes;
    QByteArray m_acceptEncodingBytes;
    QByteArray m_interfaceBytes;
    QByteArray m_dnsServersBytes;
    QByteArray m_dohUrlBytes;
    CurlOptions::CurlSlistOwner m_resolveSlist;
    CurlOptions::CurlSlistOwner m_connectToSlist;
    QByteArray m_allowedProtocolsBytes;
    QByteArray m_allowedRedirectProtocolsBytes;
};

} // namespace Internal
} // namespace QCurl

#endif // QCREPLYCURLCONFIGURATION_P_H
