#include "private/QCWebSocketCloseCode_p.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

namespace QCurl::Internal::WebSocketCloseCode {

namespace {

constexpr int kCustomCloseCodeBegin = 3000;
constexpr int kCustomCloseCodeEnd   = 4999;

} // namespace

bool isReserved(QCWebSocket::CloseCode closeCode) noexcept
{
    return closeCode == QCWebSocket::CloseCode::NoStatusReceived
           || closeCode == QCWebSocket::CloseCode::AbnormalClosure
           || closeCode == QCWebSocket::CloseCode::TlsHandshake;
}

bool isApplication(int code) noexcept
{
    return code >= kCustomCloseCodeBegin && code <= kCustomCloseCodeEnd;
}

bool tryFromWire(int code, QCWebSocket::CloseCode *out) noexcept
{
    switch (static_cast<QCWebSocket::CloseCode>(code)) {
        case QCWebSocket::CloseCode::Normal:
        case QCWebSocket::CloseCode::GoingAway:
        case QCWebSocket::CloseCode::ProtocolError:
        case QCWebSocket::CloseCode::UnsupportedData:
        case QCWebSocket::CloseCode::InvalidPayload:
        case QCWebSocket::CloseCode::PolicyViolation:
        case QCWebSocket::CloseCode::MessageTooBig:
        case QCWebSocket::CloseCode::MandatoryExtension:
        case QCWebSocket::CloseCode::InternalError:
        case QCWebSocket::CloseCode::ServiceRestart:
        case QCWebSocket::CloseCode::TryAgainLater:
            if (out) {
                *out = static_cast<QCWebSocket::CloseCode>(code);
            }
            return true;
        case QCWebSocket::CloseCode::NoStatusReceived:
        case QCWebSocket::CloseCode::AbnormalClosure:
        case QCWebSocket::CloseCode::TlsHandshake:
            return false;
    }

    return false;
}

int toWire(QCWebSocket::CloseCode closeCode) noexcept
{
    return static_cast<int>(closeCode);
}

} // namespace QCurl::Internal::WebSocketCloseCode

#endif // QCURL_WEBSOCKET_SUPPORT
