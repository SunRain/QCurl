#ifndef QCWEBSOCKET_H
#define QCWEBSOCKET_H

#include "QCGlobal.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QByteArray>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QUrl>

namespace QCurl {

// 前向声明
class QCWebSocketPrivate;
class QCWebSocketReconnectPolicy;
class QCNetworkSslConfig;
struct QCWebSocketCompressionConfig;

/**
 * @brief WebSocket 客户端
 *
 * 封装基于 libcurl 的 WebSocket 连接、消息收发和关闭握手。
 * 所有方法都要求在对象所属线程中调用。
 */
class QCURL_EXPORT QCWebSocket : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QUrl url READ url)
    Q_PROPERTY(bool isValid READ isValid)

public:
    /**
     * @brief WebSocket 连接状态
     */
    enum class State {
        Unconnected, ///< 未连接（初始状态）
        Connecting,  ///< 连接中
        Connected,   ///< 已连接（可以收发消息）
        Closing,     ///< 关闭中（正在执行关闭握手）
        Closed       ///< 已关闭
    };
    Q_ENUM(State)

    /**
     * @brief WebSocket 关闭状态码
     *
     * 符合 RFC 6455 定义的标准关闭代码。
     *
     * @see https://datatracker.ietf.org/doc/html/rfc6455#section-7.4
     */
    enum class CloseCode {
        Normal             = 1000, ///< 正常关闭
        GoingAway          = 1001, ///< 端点离开（如浏览器页面关闭）
        ProtocolError      = 1002, ///< 协议错误
        UnsupportedData    = 1003, ///< 不支持的数据类型
        NoStatusReceived   = 1005, ///< 未收到状态码（保留，不应发送）
        AbnormalClosure    = 1006, ///< 异常关闭（保留，不应发送）
        InvalidPayload     = 1007, ///< 无效的载荷数据
        PolicyViolation    = 1008, ///< 策略违规
        MessageTooBig      = 1009, ///< 消息过大
        MandatoryExtension = 1010, ///< 缺少必需的扩展
        InternalError      = 1011, ///< 服务器内部错误
        ServiceRestart     = 1012, ///< 服务重启
        TryAgainLater      = 1013, ///< 稍后重试
        TlsHandshake       = 1015  ///< TLS 握手失败（保留，不应发送）
    };
    Q_ENUM(CloseCode)

    /**
     * @brief 构造函数
     *
     * @param url WebSocket 服务器 URL（ws:// 或 wss://）
     * @param parent 父对象（可选）
     */
    explicit QCWebSocket(const QUrl &url, QObject *parent = nullptr);

    /**
     * @brief 析构函数
     *
     * 自动关闭连接并清理资源。
     */
    ~QCWebSocket() override;

    // ==================
    // 连接管理
    // ==================

    /**
     * @brief 打开 WebSocket 连接
     *
     * 异步建立到服务器的连接。连接成功后发射 connected() 信号。
     * 如果连接失败，发射 errorOccurred() 信号。
     *
     * @note 如果已经处于连接状态，此方法无效
     *
     * @see connected(), errorOccurred()
     */
    void open();

    /**
     * @brief 关闭 WebSocket 连接
     *
     * 执行优雅的关闭握手。发送关闭帧并等待服务器响应。
     * 关闭完成后发射 disconnected() 信号。
     *
     * @param closeCode 关闭状态码（默认 Normal）
     * @param reason 关闭原因描述（可选，最大 123 字节）
     * @see disconnected(), CloseCode
     */
    void close(CloseCode closeCode = CloseCode::Normal, const QString &reason = QString());

    /**
     * @brief 中止 WebSocket 连接
     *
     * 立即关闭连接，不执行关闭握手。
     * 适用于紧急情况或错误恢复。
     *
     * @note 此方法不发送关闭帧，可能导致服务器认为连接异常中断
     */
    void abort();

    // ==================
    // 消息发送
    // ==================

    /**
     * @brief 发送文本消息
     *
     * @param message 要发送的文本消息（UTF-8 编码）
     * @return qint64 实际发送的字节数，失败返回 -1
     *
     * @note 只能在 Connected 状态下发送消息
     */
    qint64 sendTextMessage(const QString &message);

    /**
     * @brief 发送二进制消息
     *
     * @param data 要发送的二进制数据
     * @return qint64 实际发送的字节数，失败返回 -1
     *
     * @note 只能在 Connected 状态下发送消息
     */
    qint64 sendBinaryMessage(const QByteArray &data);

    /**
     * @brief 发送 Ping 帧
     *
     * 用于心跳检测。服务器应该响应 Pong 帧。
     *
     * @param payload 可选的载荷数据（最大 125 字节）
     *
     * @note libcurl 可能会自动处理 Pong 响应
     *
     * @see pongReceived()
     */
    void ping(const QByteArray &payload = QByteArray());

    /**
     * @brief 发送 Pong 帧
     *
     * 当启用 NOAUTOPONG（setAutoPongEnabled(false)）时，可用于手动响应服务端的 Ping。
     *
     * @param payload 可选的载荷数据（最大 125 字节）
     *
     * @see pingReceived()
     */
    void pong(const QByteArray &payload = QByteArray());

    /**
     * @brief 设置是否启用自动 Pong
     *
     * - enabled=true（默认）：由 libcurl 自动处理 Ping→Pong
     * - enabled=false：禁用自动 Pong（CURLWS_NOAUTOPONG），应用需在 pingReceived() 中自行回复
     * pong()
     *
     * @note 必须在 open() 之前设置，连接建立后修改无效
     */
    void setAutoPongEnabled(bool enabled);

    /**
     * @brief 查询是否启用自动 Pong
     */
    [[nodiscard]] bool isAutoPongEnabled() const;

    // ==================
    // 自动重连配置
    // ==================

    /**
     * @brief 设置自动重连策略
     *
     * 配置连接断开后的自动重连行为。
     *
     * @param policy 重连策略配置
     * @see QCWebSocketReconnectPolicy, reconnectPolicy(), reconnectAttempt()
     */
    void setReconnectPolicy(const QCWebSocketReconnectPolicy &policy);

    /**
     * @brief 获取当前的重连策略
     * @return QCWebSocketReconnectPolicy 当前配置的重连策略
     */
    [[nodiscard]] QCWebSocketReconnectPolicy reconnectPolicy() const;

    // ==================
    // SSL/TLS 配置
    // ==================

    /**
     * @brief 设置 SSL/TLS 配置
     *
     * 用于 wss:// 协议的证书验证、客户端证书等配置。
     * 默认启用对等证书验证和主机名验证（安全配置）。
     *
     * @param config SSL 配置对象
     * @warning 生产环境务必启用证书验证（verifyPeer=true, verifyHost=true）
     */
    void setSslConfig(const QCNetworkSslConfig &config);

    /**
     * @brief 获取当前 SSL 配置
     * @return SSL 配置对象
     */
    [[nodiscard]] QCNetworkSslConfig sslConfig() const;

    // ==================
    // 压缩配置
    // ==================

    /**
     * @brief 设置 WebSocket 压缩配置
     *
     * 启用 RFC 7692 permessage-deflate 扩展，对 WebSocket 消息进行压缩。
     * 压缩设置必须在调用 open() 之前配置，建立连接后修改无效。
     *
     * @param config 压缩配置对象
     * @note 服务器可能不支持压缩，或修改压缩参数。使用 isCompressionNegotiated()
     *       检查实际协商结果。
     *
     * @see QCWebSocketCompressionConfig, isCompressionNegotiated(), compressionStats()
     */
    void setCompressionConfig(const QCWebSocketCompressionConfig &config);

    /**
     * @brief 获取当前压缩配置
     *
     * @return QCWebSocketCompressionConfig 当前配置的压缩参数
     */
    [[nodiscard]] QCWebSocketCompressionConfig compressionConfig() const;

    /**
     * @brief 检查压缩是否已协商成功
     *
     * 只有在连接建立后才有意义。如果服务器接受了压缩扩展，
     * 则返回 true，后续消息会自动压缩/解压缩。
     *
     * @return bool true 表示压缩已启用，false 表示未启用或服务器拒绝
     */
    [[nodiscard]] bool isCompressionNegotiated() const;

    /**
     * @brief 获取压缩统计信息
     *
     * 返回当前连接的压缩效果统计，包括原始/压缩大小、压缩率等。
     *
     * @return QString 格式化的统计字符串
     * @note 只有在 isCompressionNegotiated() 返回 true 时才有数据
     */
    [[nodiscard]] QString compressionStats() const;

    // ==================
    // 状态查询
    // ==================

    /**
     * @brief 获取当前连接状态
     *
     * @return State 当前状态
     */
    [[nodiscard]] State state() const;

    /**
     * @brief 获取 WebSocket 服务器 URL
     *
     * @return QUrl WebSocket URL
     */
    [[nodiscard]] QUrl url() const;

    /**
     * @brief 获取最后一次错误的描述
     *
     * @return QString 错误描述字符串，无错误时返回空字符串
     */
    [[nodiscard]] QString errorString() const;

    /**
     * @brief 检查连接是否有效
     *
     * @return bool true 表示连接有效（已连接），false 表示未连接或已关闭
     */
    [[nodiscard]] bool isValid() const;

Q_SIGNALS:
    /**
     * @brief 连接成功信号
     *
     * 当 WebSocket 连接成功建立后发射此信号。
     * 此时可以开始发送和接收消息。
     */
    void connected();

    /**
     * @brief 连接断开信号
     *
     * 当 WebSocket 连接关闭后发射此信号。
     * 可能是主动关闭或被动断开。
     */
    void disconnected();

    /**
     * @brief 状态变化信号
     *
     * @param state 新的连接状态
     *
     * 当连接状态改变时发射此信号。
     */
    void stateChanged(State state);

    /**
     * @brief 接收到文本消息信号
     *
     * @param message 接收到的文本消息（UTF-8 解码）
     *
     * 当从服务器接收到文本帧时发射此信号。
     */
    void textMessageReceived(const QString &message);

    /**
     * @brief 接收到二进制消息信号
     *
     * @param data 接收到的二进制数据
     *
     * 当从服务器接收到二进制帧时发射此信号。
     */
    void binaryMessageReceived(const QByteArray &data);

    /**
     * @brief 接收到 Pong 响应信号
     *
     * @param payload Pong 帧的载荷数据
     *
     * 当从服务器接收到 Pong 帧时发射此信号。
     * 通常是对之前发送的 Ping 的响应。
     */
    void pongReceived(const QByteArray &payload);

    /**
     * @brief 接收到 Ping 帧信号
     *
     * @param payload Ping 帧载荷数据
     *
     * @note 当禁用自动 Pong（setAutoPongEnabled(false)）时，应用需显式调用 pong() 回复
     */
    void pingReceived(const QByteArray &payload);

    /**
     * @brief 接收到 Close 帧信号
     *
     * @param closeCode 关闭状态码（RFC 6455）
     * @param reason 关闭原因（UTF-8）
     */
    void closeReceived(int closeCode, const QString &reason);

    /**
     * @brief 错误发生信号
     *
     * @param errorString 错误描述
     *
     * 当发生错误时发射此信号（如连接失败、发送失败等）。
     */
    void errorOccurred(const QString &errorString);

    /**
     * @brief SSL 错误信号
     *
     * @param errors SSL 错误列表
     *
     * 当建立 WSS 连接时发生 SSL 错误时发射此信号。
     */
    void sslErrors(const QStringList &errors);

    /**
     * @brief SSL 错误详细信号
     *
     * 提供逐条 SSL 错误详情，比 sslErrors() 更适合诊断输出。
     *
     * @param errors SSL 错误详情列表（每个错误一行描述）
     */
    void sslErrorsDetailed(const QStringList &errors);

    /**
     * @brief 重连尝试信号
     *
     * 当启用自动重连后，每次重连尝试前发射此信号。
     *
     * @param attemptCount 当前重连尝试次数（从 1 开始）
     * @param closeCode 导致断开的关闭状态码
     * @see setReconnectPolicy(), QCWebSocketReconnectPolicy
     */
    void reconnectAttempt(int attemptCount, int closeCode);

private:
    Q_DECLARE_PRIVATE(QCWebSocket)
    QScopedPointer<QCWebSocketPrivate> d_ptr;

    Q_DISABLE_COPY(QCWebSocket)
};

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
#endif // QCWEBSOCKET_H
