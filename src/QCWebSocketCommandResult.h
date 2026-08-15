/**
 * @file
 * @brief 声明 WebSocket 同步命令接受结果。
 */

#ifndef QCWEBSOCKETCOMMANDRESULT_H
#define QCWEBSOCKETCOMMANDRESULT_H

#include "QCGlobal.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QString>
#include <QtGlobal>

namespace QCurl {

/**
 * @brief 表示单次 WebSocket 命令是否已被同步接受。
 *
 * 结果仅描述命令返回时的接受状态；异步连接、发送和关闭是否完成，仍由
 * `QCWebSocket` 的状态与信号表达。拒绝结果不会修改连接状态，也不会写入
 * `QCWebSocket::errorString()`。
 */
class QCURL_OTHER_EXTRAS_EXPORT QCWebSocketCommandResult
{
public:
    /**
     * @brief WebSocket 命令的同步接受状态。
     */
    enum class Status {
        Accepted,          ///< 命令已进入对应的异步处理或发送链路。
        WrongThread,       ///< 调用线程不是 WebSocket 对象所属线程。
        InvalidState,      ///< 当前连接状态不接受该命令。
        InvalidArgument,   ///< 参数不满足协议或配置约束。
        QueueLimitReached, ///< 发送队列没有足够容量接受完整 payload。
    };

    /**
     * @brief 构造默认结果。
     *
     * 默认结果表示零 payload 字节的已接受命令。
     */
    QCWebSocketCommandResult() = default;

    /**
     * @brief 创建已接受的命令结果。
     * @param acceptedBytes 已接受进入异步发送链路的 payload 字节数；非 payload 命令为 0。
     * @return 同步接受结果；不表示数据已经写入网络。
     */
    [[nodiscard]] static QCWebSocketCommandResult accepted(qint64 acceptedBytes = 0) noexcept;

    /**
     * @brief 创建被拒绝的命令结果。
     * @param status 拒绝状态；不得为 `Accepted`。
     * @param error 面向调用方的诊断文本，不改变 socket 的连接错误状态。
     * @return 同步拒绝结果，接受字节数为 0。
     */
    [[nodiscard]] static QCWebSocketCommandResult rejected(Status status,
                                                           const QString &error = {});

    /**
     * @brief 返回同步接受状态。
     */
    [[nodiscard]] Status status() const noexcept;

    /**
     * @brief 判断命令是否已被同步接受。
     * @return 状态为 `Accepted` 时返回 `true`。
     */
    [[nodiscard]] bool isAccepted() const noexcept;

    /**
     * @brief 返回已接受进入异步发送链路的 payload 字节数。
     * @return 非发送命令或拒绝结果返回 0；该值不表示已经完成网络交付。
     */
    [[nodiscard]] qint64 acceptedBytes() const noexcept;

    /**
     * @brief 返回本次同步拒绝的诊断文本。
     * @return 已接受时为空；拒绝时仅描述本次命令，不改变连接错误状态。
     */
    [[nodiscard]] QString error() const;

private:
    Status m_status        = Status::Accepted;
    qint64 m_acceptedBytes = 0;
    QString m_error;
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKETCOMMANDRESULT_H
