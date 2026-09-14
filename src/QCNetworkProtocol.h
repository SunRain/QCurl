/**
 * @file
 * @brief 声明 Core 初始请求与重定向共用的 HTTP/HTTPS 协议集合。
 */

#ifndef QCNETWORKPROTOCOL_H
#define QCNETWORKPROTOCOL_H

#include "QCGlobal.h"

#include <QFlags>

namespace QCurl {

/// Core 支持的 URL 协议；不能用此类型扩展到其他 libcurl 协议。
enum class QCNetworkProtocol {
    Http  = 0x1,
    Https = 0x2,
};

/// 允许的协议集合；传入空集合会清除显式配置，恢复 HTTP 与 HTTPS 默认值。
Q_DECLARE_FLAGS(QCNetworkProtocols, QCNetworkProtocol)
Q_DECLARE_OPERATORS_FOR_FLAGS(QCNetworkProtocols)

} // namespace QCurl

#endif // QCNETWORKPROTOCOL_H
