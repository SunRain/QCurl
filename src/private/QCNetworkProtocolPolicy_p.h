/**
 * @file
 * @brief 声明 QCurl Core 的 HTTP/HTTPS 协议策略。
 */

#ifndef QCNETWORKPROTOCOLPOLICY_P_H
#define QCNETWORKPROTOCOLPOLICY_P_H

#include "QCNetworkProtocol.h"

#include <QStringList>
#include <QUrl>

#include <optional>

namespace QCurl::Internal {

/**
 * @brief 统一执行 Core 请求入口与 libcurl 协议白名单。
 *
 * Core 只承诺 HTTP/HTTPS。调用方可以收窄初始或重定向协议集合，但不能借助
 * 请求级配置把 FTP、FILE 等其他 libcurl 协议带入 Core 路径。
 */
class QCNetworkProtocolPolicy final
{
public:
    /// 校验 RFC HTTP token；方法大小写属于协议语义，不做归一化。
    [[nodiscard]] static bool isValidHttpMethodToken(QByteArrayView method);

    [[nodiscard]] static bool validateCoreUrl(const QUrl &url, QString *error);

    [[nodiscard]] static bool resolveInitialProtocols(std::optional<QCNetworkProtocols> requested,
                                                      QStringList *effective,
                                                      QString *error);

    [[nodiscard]] static bool resolveRedirectProtocols(std::optional<QCNetworkProtocols> requested,
                                                       QStringList *effective,
                                                       QString *error);

private:
    [[nodiscard]] static bool resolveProtocols(std::optional<QCNetworkProtocols> requested,
                                               const QString &scope,
                                               QStringList *effective,
                                               QString *error);
};

} // namespace QCurl::Internal

#endif // QCNETWORKPROTOCOLPOLICY_P_H
