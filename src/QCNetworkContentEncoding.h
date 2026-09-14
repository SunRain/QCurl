/**
 * @file
 * @brief 为 acceptedEncodings 配置提供常用编码名，保留自定义文本能力。
 */

#ifndef QCNETWORKCONTENTENCODING_H
#define QCNETWORKCONTENTENCODING_H

#include "QCGlobal.h"

#include <QString>

/// 编码名称只表达请求意图；实际算法支持取决于 libcurl 构建，不保证可以解码。
namespace QCurl::contentencoding {

inline const QString kGzip     = QStringLiteral("gzip");
inline const QString kDeflate  = QStringLiteral("deflate");
inline const QString kBrotli   = QStringLiteral("br");
inline const QString kZstd     = QStringLiteral("zstd");
inline const QString kIdentity = QStringLiteral("identity");

} // namespace QCurl::contentencoding

#endif // QCNETWORKCONTENTENCODING_H
