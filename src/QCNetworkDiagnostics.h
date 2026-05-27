/**
 * @file
 * @brief 声明诊断与可观测信息接口。
 */

#ifndef QCNETWORKDIAGNOSTICS_H
#define QCNETWORKDIAGNOSTICS_H

#include "QCGlobal.h"

#include <QDateTime>
#include <QSharedDataPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace QCurl {

class DiagResultData;

/**
 * @brief 诊断结果值类型。
 *
 * 该类型使用 accessor-only shared-data 形式保持 ABI 友好。
 * `details()` 只承载当前实现的诊断线索，不承诺稳定 schema。
 */
class QCURL_OTHER_EXTRAS_EXPORT DiagResult
{
public:
    DiagResult();
    DiagResult(const DiagResult &other);
    DiagResult(DiagResult &&other) noexcept;
    ~DiagResult();

    DiagResult &operator=(const DiagResult &other);
    DiagResult &operator=(DiagResult &&other) noexcept;

    /// 返回诊断是否成功。
    [[nodiscard]] bool success() const noexcept;
    void setSuccess(bool success) noexcept;

    /// 返回诊断摘要。
    [[nodiscard]] QString summary() const;
    void setSummary(const QString &summary);

    /// 返回诊断明细；键集合是诊断实现细节，不承诺稳定 schema。
    [[nodiscard]] QVariantMap details() const;
    void setDetails(const QVariantMap &details);
    void setDetail(const QString &key, const QVariant &value);

    /// 返回诊断耗时，单位为毫秒。
    [[nodiscard]] qint64 durationMs() const noexcept;
    void setDurationMs(qint64 durationMs) noexcept;

    /// 返回诊断时间戳。
    [[nodiscard]] QDateTime timestamp() const;
    void setTimestamp(const QDateTime &timestamp);

    /// 返回失败时的错误描述。
    [[nodiscard]] QString errorString() const;
    void setErrorString(const QString &errorString);

    /**
     * @brief 转换为易读的字符串格式
     */
    [[nodiscard]] QString toString() const;

private:
    QSharedDataPointer<DiagResultData> d;
};

/**
 * @brief 网络诊断工具类
 *
 * 提供 DNS、连接、SSL、HTTP 和路由探测等同步诊断入口。
 *
 * @note 大多数 API 会阻塞当前线程；其中多处实现依赖局部 `QEventLoop`
 * 或外部命令，不应在 UI 线程或低延迟线程中直接调用。
 */
class QCURL_OTHER_EXTRAS_EXPORT QCNetworkDiagnostics
{
public:
    /**
     * @brief DNS 解析（正向）
     *
     * 将域名解析为 IP 地址（支持 IPv4 和 IPv6）。
     *
     * @param hostname 要解析的域名
     * @param timeout 超时时间（毫秒），默认 5000ms
     * @return DiagResult 解析结果
     *
     * `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult resolveDNS(const QString &hostname, int timeout = 5000);

    /**
     * @brief DNS 反向解析
     *
     * 将 IP 地址解析为域名。
     *
     * @param ip IP 地址（IPv4 或 IPv6）
     * @param timeout 超时时间（毫秒），默认 5000ms
     * @return DiagResult 解析结果
     *
     * `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult reverseDNS(const QString &ip, int timeout = 5000);

    /**
     * @brief TCP 连接测试
     *
     * 测试到指定主机和端口的 TCP 连通性。
     *
     * @param host 主机名或 IP 地址
     * @param port 端口号
     * @param timeout 超时时间（毫秒），默认 5000ms
     * @return DiagResult 测试结果
     *
     * `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult testConnection(const QString &host, int port, int timeout = 5000);

    /**
     * @brief SSL/TLS 证书检查
     *
     * 检查 SSL 证书的有效性和详细信息。
     *
     * @param host 主机名
     * @param port 端口号，默认 443
     * @param timeout 超时时间（毫秒），默认 10000ms
     * @return DiagResult 检查结果
     *
     * 当前实现会进入局部事件循环等待握手完成。
     * `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult checkSSL(const QString &host, int port = 443, int timeout = 10000);

    /**
     * @brief HTTP 探测
     *
     * 测试 HTTP/HTTPS 请求的连通性与响应状态。
     *
     * @param url 要探测的 URL
     * @param timeout 超时时间（毫秒），默认 10000ms
     * @return DiagResult 探测结果
     *
     * 当前实现会进入局部事件循环等待 `QNetworkReply::finished()`。
     * `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult probeHTTP(const QUrl &url, int timeout = 10000);

    /**
     * @brief 综合诊断
     *
     * 对指定 URL 串行执行 DNS、连接、SSL（如适用）与 HTTP 诊断。
     *
     * @param url 要诊断的 URL
     * @return DiagResult 诊断结果。`details` 当前包含子步骤结果与 `overallHealth`，
     * 但不保证为稳定 schema。
     */
    static DiagResult diagnose(const QUrl &url);

    /**
     * @brief Ping 测试（ICMP Echo）
     *
     * 使用系统能力或外部命令测试主机可达性与网络延迟。
     *
     * @param host 主机名或 IP 地址
     * @param count 发送 ping 包数量，默认 4
     * @param timeout 每个 ping 的超时时间（毫秒），默认 1000ms
     * @return DiagResult 测试结果
     *
     * @note 当前实现可能依赖外部 `ping` 命令或原始套接字权限。
     * @note `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult ping(const QString &host, int count = 4, int timeout = 1000);

    /**
     * @brief Traceroute 路由跟踪
     *
     * 跟踪到目标主机的网络路径，并返回逐跳诊断结果。
     *
     * @param host 主机名或 IP 地址
     * @param maxHops 最大跳数，默认 30
     * @param timeout 每跳的超时时间（毫秒），默认 1000ms
     * @return DiagResult 测试结果
     *
     * @note 当前实现可能依赖外部 `traceroute` / `tracert` 命令或原始套接字权限。
     * @note `details` 仅暴露当前实现可诊断信息，不保证为稳定 schema。
     */
    static DiagResult traceroute(const QString &host, int maxHops = 30, int timeout = 1000);

private:
    QCNetworkDiagnostics() = delete; // 静态类，禁止实例化
};

} // namespace QCurl

#endif // QCNETWORKDIAGNOSTICS_H
