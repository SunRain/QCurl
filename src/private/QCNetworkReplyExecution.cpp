/**
 * @file
 * @brief QCNetworkReply execution path.
 */

#include "QCNetworkReply.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkMockHandler_p.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkTestSupport.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <random>

namespace QCurl {

namespace {

#ifdef QCURL_ENABLE_TEST_HOOKS
enum class TestMockChaosAction
{
    None,
    PauseRecv,
    Cancel,
};

enum class TestMockChaosActionPoint
{
    BeforeFirstChunk,
    AfterFirstChunk,
    BeforeFinish,
};

struct TestMockChaosConfig
{
    quint32 seed                          = 1u;
    int maxChunkBytes                     = 0;
    int chunkDelayMs                      = 0;
    TestMockChaosAction action            = TestMockChaosAction::None;
    TestMockChaosActionPoint actionPoint  = TestMockChaosActionPoint::AfterFirstChunk;
    int resumeDelayMs                     = 0;
};

struct TestMockChaosReplayState
{
    QPointer<QCNetworkReply> reply;
    QCNetworkReplyPrivate *d = nullptr;
    Internal::QCNetworkMockData mockData;
    QList<QByteArray> chunks;
    TestMockChaosConfig config;
    qint64 totalBytes  = 0;
    int nextChunkIndex = 0;
    bool initialized   = false;
    bool actionFired   = false;
};

[[nodiscard]] quint32 fnv1a32(const QByteArray &bytes)
{
    quint32 hash = 2166136261u;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

[[nodiscard]] std::optional<TestMockChaosConfig> testMockChaosConfigFromEnv()
{
    const QByteArray raw = qgetenv("QCURL_TEST_MOCK_CHAOS").trimmed();
    if (raw.isEmpty() || raw == "0") {
        return std::nullopt;
    }

    TestMockChaosConfig config;
    const QList<QByteArray> entries = raw.split(';');
    for (QByteArray entry : entries) {
        entry = entry.trimmed();
        if (entry.isEmpty() || entry == "1") {
            continue;
        }

        const int eqPos = entry.indexOf('=');
        if (eqPos <= 0) {
            continue;
        }

        const QByteArray key   = entry.left(eqPos).trimmed().toLower();
        const QByteArray value = entry.mid(eqPos + 1).trimmed().toLower();

        if (key == "enabled" && (value == "0" || value == "false" || value == "off")) {
            return std::nullopt;
        }

        if (key == "seed") {
            bool ok            = false;
            const quint32 seed = value.toUInt(&ok);
            if (ok) {
                config.seed = seed;
            }
            continue;
        }

        if (key == "max_chunk_bytes" || key == "chunk_bytes") {
            bool ok            = false;
            const int chunkMax = value.toInt(&ok);
            if (ok) {
                config.maxChunkBytes = qMax(0, chunkMax);
            }
            continue;
        }

        if (key == "chunk_delay_ms") {
            bool ok        = false;
            const int delay = value.toInt(&ok);
            if (ok) {
                config.chunkDelayMs = qMax(0, delay);
            }
            continue;
        }

        if (key == "resume_delay_ms") {
            bool ok         = false;
            const int delay = value.toInt(&ok);
            if (ok) {
                config.resumeDelayMs = qMax(0, delay);
            }
            continue;
        }

        if (key == "action") {
            if (value == "pause" || value == "pause_recv") {
                config.action = TestMockChaosAction::PauseRecv;
            } else if (value == "cancel") {
                config.action = TestMockChaosAction::Cancel;
            } else {
                config.action = TestMockChaosAction::None;
            }
            continue;
        }

        if (key == "action_point") {
            if (value == "before_first_chunk") {
                config.actionPoint = TestMockChaosActionPoint::BeforeFirstChunk;
            } else if (value == "before_finish") {
                config.actionPoint = TestMockChaosActionPoint::BeforeFinish;
            } else {
                config.actionPoint = TestMockChaosActionPoint::AfterFirstChunk;
            }
        }
    }

    return config;
}

[[nodiscard]] QList<QByteArray> buildDeterministicMockChunks(const QByteArray &payload,
                                                             const TestMockChaosConfig &config,
                                                             HttpMethod method,
                                                             const QUrl &url)
{
    QList<QByteArray> chunks;
    if (payload.isEmpty()) {
        return chunks;
    }

    const int maxChunkBytes = config.maxChunkBytes > 0 ? config.maxChunkBytes : payload.size();
    if (maxChunkBytes >= payload.size()) {
        chunks.append(payload);
        return chunks;
    }

    const QByteArray salt = QByteArray::number(static_cast<int>(method))
                            + QByteArrayLiteral("|")
                            + url.toString().toUtf8()
                            + QByteArrayLiteral("|")
                            + QByteArray::number(payload.size());
    std::mt19937 engine(config.seed ^ fnv1a32(salt));

    int offset = 0;
    while (offset < payload.size()) {
        const int remaining = payload.size() - offset;
        const int upper     = qMin(maxChunkBytes, remaining);
        std::uniform_int_distribution<int> dist(1, upper);
        const int chunkSize = dist(engine);
        chunks.append(payload.mid(offset, chunkSize));
        offset += chunkSize;
    }
    return chunks;
}

void scheduleTestMockChaosStep(const std::shared_ptr<TestMockChaosReplayState> &state, int delayMs);

[[nodiscard]] bool triggerTestMockChaosAction(
    const std::shared_ptr<TestMockChaosReplayState> &state, TestMockChaosActionPoint point)
{
    if (!state || !state->reply || state->actionFired || state->config.action == TestMockChaosAction::None
        || state->config.actionPoint != point) {
        return false;
    }

    state->actionFired = true;
    auto *reply        = state->reply.data();
    if (state->config.action == TestMockChaosAction::Cancel) {
        reply->cancel();
        return true;
    }

    state->d->userPauseMask = CURLPAUSE_RECV;
    state->d->setState(ReplyState::Paused);
    if (reply->state() == ReplyState::Paused && state->config.resumeDelayMs > 0) {
        QPointer<QCNetworkReply> safeReply(reply);
        QCNetworkReplyPrivate *d = state->d;
        QTimer::singleShot(state->config.resumeDelayMs, reply, [safeReply, d]() {
            if (safeReply && d && d->state == ReplyState::Paused) {
                d->userPauseMask = 0;
                d->setState(ReplyState::Running);
            }
        });
    }
    return reply->state() == ReplyState::Paused;
}

void runTestMockChaosStep(const std::shared_ptr<TestMockChaosReplayState> &state)
{
    if (!state || !state->reply || !state->d) {
        return;
    }

    QCNetworkReply *reply      = state->reply.data();
    QCNetworkReplyPrivate *d   = state->d;
    const ReplyState curState  = d->state;
    const bool terminalReached = curState == ReplyState::Cancelled || curState == ReplyState::Finished
                                 || curState == ReplyState::Error;
    if (terminalReached) {
        return;
    }

    if (curState == ReplyState::Paused) {
        scheduleTestMockChaosStep(state, 1);
        return;
    }

    if (!state->initialized) {
        Internal::resetReplyForRetry(d, false);
        d->downloadTotal   = state->totalBytes;
        d->bytesDownloaded = 0;
        applyMockResponseHeaders(d, state->mockData);
        state->initialized = true;
    }

    if (state->nextChunkIndex == 0
        && triggerTestMockChaosAction(state, TestMockChaosActionPoint::BeforeFirstChunk)) {
        if (state->reply && d->state == ReplyState::Paused) {
            scheduleTestMockChaosStep(state, 1);
        }
        return;
    }

    if (state->nextChunkIndex < state->chunks.size()) {
        const QByteArray chunk = state->chunks.at(state->nextChunkIndex);
        state->nextChunkIndex += 1;

        if (!chunk.isEmpty()) {
            d->bodyBuffer.append(chunk);
            d->bytesDownloaded += chunk.size();
            emit reply->readyRead();
            emit reply->downloadProgress(d->bytesDownloaded, d->downloadTotal);
        }

        if (state->nextChunkIndex == 1
            && triggerTestMockChaosAction(state, TestMockChaosActionPoint::AfterFirstChunk)) {
            if (state->reply && d->state == ReplyState::Paused) {
                scheduleTestMockChaosStep(state, 1);
            }
            return;
        }
    }

    if (state->nextChunkIndex < state->chunks.size()) {
        scheduleTestMockChaosStep(state, state->config.chunkDelayMs);
        return;
    }

    if (triggerTestMockChaosAction(state, TestMockChaosActionPoint::BeforeFinish)) {
        if (state->reply && d->state == ReplyState::Paused) {
            scheduleTestMockChaosStep(state, 1);
        }
        return;
    }

    const auto info = attemptErrorFromMockData(d, state->mockData);
    if (info.error == NetworkError::NoError) {
        d->setState(ReplyState::Finished);
        return;
    }

    const auto delay = Internal::advanceReplyRetryIfNeeded(d, info.error);
    if (delay.has_value()) {
        Internal::scheduleAsyncReplyRetry(state->reply, d, delay.value());
        return;
    }

    d->setError(info.error, info.message);
    d->setState(ReplyState::Error);
}

void scheduleTestMockChaosStep(const std::shared_ptr<TestMockChaosReplayState> &state, int delayMs)
{
    if (!state || !state->reply) {
        return;
    }

    QTimer::singleShot(delayMs > 0 ? delayMs : 0, state->reply.data(), [state]() {
        runTestMockChaosStep(state);
    });
}
#endif


} // namespace

// ==================
// 执行控制
// ==================

void QCNetworkReply::execute()
{
    if (QThread::currentThread() != thread()) {
        Q_D(QCNetworkReply);
        if (d->executionMode == ExecutionMode::Async) {
            QMetaObject::invokeMethod(this, [this]() { execute(); }, Qt::QueuedConnection);
            return;
        }

        qWarning()
            << "QCNetworkReply::execute: Sync 模式不支持跨线程调用（需要在 reply 所在线程执行）";
        abortWithError(NetworkError::InvalidRequest,
                       QStringLiteral("QCNetworkReply::execute(Sync) 必须在 reply 所在线程调用"));
        return;
    }

    Q_D(QCNetworkReply);

    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        qWarning() << "QCNetworkReply::execute() called while already running";
        return;
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    // ==================
    // 缓存集成 - 在发起网络请求前检查缓存
    // ==================
    QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager *>(parent());
    QCNetworkCache *cache           = manager ? manager->cache() : nullptr;
    if (cache) {
        QCNetworkCachePolicy policy = d->request.cachePolicy();

        switch (policy) {
            case QCNetworkCachePolicy::OnlyNetwork:
                // 仅网络：跳过缓存检查
                break;

            case QCNetworkCachePolicy::OnlyCache:
                // 仅缓存：不发起网络请求
                if (!loadFromCache(true)) {
                    // 使用 QPointer 防止在异步执行期间对象被删除
                    QPointer<QCNetworkReply> safeThis(this);
                    d->setError(NetworkError::InvalidRequest,
                                QStringLiteral("Cache miss with OnlyCache policy"));
                    // 异步发射信号，避免信号在用户连接之前发射
                    QTimer::singleShot(0, this, [safeThis]() {
                        if (safeThis) {
                            safeThis->d_func()->setState(ReplyState::Error);
                        }
                    });
                }
                return;

            case QCNetworkCachePolicy::PreferCache:
                // 优先缓存：缓存命中则返回
                if (loadFromCache(false)) {
                    return;
                }
                break;

            case QCNetworkCachePolicy::AlwaysCache:
                // 总是缓存：忽略过期时间
                if (loadFromCache(true)) {
                    return;
                }
                break;

            case QCNetworkCachePolicy::PreferNetwork:
                // 优先网络：标记允许回退到缓存
                d->fallbackToCache = true;
                break;
        }
    }

    // ==================
    // MockHandler 集成（离线回归/单元测试）
    // ==================
    if (manager && TestSupport::mockHandler(manager)) {
        auto *mock             = TestSupport::mockHandler(manager);
        const auto &normalized = d->curlPlan.normalized;
        const auto &bodySpec   = normalized.body;

        // 请求捕获：用于离线断言 middleware/header 注入、body 形态等
        if (mock->captureEnabled()) {
            QCNetworkCapturedRequest captured;
            captured.setUrl(normalized.request.url());
            captured.setMethod(normalized.method);
            captured.setFollowLocation(normalized.request.followLocation());
            const auto timeoutConfig = normalized.request.timeoutConfig();
            if (timeoutConfig.connectTimeout().has_value()) {
                captured.setConnectTimeoutMs(timeoutConfig.connectTimeout()->count());
            }
            if (timeoutConfig.totalTimeout().has_value()) {
                captured.setTotalTimeoutMs(timeoutConfig.totalTimeout()->count());
            }

            const auto headerNames = normalized.request.rawHeaderList();
            for (const auto &name : headerNames) {
                captured.addHeader(name, normalized.request.rawHeader(name));
            }

            captured.setBodySize(bodySpec.hasKnownSize()
                                      ? static_cast<qsizetype>(qMax<qint64>(0, bodySpec.sizeBytes))
                                      : bodySpec.inlineBytes.size());
            const int previewLimit = mock->captureBodyPreviewLimit();
            captured.setBodyPreview(previewLimit > 0 ? bodySpec.inlineBytes.left(previewLimit)
                                                      : QByteArray());
            mock->recordRequest(captured);
        }

        // 仅当存在 mock 规则时才进入回放分支；否则继续走真实网络
        if (mock->hasMock(normalized.method, normalized.request.url())) {
            // 离线回放同样应具备“可诊断配置冲突”能力（避免仅在真实网络路径提示）
            bool hasExplicitAcceptEncodingHeader = false;
            const QList<QByteArray> headerNames  = normalized.request.rawHeaderList();
            for (const QByteArray &headerName : headerNames) {
                if (headerName.trimmed().toLower() == QByteArrayLiteral("accept-encoding")) {
                    hasExplicitAcceptEncodingHeader = true;
                    break;
                }
            }

            if (hasExplicitAcceptEncodingHeader) {
                if (normalized.request.autoDecompressionEnabled()
                    || !normalized.request.acceptedEncodings().isEmpty()) {
                    Internal::appendReplyCapabilityWarning(
                        d,
                        QStringLiteral("请求配置冲突：已显式设置 Accept-Encoding header，将忽略 "
                                       "autoDecompression/acceptedEncodings（不会自动解压）"));
                }
            }

            const int responseDelayMs = mock->globalDelay();

            // 进入
            // Running（与真实网络保持一致）；完成/错误信号通过延迟触发，避免在连接信号前同步发射
            d->setState(ReplyState::Running);

            // 同步模式：阻塞执行（支持重试）
            if (d->executionMode == ExecutionMode::Sync) {
                while (true) {
                    Internal::QCNetworkMockData mockData;
                    if (!Internal::QCNetworkMockHandlerAccess::consumeMock(*mock,
                                                                            normalized.method,
                                                                            normalized.request.url(),
                                                                            mockData)) {
                        d->setError(NetworkError::InvalidRequest,
                                    QStringLiteral("MockHandler: no mock matched for %1")
                                        .arg(normalized.request.url().toString()));
                        d->setState(ReplyState::Error);
                        return;
                    }

                    if (responseDelayMs > 0) {
                        QThread::msleep(static_cast<unsigned long>(responseDelayMs));
                    }

                    Internal::resetReplyForRetry(d, false);

                    if (!mockData.response.isEmpty()) {
                        d->bodyBuffer.append(mockData.response);
                        d->bytesDownloaded = mockData.response.size();
                    }

                    applyMockResponseHeaders(d, mockData);

                    const auto info = attemptErrorFromMockData(d, mockData);
                    if (info.error == NetworkError::NoError) {
                        if (!mockData.response.isEmpty()) {
                            emit readyRead();
                        }
                        d->setState(ReplyState::Finished);
                        return;
                    }

                    const auto delay = Internal::advanceReplyRetryIfNeeded(d, info.error);
                    if (delay.has_value()) {
                        QThread::msleep(static_cast<unsigned long>(delay.value().count()));
                        continue;
                    }

                    d->setError(info.error, info.message);
                    d->setState(ReplyState::Error);
                    return;
                }
            }

            // 异步模式：以定时器回放，支持重试/取消
            QPointer<QCNetworkReply> safeThis(this);
            const HttpMethod normalizedMethod = normalized.method;
            const QUrl normalizedUrl          = normalized.request.url();
            QTimer::singleShot(
                responseDelayMs > 0 ? responseDelayMs : 0,
                this,
                [safeThis, normalizedMethod, normalizedUrl]() {
                    if (!safeThis) {
                        return;
                    }

                    auto *d = safeThis->d_func();
                    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
                        || d->state == ReplyState::Error) {
                        return;
                    }

                    auto *manager = qobject_cast<QCNetworkAccessManager *>(safeThis->parent());
                    auto *mock    = manager ? TestSupport::mockHandler(manager) : nullptr;
                    if (!mock) {
                        d->setError(NetworkError::InvalidRequest,
                                    QStringLiteral("MockHandler: not set"));
                        d->setState(ReplyState::Error);
                        return;
                    }

                    Internal::QCNetworkMockData mockData;
                    if (!Internal::QCNetworkMockHandlerAccess::consumeMock(*mock,
                                                                            normalizedMethod,
                                                                            normalizedUrl,
                                                                            mockData)) {
                        d->setError(NetworkError::InvalidRequest,
                                    QStringLiteral("MockHandler: no mock matched for %1")
                                        .arg(normalizedUrl.toString()));
                        d->setState(ReplyState::Error);
                        return;
                    }

#ifdef QCURL_ENABLE_TEST_HOOKS
                    if (const auto chaosConfig = testMockChaosConfigFromEnv(); chaosConfig.has_value()) {
                        auto replayState      = std::make_shared<TestMockChaosReplayState>();
                        replayState->reply    = safeThis;
                        replayState->d        = d;
                        replayState->mockData = mockData;
                        replayState->config   = chaosConfig.value();
                        replayState->totalBytes = mockData.response.size();
                        replayState->chunks
                            = buildDeterministicMockChunks(mockData.response,
                                                           replayState->config,
                                                           normalizedMethod,
                                                           normalizedUrl);
                        scheduleTestMockChaosStep(replayState, 0);
                        return;
                    }
#endif

                    Internal::resetReplyForRetry(d, false);

                    if (!mockData.response.isEmpty()) {
                        d->bodyBuffer.append(mockData.response);
                        d->bytesDownloaded = mockData.response.size();
                        emit safeThis->readyRead();
                    }

                    applyMockResponseHeaders(d, mockData);

                    const auto info = attemptErrorFromMockData(d, mockData);
                    if (info.error == NetworkError::NoError) {
                        d->setState(ReplyState::Finished);
                        return;
                    }

                    const auto delay = Internal::advanceReplyRetryIfNeeded(d, info.error);
                    if (delay.has_value()) {
                        Internal::scheduleAsyncReplyRetry(safeThis, d, delay.value());
                        return;
                    }

                    d->setError(info.error, info.message);
                    d->setState(ReplyState::Error);
                });

            return;
        }
    }

    // ==================
    // 应用 Cookie 配置（在 QCNetworkAccessManager 中设置）
    // ==================
    // 注意：
    // - cookieFilePath/cookieMode 可能在构造函数之后通过 d_func() 设置，
    //   所以必须在 execute() 中应用，而不是在 configureCurlOptions() 中；
    // - 对于 scheduler 等路径，如果 replyParent 指向 manager 但未显式写入 d_func()，
    //   这里会回溯 manager 配置以保持行为一致性。
    CURL *handle = d->curlManager.handle();

    if (manager && d->cookieMode == 0 && d->cookieFilePath.isEmpty()) {
        const auto mode = manager->cookieFileMode();
        const auto path = manager->cookieFilePath();
        if (mode != QCNetworkAccessManager::NotOpen && !path.isEmpty()) {
            d->cookieMode     = static_cast<int>(mode);
            d->cookieFilePath = path;
        }
    }

    if (handle && d->cookieMode != 0 && !d->cookieFilePath.isEmpty()) {
        QByteArray cookiePathBytes = d->cookieFilePath.toUtf8();

        // ReadOnly (0x1) 或 ReadWrite (0x3)：从文件读取 cookie
        if (d->cookieMode & 0x1) {
            curl_easy_setopt(handle, CURLOPT_COOKIEFILE, cookiePathBytes.constData());
        }

        // WriteOnly (0x2) 或 ReadWrite (0x3)：将 cookie 写入文件
        if (d->cookieMode & 0x2) {
            curl_easy_setopt(handle, CURLOPT_COOKIEJAR, cookiePathBytes.constData());
        }
    }

    // ==================
    // HSTS/Alt-Svc cache 持久化（LC-50）：默认关闭；显式 opt-in
    // ==================

    if (handle && manager) {
        const auto cacheCfg     = manager->hstsAltSvcCacheConfig();
        d->hstsCachePathBytes   = cacheCfg.hstsFilePath().toUtf8();
        d->altSvcCachePathBytes = cacheCfg.altSvcFilePath().toUtf8();

        if (!d->hstsCachePathBytes.isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x074a00 /* 7.74.0 */
            curl_easy_setopt(handle, CURLOPT_HSTS, d->hstsCachePathBytes.constData());
#else
            Internal::appendReplyCapabilityWarning(d, QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_HSTS"));
#endif
        }

        if (!d->altSvcCachePathBytes.isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x074001 /* 7.64.1 */
            curl_easy_setopt(handle, CURLOPT_ALTSVC, d->altSvcCachePathBytes.constData());
#else
            Internal::appendReplyCapabilityWarning(d, QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_ALTSVC"));
#endif
        }
    }

    // ==================
    // Debug trace（M5）：默认关闭；启用后强制脱敏（不输出请求体明文）
    // ==================

    if (handle && manager && manager->debugTraceEnabled() && manager->logger()) {
        curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, QCNetworkReplyPrivate::curlDebugCallback);
        curl_easy_setopt(handle, CURLOPT_DEBUGDATA, d);
    }

    // ==================
    // 流式上传（M2）：重试前回滚上传源到起始位置
    // ==================

    Internal::clearReplyBodySourceError(d->requestBodySource);

    QString bodySourceError;
    if (!Internal::rewindReplyBodySourceForRetry(d, &bodySourceError)) {
        d->setError(NetworkError::InvalidRequest, bodySourceError);
        d->setState(ReplyState::Error);
        return;
    }

    d->setState(ReplyState::Running);

    if (d->executionMode == ExecutionMode::Async) {
        // 异步模式：注册到多句柄管理器
        QCCurlMultiManager::instance()->addReply(this);
        qDebug() << "QCNetworkReply::execute: Started async request for" << d->request.url();
    } else {
        // ==================
        // 同步模式：阻塞执行（支持重试）
        // ==================
        if (!handle) {
            d->setError(NetworkError::InvalidRequest, QStringLiteral("Invalid curl handle"));
            d->setState(ReplyState::Error);
            return;
        }

        // 重试循环（attemptCount 从 0 开始，表示首次尝试）
        while (true) {
            Internal::clearReplyBodySourceError(d->requestBodySource);

            QString retryBodySourceError;
            if (!Internal::rewindReplyBodySourceForRetry(d, &retryBodySourceError)) {
                d->setError(NetworkError::InvalidRequest, retryBodySourceError);
                d->setState(ReplyState::Error);
                return;
            }

            // 执行请求（阻塞调用）
            CURLcode result = curl_easy_perform(handle);

            // 检查 HTTP 状态码（即使 CURLcode 成功）
            long httpCode = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);

            const auto info = Internal::attemptErrorFromCurlAndHttp(d, result, httpCode);

            // 如果没有错误，标记为成功
            if (info.error == NetworkError::NoError) {
                qDebug() << "QCNetworkReply::execute: Sync request succeeded for"
                         << d->request.url() << "after" << d->attemptCount << "retries";
                d->setState(ReplyState::Finished);
                return;
            }

            const auto delay = Internal::advanceReplyRetryIfNeeded(d, info.error);
            if (delay.has_value()) {
                qDebug() << "QCNetworkReply::execute: Sync request failed, retrying after"
                         << delay.value().count() << "ms. Attempt" << d->attemptCount
                         << "Error:" << info.message;

                // 同步等待（阻塞当前线程）
                QThread::msleep(static_cast<unsigned long>(delay.value().count()));

                Internal::resetReplyForRetry(d, false);

                // 继续循环重试
                continue;
            }

            // 超过最大重试次数或错误不可重试
            qDebug() << "QCNetworkReply::execute: Sync request failed after" << d->attemptCount
                     << "attempts. Error:" << info.message;

            d->setError(info.error, info.message);
            d->setState(ReplyState::Error);
            return;
        }
    }
}


} // namespace QCurl
