/**
 * @file
 * @brief 声明网络请求配置对象。
 */

#ifndef QCNETWORKREQUEST_H
#define QCNETWORKREQUEST_H

#include "QCGlobal.h"

#include <QByteArray>
#include <QList>
#include <QSharedDataPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <optional>

// 前向声明新配置类
class QDebug;
class QIODevice;
namespace QCurl {
class QCNetworkRequestPrivate;
class QCNetworkSslConfig;
class QCNetworkProxyConfig;
class QCNetworkTimeoutConfig;
class QCNetworkRetryPolicy;
enum class QCNetworkHttpVersion;
enum class QCNetworkRequestPriority;
enum class QCNetworkCachePolicy;

/**
 * @brief POST 请求在重定向(301/302/303)下的处理策略（不暴露 libcurl 宏）
 *
 * 说明：
 * - 仅在 followLocation=true 时生效（映射到 CURLOPT_POSTREDIR）。
 * - 默认使用 libcurl 的默认行为（不显式设置 CURLOPT_POSTREDIR）。
 */
enum class QCNetworkPostRedirectPolicy {
    Default,     ///< 使用 libcurl 默认行为（不设置 CURLOPT_POSTREDIR）
    KeepPost301, ///< 301 时保持 POST
    KeepPost302, ///< 302 时保持 POST
    KeepPost303, ///< 303 时保持 POST
    KeepPostAll  ///< 301/302/303 均保持 POST（等价于 libcurl 的 ALL）
};

/**
 * @brief HTTP 认证方式（对外抽象，不暴露 libcurl 宏）
 */
enum class QCNetworkHttpAuthMethod {
    Basic,  ///< Basic（可预发送；仅建议在 HTTPS 下使用）
    Any,    ///< 允许任意方法（可能触发挑战/协商，存在额外往返）
    AnySafe ///< 允许任意“非 Basic”方法（可能触发挑战/协商，存在额外往返）
};

/**
 * @brief IP 族选择策略（M4）
 *
 * 映射到 libcurl 的 CURLOPT_IPRESOLVE。
 */
enum class QCNetworkIpResolve {
    Any,  ///< 不指定（libcurl 自行选择）
    Ipv4, ///< 优先 IPv4
    Ipv6  ///< 优先 IPv6
};

/**
 * @brief HTTP 认证配置（请求级）
 *
 * 说明：
 * - 认证凭据由 libcurl 负责生成 `Authorization` 头（除非用户显式设置了 `Authorization` header）
 * - Basic 仅建议在 HTTPS 下使用；如在 HTTP 下使用，可选择仅告警不阻断
 * - `allowUnrestrictedAuth` 仅在 followLocation=true 时有意义（跨 host/port 重定向携带凭据）
 */
struct QCURL_EXPORT QCNetworkHttpAuthConfig
{
    QString userName; ///< 用户名
    QString password; ///< 密码
    QCNetworkHttpAuthMethod method = QCNetworkHttpAuthMethod::Basic;
    bool allowUnrestrictedAuth     = false; ///< 映射到 CURLOPT_UNRESTRICTED_AUTH
    bool warnIfBasicOverHttp       = true;  ///< Basic + http:// 时输出 Warning（不阻断）
};

/**
 * @brief HTTP 请求配置类
 *
 * 封装单个 HTTP 请求的 URL、header、超时、认证、重试和传输相关配置。
 * 该类型是值语义配置对象，可在发送前按方法链累积修改。
 */
class QCURL_EXPORT QCNetworkRequest
{
public:
    QCNetworkRequest();
    QCNetworkRequest(const QUrl &url);
    QCNetworkRequest(const QCNetworkRequest &other);
    virtual ~QCNetworkRequest();

    QCNetworkRequest &operator=(const QCNetworkRequest &other);
    bool operator==(const QCNetworkRequest &other) const;
    bool operator!=(const QCNetworkRequest &other) const;

    // ========== 基础配置 ==========

    QUrl url() const;

    /**
     * @brief 设置请求 URL
     * @param url 请求 URL
     * @return 返回 *this 以支持方法链
     *
     * @note 用于在保留其他请求配置（headers/timeout/proxy 等）的前提下更新 URL。
     */
    QCNetworkRequest &setUrl(const QUrl &url);

    /**
     * @brief 设置是否跟随 HTTP 重定向
     * @param followLocation true 为跟随重定向（默认），false 为不跟随
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setFollowLocation(bool followLocation = true);
    bool followLocation() const;

    // ========== 重定向策略（M1） ==========

    /**
     * @brief 设置最大重定向次数（仅在 followLocation=true 时生效）
     * @param maxRedirects 最大重定向次数（>=0）
     */
    QCNetworkRequest &setMaxRedirects(int maxRedirects);

    /**
     * @brief 获取最大重定向次数
     * @return std::nullopt 表示未显式设置（使用 libcurl 默认行为）
     */
    [[nodiscard]] std::optional<int> maxRedirects() const;

    /**
     * @brief 设置 POST 在 301/302/303 下的重定向策略（仅在 followLocation=true 时生效）
     */
    QCNetworkRequest &setPostRedirectPolicy(QCNetworkPostRedirectPolicy policy);
    [[nodiscard]] QCNetworkPostRedirectPolicy postRedirectPolicy() const;

    /**
     * @brief 启用自动 Referer（仅在 followLocation=true 时生效）
     */
    QCNetworkRequest &setAutoRefererEnabled(bool enabled = true);
    [[nodiscard]] bool autoRefererEnabled() const;

    /**
     * @brief 设置 Referer（若用户显式 setRawHeader("Referer", ...) 则以用户 header 为准）
     */
    QCNetworkRequest &setReferer(const QString &referer);
    [[nodiscard]] QString referer() const;

    /**
     * @brief 跟随重定向时允许跨站发送敏感头（Authorization/Cookie 等，强风险）
     *
     * 默认关闭（安全）：跨站定义为 scheme/host/port 任一变化。
     *
     * 注意：该开关映射到 CURLOPT_UNRESTRICTED_AUTH（对 HTTP 有效），会影响 libcurl 对
     * Authentication/Cookie 等敏感头在跨站重定向时的默认保护。
     */
    QCNetworkRequest &setAllowUnrestrictedSensitiveHeadersOnRedirect(bool enabled = true);
    [[nodiscard]] bool allowUnrestrictedSensitiveHeadersOnRedirect() const;

    /**
     * @brief 设置自定义 HTTP Header
     * @param headerName Header 名称（如 "User-Agent"）
     * @param headerValue Header 值
     * @note 仅相同 `QByteArray` key 会被覆盖；不会做 header 名大小写归一化，因此大小写不同的 key 会并存
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setRawHeader(const QByteArray &headerName, const QByteArray &headerValue);
    QList<QByteArray> rawHeaderList() const;
    QByteArray rawHeader(const QByteArray &headerName) const;

    /**
     * @brief 设置 HTTP Range 请求（部分下载）
     * @param start 起始字节位置
     * @param end 结束字节位置
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setRange(int start, int end);
    int rangeStart() const;
    int rangeEnd() const;

    // ========== 高级配置 ==========

    /**
     * @brief 设置 SSL/TLS 配置
     * @param config SSL 配置对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setSslConfig(const QCNetworkSslConfig &config);
    [[nodiscard]] QCNetworkSslConfig sslConfig() const;

    /**
     * @brief 设置代理配置
     * @param config 代理配置对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setProxyConfig(const QCNetworkProxyConfig &config);

    /**
     * @brief 获取代理配置
     * @return std::nullopt 表示未显式设置（使用 libcurl 默认行为）
     */
    [[nodiscard]] std::optional<QCNetworkProxyConfig> proxyConfig() const;

    /**
     * @brief 设置超时配置
     * @param config 超时配置对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setTimeoutConfig(const QCNetworkTimeoutConfig &config);
    [[nodiscard]] QCNetworkTimeoutConfig timeoutConfig() const;

    /**
     * @brief 设置 HTTP 协议版本
     * @param version HTTP 版本枚举（Http1_0/Http1_1/Http2/Http3）
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setHttpVersion(QCNetworkHttpVersion version);
    [[nodiscard]] QCNetworkHttpVersion httpVersion() const;
    [[nodiscard]] bool isHttpVersionExplicit() const noexcept;

    /**
     * @brief 设置请求重试策略
     * @param policy 重试策略对象
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setRetryPolicy(const QCNetworkRetryPolicy &policy);

    /**
     * @brief 获取当前的重试策略
     * @return 重试策略对象
     */
    [[nodiscard]] QCNetworkRetryPolicy retryPolicy() const;
    [[nodiscard]] bool isRetryPolicyExplicit() const noexcept;

    // ========== HTTP 认证 ==========

    /**
     * @brief 设置请求级 HTTP 认证（用户名/密码 + 认证策略）
     * @note 若请求中存在显式 `Authorization` header（大小写不敏感），则以该 header
     * 为准，自动认证会被忽略
     */
    QCNetworkRequest &setHttpAuth(const QCNetworkHttpAuthConfig &config);

    /**
     * @brief 获取请求级 HTTP 认证配置
     * @return std::nullopt 表示未设置
     */
    [[nodiscard]] std::optional<QCNetworkHttpAuthConfig> httpAuth() const;

    /**
     * @brief 清除请求级 HTTP 认证配置
     */
    QCNetworkRequest &clearHttpAuth();

    // ========== 便捷方法 ==========

    /**
     * @brief 设置总超时时间（便捷方法）
     * @param timeout 超时时长
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setTimeout(std::chrono::milliseconds timeout);

    /**
     * @brief 设置连接超时时间（便捷方法）
     * @param timeout 连接超时时长
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setConnectTimeout(std::chrono::milliseconds timeout);

    // ========== 自动解压（M1） ==========

    /**
     * @brief 启用/关闭自动解压（默认关闭：避免 silent behavior change）
     *
     * 说明：
     * - 启用后 QCurl 会通过 CURLOPT_ACCEPT_ENCODING 让 libcurl 托管 Accept-Encoding 与解压。
     * - 若用户手写 Accept-Encoding header，则默认不自动解压（避免重复/冲突），并给出可诊断
     * warning。
     */
    QCNetworkRequest &setAutoDecompressionEnabled(bool enabled = true);
    [[nodiscard]] bool autoDecompressionEnabled() const;

    /**
     * @brief 设置可接受的编码列表（由 QCurl 托管 Accept-Encoding）
     *
     * - encodings 为空：视为禁用（不设置 CURLOPT_ACCEPT_ENCODING）
     * - encodings 非空：隐式启用自动解压，并使用该列表（例如 {"gzip","br"}）
     */
    QCNetworkRequest &setAcceptedEncodings(const QStringList &encodings);
    [[nodiscard]] QStringList acceptedEncodings() const;

    // ========== 传输限速（M1） ==========

    /**
     * @brief 设置最大下载速度（bytes/sec）
     *
     * - bytesPerSec > 0：设置上限
     * - bytesPerSec == 0：禁用限速（不设置对应 libcurl 选项）
     * - bytesPerSec < 0：视为无效输入，禁用并给出 warning
     */
    QCNetworkRequest &setMaxDownloadBytesPerSec(qint64 bytesPerSec);

    /**
     * @brief 获取最大下载速度上限
     * @return std::nullopt 表示未设置/已禁用
     */
    [[nodiscard]] std::optional<qint64> maxDownloadBytesPerSec() const;

    /**
     * @brief 设置最大上传速度（bytes/sec）
     *
     * 语义同 setMaxDownloadBytesPerSec。
     */
    QCNetworkRequest &setMaxUploadBytesPerSec(qint64 bytesPerSec);

    /**
     * @brief 获取最大上传速度上限
     * @return std::nullopt 表示未设置/已禁用
     */
    [[nodiscard]] std::optional<qint64> maxUploadBytesPerSec() const;

    // ========== 下载 backpressure（P2） ==========

    /**
     * @brief 设置下载 backpressure 上限（bytes）
     *
     * 仅在 ExecutionMode::Async 下生效。
     *
     * - bytes <= 0：禁用（默认）
     * - bytes > 0：启用 backpressure（soft limit，高水位线）
     *   - 缓冲达到/超过高水位线后自动 pause 接收；消费到低水位线后自动 resume
     *   - 注意：该值不是 hard cap。由于 libcurl write callback 无法部分消费，bytesAvailable()
     *     允许出现“有界超限”（通常最多一个 write callback chunk）
     * - 建议 limitBytes 不要过小（例如 >= 16KiB / CURL_MAX_WRITE_SIZE），否则会频繁 pause/resume，
     *   影响吞吐并增加调度开销
     */
    QCNetworkRequest &setBackpressureLimitBytes(qint64 bytes);

    /**
     * @brief 获取 backpressure 上限（bytes）
     *
     * @return 0 表示未启用（默认）
     */
    [[nodiscard]] qint64 backpressureLimitBytes() const noexcept;

    /**
     * @brief 设置 backpressure 低水位线（bytes）
     *
     * 仅在 limitBytes > 0 时有意义：
     * - bytes <= 0：使用默认值（limit/2）
     * - 0 < bytes < limit：自定义低水位线
     */
    QCNetworkRequest &setBackpressureResumeBytes(qint64 bytes);

    /**
     * @brief 获取 backpressure 低水位线（bytes）
     *
     * @return 0 表示使用默认值（limit/2）
     */
    [[nodiscard]] qint64 backpressureResumeBytes() const noexcept;

    // ========== 流式上传（M2） ==========

    /**
     * @brief 设置流式上传的请求体来源（QIODevice）
     *
     * 约束（M2 起生效）：
     * - 仅支持 PUT/POST 的流式 body，其它方法延后
     * - 所有权：device 由调用方管理，QCurl 不会 close/delete
     * - 生命周期：device 必须在请求完成前保持有效且可读；中途销毁/不可读将以可诊断错误结束
     *
     * @param device 源设备（可为 nullptr，表示清除上传源）
     * @param sizeBytes 可选的请求体长度（bytes）。若为空则由实现决定是否使用 chunked/unknown size
     */
    QCNetworkRequest &setUploadDevice(QIODevice *device,
                                      std::optional<qint64> sizeBytes = std::nullopt);
    [[nodiscard]] QIODevice *uploadDevice() const;

    /**
     * @brief 设置流式上传的请求体来源（本地文件路径）
     *
     * 说明：由 QCurl 在请求执行时打开文件并读取。
     */
    QCNetworkRequest &setUploadFile(const QString &filePath,
                                    std::optional<qint64> sizeBytes = std::nullopt);
    [[nodiscard]] std::optional<QString> uploadFilePath() const;

    /**
     * @brief 获取上传请求体长度（bytes）
     *
     * 对于 uploadDevice/uploadFile 均适用。未设置时返回 std::nullopt。
     */
    [[nodiscard]] std::optional<qint64> uploadBodySizeBytes() const;

    /**
     * @brief 允许 POST 在 sizeBytes 未知时使用 chunked（HTTP/1.1）
     *
     * 默认关闭（保持 unknown size fast-fail，避免 silent behavior change）。
     *
     * 约束：
     * - 仅对 POST + uploadDevice/uploadFile 且 sizeBytes 未指定且无法推导时生效
     * - 仅支持 HTTP/1.1（Http1_1）；其它版本将以可诊断错误失败
     * - PUT 仍要求 sizeBytes（不启用 chunked）
     */
    QCNetworkRequest &setAllowChunkedUploadForPost(bool enabled = true);
    [[nodiscard]] bool allowChunkedUploadForPost() const;

    /**
     * @brief 清除流式上传源配置
     */
    QCNetworkRequest &clearUploadBody();

    // ========== Expect: 100-continue（P1） ==========

    /**
     * @brief 设置 Expect: 100-continue 等待超时（仅 PUT/POST 且有 body 时生效）
     *
     * 说明：
     * - 默认未设置（不改变 libcurl 默认行为）。
     * - 仅在 PUT/POST 且存在 body（`QByteArray` 或 `uploadDevice/uploadFile`）时允许启用该配置。
     *
     * @param timeout 等待超时（timeout>=0 有效；timeout<0 视为无效输入并禁用）
     */
    QCNetworkRequest &setExpect100ContinueTimeout(std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<std::chrono::milliseconds> expect100ContinueTimeout() const;

    // ========== 网络路径与 DNS 控制（M4） ==========

    /**
     * @brief 设置 IP 族选择策略（默认不设置，避免 silent behavior change）
     */
    QCNetworkRequest &setIpResolve(QCNetworkIpResolve resolve);
    [[nodiscard]] std::optional<QCNetworkIpResolve> ipResolve() const;

    /**
     * @brief 设置 Happy Eyeballs 超时（毫秒，默认不设置）
     *
     * 映射到 CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS。
     */
    QCNetworkRequest &setHappyEyeballsTimeout(std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<std::chrono::milliseconds> happyEyeballsTimeout() const;

    /**
     * @brief 设置出站绑定网卡/地址（默认不设置）
     *
     * 映射到 CURLOPT_INTERFACE。
     */
    QCNetworkRequest &setNetworkInterface(const QString &interfaceName);
    [[nodiscard]] std::optional<QString> networkInterface() const;

    /**
     * @brief 设置本地端口与端口范围（默认不设置）
     *
     * 映射到 CURLOPT_LOCALPORT / CURLOPT_LOCALPORTRANGE。
     *
     * @param port 本地端口（1-65535）
     * @param range 范围（>= 0）
     */
    QCNetworkRequest &setLocalPortRange(int port, int range = 0);
    [[nodiscard]] std::optional<int> localPort() const;
    [[nodiscard]] std::optional<int> localPortRange() const;

    /**
     * @brief DNS 覆写（host:port:addr[,addr...]），默认不设置
     *
     * 映射到 CURLOPT_RESOLVE。
     */
    QCNetworkRequest &setResolveOverride(const QStringList &entries);
    [[nodiscard]] std::optional<QStringList> resolveOverride() const;

    /**
     * @brief 路由覆写（host:port:connect-to-host:connect-to-port），默认不设置
     *
     * 映射到 CURLOPT_CONNECT_TO。
     */
    QCNetworkRequest &setConnectTo(const QStringList &entries);
    [[nodiscard]] std::optional<QStringList> connectTo() const;

    /**
     * @brief 设置自定义 DNS 服务器（默认不设置）
     *
     * 映射到 CURLOPT_DNS_SERVERS。
     */
    QCNetworkRequest &setDnsServers(const QStringList &servers);
    [[nodiscard]] std::optional<QStringList> dnsServers() const;

    /**
     * @brief 设置 DoH URL（默认不设置）
     *
     * 映射到 CURLOPT_DOH_URL。
     */
    QCNetworkRequest &setDohUrl(const QUrl &url);
    [[nodiscard]] std::optional<QUrl> dohUrl() const;

    // ========== 协议白名单（M5，安全） ==========

    /**
     * @brief 设置允许的协议白名单（默认不设置）
     *
     * 映射到 CURLOPT_PROTOCOLS_STR。
     */
    QCNetworkRequest &setAllowedProtocols(const QStringList &protocols);
    [[nodiscard]] std::optional<QStringList> allowedProtocols() const;

    /**
     * @brief 设置允许的重定向协议白名单（默认不设置）
     *
     * 映射到 CURLOPT_REDIR_PROTOCOLS_STR。
     */
    QCNetworkRequest &setAllowedRedirectProtocols(const QStringList &protocols);
    [[nodiscard]] std::optional<QStringList> allowedRedirectProtocols() const;

    /**
     * @brief 设置安全相关选项不可用时的处理策略
     *
     * 默认 Fail（更安全）。
     */
    QCNetworkRequest &setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy policy);
    [[nodiscard]] QCUnsupportedSecurityOptionPolicy unsupportedSecurityOptionPolicy() const;

    // ========== 调度 lane ==========

    /**
     * @brief 设置调度 lane 标签
     *
     * 空字符串表示 default lane。
     */
    QCNetworkRequest &setLane(const QString &lane);

    /**
     * @brief 获取调度 lane 标签
     *
     * @return 当前 lane；空字符串表示 default lane
     */
    [[nodiscard]] QString lane() const;

    // ========== 请求优先级 ==========

    /**
     * @brief 设置请求优先级
     *
     * 用于请求调度器（QCNetworkRequestScheduler）按优先级排序和执行请求。
     *
     * @note 当前调度契约为非抢占（non-preemptive）：优先级只影响 pending 队列出队顺序；
     * 已 Running 的请求不会因更高优先级到来而被中断。详见 `QCNetworkRequestScheduler` 注释。
     *
     * @param priority 请求优先级（VeryLow/Low/Normal/High/VeryHigh/Critical）
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setPriority(QCNetworkRequestPriority priority);

    /**
     * @brief 获取请求优先级
     *
     * @return 当前的请求优先级（默认：Normal）
     */
    [[nodiscard]] QCNetworkRequestPriority priority() const;

    // ========== 缓存策略 ==========

    /**
     * @brief 设置缓存策略
     *
     * 控制请求如何使用缓存数据。
     *
     * @param policy 缓存策略（AlwaysCache/PreferCache/PreferNetwork/OnlyNetwork/OnlyCache）
     * @return 返回 *this 以支持方法链
     */
    QCNetworkRequest &setCachePolicy(QCNetworkCachePolicy policy);

    /**
     * @brief 获取缓存策略
     *
     * @return 当前的缓存策略（默认：PreferCache）
     */
    [[nodiscard]] QCNetworkCachePolicy cachePolicy() const;

private:
    QSharedDataPointer<QCurl::QCNetworkRequestPrivate> d;
};

QCURL_EXPORT QDebug operator<<(QDebug dbg, const QCNetworkRequest &req);

} // namespace QCurl
#endif // QCNETWORKREQUEST_H
