/**
 * @file
 * @brief 声明基于 QObject 的网络传输任务。
 */

#ifndef QCNETWORKTRANSFERJOB_H
#define QCNETWORKTRANSFERJOB_H

#include "QCGlobal.h"
#include "QCNetworkError.h"

#include <QObject>
#include <QScopedPointer>

namespace QCurl {

class QCNetworkReply;
class QCNetworkTransferJobPrivate;

/**
 * @brief 异步网络传输任务基类。
 *
 * 派生类用于封装更高层传输流程，并统一 reply 关联和终态信号语义。构造函数只保存参数，
 * 不启动网络、不读写目标、不创建 reply。每个任务只发出一次 finished()；失败时先发出
 * failed()，再发出 finished()。底层 reply 仍由调用方按 Qt 生命周期在合适线程
 * deleteLater()。
 *
 * @note 错误生命周期：任务开始时没有错误；首次终态确定 `error()` 与 `errorString()`，
 * 成功时分别为 `NoError` 和空字符串，失败时错误码为权威分类。`finished()` 发出后状态固定。
 * @note QObject 借用合同：`reply()` 返回可空、non-owning 借用；reply 销毁会使借用失效并
 * 触发任务清理。job、reply 与可选 parent 必须处于同一 owner thread。
 */
class QCURL_EXPORT QCNetworkTransferJob : public QObject
{
    Q_OBJECT

public:
    ~QCNetworkTransferJob() override;

    /**
     * @brief 返回任务关联的底层 reply。
     * @return 已创建的 reply 指针；请求启动前失败时返回 nullptr。
     */
    [[nodiscard]] QCNetworkReply *reply() const noexcept;

    /// 任务已锁定终态时返回 true，包括 failed()/finished() 的直接槽执行期间。
    [[nodiscard]] bool isFinished() const noexcept;

    /// 返回终态错误码；成功时返回 NetworkError::NoError。
    [[nodiscard]] NetworkError error() const noexcept;

    /// 返回终态错误文本；成功时返回空字符串。
    [[nodiscard]] QString errorString() const;

Q_SIGNALS:
    /// 任务到达终态时至多发出一次；failed() 的直接槽销毁任务后不再发出。
    void finished();

    /// 任务以错误终止时在 finished() 之前发出。
    void failed(QCurl::NetworkError errorCode, const QString &message);

    /// 转发底层 reply 报告的传输进度。
    void progress(qint64 bytesReceived, qint64 bytesTotal);

protected:
    explicit QCNetworkTransferJob(QObject *parent = nullptr);

    /// 保存派生类在校验通过后创建的 reply。
    void setReply(QCNetworkReply *reply);

    /// 标记任务失败并发出 failed()；任务仍存活时继续发出 finished()。
    void fail(QCurl::NetworkError errorCode, const QString &message);

    /// 标记任务成功，并发出 finished()。
    void finish();

    /// 按 reply 当前错误状态结束任务；成功时 finish()，失败时 fail()。
    void finishFromReply(QCNetworkReply *reply);

    /// 在底层 reply 被销毁但 job 尚未完成时，按统一取消语义结束任务。
    void failBecauseReplyDestroyed();

private:
    Q_DISABLE_COPY_MOVE(QCNetworkTransferJob)

    Q_DECLARE_PRIVATE(QCNetworkTransferJob)
    QScopedPointer<QCNetworkTransferJobPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKTRANSFERJOB_H
