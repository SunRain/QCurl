/**
 * @file QCNetworkDiagnostics.h
 * @brief 网络诊断工具类
 * 
 * 提供 DNS 解析、连接测试、SSL 检查、HTTP 探测等诊断功能。
 * 
 */

#ifndef QCNETWORKDIAGNOSTICS_H
#define QCNETWORKDIAGNOSTICS_H

#include "QCurlConfig.h"
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QVariantMap>
#include <QStringList>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief 诊断结果结构
 */
struct DiagResult
{
    bool success;                ///< 诊断是否成功
    QString summary;             ///< 简要总结
    QVariantMap details;         ///< 详细信息（键值对）
    qint64 durationMs;           ///< 诊断耗时（毫秒）
    QDateTime timestamp;         ///< 诊断时间戳
    QString errorString;         ///< 错误描述（失败时）
    
    /**
     * @brief 转换为易读的字符串格式
     */
    QString toString() const;
};

/**
 * @brief 网络诊断工具类
 * 
 * 提供一套完整的网络诊断功能，帮助快速定位网络问题。
 * 
 * @par 功能列表
 * - DNS 解析（正向和反向）
 * - TCP 连接测试
 * - SSL/TLS 证书检查
 * - HTTP 探测（时间分解）
 * - 综合诊断（一键诊断）
 * 
 * @par 基本用法
 * @code
 * // DNS 解析
 * auto result = QCNetworkDiagnostics::resolveDNS("example.com");
 * qDebug() << result.toString();
 * 
 * // SSL 检查
 * auto sslResult = QCNetworkDiagnostics::checkSSL("example.com");
 * qDebug() << "证书有效期:" << sslResult.details["daysValid"].toInt() << "天";
 * 
 * // 综合诊断
 * auto diagResult = QCNetworkDiagnostics::diagnose(QUrl("https://example.com"));
 * qDebug() << diagResult.toString();
 * @endcode
 * 
 */
class QCNetworkDiagnostics
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
     * @par 返回的 details 键
     * - "hostname" (QString) - 主机名
     * - "ipv4" (QStringList) - IPv4 地址列表
     * - "ipv6" (QStringList) - IPv6 地址列表
     * - "resolveDuration" (qint64) - 解析耗时（毫秒）
     * 
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::resolveDNS("example.com");
     * if (result.success) {
     *     qDebug() << "IPv4:" << result.details["ipv4"].toStringList();
     * }
     * @endcode
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
     * @par 返回的 details 键
     * - "ip" (QString) - IP 地址
     * - "hostname" (QString) - 解析出的主机名
     * - "resolveDuration" (qint64) - 解析耗时（毫秒）
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
     * @par 返回的 details 键
     * - "host" (QString) - 主机名
     * - "port" (int) - 端口号
     * - "connected" (bool) - 是否连接成功
     * - "connectDuration" (qint64) - 连接耗时（毫秒）
     * - "resolvedIP" (QString) - 解析后的 IP 地址
     * 
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::testConnection("example.com", 443);
     * if (result.success) {
     *     qDebug() << "连接成功，耗时:" << result.durationMs << "ms";
     * }
     * @endcode
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
     * @par 返回的 details 键
     * - "issuer" (QString) - 证书颁发者
     * - "subject" (QString) - 证书主题
     * - "notBefore" (QDateTime) - 生效时间
     * - "notAfter" (QDateTime) - 过期时间
     * - "daysValid" (int) - 剩余有效天数
     * - "tlsVersion" (QString) - TLS 版本
     * - "certChain" (QStringList) - 证书链
     * - "verified" (bool) - 证书是否验证通过
     * 
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::checkSSL("example.com");
     * if (result.success) {
     *     int days = result.details["daysValid"].toInt();
     *     qDebug() << "证书剩余有效期:" << days << "天";
     * }
     * @endcode
     */
    static DiagResult checkSSL(const QString &host, int port = 443, int timeout = 10000);
    
    /**
     * @brief HTTP 探测
     * 
     * 测试 HTTP/HTTPS 请求的各个阶段耗时。
     * 
     * @param url 要探测的 URL
     * @param timeout 超时时间（毫秒），默认 10000ms
     * @return DiagResult 探测结果
     * 
     * @par 返回的 details 键
     * - "url" (QString) - 请求的 URL
     * - "statusCode" (int) - HTTP 状态码
     * - "statusText" (QString) - 状态描述
     * - "dnsTime" (qint64) - DNS 解析时间（毫秒）
     * - "connectTime" (qint64) - TCP 连接时间（毫秒）
     * - "sslTime" (qint64) - SSL 握手时间（毫秒）
     * - "ttfbTime" (qint64) - 首字节时间（毫秒）
     * - "totalTime" (qint64) - 总耗时（毫秒）
     * - "redirectCount" (int) - 重定向次数
     * - "finalURL" (QString) - 最终 URL（如有重定向）
     * 
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::probeHTTP(QUrl("https://example.com"));
     * if (result.success) {
     *     qDebug() << "DNS:" << result.details["dnsTime"].toLongLong() << "ms";
     *     qDebug() << "连接:" << result.details["connectTime"].toLongLong() << "ms";
     *     qDebug() << "SSL:" << result.details["sslTime"].toLongLong() << "ms";
     * }
     * @endcode
     */
    static DiagResult probeHTTP(const QUrl &url, int timeout = 10000);
    
    /**
     * @brief 综合诊断
     *
     * 对指定 URL 进行完整的网络诊断，包括 DNS、连接、SSL、HTTP 等所有方面。
     *
     * @param url 要诊断的 URL
     * @return DiagResult 诊断结果（包含所有子项的详细信息）
     *
     * @par 返回的 details 键
     * - "dns" (QVariantMap) - DNS 解析结果
     * - "connection" (QVariantMap) - 连接测试结果
     * - "ssl" (QVariantMap) - SSL 检查结果（如果是 HTTPS）
     * - "http" (QVariantMap) - HTTP 探测结果
     * - "overallHealth" (QString) - 整体健康状态（"excellent"/"good"/"warning"/"error"）
     *
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::diagnose(QUrl("https://example.com"));
     * qDebug() << result.toString();
     * qDebug() << "整体状态:" << result.details["overallHealth"].toString();
     * @endcode
     */
    static DiagResult diagnose(const QUrl &url);

    /**
     * @brief Ping 测试（ICMP Echo）
     *
     * 使用 ICMP Echo Request/Reply 测试主机可达性和网络延迟。
     *
     * @param host 主机名或 IP 地址
     * @param count 发送 ping 包数量，默认 4
     * @param timeout 每个 ping 的超时时间（毫秒），默认 1000ms
     * @return DiagResult 测试结果
     *
     * @par 返回的 details 键
     * - "host" (QString) - 主机名
     * - "resolvedIP" (QString) - 解析后的 IP 地址
     * - "packetsSent" (int) - 发送的包数量
     * - "packetsReceived" (int) - 收到的包数量
     * - "packetsLost" (int) - 丢失的包数量
     * - "lossRate" (double) - 丢包率（百分比）
     * - "minRTT" (qint64) - 最小往返时间（毫秒）
     * - "maxRTT" (qint64) - 最大往返时间（毫秒）
     * - "avgRTT" (qint64) - 平均往返时间（毫秒）
     * - "rttList" (QList<qint64>) - 每次 ping 的 RTT 列表
     *
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::ping("example.com", 4);
     * if (result.success) {
     *     qDebug() << "平均延迟:" << result.details["avgRTT"].toLongLong() << "ms";
     *     qDebug() << "丢包率:" << result.details["lossRate"].toDouble() << "%";
     * }
     * @endcode
     *
     * @note Linux 下需要 root 权限或 CAP_NET_RAW 能力
     * @note Windows 下使用 IcmpSendEcho API
     * @note macOS 下使用原始套接字
     *
     */
    static DiagResult ping(const QString &host, int count = 4, int timeout = 1000);

    /**
     * @brief Traceroute 路由跟踪
     *
     * 跟踪到目标主机的网络路径，显示每一跳的延迟。
     *
     * @param host 主机名或 IP 地址
     * @param maxHops 最大跳数，默认 30
     * @param timeout 每跳的超时时间（毫秒），默认 1000ms
     * @return DiagResult 测试结果
     *
     * @par 返回的 details 键
     * - "host" (QString) - 目标主机名
     * - "resolvedIP" (QString) - 解析后的 IP 地址
     * - "hops" (QList<QVariantMap>) - 每一跳的信息列表
     *   - "hopNumber" (int) - 跳数序号（从 1 开始）
     *   - "ip" (QString) - 该跳的 IP 地址
     *   - "hostname" (QString) - 该跳的主机名（如果能解析）
     *   - "rtt1" (qint64) - 第一次探测的 RTT（毫秒）
     *   - "rtt2" (qint64) - 第二次探测的 RTT（毫秒）
     *   - "rtt3" (qint64) - 第三次探测的 RTT（毫秒）
     *   - "avgRTT" (qint64) - 平均 RTT
     *   - "timeout" (bool) - 是否超时
     * - "totalHops" (int) - 实际跳数
     * - "reachedDestination" (bool) - 是否到达目标
     *
     * @par 示例
     * @code
     * auto result = QCNetworkDiagnostics::traceroute("example.com");
     * if (result.success) {
     *     QList<QVariantMap> hops = result.details["hops"].value<QList<QVariantMap>>();
     *     for (const auto &hop : hops) {
     *         qDebug() << hop["hopNumber"].toInt()
     *                  << hop["ip"].toString()
     *                  << hop["avgRTT"].toLongLong() << "ms";
     *     }
     * }
     * @endcode
     *
     * @note 需要原始套接字权限（类似 ping）
     * @note 某些防火墙可能阻止 ICMP Time Exceeded 消息
     *
     */
    static DiagResult traceroute(const QString &host, int maxHops = 30, int timeout = 1000);

private:
    QCNetworkDiagnostics() = delete;  // 静态类，禁止实例化
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCNETWORKDIAGNOSTICS_H
