/**
 * @file
 * @brief QCNetworkReply response header 与 attempt error 实现。
 */

#include "private/QCNetworkReplyResponse_p.h"

#include "QCNetworkMockHandler_p.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyBodySource_p.h"

#include <QList>
#include <QVariant>

namespace QCurl::Internal {
namespace {

std::optional<qint64> parseContentRangeCompleteSize(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix = QByteArrayLiteral("bytes */");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    bool ok = false;
    const qint64 totalSize = trimmed.mid(prefix.size()).toLongLong(&ok);
    if (!ok || totalSize < 0) {
        return std::nullopt;
    }
    return totalSize;
}

void clearParsedHeaders(QCNetworkReplyPrivate *reply)
{
    reply->finalHeaderList.clear();
    reply->finalHeaderMap.clear();
    reply->headerMap.clear();
}

void flushCurrentHeader(QCNetworkReplyPrivate *reply,
                        const QByteArray &name,
                        const QList<QByteArray> &segments)
{
    if (name.isEmpty()) {
        return;
    }

    QByteArray value;
    for (const QByteArray &seg : segments) {
        const QByteArray part = seg.trimmed();
        if (part.isEmpty()) {
            continue;
        }
        if (!value.isEmpty()) {
            value.append(' ');
        }
        value.append(part);
    }

    reply->finalHeaderList.append(qMakePair(name, value));
    reply->finalHeaderMap.insert(name.trimmed().toLower(), value);
    reply->headerMap.insert(QString::fromUtf8(name), QString::fromUtf8(value));
}

void parseStatusLine(QCNetworkReplyPrivate *reply, const QByteArray &line)
{
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 2) {
        return;
    }

    bool ok = false;
    const int parsedStatus = parts.at(1).trimmed().toInt(&ok);
    if (ok) {
        reply->httpStatusCode = parsedStatus;
    }
}

void applyBodySourceErrorOverride(QCNetworkReplyPrivate *reply, ReplyAttemptErrorInfo *info)
{
    if (!hasReplyBodySourceError(reply->requestBodySource)) {
        return;
    }

    info->error = replyBodySourceErrorCode(reply->requestBodySource);
    info->message = replyBodySourceErrorMessage(reply->requestBodySource);
}

void applySendFailRewindOverride(QCNetworkReplyPrivate *reply,
                                 CURLcode curlCode,
                                 ReplyAttemptErrorInfo *info)
{
#if defined(CURLE_SEND_FAIL_REWIND)
    if (curlCode != CURLE_SEND_FAIL_REWIND || !reply->requestBodySource.device) {
        return;
    }
    if (hasReplyBodySourceError(reply->requestBodySource)) {
        return;
    }

    info->error = NetworkError::InvalidRequest;
    info->message = QStringLiteral("request body source: 无法重发 body（seek/rewind 失败：%1）")
                        .arg(QString::fromUtf8(curl_easy_strerror(curlCode)));
#else
    Q_UNUSED(reply);
    Q_UNUSED(curlCode);
    Q_UNUSED(info);
#endif
}

bool isSatisfiedRangeCompletion(QCNetworkReplyPrivate *reply)
{
    parseReplyHeaders(reply);

    const QVariant existingSizeVar = reply->q_ptr->property("_qcurl_resumable_existing_size");
    bool ok = false;
    const qint64 existingSize = existingSizeVar.toLongLong(&ok);
    const auto completeSize =
        parseContentRangeCompleteSize(reply->finalHeaderMap.value(QByteArrayLiteral("content-range")));
    return ok && existingSize >= 0 && completeSize.has_value() && completeSize.value() == existingSize;
}

} // namespace

void parseReplyHeaders(QCNetworkReplyPrivate *reply)
{
    clearParsedHeaders(reply);

    QByteArray currentName;
    QList<QByteArray> currentSegments;
    const QList<QByteArray> lines = reply->headerData.split('\n');

    for (QByteArray line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            flushCurrentHeader(reply, currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
            continue;
        }
        if (line.startsWith("HTTP/")) {
            flushCurrentHeader(reply, currentName, currentSegments);
            clearParsedHeaders(reply);
            currentName.clear();
            currentSegments.clear();
            parseStatusLine(reply, line);
            continue;
        }

        const bool isContinuation = !currentName.isEmpty()
                                    && (line.startsWith(' ') || line.startsWith('\t'));
        if (isContinuation) {
            currentSegments.append(line);
            continue;
        }

        const int colonPos = line.indexOf(':');
        if (colonPos <= 0) {
            continue;
        }

        flushCurrentHeader(reply, currentName, currentSegments);
        currentName = line.left(colonPos).trimmed();
        currentSegments.clear();
        currentSegments.append(line.mid(colonPos + 1));
    }

    flushCurrentHeader(reply, currentName, currentSegments);
}

size_t headerReplyCurlCallback(char *ptr, size_t size, size_t nmemb, QCNetworkReplyPrivate *reply)
{
    if (!reply || !reply->q_ptr) {
        return 0;
    }

    const size_t totalSize = size * nmemb;
    reply->headerData.append(ptr, static_cast<int>(totalSize));
    if (reply->headerData.endsWith(QByteArrayLiteral("\r\n\r\n"))
        || reply->headerData.endsWith(QByteArrayLiteral("\n\n"))) {
        parseReplyHeaders(reply);
    }

    if (reply->executionMode == ExecutionMode::Sync && reply->headerCallback) {
        reply->headerCallback(ptr, totalSize);
    }

    return totalSize;
}

ReplyAttemptErrorInfo attemptErrorFromCurlAndHttp(QCNetworkReplyPrivate *reply,
                                                  CURLcode curlCode,
                                                  long httpCode)
{
    if (!reply) {
        return {};
    }

    reply->httpStatusCode = static_cast<int>(httpCode);
    ReplyAttemptErrorInfo info;
    if (curlCode != CURLE_OK) {
        info.error = fromCurlCode(static_cast<int>(curlCode));
        info.message = QString::fromUtf8(curl_easy_strerror(curlCode));
        applySendFailRewindOverride(reply, curlCode, &info);
    } else if (httpCode == 416 && !isSatisfiedRangeCompletion(reply)) {
        info.error = fromHttpCode(httpCode);
        info.message = QStringLiteral("HTTP error %1").arg(httpCode);
    } else if (httpCode >= 400 && httpCode != 416) {
        info.error = fromHttpCode(httpCode);
        info.message = QStringLiteral("HTTP error %1").arg(httpCode);
    }

    applyBodySourceErrorOverride(reply, &info);
    return info;
}

ReplyAttemptErrorInfo attemptErrorFromMockData(QCNetworkReplyPrivate *reply,
                                               const QCNetworkMockData &mockData)
{
    if (!reply) {
        return {};
    }

    reply->httpStatusCode = mockData.statusCode;
    ReplyAttemptErrorInfo info;
    if (mockData.isError && mockData.error != NetworkError::NoError) {
        info.error = mockData.error;
        info.message = QCurl::errorString(info.error);
    } else if (mockData.statusCode >= 400) {
        info.error = fromHttpCode(mockData.statusCode);
        info.message = QStringLiteral("HTTP error %1").arg(mockData.statusCode);
    }

    applyBodySourceErrorOverride(reply, &info);
    return info;
}

void applyMockResponseHeaders(QCNetworkReplyPrivate *reply, const QCNetworkMockData &mockData)
{
    if (!reply) {
        return;
    }

    if (mockData.rawHeaderData.has_value()) {
        reply->headerData = mockData.rawHeaderData.value();
        return;
    }

    for (auto it = mockData.headers.cbegin(); it != mockData.headers.cend(); ++it) {
        reply->headerData.append(it.key());
        reply->headerData.append(": ");
        reply->headerData.append(it.value());
        reply->headerData.append('\n');
    }
}

} // namespace QCurl::Internal
