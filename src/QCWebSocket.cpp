#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket_p.h"
#include <QDebug>
#include <zlib.h>

QT_BEGIN_NAMESPACE

namespace QCurl {

// ============================================================================
// QCWebSocketPrivate 实现
// ============================================================================

QCWebSocketPrivate::QCWebSocketPrivate(QCWebSocket *parent)
    : q_ptr(parent)
{
}

QCWebSocketPrivate::~QCWebSocketPrivate()
{
    // 停止接收定时器
    if (receiveTimer) {
        receiveTimer->stop();
        delete receiveTimer;
        receiveTimer = nullptr;
    }

    // 清理 curl 资源（QCCurlHandleManager 的 RAII 机制会自动清理）
}

void QCWebSocketPrivate::setState(QCWebSocket::State newState)
{
    if (state == newState) {
        return;
    }

    state = newState;

    Q_Q(QCWebSocket);
    emit q->stateChanged(newState);
}

void QCWebSocketPrivate::processIncomingData()
{
    Q_Q(QCWebSocket);

    // 如果未连接，不处理数据
    if (state != QCWebSocket::State::Connected) {
        return;
    }

    char buffer[4096];
    size_t received = 0;
    const struct curl_ws_frame *meta = nullptr;

    // 尝试接收 WebSocket 数据
    CURLcode res = curl_ws_recv(curlManager.handle(),
                                 buffer,
                                 sizeof(buffer),
                                 &received,
                                 &meta);

    // CURLE_AGAIN 表示无数据可读（非阻塞模式）
    if (res == CURLE_AGAIN) {
        return;
    }

    // 其他错误
    if (res != CURLE_OK) {
        handleError(QString::fromUtf8(curl_easy_strerror(res)));
        return;
    }

    // 没有接收到数据
    if (received == 0) {
        return;
    }

    QByteArray data(buffer, static_cast<int>(received));

    // 检查帧元数据
    if (!meta) {
        qWarning() << "QCWebSocket: Received data but no metadata";
        return;
    }

    // 如果压缩已协商，解压缩数据（仅对 TEXT 和 BINARY 帧）
    if (compressionNegotiated && (meta->flags & (CURLWS_TEXT | CURLWS_BINARY))) {
        QByteArray decompressed;
        if (decompressData(data, decompressed)) {
            receivedBytesCompressed += data.size();
            receivedBytesRaw += decompressed.size();
            data = decompressed;
            qDebug() << "QCWebSocket: Decompressed" << receivedBytesCompressed 
                     << "bytes to" << decompressed.size() << "bytes";
        } else {
            // 解压缩失败，使用原始数据（可能未压缩）
            qWarning() << "QCWebSocket: Decompression failed, using raw data";
        }
    }

    // 处理文本帧
    if (meta->flags & CURLWS_TEXT) {
        // 使用 bytesleft 判断是否还有更多分片
        // bytesleft > 0 表示还有更多分片要接收
        // bytesleft == 0 表示这是最后一个分片（或完整消息）
        
        if (meta->bytesleft > 0) {
            // 还有更多分片，继续累积
            fragmentBuffer.append(data);
            qDebug() << "QCWebSocket: Text fragment received, offset:" << meta->offset
                     << "received:" << received << "bytesleft:" << meta->bytesleft
                     << "buffer size:" << fragmentBuffer.size();
        } else {
            // 最后一个分片或完整消息
            if (!fragmentBuffer.isEmpty()) {
                // 这是分片消息的最后一帧
                fragmentBuffer.append(data);
                qDebug() << "QCWebSocket: Text message complete, total size:" 
                         << fragmentBuffer.size() << "bytes";
                emit q->textMessageReceived(QString::fromUtf8(fragmentBuffer));
                fragmentBuffer.clear();
            } else {
                // 完整的单帧消息
                emit q->textMessageReceived(QString::fromUtf8(data));
            }
        }
    }
    // 处理二进制帧
    else if (meta->flags & CURLWS_BINARY) {
        // 使用 bytesleft 判断是否还有更多分片
        if (meta->bytesleft > 0) {
            // 还有更多分片，继续累积
            fragmentBuffer.append(data);
            qDebug() << "QCWebSocket: Binary fragment received, offset:" << meta->offset
                     << "received:" << received << "bytesleft:" << meta->bytesleft
                     << "buffer size:" << fragmentBuffer.size();
        } else {
            // 最后一个分片或完整消息
            if (!fragmentBuffer.isEmpty()) {
                // 这是分片消息的最后一帧
                fragmentBuffer.append(data);
                qDebug() << "QCWebSocket: Binary message complete, total size:" 
                         << fragmentBuffer.size() << "bytes";
                emit q->binaryMessageReceived(fragmentBuffer);
                fragmentBuffer.clear();
            } else {
                // 完整的单帧消息
                emit q->binaryMessageReceived(data);
            }
        }
    }
    // 处理 Pong 响应
    else if (meta->flags & CURLWS_PONG) {
        emit q->pongReceived(data);
    }
    // 处理 Close 帧
    else if (meta->flags & CURLWS_CLOSE) {
        // 从 Close 帧中提取关闭状态码
        // libcurl 会将状态码放在前 2 个字节（大端序）
        if (received >= 2) {
            lastCloseCode = (static_cast<unsigned char>(buffer[0]) << 8) | 
                           static_cast<unsigned char>(buffer[1]);
        } else {
            lastCloseCode = 1006;  // AbnormalClosure
        }
        
        qDebug() << "QCWebSocket: Received Close frame, code:" << lastCloseCode;
        setState(QCWebSocket::State::Closing);
        cleanupConnection();
    }
    // 处理 Ping 帧（通常 libcurl 会自动响应 Pong）
    else if (meta->flags & CURLWS_PING) {
        // libcurl 通常会自动发送 Pong 响应
        qDebug() << "QCWebSocket: Received Ping frame";
    }
}

void QCWebSocketPrivate::handleError(const QString &error)
{
    errorString = error;
    setState(QCWebSocket::State::Unconnected);

    Q_Q(QCWebSocket);
    emit q->errorOccurred(error);
}

void QCWebSocketPrivate::cleanupConnection()
{
    // 停止并删除 Socket 通知器
    if (socketReadNotifier) {
        socketReadNotifier->setEnabled(false);
        delete socketReadNotifier;
        socketReadNotifier = nullptr;
    }

    // 停止接收定时器
    if (receiveTimer) {
        receiveTimer->stop();
    }

    eventDrivenMode = false;

    // 清空缓冲区
    receiveBuffer.clear();
    fragmentBuffer.clear();

    // 处理重连逻辑
    // 如果启用了重连且应该重连，则调用 handleDisconnection
    if (lastCloseCode != 0 && reconnectPolicy.shouldRetry(lastCloseCode, reconnectAttemptCount + 1)) {
        handleDisconnection(lastCloseCode);
    } else {
        // 不重连，正常关闭
        setState(QCWebSocket::State::Closed);
        Q_Q(QCWebSocket);
        emit q->disconnected();
    }
}

qint64 QCWebSocketPrivate::sendFrame(const QByteArray &data, unsigned int flags)
{
    if (state != QCWebSocket::State::Connected) {
        qWarning() << "QCWebSocket: Cannot send data, not connected";
        return -1;
    }

    // 如果压缩已协商，压缩数据
    QByteArray dataToSend = data;
    qint64 originalSize = data.size();
    
    if (compressionNegotiated && (flags & (CURLWS_TEXT | CURLWS_BINARY))) {
        QByteArray compressed;
        if (compressData(data, compressed)) {
            dataToSend = compressed;
            sentBytesRaw += originalSize;
            sentBytesCompressed += compressed.size();
            qDebug() << "QCWebSocket: Compressed" << originalSize << "bytes to" << compressed.size() << "bytes";
        } else {
            // 压缩失败，发送原始数据
            qWarning() << "QCWebSocket: Compression failed, sending uncompressed";
        }
    }

    size_t sent = 0;
    CURLcode res = curl_ws_send(curlManager.handle(),
                                 dataToSend.constData(),
                                 dataToSend.size(),
                                 &sent,
                                 0,  // fragsize = 0 表示不分片
                                 flags);

    if (res != CURLE_OK) {
        handleError(QString::fromUtf8(curl_easy_strerror(res)));
        return -1;
    }

    // 返回原始大小（对用户透明）
    return originalSize;
}

// ============================================================================
// QCWebSocket 公共接口实现
// ============================================================================

QCWebSocket::QCWebSocket(const QUrl &url, QObject *parent)
    : QObject(parent)
    , d_ptr(new QCWebSocketPrivate(this))
{
    Q_D(QCWebSocket);
    d->url = url;
}

QCWebSocket::~QCWebSocket()
{
    Q_D(QCWebSocket);

    // 如果还在连接状态，先关闭
    if (d->state == State::Connected || d->state == State::Connecting) {
        abort();
    }

    delete d_ptr;
}

void QCWebSocket::open()
{
    Q_D(QCWebSocket);

    // 如果已经连接或正在连接，直接返回
    if (d->state != State::Unconnected) {
        qWarning() << "QCWebSocket: Already connected or connecting";
        return;
    }

    d->setState(State::Connecting);

    // 使用 QTimer 延迟执行连接，避免阻塞事件循环
    // 这样可以让信号正常发射和处理
    QTimer::singleShot(0, this, [this, d]() {
        CURL *curl = d->curlManager.handle();
        if (!curl) {
            d->handleError(tr("Failed to initialize curl handle"));
            return;
        }

        // 设置 WebSocket URL
        QByteArray urlBytes = d->url.toString().toUtf8();
        curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());

        // 使用 CONNECT_ONLY 模式建立 WebSocket 连接
        // 2L 表示 WebSocket 模式（libcurl 7.86.0+）
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

        // 设置连接超时（10 秒）
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        // 应用 SSL/TLS 配置（仅对 wss:// 协议）
        if (d->url.scheme() == "wss") {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 
                           d->sslConfig.verifyPeer ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 
                           d->sslConfig.verifyHost ? 2L : 0L);
            
            if (!d->sslConfig.caCertPath.isEmpty()) {
                QByteArray caPath = d->sslConfig.caCertPath.toUtf8();
                curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.constData());
            }
            
            if (!d->sslConfig.clientCertPath.isEmpty()) {
                QByteArray certPath = d->sslConfig.clientCertPath.toUtf8();
                curl_easy_setopt(curl, CURLOPT_SSLCERT, certPath.constData());
            }
            
            if (!d->sslConfig.clientKeyPath.isEmpty()) {
                QByteArray keyPath = d->sslConfig.clientKeyPath.toUtf8();
                curl_easy_setopt(curl, CURLOPT_SSLKEY, keyPath.constData());
            }
            
            if (!d->sslConfig.clientKeyPassword.isEmpty()) {
                QByteArray keyPass = d->sslConfig.clientKeyPassword.toUtf8();
                curl_easy_setopt(curl, CURLOPT_KEYPASSWD, keyPass.constData());
            }
        }

        // 添加 WebSocket 压缩扩展请求头
        struct curl_slist *headers = nullptr;
        if (d->compressionConfig.enabled) {
            QString extHeader = d->compressionConfig.toExtensionHeader();
            if (!extHeader.isEmpty()) {
                QString headerStr = QString("Sec-WebSocket-Extensions: %1").arg(extHeader);
                QByteArray headerBytes = headerStr.toUtf8();
                headers = curl_slist_append(headers, headerBytes.constData());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                
                qDebug() << "QCWebSocket: Requesting compression:" << extHeader;
            }
        }

        // 执行连接（这是阻塞调用，但在 QTimer 回调中不会阻塞 open() 方法本身）
        CURLcode res = curl_easy_perform(curl);
        
        // 清理自定义头
        if (headers) {
            curl_slist_free_all(headers);
        }
        if (res != CURLE_OK) {
            QString errorMsg = QString::fromUtf8(curl_easy_strerror(res));
            
            // 检测 SSL 错误并发射详细信号
            if (res == CURLE_SSL_CONNECT_ERROR || 
                res == CURLE_PEER_FAILED_VERIFICATION ||
                res == CURLE_SSL_CERTPROBLEM ||
                res == CURLE_SSL_CIPHER ||
                res == CURLE_SSL_CACERT) {
                
                QStringList sslErrorList;
                sslErrorList << errorMsg;
                
                // 获取 SSL 验证结果详情
                long sslVerifyResult = 0;
                curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &sslVerifyResult);
                if (sslVerifyResult != 0) {
                    sslErrorList << QString("SSL 验证结果码: %1").arg(sslVerifyResult);
                }
                
                // 发射详细 SSL 错误信号
                emit sslErrorsDetailed(sslErrorList);
                emit sslErrors(sslErrorList);  // 兼容旧信号
            }
            
            // 连接失败映射到 CloseCode
            if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST) {
                d->lastCloseCode = 1006;
            } else {
                d->lastCloseCode = 1002;
            }
            
            d->handleError(errorMsg);
            
            // 尝试重连
            if (d->reconnectPolicy.shouldRetry(d->lastCloseCode, d->reconnectAttemptCount + 1)) {
                d->cleanupConnection();
            }
            return;
        }

        // 连接成功
        d->setState(State::Connected);
        
        // 重置重连状态
        d->reconnectAttemptCount = 0;
        d->lastCloseCode = 0;
        
        // 检查服务器是否接受了压缩扩展
        // 注意：libcurl 的 CONNECT_ONLY=2 模式不提供简单的响应头访问
        // 这里我们假设如果请求了压缩且连接成功，压缩就可能被启用
        // TODO: 更精确的方法是使用 CURLOPT_HEADERFUNCTION 回调捕获响应头
        if (d->compressionConfig.enabled) {
            // 简化实现：假设压缩协商成功
            // 实际应用中需要解析响应头的 Sec-WebSocket-Extensions
            d->compressionNegotiated = true;
            qDebug() << "QCWebSocket: Compression negotiated (simplified)";
        }
        
        emit connected();

        // 尝试启用事件驱动接收（替代原有的 receiveTimer 轮询）
        d->enableEventDrivenReceive();
    });
}

void QCWebSocket::close(CloseCode closeCode, const QString &reason)
{
    Q_D(QCWebSocket);

    if (d->state != State::Connected) {
        qWarning() << "QCWebSocket: Not connected, cannot close";
        return;
    }

    d->setState(State::Closing);

    // 构造 Close 帧的载荷（2 字节状态码 + 可选的原因字符串）
    QByteArray payload;
    quint16 code = static_cast<quint16>(closeCode);
    payload.append(static_cast<char>((code >> 8) & 0xFF));  // 高字节
    payload.append(static_cast<char>(code & 0xFF));         // 低字节

    if (!reason.isEmpty()) {
        QByteArray reasonBytes = reason.toUtf8();
        // RFC 6455: 原因字符串最大 123 字节（125 - 2 字节状态码）
        if (reasonBytes.size() > 123) {
            reasonBytes = reasonBytes.left(123);
        }
        payload.append(reasonBytes);
    }

    // 发送 Close 帧
    size_t sent = 0;
    CURLcode res = curl_ws_send(d->curlManager.handle(),
                                 payload.constData(),
                                 payload.size(),
                                 &sent,
                                 0,
                                 CURLWS_CLOSE);

    if (res != CURLE_OK) {
        qWarning() << "QCWebSocket: Failed to send close frame:" << curl_easy_strerror(res);
    }

    // 延迟清理连接资源，确保 disconnected() 信号在事件循环中发射
    // 这样测试代码可以正确捕获信号
    QTimer::singleShot(0, this, [d]() {
        d->cleanupConnection();
    });
}

void QCWebSocket::abort()
{
    Q_D(QCWebSocket);

    // 立即清理连接，不发送 Close 帧
    d->cleanupConnection();
}

qint64 QCWebSocket::sendTextMessage(const QString &message)
{
    Q_D(QCWebSocket);
    QByteArray data = message.toUtf8();
    return d->sendFrame(data, CURLWS_TEXT);
}

qint64 QCWebSocket::sendBinaryMessage(const QByteArray &data)
{
    Q_D(QCWebSocket);
    return d->sendFrame(data, CURLWS_BINARY);
}

void QCWebSocket::ping(const QByteArray &payload)
{
    Q_D(QCWebSocket);

    if (d->state != State::Connected) {
        qWarning() << "QCWebSocket: Cannot send ping, not connected";
        return;
    }

    // Ping 帧的载荷最大 125 字节
    QByteArray data = payload;
    if (data.size() > 125) {
        data = data.left(125);
    }

    size_t sent = 0;
    CURLcode res = curl_ws_send(d->curlManager.handle(),
                                 data.constData(),
                                 data.size(),
                                 &sent,
                                 0,
                                 CURLWS_PING);

    if (res != CURLE_OK) {
        qWarning() << "QCWebSocket: Failed to send ping:" << curl_easy_strerror(res);
    }
}

QCWebSocket::State QCWebSocket::state() const
{
    Q_D(const QCWebSocket);
    return d->state;
}

QUrl QCWebSocket::url() const
{
    Q_D(const QCWebSocket);
    return d->url;
}

QString QCWebSocket::errorString() const
{
    Q_D(const QCWebSocket);
    return d->errorString;
}

bool QCWebSocket::isValid() const
{
    Q_D(const QCWebSocket);
    return d->state == State::Connected;
}

// ============================================================================
// 自动重连配置（v2.4.0）
// ============================================================================

void QCWebSocket::setReconnectPolicy(const QCWebSocketReconnectPolicy &policy)
{
    Q_D(QCWebSocket);
    d->reconnectPolicy = policy;
}

QCWebSocketReconnectPolicy QCWebSocket::reconnectPolicy() const
{
    Q_D(const QCWebSocket);
    return d->reconnectPolicy;
}

// ============================================================================
// SSL/TLS 配置（v2.4.1）
// ============================================================================

void QCWebSocket::setSslConfig(const QCNetworkSslConfig &config)
{
    Q_D(QCWebSocket);
    d->sslConfig = config;
}

QCNetworkSslConfig QCWebSocket::sslConfig() const
{
    Q_D(const QCWebSocket);
    return d->sslConfig;
}

// ============================================================================
// Public 接口：压缩配置（v2.18.0）
// ============================================================================

void QCWebSocket::setCompressionConfig(const QCWebSocketCompressionConfig &config)
{
    Q_D(QCWebSocket);
    d->compressionConfig = config;
    
    // 如果已连接，发出警告
    if (d->state != State::Unconnected) {
        qWarning() << "QCWebSocket::setCompressionConfig: 压缩配置必须在 open() 之前设置";
    }
}

QCWebSocketCompressionConfig QCWebSocket::compressionConfig() const
{
    Q_D(const QCWebSocket);
    return d->compressionConfig;
}

bool QCWebSocket::isCompressionNegotiated() const
{
    Q_D(const QCWebSocket);
    return d->compressionNegotiated;
}

QString QCWebSocket::compressionStats() const
{
    Q_D(const QCWebSocket);
    
    if (!d->compressionNegotiated) {
        return QStringLiteral("Compression not negotiated");
    }
    
    // 计算压缩率
    double sentRatio = 0.0;
    if (d->sentBytesRaw > 0) {
        sentRatio = 100.0 * (1.0 - static_cast<double>(d->sentBytesCompressed) / d->sentBytesRaw);
    }
    
    double recvRatio = 0.0;
    if (d->receivedBytesRaw > 0) {
        recvRatio = 100.0 * (1.0 - static_cast<double>(d->receivedBytesCompressed) / d->receivedBytesRaw);
    }
    
    return QString("Sent: %1 bytes -> %2 bytes (%.1f%% compression)\n"
                   "Recv: %3 bytes <- %4 bytes (%.1f%% compression)")
            .arg(d->sentBytesRaw)
            .arg(d->sentBytesCompressed)
            .arg(sentRatio)
            .arg(d->receivedBytesRaw)
            .arg(d->receivedBytesCompressed)
            .arg(recvRatio);
}

// ============================================================================
// QCWebSocketPrivate 事件驱动接收实现（v2.4.2）
// ============================================================================

curl_socket_t QCWebSocketPrivate::getSocketDescriptor()
{
    curl_socket_t sockfd = CURL_SOCKET_BAD;
    
    CURL *curl = curlManager.handle();
    if (!curl) {
        return CURL_SOCKET_BAD;
    }
    
    // 从 curl 句柄获取活动 socket（libcurl 7.45.0+）
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd);
    if (res != CURLE_OK || sockfd == CURL_SOCKET_BAD) {
        qWarning() << "QCWebSocket: 无法获取 socket 描述符:" 
                   << curl_easy_strerror(res);
        return CURL_SOCKET_BAD;
    }
    
    qDebug() << "QCWebSocket: 获取到 socket 描述符:" << sockfd;
    return sockfd;
}

void QCWebSocketPrivate::enableEventDrivenReceive()
{
    Q_Q(QCWebSocket);
    
    // 尝试获取 socket 描述符
    curl_socket_t sockfd = getSocketDescriptor();
    
    if (sockfd != CURL_SOCKET_BAD) {
        // 创建 QSocketNotifier（监听读事件）
        socketReadNotifier = new QSocketNotifier(sockfd, 
                                                 QSocketNotifier::Read, 
                                                 q);
        
        // 连接信号：socket 可读时调用 processIncomingData()
        QObject::connect(socketReadNotifier, &QSocketNotifier::activated,
                        q, [this]() {
            processIncomingData();
        });
        
        socketReadNotifier->setEnabled(true);
        eventDrivenMode = true;
        
        qInfo() << "QCWebSocket: 事件驱动模式已启用（socket fd:" 
                << sockfd << "）延迟 <1ms";
    } else {
        // 降级到轮询模式
        fallbackToPollingMode();
    }
}

void QCWebSocketPrivate::fallbackToPollingMode()
{
    Q_Q(QCWebSocket);
    
    // 创建 QTimer（每 50ms 轮询）
    if (!receiveTimer) {
        receiveTimer = new QTimer(q);
        QObject::connect(receiveTimer, &QTimer::timeout, q, [this]() {
            processIncomingData();
        });
    }
    receiveTimer->start(50);
    eventDrivenMode = false;
    
    qWarning() << "QCWebSocket: 降级到轮询模式（50ms），延迟较高";
}

// ============================================================================
// QCWebSocketPrivate 重连实现（v2.4.0）
// ============================================================================

void QCWebSocketPrivate::handleDisconnection(int closeCode)
{
    lastCloseCode = closeCode;
    
    // 检查是否应该重连
    if (reconnectPolicy.shouldRetry(closeCode, reconnectAttemptCount + 1)) {
        reconnectAttemptCount++;
        
        // 计算延迟时间
        auto delay = reconnectPolicy.delayForAttempt(reconnectAttemptCount);
        
        qDebug() << "QCWebSocket: Scheduling reconnect attempt" << reconnectAttemptCount
                 << "in" << delay.count() << "ms, close code:" << closeCode;
        
        // 发射重连尝试信号
        Q_Q(QCWebSocket);
        emit q->reconnectAttempt(reconnectAttemptCount, closeCode);
        
        // 设置延迟重连定时器
        if (!reconnectTimer) {
            reconnectTimer = new QTimer(q);
            reconnectTimer->setSingleShot(true);
            QObject::connect(reconnectTimer, &QTimer::timeout, q, [this]() {
                attemptReconnect();
            });
        }
        reconnectTimer->start(delay.count());
    } else {
        // 不重连或达到最大重连次数
        qDebug() << "QCWebSocket: Not reconnecting, close code:" << closeCode
                 << "attempts:" << reconnectAttemptCount;
        
        // 重置重连状态
        reconnectAttemptCount = 0;
        lastCloseCode = 0;
        
        setState(QCWebSocket::State::Closed);
        Q_Q(QCWebSocket);
        emit q->disconnected();
    }
}

void QCWebSocketPrivate::attemptReconnect()
{
    qDebug() << "QCWebSocket: Attempting reconnect, attempt" << reconnectAttemptCount;
    
    // 重新连接
    Q_Q(QCWebSocket);
    q->open();
}

// ============================================================================
// 压缩/解压缩实现（v2.18.0）
// ============================================================================

bool QCWebSocketPrivate::compressData(const QByteArray &input, QByteArray &output)
{
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    
    int windowBits = -compressionConfig.clientMaxWindowBits;  // 负值表示无 zlib 头
    int ret = deflateInit2(&stream, compressionConfig.compressionLevel, Z_DEFLATED,
                          windowBits, 8, Z_DEFAULT_STRATEGY);
    
    if (ret != Z_OK) {
        qWarning() << "QCWebSocket: deflateInit2 failed:" << ret;
        return false;
    }
    
    output.resize(input.size() + 128);  // 预分配空间
    
    stream.avail_in = input.size();
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_out = output.size();
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    
    ret = deflate(&stream, Z_FINISH);
    
    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        qWarning() << "QCWebSocket: deflate failed:" << ret;
        return false;
    }
    
    output.resize(stream.total_out);
    
    // RFC 7692: 移除末尾的 0x00 0x00 0xFF 0xFF
    if (output.size() >= 4 && 
        output[output.size()-4] == 0x00 &&
        output[output.size()-3] == 0x00 &&
        output[output.size()-2] == static_cast<char>(0xFF) &&
        output[output.size()-1] == static_cast<char>(0xFF)) {
        output.chop(4);
    }
    
    deflateEnd(&stream);
    return true;
}

bool QCWebSocketPrivate::decompressData(const QByteArray &input, QByteArray &output)
{
    // RFC 7692: 添加末尾的 0x00 0x00 0xFF 0xFF
    QByteArray inputWithTail = input;
    inputWithTail.append("\x00\x00\xFF\xFF", 4);
    
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    
    int windowBits = -compressionConfig.serverMaxWindowBits;  // 负值表示无 zlib 头
    int ret = inflateInit2(&stream, windowBits);
    
    if (ret != Z_OK) {
        qWarning() << "QCWebSocket: inflateInit2 failed:" << ret;
        return false;
    }
    
    output.resize(inputWithTail.size() * 4);  // 预分配4倍空间
    
    stream.avail_in = inputWithTail.size();
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(inputWithTail.constData()));
    stream.avail_out = output.size();
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    
    ret = inflate(&stream, Z_FINISH);
    
    if (ret != Z_STREAM_END) {
        inflateEnd(&stream);
        qWarning() << "QCWebSocket: inflate failed:" << ret;
        return false;
    }
    
    output.resize(stream.total_out);
    inflateEnd(&stream);
    return true;
}

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCURL_WEBSOCKET_SUPPORT
