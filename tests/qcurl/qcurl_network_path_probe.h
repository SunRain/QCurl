#ifndef QCURL_NETWORK_PATH_PROBE_H
#define QCURL_NETWORK_PATH_PROBE_H

#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_native_multi_probe.h"

#include <QSignalSpy>
#include <QtTest>

#include <future>

namespace NetworkPathProbe {

template<typename Result>
bool waitWithEvents(std::future<Result> &future)
{
    QElapsedTimer timer;
    timer.start();
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (timer.elapsed() > 4000) {
            QTest::qFail("请求未在本机验证时限内完成", __FILE__, __LINE__);
            return false;
        }
        QTest::qWait(1);
    }
    return true;
}

/** 独立原生 easy 基线，不保留 Blocking 合同中不存在的跨次缓存。 */
inline NativeProbeResult nativeEasy(const QUrl &url, const NativeProbeOptions &options)
{
    auto future = std::async(std::launch::async, [url, options]() {
        NativeMultiProbe standalone(false, false, false);
        return standalone.get(url, options);
    });
    return waitWithEvents(future) ? future.get() : NativeProbeResult{};
}

/** 在工作线程调用真实 Blocking 执行器，主线程继续服务本机 Qt 夹具。 */
inline NativeProbeResult blocking(const QCurl::QCNetworkRequest &request)
{
    auto future = std::async(std::launch::async, [request]() {
        const auto reply = QCurl::QCBlockingNetworkClient().get(request);
        NativeProbeResult result;
        result.code   = static_cast<CURLcode>(reply.diagnosticCurlCode());
        result.status = reply.statusCode();
        result.body   = reply.body();
        return result;
    });
    return waitWithEvents(future) ? future.get() : NativeProbeResult{};
}

/** 通过真实异步 manager 获取结果；只观察终态，不读取或替换私有传输配置。 */
inline NativeProbeResult asynchronous(QCurl::QCNetworkAccessManager *manager,
                                      const QCurl::QCNetworkRequest &request)
{
    NativeProbeResult result;
    auto *reply = manager->get(request);
    QSignalSpy done(reply, &QCurl::QCNetworkReply::finished);
    if (!reply->isFinished() && !done.wait(4000)) {
        QTest::qFail("异步请求未完成", __FILE__, __LINE__);
        reply->cancel();
        return result;
    }
    result.code   = static_cast<CURLcode>(reply->diagnosticCurlCode());
    result.status = reply->httpStatusCode();
    result.body   = reply->readAll().value_or(QByteArray{});
    reply->deleteLater();
    return result;
}

} // namespace NetworkPathProbe

#endif // QCURL_NETWORK_PATH_PROBE_H
