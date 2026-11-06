/**
 * @file
 * @brief 集中管理请求生命周期、结果诊断、计时及公开观察事实。
 */

#ifndef QCREPLYLIFECYCLE_P_H
#define QCREPLYLIFECYCLE_P_H

#include "QCNetworkError.h"
#include "QCNetworkReply.h"

#include <QElapsedTimer>
#include <QStringList>

#include <curl/curl.h>
#include <optional>

namespace QCurl::Internal {

/** @brief 当前 attempt 的错误分类结果；NoError 表示允许提交成功终态。 */
struct ReplyAttemptErrorInfo
{
    NetworkError error = NetworkError::NoError;
    QString message;
};

/**
 * @brief 不发布 Qt 信号的请求级生命周期。
 *
 * 显式重试重置保留首次 Running 起算的时钟和累积 warning；普通转换不能离开终态。
 */
class Q_DECL_HIDDEN QCReplyLifecycle
{
public:
    /// 前置协作完成后提交状态；重复转换或已到终态时返回 false。
    [[nodiscard]] bool transition(ReplyState state);
    /// 清当前 attempt，不重启请求时钟，也不清除已累积的 warnings。
    void resetForRetry();
    /// 回放初始化只清原生诊断和进度，不改变当前 phase 或错误。
    void resetReplayObservations();
    void recordError(NetworkError error, const QString &message);
    void clearNativeDiagnostic();
    void acceptCachedResult();
    void cancelSilently();

    /// 先保存实际原生码，再按来源错误、原生错误与 HTTP 结果的优先级分类。
    [[nodiscard]] ReplyAttemptErrorInfo classifyNetworkResult(
        CURLcode code,
        int httpStatus,
        const std::optional<ReplyAttemptErrorInfo> &sourceFailure,
        bool sourcePresent,
        bool rangeSatisfied);
    /// 回放不具有原生诊断；显式错误优先于 HTTP status，来源错误优先于两者。
    [[nodiscard]] ReplyAttemptErrorInfo classifyMockResult(
        bool explicitError,
        NetworkError error,
        int httpStatus,
        const std::optional<ReplyAttemptErrorInfo> &sourceFailure);

    void observeNetworkProgress(qint64 downloadTotal,
                                qint64 downloaded,
                                qint64 uploadTotal,
                                qint64 uploaded);
    void observeBodyChunk(qint64 bytes);
    void beginReplay(qint64 total);
    void observeReplayedBody(qint64 bytes);
    void appendWarnings(const QStringList &warnings);

    [[nodiscard]] ReplyState state() const noexcept { return m_state; }
    [[nodiscard]] bool isTerminal() const noexcept;
    [[nodiscard]] NetworkError error() const noexcept { return m_error; }
    [[nodiscard]] QString errorMessage() const { return m_errorMessage; }
    [[nodiscard]] int diagnosticCurlCode() const noexcept;
    [[nodiscard]] qint64 durationMs() const noexcept { return m_durationMs; }
    [[nodiscard]] qint64 elapsedMs() const noexcept;
    [[nodiscard]] qint64 bytesDownloaded() const noexcept { return m_bytesDownloaded; }
    [[nodiscard]] qint64 bytesUploaded() const noexcept { return m_bytesUploaded; }
    [[nodiscard]] qint64 downloadTotal() const noexcept { return m_downloadTotal; }
    [[nodiscard]] qint64 uploadTotal() const noexcept { return m_uploadTotal; }
    [[nodiscard]] QStringList warnings() const { return m_warnings; }

private:
    ReplyState m_state   = ReplyState::Idle;
    NetworkError m_error = NetworkError::NoError;
    QString m_errorMessage;
    int m_nativeCode    = 0;
    qint64 m_durationMs = -1;
    QElapsedTimer m_clock;
    qint64 m_bytesDownloaded = 0;
    qint64 m_bytesUploaded   = 0;
    qint64 m_downloadTotal   = -1;
    qint64 m_uploadTotal     = -1;
    QStringList m_warnings;
};

} // namespace QCurl::Internal

#endif // QCREPLYLIFECYCLE_P_H
