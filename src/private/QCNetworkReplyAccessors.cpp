/**
 * @file
 * @brief QCNetworkReply public accessors and cache helpers.
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkCacheIntegration_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>
#include <QPointer>
#include <QTimer>

namespace QCurl {

namespace {

[[nodiscard]] bool cacheReplayHeaderIsForbidden(const QByteArray &name)
{
    const QByteArray normalized = name.trimmed().toLower();
    return normalized == QByteArrayLiteral("set-cookie")
           || normalized == QByteArrayLiteral("set-cookie2")
           || normalized == QByteArrayLiteral("authorization")
           || normalized == QByteArrayLiteral("proxy-authorization");
}

[[nodiscard]] QByteArray cacheResponseHeaderBlock(const QCNetworkCacheMetadata &metadata)
{
    QByteArray headerBlock      = QByteArrayLiteral("HTTP/1.1 ")
                                  + QByteArray::number(metadata.statusCode())
                                  + QByteArrayLiteral(" Cached\r\n");
    const auto rawHeaders       = metadata.rawHeaders();
    const QByteArray currentAge = QByteArray::number(metadata.currentAgeSeconds());
    bool ageWritten             = false;
    for (const auto &[name, value] : rawHeaders) {
        if (cacheReplayHeaderIsForbidden(name)) {
            continue;
        }
        const bool isAge = QByteArrayView(name).compare(QByteArrayView("age"), Qt::CaseInsensitive)
                           == 0;
        headerBlock += name + QByteArrayLiteral(": ") + (isAge ? currentAge : value)
                       + QByteArrayLiteral("\r\n");
        ageWritten = ageWritten || isAge;
    }
    if (!ageWritten) {
        headerBlock += QByteArrayLiteral("Age: ") + currentAge + QByteArrayLiteral("\r\n");
    }
    return headerBlock + QByteArrayLiteral("\r\n");
}

[[nodiscard]] QByteArray joinedHeaderSegments(const QList<QByteArray> &segments)
{
    QByteArray value;
    for (const QByteArray &segment : segments) {
        const QByteArray part = segment.trimmed();
        if (part.isEmpty()) {
            continue;
        }
        if (!value.isEmpty()) {
            value.append(' ');
        }
        value.append(part);
    }
    return value;
}

} // namespace

// ==================
// 数据访问（现代 C++17 风格）
// ==================

std::optional<QByteArray> QCNetworkReply::readAll()
{
    Q_D(QCNetworkReply);

    if (d->bodyBuffer.isEmpty()) {
        // 约定：在“终态且响应体为空”的场景下，返回空 QByteArray（而不是 std::nullopt），
        // 否则会把“空 body”与“尚无数据可读”混为一谈，导致可观测层面不可区分。
        if (d->state == ReplyState::Finished || d->state == ReplyState::Error
            || d->state == ReplyState::Cancelled) {
            return QByteArray();
        }
        return std::nullopt;
    }

    // 注意：这会清空当前 reply 缓冲区。
    QByteArray out = d->bodyBuffer.readAll();

    Internal::scheduleReplyBackpressureResumeAfterRead(this, d);
    return out;
}

QList<RawHeaderPair> QCNetworkReply::rawHeaders() const
{
    Q_D(const QCNetworkReply);

    if (!d || d->state == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaders: Invalid state"
                   << (d ? static_cast<int>(d->state) : -1);
        return QList<RawHeaderPair>();
    }

    return d->finalHeaderList;
}

QByteArray QCNetworkReply::rawHeader(const QByteArray &name) const
{
    Q_D(const QCNetworkReply);
    if (!d || d->state == ReplyState::Idle) {
        return {};
    }
    return d->finalHeaderMap.value(name.trimmed().toLower());
}

bool QCNetworkReply::hasRawHeader(const QByteArray &name) const
{
    Q_D(const QCNetworkReply);
    if (!d || d->state == ReplyState::Idle) {
        return false;
    }
    return d->finalHeaderMap.contains(name.trimmed().toLower());
}

QByteArray QCNetworkReply::rawHeaderData() const
{
    Q_D(const QCNetworkReply);

    if (!d || d->state == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaderData: Invalid state";
        return QByteArray();
    }

    return d->headerData;
}

QUrl QCNetworkReply::url() const
{
    Q_D(const QCNetworkReply);
    return d->request.url();
}

HttpMethod QCNetworkReply::method() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->httpMethod;
}

int QCNetworkReply::httpStatusCode() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->httpStatusCode;
}

qint64 QCNetworkReply::durationMs() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->durationMs;
}

qint64 QCNetworkReply::bytesAvailable() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->bodyBuffer.byteAmount();
}

QStringList QCNetworkReply::capabilityWarnings() const
{
    Q_D(const QCNetworkReply);
    return d->capabilityWarnings;
}

// ==================
// 状态查询
// ==================

ReplyState QCNetworkReply::state() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state;
}

NetworkError QCNetworkReply::error() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->errorCode;
}

QString QCNetworkReply::errorString() const
{
    Q_D(const QCNetworkReply);
    return d->errorMessage;
}

bool QCNetworkReply::isFinished() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state == ReplyState::Finished || d->state == ReplyState::Cancelled
           || d->state == ReplyState::Error;
}

bool QCNetworkReply::isRunning() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state == ReplyState::Running;
}

bool QCNetworkReply::isPaused() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state == ReplyState::Paused;
}

bool QCNetworkReply::isBackpressureActive() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 && d->backpressureActive;
}

bool QCNetworkReply::isUploadSendPaused() const noexcept
{
    Q_D(const QCNetworkReply);
    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error
        || d->state == ReplyState::Finished) {
        return false;
    }
    return d->uploadSendPaused && ((d->internalPauseMask & CURLPAUSE_SEND) != 0);
}

qint64 QCNetworkReply::backpressureLimitBytes() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 ? d->backpressureLimitBytes : 0;
}

qint64 QCNetworkReply::backpressureResumeBytes() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 ? d->backpressureResumeBytes : 0;
}

qint64 QCNetworkReply::backpressureBufferedBytesPeak() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 ? d->backpressurePeakBufferedBytes : 0;
}

qint64 QCNetworkReply::bytesReceived() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->bytesDownloaded;
}

qint64 QCNetworkReply::bytesTotal() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->downloadTotal;
}

// ==================
// 缓存集成私有方法实现
// ==================

bool QCNetworkReply::loadFromCache(bool ignoreExpiry)
{
    Q_D(QCNetworkReply);

    QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager *>(parent());
    QCNetworkCache *cache           = manager ? manager->cache() : nullptr;
    if (!cache) {
        return false;
    }

    const bool managerUsesCookies = manager->shareHandleConfig().shareCookies()
                                    || !manager->cookieFilePath().isEmpty();
    const auto key                = d->cacheRequestKeyInitialized
                                        ? d->cacheRequestKey
                                        : Internal::buildCacheRequestKey(d->request,
                                                                         d->httpMethod,
                                                                         managerUsesCookies);
    const auto mode               = ignoreExpiry ? QCNetworkCacheReadMode::AllowStale
                                                 : QCNetworkCacheReadMode::FreshOnly;
    const auto cached             = cache->lookup(key, mode);
    if (!cached.hit()) {
        return false;
    }

    const auto meta       = cached.metadata();
    const QByteArray data = d->httpMethod == HttpMethod::Head ? QByteArray() : cached.body();

    d->bodyBuffer.append(data);
    d->cacheBodyBuffer = data;
    d->headerData      = cacheResponseHeaderBlock(meta);
    d->parseHeaders();
    d->errorCode = NetworkError::NoError;

    const bool hasBody = !data.isEmpty();
    QPointer<QCNetworkReply> safeThis(this);
    QTimer::singleShot(0, this, [safeThis, hasBody]() {
        if (!safeThis) {
            return;
        }

        auto *d = safeThis->d_func();
        if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error
            || d->state == ReplyState::Finished) {
            return;
        }

        if (hasBody) {
            if (Internal::emitReplySignal(safeThis,
                                          [](QCNetworkReply *reply) { Q_EMIT reply->readyRead(); })
                == Internal::SignalEmissionResult::Destroyed) {
                return;
            }
        }
        Q_UNUSED(d->setState(ReplyState::Finished));
    });

    return true;
}

QByteArray Internal::testCurlPlanDigest(const QCNetworkReply *reply)
{
    if (!reply) {
        return QByteArray();
    }

#ifdef QCURL_ENABLE_TEST_HOOKS
    return reply->property(Internal::kTestCurlPlanDigestProperty).toByteArray();
#else
    return QByteArray();
#endif
}

QMap<QByteArray, QByteArray> QCNetworkReplyPrivate::parsedResponseHeaders() const
{
    QMap<QByteArray, QByteArray> headers;
    auto flushCurrent = [&](const QByteArray &name, const QList<QByteArray> &segments) {
        if (name.isEmpty()) {
            return;
        }
        headers.insert(name, joinedHeaderSegments(segments));
    };

    QByteArray currentName;
    QList<QByteArray> currentSegments;

    const QList<QByteArray> lines = headerData.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            flushCurrent(currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
            continue;
        }
        if (line.startsWith("HTTP/")) {
            flushCurrent(currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
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
        flushCurrent(currentName, currentSegments);
        currentName = line.left(colonPos).trimmed();
        currentSegments.clear();
        currentSegments.append(line.mid(colonPos + 1));
    }

    flushCurrent(currentName, currentSegments);

    return headers;
}

QMap<QByteArray, QByteArray> QCNetworkReply::parseResponseHeaders()
{
    Q_D(QCNetworkReply);
    return d->parsedResponseHeaders();
}

} // namespace QCurl
