/**
 * @file
 * @brief 声明 Blocking Extras 响应体输出状态。
 */

#ifndef QCBLOCKINGRESPONSESINK_P_H
#define QCBLOCKINGRESPONSESINK_P_H

#include "QCBlockingNetworkClient.h"
#include "QCBlockingNetworkResult.h"

#include <QByteArray>
#include <QString>

#include <cstddef>

class QIODevice;

namespace QCurl::Internal {

/**
 * @brief 保存一次 Blocking 响应体回调的输出目标与累计状态。
 */
struct QCBlockingResponseSink
{
    QByteArray *body        = nullptr;
    QIODevice *device       = nullptr;
    qint64 maxInMemoryBytes = 0;
    qint64 bytesReceived    = 0;
    qint64 abortAfterBytes  = -1;
    QString failureMessage;
    bool cancelledByProgress = false;

    /// 消费一个 libcurl body chunk；失败时返回 0 并写入 failureMessage。
    [[nodiscard]] size_t write(char *data, size_t size, size_t count);
};

/// 保存响应头回调目标与失败信息。
struct QCBlockingHeaderSink
{
    QCBlockingNetworkResult::HeaderList *headers = nullptr;
    QString failureMessage;
};

/// 保存 Blocking 进度回调与失败信息。
struct QCBlockingProgressState
{
    QCBlockingProgressCallback callback = nullptr;
    void *userData                      = nullptr;
    QString failureMessage;
};

/// libcurl `CURLOPT_WRITEFUNCTION` 适配入口。
size_t writeBlockingResponseBody(char *data, size_t size, size_t count, void *userdata);

/// libcurl `CURLOPT_HEADERFUNCTION` 适配入口。
size_t writeBlockingResponseHeader(char *data, size_t size, size_t count, void *userdata);

/// libcurl `CURLOPT_XFERINFOFUNCTION` 适配入口。
int invokeBlockingProgress(
    void *userdata, qint64 downloadTotal, qint64 downloaded, qint64 uploadTotal, qint64 uploaded);

} // namespace QCurl::Internal

#endif // QCBLOCKINGRESPONSESINK_P_H
