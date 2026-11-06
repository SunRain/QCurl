/**
 * @file
 * @brief 声明配置诊断发布与原生 callback 适配的窄边界。
 */

#ifndef QCNETWORKREPLYCONFIGURATIONEXECUTION_P_H
#define QCNETWORKREPLYCONFIGURATIONEXECUTION_P_H

#include "private/QCReplyCurlConfiguration_p.h"

namespace QCurl {

class QCNetworkReplyPrivate;

namespace Internal {

/// 按原顺序发布普通/capability warning，再记录首次 fatal，不重新解释配置策略。
[[nodiscard]] bool publishReplyCurlConfigurationResult(QCNetworkReplyPrivate *reply,
                                                       const ReplyCurlConfigurationResult &result);
[[nodiscard]] bool publishReplyTlsConfigurationResult(QCNetworkReplyPrivate *reply,
                                                      const TlsConfigurationResult &result);

/// 原生 callback 绑定复用配置模块的 required 失败语义；value 只借用到当次 setopt。
template<typename T>
[[nodiscard]] bool setRequiredReplyCallbackOption(QCNetworkReplyPrivate *reply,
                                                  CURL *easy,
                                                  CurlOptions::Option option,
                                                  T value)
{
    return publishReplyCurlConfigurationResult(reply,
                                               QCReplyCurlConfiguration::classifyRequiredOption(
                                                   CurlOptions::setWithTestHook(easy, option, value),
                                                   option.name));
}

/// 在原 base/network 末尾检查线程，再让 body module 探测设备；readyRead 连接属于 adapter。
[[nodiscard]] bool prepareReplyBodySource(QCNetworkReplyPrivate *reply);
/// READ 只在设备传输模式注册，保留 method selection 与 body size 之间的插点。
[[nodiscard]] bool configureReplyReadCallbacks(QCNetworkReplyPrivate *reply, CURL *easy);
/// response/seek/progress 位于 timeout 后，不能提前到 request 配置前。
[[nodiscard]] bool configureReplyResponseCallbacks(QCNetworkReplyPrivate *reply, CURL *easy);
[[nodiscard]] bool configureReplyTransferCallbacks(QCNetworkReplyPrivate *reply, CURL *easy);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKREPLYCONFIGURATIONEXECUTION_P_H
