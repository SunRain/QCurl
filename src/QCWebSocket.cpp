#include "QCWebSocket.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include "QCWebSocket_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCWebSocketCloseCode_p.h"
#include "private/QCWebSocketCurlOptions_p.h"

#include <QDebug>

#include <zlib.h>

namespace QCurl {

namespace {

namespace WsClose = Internal::WebSocketCloseCode;
namespace WsCurl  = Internal::WebSocketCurlOptions;

constexpr int kReceiveDrainIterationLimit = 1024;
constexpr int kReceiveBufferBytes         = 4096;
constexpr int kPollingIntervalMs          = 50;
constexpr int kCompressionScratchBytes    = 128;
constexpr int kDeflateMemoryLevel         = 8;

bool isConfigurationLockedState(QCWebSocket::State state)
{
    return state == QCWebSocket::State::Connecting || state == QCWebSocket::State::Connected
           || state == QCWebSocket::State::Closing;
}

bool failOption(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

// ==================
// QCWebSocketPrivate 实现
// ==================

QCWebSocketPrivate::QCWebSocketPrivate(QCWebSocket *parent)
    : q_ptr(parent)
{}

QCWebSocketPrivate::~QCWebSocketPrivate()
{
    if (socketReadNotifier) {
        socketReadNotifier->setEnabled(false);
        QObject::disconnect(socketReadNotifier, nullptr, q_ptr, nullptr);
        socketReadNotifier->deleteLater();
        socketReadNotifier = nullptr;
    }

    // 停止接收定时器
    if (receiveTimer) {
        receiveTimer->stop();
        QObject::disconnect(receiveTimer, nullptr, q_ptr, nullptr);
        receiveTimer->deleteLater();
        receiveTimer = nullptr;
    }

    if (reconnectTimer) {
        reconnectTimer->stop();
        QObject::disconnect(reconnectTimer, nullptr, q_ptr, nullptr);
        reconnectTimer->deleteLater();
        reconnectTimer = nullptr;
    }

    clearRequestHeaders();

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

    // 事件驱动下
    // libcurl 可能预读
    // 多段数据到缓冲
    // 只 recv 一次会死等
    // bytesleft>0 但
    // 不再触发可读事件
    // 需循环至 CURLE_AGAIN
    for (int i = 0; i < kReceiveDrainIterationLimit; ++i) {
        char buffer[kReceiveBufferBytes];
        size_t received                  = 0;
        const struct curl_ws_frame *meta = nullptr;

        CURLcode res = curl_ws_recv(curlManager.handle(), buffer, sizeof(buffer), &received, &meta);

        if (res == CURLE_AGAIN) {
            return;
        }

        if (res != CURLE_OK) {
            handleError(QString::fromUtf8(curl_easy_strerror(res)));
            return;
        }

        if (!meta) {
            qWarning() << "QCWebSocket: Received data but no metadata";
            continue;
        }

        QByteArray data(buffer, static_cast<int>(received));

        // 如果压缩已协商，解压缩数据（仅对 TEXT 和 BINARY 帧）
        if (compressionNegotiated && (meta->flags & (CURLWS_TEXT | CURLWS_BINARY))) {
            QByteArray decompressed;
            if (decompressData(data, decompressed)) {
                receivedBytesCompressed += data.size();
                receivedBytesRaw += decompressed.size();
                data = decompressed;
                qDebug() << "QCWebSocket: Decompressed" << receivedBytesCompressed << "bytes to"
                         << decompressed.size() << "bytes";
            } else {
                qWarning() << "QCWebSocket: Decompression failed, using raw data";
            }
        }

        // TEXT/BINARY：既可能是“单帧消息”，也可能是“多帧分片消息”（CURLWS_CONT）或“单帧大 payload 的分段读取”（bytesleft）。
        const bool isText   = (meta->flags & CURLWS_TEXT) != 0;
        const bool isBinary = (meta->flags & CURLWS_BINARY) != 0;
        if (isText || isBinary) {
            const unsigned int msgType = isText ? CURLWS_TEXT : CURLWS_BINARY;

            // 进入/继续一条分片消息（包含：CURLWS_CONT 多帧分片；或 bytesleft>0 的单帧分段读取）。
            if (fragmentTypeFlags == 0) {
                fragmentTypeFlags = msgType;
            } else if (fragmentTypeFlags != msgType) {
                // 防御：理论上不应发生（协议错误/实现差异）。避免混合污染，重置缓冲。
                qWarning()
                    << "QCWebSocket: Mixed fragmented message types; resetting fragment buffer";
                fragmentBuffer.clear();
                fragmentTypeFlags = msgType;
            }

            fragmentBuffer.append(data);

            const bool frameComplete    = (meta->bytesleft <= 0);
            const bool messageContinues = (meta->flags & CURLWS_CONT) != 0;

            // 仅当“当前帧已完整接收”且“本消息已结束”时才发射 message signal。
            if (frameComplete && !messageContinues) {
                if (fragmentTypeFlags == CURLWS_TEXT) {
                    emit q->textMessageReceived(QString::fromUtf8(fragmentBuffer));
                } else {
                    emit q->binaryMessageReceived(fragmentBuffer);
                }
                fragmentBuffer.clear();
                fragmentTypeFlags = 0;
            }
        }
        // 处理 Pong 响应
        else if (meta->flags & CURLWS_PONG) {
            emit q->pongReceived(data);
        }
        // 处理 Close 帧
        else if (meta->flags & CURLWS_CLOSE) {
            QString closeReason;
            if (received >= WsClose::kCloseCodePayloadBytes) {
                const auto codeHighByte = static_cast<unsigned char>(buffer[0]);
                const auto codeLowByte  = static_cast<unsigned char>(buffer[1]);
                const int wireCloseCode = (codeHighByte << WsClose::kCloseCodeByteShift)
                                          | codeLowByte;
                lastWireCloseCode       = wireCloseCode;
                if (WsClose::tryFromWire(wireCloseCode, &lastCloseCode)) {
                    hasRetriableCloseCode = true;
                } else if (WsClose::isApplication(wireCloseCode)) {
                    hasRetriableCloseCode = false;
                } else {
                    handleError(
                        QStringLiteral("收到非法 WebSocket close code: %1").arg(wireCloseCode));
                    cleanupConnection();
                    return;
                }
                if (received > WsClose::kCloseCodePayloadBytes) {
                    const auto reasonBytes = static_cast<int>(received
                                                              - WsClose::kCloseCodePayloadBytes);
                    closeReason = QString::fromUtf8(buffer + WsClose::kCloseCodePayloadBytes,
                                                    reasonBytes);
                }
            } else {
                lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
                lastWireCloseCode     = static_cast<int>(lastCloseCode);
                hasRetriableCloseCode = true;
            }

            qDebug() << "QCWebSocket: Received Close frame, code:" << lastWireCloseCode;
            emit q->closeReceived(lastWireCloseCode, closeReason);
            setState(QCWebSocket::State::Closing);
            cleanupConnection();
            return;
        }
        // 处理 Ping 帧（通常 libcurl 会自动响应 Pong）
        else if (meta->flags & CURLWS_PING) {
            emit q->pingReceived(data);
            qDebug() << "QCWebSocket: Received Ping frame";
        }
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
    clearRequestHeaders();

    // 停止并删除 Socket 通知器
    if (socketReadNotifier) {
        socketReadNotifier->setEnabled(false);
        QObject::disconnect(socketReadNotifier, nullptr, q_ptr, nullptr);
        socketReadNotifier->deleteLater();
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
    fragmentTypeFlags = 0;

    // 处理重连逻辑
    // 如果启用了重连且应该重连，则调用 handleDisconnection
    if (hasRetriableCloseCode
        && options.reconnectPolicy().shouldRetry(lastCloseCode, reconnectAttemptCount + 1)) {
        handleDisconnection(lastCloseCode);
    } else {
        // 不重连，正常关闭
        setState(QCWebSocket::State::Closed);
        Q_Q(QCWebSocket);
        emit q->disconnected();
    }
}

void QCWebSocketPrivate::clearRequestHeaders()
{
    CURL *curl = curlManager.handle();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
    }

    if (requestHeaders) {
        curl_slist_free_all(requestHeaders);
        requestHeaders = nullptr;
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
    qint64 originalSize   = data.size();

    if (compressionNegotiated && (flags & (CURLWS_TEXT | CURLWS_BINARY))) {
        QByteArray compressed;
        if (compressData(data, compressed)) {
            dataToSend = compressed;
            sentBytesRaw += originalSize;
            sentBytesCompressed += compressed.size();
            qDebug() << "QCWebSocket: Compressed" << originalSize << "bytes to" << compressed.size()
                     << "bytes";
        } else {
            // 压缩失败，发送原始数据
            qWarning() << "QCWebSocket: Compression failed, sending uncompressed";
        }
    }

    size_t sent  = 0;
    CURLcode res = curl_ws_send(curlManager.handle(),
                                dataToSend.constData(),
                                dataToSend.size(),
                                &sent,
                                0, // fragsize = 0 表示不分片
                                flags);

    if (res != CURLE_OK) {
        handleError(QString::fromUtf8(curl_easy_strerror(res)));
        return -1;
    }

    // 返回原始大小（对用户透明）
    return originalSize;
}

// ==================
// QCWebSocket 公共接口实现
// ==================

QCWebSocket::QCWebSocket(const QUrl &url, const QCWebSocketOptions &options, QObject *parent)
    : QObject(parent)
    , d_ptr(new QCWebSocketPrivate(this))
{
    Q_D(QCWebSocket);
    d->url     = url;
    d->options = options;
}

QCWebSocket::~QCWebSocket()
{
    Q_D(QCWebSocket);

    // 如果还在连接状态，先关闭
    if (d->state == State::Connected || d->state == State::Connecting) {
        abort();
    }
}

void QCWebSocket::open()
{
    Q_D(QCWebSocket);

    // 允许在 Closed 状态下重新 open()，但仍禁止与活动连接/关闭握手并发。
    if (d->state == State::Connecting || d->state == State::Connected
        || d->state == State::Closing) {
        qWarning() << "QCWebSocket: Already connected, connecting, or closing";
        return;
    }

    d->errorString.clear();
    d->compressionNegotiated   = false;
    d->sentBytesRaw            = 0;
    d->sentBytesCompressed     = 0;
    d->receivedBytesCompressed = 0;
    d->receivedBytesRaw        = 0;

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
        if (!WsCurl::apply(Internal::CurlOptions::setString(curl, CURLOPT_URL, urlBytes.constData()),
                           d,
                           "CURLOPT_URL")) {
            return;
        }

        // 使用 CONNECT_ONLY 模式建立 WebSocket 连接
        if (!WsCurl::apply(Internal::CurlOptions::setConnectOnlyWebSocket(curl),
                           d,
                           "CURLOPT_CONNECT_ONLY")) {
            return;
        }

        // WebSocket 选项：自动 Pong 开关
        if (!d->options.autoPongEnabled()) {
            if (!WsCurl::apply(Internal::CurlOptions::setWebSocketNoAutoPong(curl),
                               d,
                               "CURLOPT_WS_OPTIONS")) {
                return;
            }
        }

        if (!WsCurl::apply(Internal::CurlOptions::setConnectTimeout(curl,
                                                                    d->options.connectTimeout()),
                           d,
                           "CURLOPT_CONNECTTIMEOUT_MS")) {
            return;
        }

        // 应用 SSL/TLS 配置（仅对 wss:// 协议）
        if (d->url.scheme() == QStringLiteral("wss")) {
            if (!WsCurl::apply(Internal::CurlOptions::setSslVerifyPeer(curl,
                                                                       d->options.sslConfig()
                                                                           .verifyPeer()),
                               d,
                               "CURLOPT_SSL_VERIFYPEER")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setSslVerifyHost(curl,
                                                                       d->options.sslConfig()
                                                                           .verifyHost()),
                               d,
                               "CURLOPT_SSL_VERIFYHOST")) {
                return;
            }

            d->sslCaInfoUtf8      = d->options.sslConfig().caCertPath().toUtf8();
            d->sslCertUtf8        = d->options.sslConfig().clientCertPath().toUtf8();
            d->sslKeyUtf8         = d->options.sslConfig().clientKeyPath().toUtf8();
            d->sslKeyPasswordUtf8 = d->options.sslConfig().clientKeyPassword().toUtf8();

            if (!WsCurl::apply(Internal::CurlOptions::setString(curl,
                                                                CURLOPT_CAINFO,
                                                                d->sslCaInfoUtf8.isEmpty()
                                                                    ? nullptr
                                                                    : d->sslCaInfoUtf8.constData()),
                               d,
                               "CURLOPT_CAINFO")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl,
                                                                CURLOPT_SSLCERT,
                                                                d->sslCertUtf8.isEmpty()
                                                                    ? nullptr
                                                                    : d->sslCertUtf8.constData()),
                               d,
                               "CURLOPT_SSLCERT")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl,
                                                                CURLOPT_SSLKEY,
                                                                d->sslKeyUtf8.isEmpty()
                                                                    ? nullptr
                                                                    : d->sslKeyUtf8.constData()),
                               d,
                               "CURLOPT_SSLKEY")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl,
                                                                CURLOPT_KEYPASSWD,
                                                                d->sslKeyPasswordUtf8.isEmpty()
                                                                    ? nullptr
                                                                    : d->sslKeyPasswordUtf8
                                                                          .constData()),
                               d,
                               "CURLOPT_KEYPASSWD")) {
                return;
            }
        } else {
            d->sslCaInfoUtf8.clear();
            d->sslCertUtf8.clear();
            d->sslKeyUtf8.clear();
            d->sslKeyPasswordUtf8.clear();
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl, CURLOPT_CAINFO, nullptr),
                               d,
                               "CURLOPT_CAINFO")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl, CURLOPT_SSLCERT, nullptr),
                               d,
                               "CURLOPT_SSLCERT")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl, CURLOPT_SSLKEY, nullptr),
                               d,
                               "CURLOPT_SSLKEY")) {
                return;
            }
            if (!WsCurl::apply(Internal::CurlOptions::setString(curl, CURLOPT_KEYPASSWD, nullptr),
                               d,
                               "CURLOPT_KEYPASSWD")) {
                return;
            }
        }

        // 添加 WebSocket 压缩扩展请求头
        d->clearRequestHeaders();
        if (d->options.compressionConfig().enabled()) {
            const QString extHeader = d->options.compressionConfig().toExtensionHeader();
            if (!extHeader.isEmpty()) {
                const QString headerStr = QStringLiteral("Sec-WebSocket-Extensions: %1")
                                              .arg(extHeader);
                d->requestHeaders = curl_slist_append(nullptr, headerStr.toUtf8().constData());
                if (!d->requestHeaders) {
                    d->handleError(tr("Failed to allocate WebSocket extension headers"));
                    return;
                }
                if (!WsCurl::apply(Internal::CurlOptions::setPointer(curl,
                                                                     CURLOPT_HTTPHEADER,
                                                                     d->requestHeaders),
                                   d,
                                   "CURLOPT_HTTPHEADER")) {
                    return;
                }

                qDebug() << "QCWebSocket: Requesting compression:" << extHeader;
            }
        }

        // 执行连接（这是阻塞调用，但在 QTimer 回调中不会阻塞 open() 方法本身）
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            QString errorMsg = QString::fromUtf8(curl_easy_strerror(res));

            // 检测 SSL 错误并发射详细信号
            if (res == CURLE_SSL_CONNECT_ERROR || res == CURLE_PEER_FAILED_VERIFICATION
                || res == CURLE_SSL_CERTPROBLEM || res == CURLE_SSL_CIPHER
                || res == CURLE_SSL_CACERT) {
                QStringList sslErrorList;
                sslErrorList << errorMsg;

                // 获取 SSL 验证结果详情
                long sslVerifyResult = 0;
                curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &sslVerifyResult);
                if (sslVerifyResult != 0) {
                    sslErrorList << QStringLiteral("SSL 验证结果码: %1").arg(sslVerifyResult);
                }

                emit sslErrorsDetailed(sslErrorList);
            }

            // 连接失败映射到 CloseCode
            if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST) {
                d->lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
                d->lastWireCloseCode     = static_cast<int>(d->lastCloseCode);
                d->hasRetriableCloseCode = true;
            } else {
                d->lastCloseCode         = QCWebSocket::CloseCode::ProtocolError;
                d->lastWireCloseCode     = static_cast<int>(d->lastCloseCode);
                d->hasRetriableCloseCode = true;
            }

            d->handleError(errorMsg);

            // 尝试重连
            if (d->options.reconnectPolicy().shouldRetry(d->lastCloseCode,
                                                         d->reconnectAttemptCount + 1)) {
                d->cleanupConnection();
            }
            return;
        }

        // 连接成功
        d->setState(State::Connected);

        // 重置重连状态
        d->reconnectAttemptCount = 0;
        d->lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
        d->lastWireCloseCode     = static_cast<int>(d->lastCloseCode);
        d->hasRetriableCloseCode = false;

        // 检查服务器是否接受了压缩扩展
        // 注意：libcurl 的 CONNECT_ONLY=2 模式不提供简单的响应头访问
        // 这里我们假设如果请求了压缩且连接成功，压缩就可能被启用
        // TODO: 更精确的方法是使用 CURLOPT_HEADERFUNCTION 回调捕获响应头
        if (d->options.compressionConfig().enabled()) {
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

    if (WsClose::isReserved(closeCode)) {
        d->errorString = QStringLiteral("不能发送保留 WebSocket close code: %1")
                             .arg(static_cast<int>(closeCode));
        emit errorOccurred(d->errorString);
        return;
    }

    d->setState(State::Closing);

    // 构造 Close 帧的载荷（2 字节状态码 + 可选的原因字符串）
    QByteArray payload;
    quint16 code = static_cast<quint16>(closeCode);
    payload.append(static_cast<char>((code >> WsClose::kCloseCodeByteShift) & 0xFF));
    payload.append(static_cast<char>(code & 0xFF));

    if (!reason.isEmpty()) {
        QByteArray reasonBytes = reason.toUtf8();
        // RFC 6455: 原因字符串最大 123 字节（125 - 2 字节状态码）
        if (reasonBytes.size() > WsClose::kCloseReasonMaxBytes) {
            reasonBytes = reasonBytes.left(WsClose::kCloseReasonMaxBytes);
        }
        payload.append(reasonBytes);
    }

    // 发送 Close 帧
    size_t sent  = 0;
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
    QTimer::singleShot(0, this, [d]() { d->cleanupConnection(); });
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
    if (data.size() > WsClose::kControlFrameMaxPayloadBytes) {
        data = data.left(WsClose::kControlFrameMaxPayloadBytes);
    }

    size_t sent  = 0;
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

void QCWebSocket::pong(const QByteArray &payload)
{
    Q_D(QCWebSocket);

    if (d->state != State::Connected) {
        qWarning() << "QCWebSocket: Cannot send pong, not connected";
        return;
    }

    QByteArray data = payload;
    if (data.size() > WsClose::kControlFrameMaxPayloadBytes) {
        data = data.left(WsClose::kControlFrameMaxPayloadBytes);
    }

    size_t sent  = 0;
    CURLcode res = curl_ws_send(d->curlManager.handle(),
                                data.constData(),
                                data.size(),
                                &sent,
                                0,
                                CURLWS_PONG);

    if (res != CURLE_OK) {
        qWarning() << "QCWebSocket: Failed to send pong:" << curl_easy_strerror(res);
    }
}

QCWebSocketOptions QCWebSocket::options() const
{
    Q_D(const QCWebSocket);
    return d->options;
}

bool QCWebSocket::setOptions(const QCWebSocketOptions &options, QString *error)
{
    Q_D(QCWebSocket);
    if (isConfigurationLockedState(d->state)) {
        return failOption(error, QStringLiteral("活动连接状态不允许修改 WebSocket 配置"));
    }

    d->options = options;
    return true;
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

    double sentRatio = 0.0;
    if (d->sentBytesRaw > 0) {
        sentRatio = 100.0 * (1.0 - static_cast<double>(d->sentBytesCompressed) / d->sentBytesRaw);
    }

    double recvRatio = 0.0;
    if (d->receivedBytesRaw > 0) {
        recvRatio = 100.0
                    * (1.0 - static_cast<double>(d->receivedBytesCompressed) / d->receivedBytesRaw);
    }

    return QStringLiteral("Sent: %1 bytes -> %2 bytes (%.1f%% compression)\n"
                          "Recv: %3 bytes <- %4 bytes (%.1f%% compression)")
        .arg(d->sentBytesRaw)
        .arg(d->sentBytesCompressed)
        .arg(sentRatio)
        .arg(d->receivedBytesRaw)
        .arg(d->receivedBytesCompressed)
        .arg(recvRatio);
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

// ==================
// QCWebSocketPrivate 事件驱动接收实现（v2.4.2）
// ==================

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
        qWarning() << "QCWebSocket: 无法获取 socket 描述符:" << curl_easy_strerror(res);
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
        socketReadNotifier = new QSocketNotifier(sockfd, QSocketNotifier::Read, q);

        // 连接信号：socket 可读时调用 processIncomingData()
        QObject::connect(socketReadNotifier, &QSocketNotifier::activated, q, [this]() {
            processIncomingData();
        });

        socketReadNotifier->setEnabled(true);
        eventDrivenMode = true;

        qInfo() << "QCWebSocket: 事件驱动模式已启用（socket fd:" << sockfd << "）延迟 <1ms";

        // 重要：启用 notifier 后立即尝试 drain 一次，避免“数据已到达但未再触发可读事件”的死等。
        processIncomingData();
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
        QObject::connect(receiveTimer, &QTimer::timeout, q, [this]() { processIncomingData(); });
    }
    receiveTimer->start(kPollingIntervalMs);
    eventDrivenMode = false;

    qWarning() << "QCWebSocket: 降级到轮询模式（50ms），延迟较高";
}

// ==================
// QCWebSocketPrivate 重连实现（Other Extras / Preview）
// ==================

void QCWebSocketPrivate::handleDisconnection(QCWebSocket::CloseCode closeCode)
{
    lastCloseCode         = closeCode;
    lastWireCloseCode     = static_cast<int>(closeCode);
    hasRetriableCloseCode = true;

    // 检查是否应该重连
    if (options.reconnectPolicy().shouldRetry(closeCode, reconnectAttemptCount + 1)) {
        reconnectAttemptCount++;

        // 计算延迟时间
        auto delay = options.reconnectPolicy().delayForAttempt(reconnectAttemptCount);

        qDebug() << "QCWebSocket: Scheduling reconnect attempt" << reconnectAttemptCount << "in"
                 << delay.count() << "ms, close code:" << WsClose::toWire(closeCode);

        // 发射重连尝试信号
        Q_Q(QCWebSocket);
        emit q->reconnectAttempt(reconnectAttemptCount, closeCode);

        // 设置延迟重连定时器
        if (!reconnectTimer) {
            reconnectTimer = new QTimer(q);
            reconnectTimer->setSingleShot(true);
            QObject::connect(reconnectTimer, &QTimer::timeout, q, [this]() { attemptReconnect(); });
        }
        reconnectTimer->start(delay.count());
    } else {
        // 不重连或达到最大重连次数
        qDebug() << "QCWebSocket: Not reconnecting, close code:" << WsClose::toWire(closeCode)
                 << "attempts:" << reconnectAttemptCount;

        // 重置重连状态
        reconnectAttemptCount = 0;
        lastCloseCode         = QCWebSocket::CloseCode::AbnormalClosure;
        lastWireCloseCode     = static_cast<int>(lastCloseCode);
        hasRetriableCloseCode = false;

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

// ==================
// 压缩/解压缩实现（Other Extras / Preview）
// ==================

bool QCWebSocketPrivate::compressData(const QByteArray &input, QByteArray &output)
{
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree  = Z_NULL;
    stream.opaque = Z_NULL;

    int windowBits = -options.compressionConfig().clientMaxWindowBits(); // 负值表示无 zlib 头
    int ret        = deflateInit2(&stream,
                                  options.compressionConfig().compressionLevel(),
                                  Z_DEFLATED,
                                  windowBits,
                                  kDeflateMemoryLevel,
                                  Z_DEFAULT_STRATEGY);

    if (ret != Z_OK) {
        qWarning() << "QCWebSocket: deflateInit2 failed:" << ret;
        return false;
    }

    output.resize(input.size() + kCompressionScratchBytes); // 预分配空间

    stream.avail_in  = input.size();
    stream.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_out = output.size();
    stream.next_out  = reinterpret_cast<Bytef *>(output.data());

    ret = deflate(&stream, Z_FINISH);

    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        qWarning() << "QCWebSocket: deflate failed:" << ret;
        return false;
    }

    output.resize(stream.total_out);

    // RFC 7692: 移除末尾的 0x00 0x00 0xFF 0xFF
    if (output.size() >= 4 && output[output.size() - 4] == 0x00 && output[output.size() - 3] == 0x00
        && output[output.size() - 2] == static_cast<char>(0xFF)
        && output[output.size() - 1] == static_cast<char>(0xFF)) {
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
    stream.zfree  = Z_NULL;
    stream.opaque = Z_NULL;

    int windowBits = -options.compressionConfig().serverMaxWindowBits(); // 负值表示无 zlib 头
    int ret        = inflateInit2(&stream, windowBits);

    if (ret != Z_OK) {
        qWarning() << "QCWebSocket: inflateInit2 failed:" << ret;
        return false;
    }

    output.resize(inputWithTail.size() * 4); // 预分配4倍空间

    stream.avail_in  = inputWithTail.size();
    stream.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(inputWithTail.constData()));
    stream.avail_out = output.size();
    stream.next_out  = reinterpret_cast<Bytef *>(output.data());

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

#endif // QCURL_WEBSOCKET_SUPPORT
