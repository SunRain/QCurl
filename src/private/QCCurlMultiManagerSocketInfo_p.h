/**
 * @file
 * @brief 声明 curl multi 事件驱动器使用的私有 socket notifier 状态。
 */

#ifndef QCCURLMULTIMANAGERSOCKETINFO_P_H
#define QCCURLMULTIMANAGERSOCKETINFO_P_H

#include <QSocketNotifier>

#include <curl/curl.h>

namespace QCurl {

/**
 * @brief 保存一个 curl multi socket 及其 owner-thread notifier。
 *
 * 该内部记录由 QCCurlMultiManager 统一维护，直接字段只承载同一事件驱动职责。
 */
struct SocketInfo
{
    curl_socket_t socketfd         = CURL_SOCKET_BAD;
    QSocketNotifier *readNotifier  = nullptr;
    QSocketNotifier *writeNotifier = nullptr;
};

} // namespace QCurl

#endif // QCCURLMULTIMANAGERSOCKETINFO_P_H
