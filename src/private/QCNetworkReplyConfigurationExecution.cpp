/**
 * @file
 * @brief 发布配置诊断，并在既有插点准备设备和注册 C callbacks。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyConfigurationExecution_p.h"
#include "private/QCNetworkReplyFlowControlExecution_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>
#include <QIODevice>
#include <QThread>

namespace QCurl::Internal {

bool publishReplyCurlConfigurationResult(QCNetworkReplyPrivate *reply,
                                         const ReplyCurlConfigurationResult &result)
{
    for (const auto &warning : result.warnings) {
        if (warning.kind == ReplyCurlWarningKind::Capability) {
            appendReplyCapabilityWarning(reply, warning.message);
        } else {
            qWarning().noquote() << warning.message;
        }
    }
    if (result.failure) {
        reply->setError(NetworkError::InvalidRequest, *result.failure);
        return false;
    }
    return true;
}

bool publishReplyTlsConfigurationResult(QCNetworkReplyPrivate *reply,
                                        const TlsConfigurationResult &result)
{
    for (const QString &warning : result.warnings) {
        appendReplyCapabilityWarning(reply, warning);
    }
    if (result.failure) {
        reply->setError(result.failure->error, result.failure->message);
        return false;
    }
    return true;
}

namespace {

bool validateBodySourceThreads(QCNetworkReplyPrivate *reply, QIODevice *device)
{
    if (!device) {
        return true;
    }
    const auto *observer = reply->qObject();
    if (QThread::currentThread() != observer->thread()
        || observer->thread() != QCCurlMultiManager::instance()->thread()) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("request body source: Reply 线程与 MultiManager 线程不一致，"
                                       "无法安全流式读取"));
        return false;
    }
    if (device->thread() != observer->thread()) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("request body source: 源 QIODevice 与 Reply 不在同一线程"));
        return false;
    }
    return true;
}

} // namespace

bool prepareReplyBodySource(QCNetworkReplyPrivate *reply)
{
    const auto &normalized = reply->transferState->configuration.plan().normalized;
    if (!validateBodySourceThreads(reply, normalized.body.device.data())) {
        return false;
    }
    auto &source = reply->transferState->bodySource;
    QString error;
    if (!source.prepare(normalized.body,
                        normalized.method,
                        normalized.request.httpVersion(),
                        normalized.request.retryPolicy().isEnabled(),
                        &error)) {
        reply->setError(NetworkError::InvalidRequest, error);
        return false;
    }
    if (source.device()) {
        QObject::connect(source.device(), &QIODevice::readyRead, reply->qObject(), [reply]() {
            Q_UNUSED(resumeReplySendFromRequestBodySourceIfNeeded(reply));
        });
    }
    return true;
}

bool configureReplyReadCallbacks(QCNetworkReplyPrivate *reply, CURL *easy)
{
    if (!reply->transferState->configuration.plan().usesRequestBodySource()) {
        return true;
    }
    if (!reply->transferState->bodySource.device()) {
        reply->setError(NetworkError::InvalidRequest,
                        QStringLiteral("request body source 需要有效的设备来源"));
        return false;
    }
    if (!setRequiredReplyCallbackOption(reply,
                                        easy,
                                        QCURL_CURL_OPTION(CURLOPT_READFUNCTION),
                                        QCNetworkReplyPrivate::curlReadCallback)
        || !setRequiredReplyCallbackOption(reply, easy, QCURL_CURL_OPTION(CURLOPT_READDATA), reply)) {
        return false;
    }
    reply->transferState->readCallbackConfigured = true;
    return true;
}

bool configureReplyResponseCallbacks(QCNetworkReplyPrivate *reply, CURL *easy)
{
    return setRequiredReplyCallbackOption(reply,
                                          easy,
                                          QCURL_CURL_OPTION(CURLOPT_WRITEFUNCTION),
                                          QCNetworkReplyPrivate::curlWriteCallback)
           && setRequiredReplyCallbackOption(reply,
                                             easy,
                                             QCURL_CURL_OPTION(CURLOPT_WRITEDATA),
                                             reply)
           && setRequiredReplyCallbackOption(reply,
                                             easy,
                                             QCURL_CURL_OPTION(CURLOPT_HEADERFUNCTION),
                                             QCNetworkReplyPrivate::curlHeaderCallback)
           && setRequiredReplyCallbackOption(reply,
                                             easy,
                                             QCURL_CURL_OPTION(CURLOPT_HEADERDATA),
                                             reply);
}

bool configureReplyTransferCallbacks(QCNetworkReplyPrivate *reply, CURL *easy)
{
    return setRequiredReplyCallbackOption(reply,
                                          easy,
                                          QCURL_CURL_OPTION(CURLOPT_SEEKFUNCTION),
                                          QCNetworkReplyPrivate::curlSeekCallback)
           && setRequiredReplyCallbackOption(reply, easy, QCURL_CURL_OPTION(CURLOPT_SEEKDATA), reply)
           && setRequiredReplyCallbackOption(reply,
                                             easy,
                                             QCURL_CURL_OPTION(CURLOPT_XFERINFOFUNCTION),
                                             QCNetworkReplyPrivate::curlProgressCallback)
           && setRequiredReplyCallbackOption(reply,
                                             easy,
                                             QCURL_CURL_OPTION(CURLOPT_XFERINFODATA),
                                             reply)
           && publishReplyCurlConfigurationResult(reply,
                                                  QCReplyCurlConfiguration::classifyRequiredOption(
                                                      CurlOptions::setEnabled(easy,
                                                                              CURLOPT_NOPROGRESS,
                                                                              false),
                                                      QCURL_CURL_OPTION(CURLOPT_NOPROGRESS).name));
}

} // namespace QCurl::Internal
