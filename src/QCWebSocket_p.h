#ifndef QCWEBSOCKET_P_H
#define QCWEBSOCKET_P_H

#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCCurlHandleManager.h"
#include "QCWebSocketReconnectPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCWebSocketCompressionConfig.h"
#include <QTimer>
#include <QSocketNotifier>
#include <QByteArray>
#include <curl/curl.h>
#include <curl/websockets.h>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief QCWebSocket 的私有实现类
 *
 * 使用 Pimpl 模式隐藏实现细节，管理 libcurl WebSocket 资源和消息处理。
 *
 * @internal
 */
class QCWebSocketPrivate
{
public:
    /**
     * @brief 构造函数
     * @param parent 指向公共接口对象的指针
     */
    explicit QCWebSocketPrivate(QCWebSocket *parent);

    /**
     * @brief 析构函数
     *
     * 自动清理所有资源（curl 句柄、定时器等）。
     */
    ~QCWebSocketPrivate();

    // ========================================================================
    // 核心状态
    // ========================================================================

    /// 当前 WebSocket 连接状态
    QCWebSocket::State state = QCWebSocket::State::Unconnected;

    /// WebSocket 服务器 URL
    QUrl url;

    /// 最后一次错误的描述信息
    QString errorString;

    // ========================================================================
    // libcurl 资源（RAII 管理）
    // ========================================================================

    /// RAII curl 句柄管理器（自动初始化和清理）
    QCCurlHandleManager curlManager;

    // ========================================================================
    // 消息处理
    // ========================================================================

    /// 接收消息的定时器（轮询模式，每 50ms 检查一次）
    QTimer *receiveTimer = nullptr;

    /// 接收数据的临时缓冲区
    QByteArray receiveBuffer;

    /// 分片消息的累积缓冲区（用于处理 CURLWS_CONT 标志）
    QByteArray fragmentBuffer;

    // ========================================================================
    // 自动重连状态（v2.4.0）
    // ========================================================================

    /// 重连策略配置
    QCWebSocketReconnectPolicy reconnectPolicy;

    /// 当前重连尝试次数（从 0 开始，连接成功后重置）
    int reconnectAttemptCount = 0;

    /// 最后一次关闭的状态码（用于判断是否可重连）
    int lastCloseCode = 0;

    /// 重连定时器（用于延迟重连）
    QTimer *reconnectTimer = nullptr;

    // ========================================================================
    // SSL/TLS 配置（v2.4.1）
    // ========================================================================

    /// SSL 配置（默认为安全配置：启用证书验证）
    QCNetworkSslConfig sslConfig;

    // ========================================================================
    // 压缩配置（v2.18.0）
    // ========================================================================

    /// WebSocket 压缩配置（RFC 7692 permessage-deflate）
    QCWebSocketCompressionConfig compressionConfig;

    /// 压缩是否已协商成功
    bool compressionNegotiated = false;

    /// 发送统计：原始字节数
    qint64 sentBytesRaw = 0;

    /// 发送统计：压缩后字节数
    qint64 sentBytesCompressed = 0;

    /// 接收统计：压缩字节数
    qint64 receivedBytesCompressed = 0;

    /// 接收统计：解压后字节数
    qint64 receivedBytesRaw = 0;

    // ========================================================================
    // 事件驱动接收（v2.4.2）
    // ========================================================================

    /// Socket 读事件通知器（事件驱动模式）
    /// 当 socket 有数据可读时立即触发，延迟 <1ms
    QSocketNotifier *socketReadNotifier = nullptr;

    /// 是否启用事件驱动模式（true=QSocketNotifier, false=QTimer）
    bool eventDrivenMode = false;

    // ========================================================================
    // 内部方法
    // ========================================================================

    /**
     * @brief 设置新的连接状态
     * @param newState 新的状态值
     *
     * 更新内部状态并发射 stateChanged() 信号。
     */
    void setState(QCWebSocket::State newState);

    /**
     * @brief 处理从服务器接收的数据
     *
     * 使用 curl_ws_recv() 接收 WebSocket 帧，根据帧类型（TEXT/BINARY/PING/PONG/CLOSE）
     * 分别处理，并发射相应的信号。
     *
     * @note 此方法由 receiveTimer 定时调用（每 50ms）
     */
    void processIncomingData();

    /**
     * @brief 处理错误情况
     * @param error 错误描述字符串
     *
     * 设置错误信息、更新状态为 Error，并发射 errorOccurred() 信号。
     */
    void handleError(const QString &error);

    /**
     * @brief 清理连接资源
     *
     * 停止接收定时器，清理缓冲区，设置状态为 Closed。
     * 发射 disconnected() 信号。
     */
    void cleanupConnection();

    /**
     * @brief 发送 WebSocket 数据帧
     * @param data 要发送的数据
     * @param flags WebSocket 帧标志（CURLWS_TEXT、CURLWS_BINARY 等）
     * @return 实际发送的字节数，失败返回 -1
     */
    qint64 sendFrame(const QByteArray &data, unsigned int flags);

    // ========================================================================
    // 事件驱动接收方法（v2.4.2）
    // ========================================================================

    /**
     * @brief 获取 WebSocket 的底层 socket 描述符
     * @return socket 文件描述符，失败返回 CURL_SOCKET_BAD
     *
     * 使用 CURLINFO_ACTIVESOCKET 从 curl 句柄获取活动 socket。
     * 仅在连接成功后有效。
     */
    curl_socket_t getSocketDescriptor();

    /**
     * @brief 启用事件驱动接收
     *
     * 尝试获取 socket 并创建 QSocketNotifier。
     * 如果失败，自动降级到 QTimer 轮询模式。
     *
     * @note 在连接成功后调用
     */
    void enableEventDrivenReceive();

    /**
     * @brief 降级到轮询模式（QTimer）
     *
     * 当无法获取 socket 描述符时使用。
     * 创建 QTimer 每 50ms 轮询一次。
     */
    void fallbackToPollingMode();

    // ========================================================================
    // 压缩/解压缩辅助方法（v2.18.0）
    // ========================================================================

    /**
     * @brief 使用 zlib deflate 压缩数据
     *
     * @param input 原始数据
     * @param output 压缩后的数据（输出参数）
     * @return true 成功，false 失败
     */
    bool compressData(const QByteArray &input, QByteArray &output);

    /**
     * @brief 使用 zlib inflate 解压缩数据
     *
     * @param input 压缩数据
     * @param output 解压后的数据（输出参数）
     * @return true 成功，false 失败
     */
    bool decompressData(const QByteArray &input, QByteArray &output);

    // ========================================================================
    // 自动重连方法（v2.4.0）
    // ========================================================================

    /**
     * @brief 处理连接断开并判断是否重连
     * @param closeCode WebSocket 关闭状态码
     *
     * 根据重连策略判断是否应该重连：
     * - 如果应该重连，设置延迟定时器
     * - 如果不重连，发射 disconnected() 信号
     */
    void handleDisconnection(int closeCode);

    /**
     * @brief 尝试重新连接
     *
     * 被 reconnectTimer 触发，执行实际的重连操作。
     * - 增加重连计数
     * - 发射 reconnectAttempt() 信号
     * - 调用 open() 重新连接
     */
    void attemptReconnect();

private:
    /// 指向公共接口对象的指针（用于 Q_DECLARE_PUBLIC 宏）
    QCWebSocket *q_ptr;
    Q_DECLARE_PUBLIC(QCWebSocket)

    Q_DISABLE_COPY(QCWebSocketPrivate)
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKET_P_H
