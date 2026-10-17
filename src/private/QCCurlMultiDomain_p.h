/**
 * @file
 * @brief 保存一个 owner-thread 传输域的原生缓存、事件源和实际限额。
 */

#ifndef QCCURLMULTIDOMAIN_P_H
#define QCCURLMULTIDOMAIN_P_H

#include "private/QCCurlMultiManagerSocketInfo_p.h"

#include <QHash>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>

#include <chrono>
#include <curl/curl.h>
#include <memory>
#include <optional>

namespace QCurl {

class QCCurlMultiManager;

// 只承载域状态。原生释放由引擎在 owner thread 显式完成；Reply/record 只延长引用，
// 不得让最后一个观察者的析构线程承担 CURLM 或 QObject 的销毁。
struct QCCurlMultiDomain final
{
    QCCurlMultiManager *owner = nullptr;
    std::weak_ptr<QCCurlMultiDomain> weakSelf;
    CURLM *multi = nullptr;
    QPointer<QTimer> timer;
    QHash<curl_socket_t, QSharedPointer<SocketInfo>> sockets;
    std::optional<long> maxTotalConnections;
    std::optional<long> maxHostConnections;
    std::optional<long> maxConcurrentStreams;
    std::optional<long> maxConnects;
    std::optional<long> multiplexing;
    std::optional<std::chrono::steady_clock::time_point> idleSince;
    bool wakeupQueued = false;
};

} // namespace QCurl

#endif // QCCURLMULTIDOMAIN_P_H
