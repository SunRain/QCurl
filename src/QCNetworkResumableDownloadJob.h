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
 * 返回匹配本地大小的 206 Content-Range 时才追加写入；范围不匹配会失败，避免污染目标文件。
 */
class QCURL_EXPORT QCNetworkResumableDownloadJob final : public QCNetworkTransferJob
{
    Q_OBJECT

public:
    ~QCNetworkResumableDownloadJob() override;
    Q_DISABLE_COPY_MOVE(QCNetworkResumableDownloadJob)

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
                                           bool overwrite = false,
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
                                           bool overwrite = false,
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
    /// 在 start() 之后执行校验、Range 计算、reply 创建和信号连接。
    void doStart();

    Q_DECLARE_PRIVATE(QCNetworkResumableDownloadJob)
    QScopedPointer<QCNetworkResumableDownloadJobPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKRESUMABLEDOWNLOADJOB_H
