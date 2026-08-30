/**
 * @file
 * @brief Implements deterministic chunking, pause, and cancel mock replay hooks.
 */

#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkMockProvider_p.h"
#include "private/QCNetworkReplyMockChaos_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>
#include <random>

namespace QCurl::Internal {

#ifdef QCURL_ENABLE_TEST_HOOKS
namespace {

constexpr quint32 kFnv1a32OffsetBasis = 2166136261u;
constexpr quint32 kFnv1a32Prime       = 16777619u;

enum class MockChaosAction {
    None,
    PauseRecv,
    Cancel,
};

enum class MockChaosActionPoint {
    BeforeFirstChunk,
    AfterFirstChunk,
    BeforeFinish,
};

struct MockChaosConfig
{
    quint32 seed                     = 1u;
    int maxChunkBytes                = 0;
    int chunkDelayMs                 = 0;
    MockChaosAction action           = MockChaosAction::None;
    MockChaosActionPoint actionPoint = MockChaosActionPoint::AfterFirstChunk;
    int resumeDelayMs                = 0;
};

struct MockChaosReplayState
{
    QPointer<QCNetworkReply> reply;
    QCNetworkReplyPrivate *replyPrivate = nullptr;
    QCNetworkMockData mockData;
    QList<QByteArray> chunks;
    MockChaosConfig config;
    qint64 totalBytes  = 0;
    int nextChunkIndex = 0;
    bool initialized   = false;
    bool actionFired   = false;
};

[[nodiscard]] quint32 fnv1a32(const QByteArray &bytes)
{
    quint32 hash = kFnv1a32OffsetBasis;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= kFnv1a32Prime;
    }
    return hash;
}

bool parseNonNegativeInt(const QByteArray &value, int *target)
{
    bool ok          = false;
    const int parsed = value.toInt(&ok);
    if (ok) {
        *target = qMax(0, parsed);
    }
    return ok;
}

[[nodiscard]] bool applyNumericSetting(MockChaosConfig *config,
                                       const QByteArray &key,
                                       const QByteArray &value)
{
    if (key == "seed") {
        bool ok            = false;
        const quint32 seed = value.toUInt(&ok);
        if (ok) {
            config->seed = seed;
        }
        return true;
    }
    if (key == "max_chunk_bytes" || key == "chunk_bytes") {
        parseNonNegativeInt(value, &config->maxChunkBytes);
        return true;
    }
    if (key == "chunk_delay_ms") {
        parseNonNegativeInt(value, &config->chunkDelayMs);
        return true;
    }
    if (key == "resume_delay_ms") {
        parseNonNegativeInt(value, &config->resumeDelayMs);
        return true;
    }
    return false;
}

void applyActionSetting(MockChaosConfig *config, const QByteArray &key, const QByteArray &value)
{
    if (key == "action") {
        if (value == "pause" || value == "pause_recv") {
            config->action = MockChaosAction::PauseRecv;
        } else if (value == "cancel") {
            config->action = MockChaosAction::Cancel;
        } else {
            config->action = MockChaosAction::None;
        }
        return;
    }
    if (key != "action_point") {
        return;
    }
    if (value == "before_first_chunk") {
        config->actionPoint = MockChaosActionPoint::BeforeFirstChunk;
    } else if (value == "before_finish") {
        config->actionPoint = MockChaosActionPoint::BeforeFinish;
    } else {
        config->actionPoint = MockChaosActionPoint::AfterFirstChunk;
    }
}

[[nodiscard]] std::optional<MockChaosConfig> mockChaosConfigFromEnvironment()
{
    const QByteArray raw = qgetenv("QCURL_TEST_MOCK_CHAOS").trimmed();
    if (raw.isEmpty() || raw == "0") {
        return std::nullopt;
    }

    MockChaosConfig config;
    for (QByteArray entry : raw.split(';')) {
        entry               = entry.trimmed();
        const int separator = entry.indexOf('=');
        if (entry.isEmpty() || entry == "1" || separator <= 0) {
            continue;
        }

        const QByteArray key   = entry.left(separator).trimmed().toLower();
        const QByteArray value = entry.mid(separator + 1).trimmed().toLower();
        if (key == "enabled" && (value == "0" || value == "false" || value == "off")) {
            return std::nullopt;
        }
        if (!applyNumericSetting(&config, key, value)) {
            applyActionSetting(&config, key, value);
        }
    }
    return config;
}

[[nodiscard]] QList<QByteArray> buildDeterministicChunks(const QByteArray &payload,
                                                         const MockChaosConfig &config,
                                                         HttpMethod method,
                                                         const QUrl &url)
{
    if (payload.isEmpty()) {
        return {};
    }
    const int maximum = config.maxChunkBytes > 0 ? config.maxChunkBytes : payload.size();
    if (maximum >= payload.size()) {
        return {payload};
    }

    const QByteArray salt = QByteArray::number(static_cast<int>(method)) + QByteArrayLiteral("|")
                            + url.toString().toUtf8() + QByteArrayLiteral("|")
                            + QByteArray::number(payload.size());
    std::mt19937 engine(config.seed ^ fnv1a32(salt));
    QList<QByteArray> chunks;
    for (int offset = 0; offset < payload.size();) {
        const int upper = qMin(maximum, payload.size() - offset);
        const int size  = std::uniform_int_distribution<int>(1, upper)(engine);
        chunks.append(payload.mid(offset, size));
        offset += size;
    }
    return chunks;
}

void scheduleReplayStep(const std::shared_ptr<MockChaosReplayState> &state, int delayMs);

[[nodiscard]] bool triggerAction(const std::shared_ptr<MockChaosReplayState> &state,
                                 MockChaosActionPoint point)
{
    if (!state || !state->reply || state->actionFired
        || state->config.action == MockChaosAction::None || state->config.actionPoint != point) {
        return false;
    }

    state->actionFired    = true;
    QCNetworkReply *reply = state->reply.data();
    if (state->config.action == MockChaosAction::Cancel) {
        reply->cancel();
        return true;
    }

    state->replyPrivate->userPauseMask = CURLPAUSE_RECV;
    if (state->replyPrivate->setState(ReplyState::Paused) == SignalEmissionResult::Destroyed) {
        return true;
    }
    if (reply->state() == ReplyState::Paused && state->config.resumeDelayMs > 0) {
        QPointer<QCNetworkReply> safeReply(reply);
        auto *replyPrivate = state->replyPrivate;
        QTimer::singleShot(state->config.resumeDelayMs, reply, [safeReply, replyPrivate]() {
            if (safeReply && replyPrivate->state == ReplyState::Paused) {
                replyPrivate->userPauseMask = 0;
                Q_UNUSED(replyPrivate->setState(ReplyState::Running));
            }
        });
    }
    return reply->state() == ReplyState::Paused;
}

void initializeReplay(const std::shared_ptr<MockChaosReplayState> &state)
{
    if (state->initialized) {
        return;
    }
    resetReplyForRetry(state->replyPrivate, false);
    state->replyPrivate->downloadTotal   = state->totalBytes;
    state->replyPrivate->bytesDownloaded = 0;
    applyMockResponseHeaders(state->replyPrivate, state->mockData);
    state->initialized = true;
}

SignalEmissionResult emitNextChunk(const std::shared_ptr<MockChaosReplayState> &state)
{
    if (state->nextChunkIndex >= state->chunks.size()) {
        return SignalEmissionResult::Alive;
    }
    const QByteArray chunk = state->chunks.at(state->nextChunkIndex++);
    if (chunk.isEmpty()) {
        return SignalEmissionResult::Alive;
    }

    state->replyPrivate->bodyBuffer.append(chunk);
    state->replyPrivate->bytesDownloaded += chunk.size();
    const QPointer<QCNetworkReply> observer = state->reply;
    if (emitReplySignal(observer, [](QCNetworkReply *reply) { Q_EMIT reply->readyRead(); })
        == SignalEmissionResult::Destroyed) {
        return SignalEmissionResult::Destroyed;
    }
    const qint64 bytesDownloaded = state->replyPrivate->bytesDownloaded;
    const qint64 downloadTotal   = state->replyPrivate->downloadTotal;
    return emitReplySignal(observer, [bytesDownloaded, downloadTotal](QCNetworkReply *reply) {
        Q_EMIT reply->downloadProgress(bytesDownloaded, downloadTotal);
    });
}

void finishReplay(const std::shared_ptr<MockChaosReplayState> &state)
{
    const auto info = attemptErrorFromMockData(state->replyPrivate, state->mockData);
    if (info.error == NetworkError::NoError) {
        Q_UNUSED(state->replyPrivate->setState(ReplyState::Finished));
        return;
    }

    const auto retry = advanceReplyRetryIfNeeded(state->replyPrivate, info.error);
    if (retry.emissionResult == SignalEmissionResult::Destroyed) {
        return;
    }
    if (retry.delay.has_value()) {
        scheduleAsyncReplyRetry(state->reply, state->replyPrivate, retry.delay.value());
        return;
    }
    state->replyPrivate->setError(info.error, info.message);
    Q_UNUSED(state->replyPrivate->setState(ReplyState::Error));
}

void runReplayStep(const std::shared_ptr<MockChaosReplayState> &state)
{
    if (!state || !state->reply || !state->replyPrivate) {
        return;
    }
    const ReplyState current = state->replyPrivate->state;
    if (current == ReplyState::Cancelled || current == ReplyState::Finished
        || current == ReplyState::Error) {
        return;
    }
    if (current == ReplyState::Paused) {
        scheduleReplayStep(state, 1);
        return;
    }

    initializeReplay(state);
    if (state->nextChunkIndex == 0 && triggerAction(state, MockChaosActionPoint::BeforeFirstChunk)) {
        if (state->reply && state->replyPrivate->state == ReplyState::Paused) {
            scheduleReplayStep(state, 1);
        }
        return;
    }

    if (emitNextChunk(state) == SignalEmissionResult::Destroyed) {
        return;
    }
    if (state->nextChunkIndex == 1 && triggerAction(state, MockChaosActionPoint::AfterFirstChunk)) {
        if (state->reply && state->replyPrivate->state == ReplyState::Paused) {
            scheduleReplayStep(state, 1);
        }
        return;
    }
    if (state->nextChunkIndex < state->chunks.size()) {
        scheduleReplayStep(state, state->config.chunkDelayMs);
        return;
    }
    if (!triggerAction(state, MockChaosActionPoint::BeforeFinish)) {
        finishReplay(state);
    } else if (state->reply && state->replyPrivate->state == ReplyState::Paused) {
        scheduleReplayStep(state, 1);
    }
}

void scheduleReplayStep(const std::shared_ptr<MockChaosReplayState> &state, int delayMs)
{
    if (!state || !state->reply) {
        return;
    }
    QTimer::singleShot(qMax(0, delayMs), state->reply.data(), [state]() { runReplayStep(state); });
}

} // namespace
#endif

bool startMockChaosReplay(QCNetworkReply *reply,
                          QCNetworkReplyPrivate *replyPrivate,
                          const QCNetworkMockData &mockData,
                          HttpMethod method,
                          const QUrl &url)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const auto config = mockChaosConfigFromEnvironment();
    if (!config.has_value()) {
        return false;
    }

    auto state          = std::make_shared<MockChaosReplayState>();
    state->reply        = reply;
    state->replyPrivate = replyPrivate;
    state->mockData     = mockData;
    state->config       = config.value();
    state->totalBytes   = mockData.response.size();
    state->chunks       = buildDeterministicChunks(mockData.response, state->config, method, url);
    scheduleReplayStep(state, 0);
    return true;
#else
    Q_UNUSED(reply)
    Q_UNUSED(replyPrivate)
    Q_UNUSED(mockData)
    Q_UNUSED(method)
    Q_UNUSED(url)
    return false;
#endif
}

} // namespace QCurl::Internal
