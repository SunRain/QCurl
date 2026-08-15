#ifndef QCNETWORKDIAGNOSTICSOPERATION_P_H
#define QCNETWORKDIAGNOSTICSOPERATION_P_H

#include "QCNetworkDiagnostics.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QPromise>
#include <QTimer>

#include <functional>

namespace QCurl {

class QCNetworkCancelToken;

namespace Internal {

class DiagnosticsOperation final : public QObject
{
    Q_OBJECT

public:
    /** 创建由 deadline 驱动的恰好一次完成状态；取消令牌取消或析构时均停止操作。 */
    DiagnosticsOperation(QString operationName,
                         QString target,
                         int timeoutMs,
                         QCNetworkCancelToken *cancelToken);
    ~DiagnosticsOperation() override;

    /** 返回与当前操作共享状态的 Future。 */
    [[nodiscard]] QFuture<DiagResult> future() const;
    /** 返回操作启动后的经过时间。 */
    [[nodiscard]] qint64 elapsedMs() const;
    /** 返回操作是否已经完成或正在停止。 */
    [[nodiscard]] bool isFinished() const noexcept;

    /** 设置底层资源的取消回调。 */
    void setCancelHandler(std::function<void()> handler);
    /** 设置 deadline 触发时的底层资源停止回调。 */
    void setTimeoutHandler(std::function<void()> handler);
    /** 以一个结构化结果完成操作。 */
    void finish(DiagResult result);
    /** 构造失败结果并完成操作。 */
    void fail(const QString &summary, const QString &errorString);
    /** 终止底层资源，并以结构化取消结果完成操作。 */
    void cancel();

private:
    Q_DISABLE_COPY_MOVE(DiagnosticsOperation)

    void timeout();

    QString m_operationName;
    QString m_target;
    QPromise<DiagResult> m_promise;
    QElapsedTimer m_elapsed;
    QTimer m_deadline;
    QFutureWatcher<DiagResult> m_cancelWatcher;
    std::function<void()> m_cancelHandler;
    std::function<void()> m_timeoutHandler;
    bool m_finished = false;
    bool m_stopping = false;
};

/** 创建已完成的 Diagnostics Future。 */
[[nodiscard]] QFuture<DiagResult> finishedDiagnosticsFuture(DiagResult result);
/** 将公共 chrono timeout 饱和转换为 Qt timer 使用的毫秒值。 */
[[nodiscard]] int diagnosticsTimeoutMs(const QCNetworkDiagnosticsOptions &options);
/** 将子步骤结果转换为综合诊断 details。 */
[[nodiscard]] QVariantMap diagResultToVariantMap(const DiagResult &result);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKDIAGNOSTICSOPERATION_P_H
