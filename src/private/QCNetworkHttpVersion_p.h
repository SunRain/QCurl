/**
 * @file
 * @brief 声明 HTTP 版本内部辅助接口。
 */

#ifndef QCNETWORKHTTPVERSION_P_H
#define QCNETWORKHTTPVERSION_P_H

#include "private/QCNetworkRequestPolicy_p.h"

namespace QCurl::detail {

[[nodiscard]] long toCurlHttpVersion(QCNetworkHttpVersion version) noexcept;

/// 在调用方的既有 HTTP 检查点采样能力；策略判断由纯值模块负责。
[[nodiscard]] Internal::HttpVersionFacts sampleHttpVersionFacts();

} // namespace QCurl::detail

#endif // QCNETWORKHTTPVERSION_P_H
