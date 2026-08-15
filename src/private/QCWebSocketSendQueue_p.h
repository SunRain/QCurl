/**
 * @file
 * @brief 声明 WebSocket 有界发送队列。
 */

#ifndef QCWEBSOCKETSENDQUEUE_P_H
#define QCWEBSOCKETSENDQUEUE_P_H

#include <QByteArray>
#include <QQueue>
#include <QString>

#include <curl/curl.h>
#include <functional>

namespace QCurl::Internal {

class QCWebSocketSendQueue final
{
public:
    enum class FlushStatus {
        Idle,
        Progress,
        WouldBlock,
        FrameCompleted,
        CloseCompleted,
        Error,
    };

    struct FlushResult
    {
        FlushStatus status = FlushStatus::Idle;
        CURLcode curlCode  = CURLE_OK;
        QString error;
    };

    using Sender
        = std::function<CURLcode(const char *data, size_t size, size_t *sent, unsigned int flags)>;

    [[nodiscard]] bool enqueue(const QByteArray &payload,
                               unsigned int flags,
                               bool closeFrame,
                               qint64 maxPendingBytes,
                               QString *error = nullptr);
    [[nodiscard]] FlushResult flushOne(const Sender &sender);

    void clear() noexcept;

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] qsizetype size() const noexcept;
    [[nodiscard]] qint64 pendingBytes() const noexcept;
    [[nodiscard]] bool hasCloseFrame() const noexcept;

private:
    struct PendingFrame
    {
        QByteArray payload;
        unsigned int flags = 0;
        qint64 offset      = 0;
        bool closeFrame    = false;
    };

    void prioritizeCloseFrame(const PendingFrame &closeFrame);

    QQueue<PendingFrame> m_frames;
    qint64 m_pendingBytes = 0;
};

} // namespace QCurl::Internal

#endif // QCWEBSOCKETSENDQUEUE_P_H
