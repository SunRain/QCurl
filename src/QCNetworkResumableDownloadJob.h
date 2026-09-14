/**
 * @file
 * @brief 声明支持断点续传的文件下载任务。
 */

#ifndef QCNETWORKRESUMABLEDOWNLOADJOB_H
#define QCNETWORKRESUMABLEDOWNLOADJOB_H

#include "QCNetworkTransferJob.h"

#include <QScopedPointer>
#include <QUrl>

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkRequest;
class QCNetworkResumableDownloadJobPrivate;

/**
 * @brief 以 HTTP byte-range 语义下载文件并支持断点续传。
 *
 * 调用 start() 后，当 `overwrite` 为 false 且目标文件已存在时，任务会发送 `Range: bytes=N-`。只有服务端
 * 返回 identity 表示且 Content-Range 起点、终点、总长与本次实际字节数都完整匹配时才成功。
 * 响应头中可判定的矛盾在写入前拒绝；服务端忽略 Range 返回 200 时安全覆盖，匹配本地长度
 * 的 416 表示已经完成。206 压缩表示不与解码后的文件偏移混用，直接拒绝。
 * 重试前仅恢复该任务独占 writer 的输出：追加截回原长度，覆盖丢弃临时文件，新文件清空；
 * 恢复失败即终止，不改变请求方法、幂等键或重试次数限制。
 *
 * @note 错误生命周期：resume 校验、文件打开、写入或提交失败进入基类的唯一失败终态；
 * 成功时错误为空，`finished()` 后状态固定。失败的安全覆盖不替换原文件；新目标可能保留
 * 未完成数据，调用方不得在失败后把它视为完整下载。
 * @note QObject 借用合同：`manager` 必须非空且由调用方保活到任务完成；job 不拥有 manager。
 * job、manager 与 reply 必须处于同一 owner thread，manager 销毁后借用立即失效。
 */
class QCURL_EXPORT QCNetworkResumableDownloadJob final : public QCNetworkTransferJob
{
    Q_OBJECT

public:
    ~QCNetworkResumableDownloadJob() override;

    /**
     * @brief 基于完整请求创建断点续传下载任务。
     * @param manager 用于创建 GET reply 的网络访问管理器。
     * @param request 下载请求配置。
     * @param savePath 目标文件路径。
     * @param overwrite 是否忽略已有字节并覆盖目标文件。
     * @param parent 任务的可选 QObject parent。
     */
    explicit QCNetworkResumableDownloadJob(QCNetworkAccessManager *manager,
                                           const QCNetworkRequest &request,
                                           const QString &savePath,
                                           bool overwrite  = false,
                                           QObject *parent = nullptr);

    /**
     * @brief 基于 URL 创建断点续传下载任务。
     * @param manager 用于创建 GET reply 的网络访问管理器。
     * @param url 下载 URL。
     * @param savePath 目标文件路径。
     * @param overwrite 是否忽略已有字节并覆盖目标文件。
     * @param parent 任务的可选 QObject parent。
     */
    explicit QCNetworkResumableDownloadJob(QCNetworkAccessManager *manager,
                                           const QUrl &url,
                                           const QString &savePath,
                                           bool overwrite  = false,
                                           QObject *parent = nullptr);

    /**
     * @brief 排队执行异步校验并启动请求。
     *
     * 构造函数只保存参数，不读取目标文件、不创建 reply、不启动网络。重复调用会被忽略。
     * 校验执行时，当前 job 与 manager 必须位于同一线程；reply 创建后由调用方按 Qt
     * 生命周期 deleteLater()。目标文件打开、Content-Range 校验和提交错误都会通过
     * failed() 后接 finished() 报告。
     */
    void start();

    /// 返回当前任务使用的目标文件路径。
    [[nodiscard]] QString savePath() const;

    /// 返回 start() 后用于构造初始 Range 请求头的本地已有字节数；启动前为 0。
    [[nodiscard]] qint64 existingSize() const noexcept;

private:
    Q_DISABLE_COPY_MOVE(QCNetworkResumableDownloadJob)

    /// 在 start() 之后执行校验、Range 计算、reply 创建和信号连接。
    void doStart();
    Q_DECL_HIDDEN void attachDownloadReply(QCNetworkReply *networkReply, bool hadExistingFile);

    /**
     * @brief 在 job owner thread 提交或取消唯一 writer 后传播 reply 终态。
     * @param reply 当前任务关联的非 owning reply 借用。
     */
    void handleReplyFinished(QCNetworkReply *reply);

    Q_DECLARE_PRIVATE(QCNetworkResumableDownloadJob)
    QScopedPointer<QCNetworkResumableDownloadJobPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKRESUMABLEDOWNLOADJOB_H
