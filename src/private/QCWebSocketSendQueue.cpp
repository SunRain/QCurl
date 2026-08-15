#include "QCWebSocketSendQueue_p.h"

#include <algorithm>
#include <utility>

namespace QCurl::Internal {

namespace {

constexpr qsizetype kMaxQueuedFrames = 4096;

bool failQueue(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

} // namespace

bool QCWebSocketSendQueue::enqueue(const QByteArray &payload,
                                   unsigned int flags,
                                   bool closeFrame,
                                   qint64 maxPendingBytes,
                                   QString *error)
{
    if (closeFrame && hasCloseFrame()) {
        return true;
    }

    if (!closeFrame) {
        const qint64 payloadBytes = static_cast<qint64>(payload.size());
        if (payloadBytes > maxPendingBytes || m_pendingBytes > maxPendingBytes - payloadBytes) {
            return failQueue(error, QStringLiteral("WebSocket pending send queue 超过配置上限"));
        }
        if (m_frames.size() >= kMaxQueuedFrames) {
            return failQueue(error,
                             QStringLiteral("WebSocket pending send queue frame 数超过内部上限"));
        }
    }

    PendingFrame frame;
    frame.payload    = payload;
    frame.flags      = flags;
    frame.closeFrame = closeFrame;

    if (closeFrame) {
        prioritizeCloseFrame(frame);
    } else {
        m_frames.enqueue(std::move(frame));
        m_pendingBytes += payload.size();
    }
    return true;
}

QCWebSocketSendQueue::FlushResult QCWebSocketSendQueue::flushOne(const Sender &sender)
{
    if (m_frames.isEmpty()) {
        return {};
    }
    if (!sender) {
        return {FlushStatus::Error,
                CURLE_FAILED_INIT,
                QStringLiteral("WebSocket send queue 缺少发送函数")};
    }

    PendingFrame &frame    = m_frames.head();
    const qint64 remaining = static_cast<qint64>(frame.payload.size()) - frame.offset;
    if (remaining < 0) {
        return {FlushStatus::Error,
                CURLE_OK,
                QStringLiteral("WebSocket send queue offset 超出 payload")};
    }

    size_t sent           = 0;
    const CURLcode result = sender(frame.payload.constData() + frame.offset,
                                   static_cast<size_t>(remaining),
                                   &sent,
                                   frame.flags);
    if (sent > static_cast<size_t>(remaining)) {
        return {FlushStatus::Error,
                CURLE_OK,
                QStringLiteral("WebSocket send 返回了超出请求范围的 offset")};
    }

    frame.offset += static_cast<qint64>(sent);
    m_pendingBytes -= static_cast<qint64>(sent);

    if (result != CURLE_OK && result != CURLE_AGAIN) {
        return {FlushStatus::Error, result, {}};
    }

    const bool frameCompleted = frame.offset == frame.payload.size();
    if (frameCompleted) {
        const bool closeCompleted = frame.closeFrame;
        m_frames.dequeue();
        return {closeCompleted ? FlushStatus::CloseCompleted : FlushStatus::FrameCompleted,
                result,
                {}};
    }

    if (result == CURLE_AGAIN) {
        return {FlushStatus::WouldBlock, result, {}};
    }
    if (sent == 0) {
        return {FlushStatus::Error, CURLE_OK, QStringLiteral("WebSocket send 没有推进队列 offset")};
    }
    return {FlushStatus::Progress, CURLE_OK, {}};
}

void QCWebSocketSendQueue::clear() noexcept
{
    m_frames.clear();
    m_pendingBytes = 0;
}

bool QCWebSocketSendQueue::isEmpty() const noexcept
{
    return m_frames.isEmpty();
}

qsizetype QCWebSocketSendQueue::size() const noexcept
{
    return m_frames.size();
}

qint64 QCWebSocketSendQueue::pendingBytes() const noexcept
{
    return m_pendingBytes;
}

bool QCWebSocketSendQueue::hasCloseFrame() const noexcept
{
    return std::any_of(m_frames.cbegin(), m_frames.cend(), [](const PendingFrame &frame) {
        return frame.closeFrame;
    });
}

void QCWebSocketSendQueue::prioritizeCloseFrame(const PendingFrame &closeFrame)
{
    QQueue<PendingFrame> prioritized;
    qint64 retainedBytes = 0;

    if (!m_frames.isEmpty() && m_frames.head().offset > 0) {
        PendingFrame partial = std::move(m_frames.head());
        retainedBytes        = static_cast<qint64>(partial.payload.size()) - partial.offset;
        prioritized.enqueue(std::move(partial));
    }

    prioritized.enqueue(closeFrame);
    m_frames       = std::move(prioritized);
    m_pendingBytes = retainedBytes + closeFrame.payload.size();
}

} // namespace QCurl::Internal
