#include "QCWebSocketCommandResult.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QtGlobal>

namespace QCurl {

QCWebSocketCommandResult QCWebSocketCommandResult::accepted(qint64 acceptedBytes) noexcept
{
    Q_ASSERT(acceptedBytes >= 0);
    QCWebSocketCommandResult result;
    result.m_acceptedBytes = qMax<qint64>(0, acceptedBytes);
    return result;
}

QCWebSocketCommandResult QCWebSocketCommandResult::rejected(Status status, const QString &error)
{
    Q_ASSERT(status != Status::Accepted);
    QCWebSocketCommandResult result;
    result.m_status = status == Status::Accepted ? Status::InvalidArgument : status;
    result.m_error  = error;
    return result;
}

QCWebSocketCommandResult::Status QCWebSocketCommandResult::status() const noexcept
{
    return m_status;
}

bool QCWebSocketCommandResult::isAccepted() const noexcept
{
    return m_status == Status::Accepted;
}

qint64 QCWebSocketCommandResult::acceptedBytes() const noexcept
{
    return m_acceptedBytes;
}

QString QCWebSocketCommandResult::error() const
{
    return m_error;
}

} // namespace QCurl

#endif // QCURL_WEBSOCKET_SUPPORT
