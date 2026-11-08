#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"

namespace QCurl {

bool QCCurlMultiTransferRecord::bindReceiveIdleCallback(QString *errorMessage)
{
    if (!m_transferState->receiveIdle.enabled()) {
        return true;
    }
    auto code = Internal::CurlOptions::setWithTestHook(handle(),
                                                       QCURL_CURL_OPTION(CURLOPT_PREREQFUNCTION),
                                                       &prerequisiteCallback);
    if (code == CURLE_OK) {
        code = Internal::CurlOptions::setWithTestHook(handle(),
                                                      QCURL_CURL_OPTION(CURLOPT_PREREQDATA),
                                                      static_cast<void *>(this));
    }
    if (code == CURLE_OK) {
        code = Internal::CurlOptions::setWithTestHook(handle(),
                                                      QCURL_CURL_OPTION(CURLOPT_SOCKOPTFUNCTION),
                                                      &socketOptionCallback);
    }
    if (code == CURLE_OK) {
        code = Internal::CurlOptions::setWithTestHook(handle(),
                                                      QCURL_CURL_OPTION(CURLOPT_SOCKOPTDATA),
                                                      static_cast<void *>(this));
    }
    if (code != CURLE_OK && errorMessage) {
        *errorMessage = QStringLiteral("设置接收空闲超时 PREREQ 回调失败（%1）")
                            .arg(QString::fromUtf8(curl_easy_strerror(code)));
    }
    return code == CURLE_OK;
}

int QCCurlMultiTransferRecord::prerequisiteCallback(void *userdata, char *, char *, int, int)
{
    Internal::ReplyCurlCallbackScope callbackScope;
    auto *record     = static_cast<QCCurlMultiTransferRecord *>(userdata);
    const auto state = record ? record->m_transferState : nullptr;
    if (!state || !record->m_observer || state->lifecycle.isTerminal()
        || state->receiveIdle.expired()) {
        return CURL_PREREQFUNC_ABORT;
    }
    state->receiveIdle.beginRequest(record->handle());
    state->receiveIdle.setReceivePaused((state->flow.desiredMask() & CURLPAUSE_RECV) != 0);
    return CURL_PREREQFUNC_OK;
}

int QCCurlMultiTransferRecord::socketOptionCallback(void *userdata, curl_socket_t, curlsocktype)
{
    Internal::ReplyCurlCallbackScope callbackScope;
    auto *record = static_cast<QCCurlMultiTransferRecord *>(userdata);
    if (record && record->m_transferState) {
        // libcurl 隐式重连（例如复用失效连接）也必须在 TCP/TLS 前停旧计时。
        record->m_transferState->receiveIdle.stop();
    }
    return CURL_SOCKOPT_OK;
}

} // namespace QCurl
