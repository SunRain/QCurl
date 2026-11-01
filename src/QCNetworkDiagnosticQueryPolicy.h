/**
 * @file
 * @brief 声明请求级诊断 query 可见性策略。
 */

#ifndef QCNETWORKDIAGNOSTICQUERYPOLICY_H
#define QCNETWORKDIAGNOSTICQUERYPOLICY_H

namespace QCurl {

/**
 * @brief 控制内建日志中调用方初始 URL 的 query 呈现，不改变实际请求。
 *
 * 两档都移除 userinfo/fragment，隐藏敏感头、重定向目标与自由文本中的敏感数据。
 */
enum class QCNetworkDiagnosticQueryPolicy {
    Redacted, ///< 默认：完整 query 显示为 ?[REDACTED]。
    Full,     ///< 显式允许初始请求 query 可见；调用方须确认其中没有敏感信息。
};

} // namespace QCurl

#endif // QCNETWORKDIAGNOSTICQUERYPOLICY_H
