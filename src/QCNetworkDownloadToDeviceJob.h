/**
 * @file
 * @brief 声明流式下载到设备的传输任务。
 */

#ifndef QCNETWORKDOWNLOADTODEVICEJOB_H
#define QCNETWORKDOWNLOADTODEVICEJOB_H

#include "QCNetworkTransferJob.h"

#include <QScopedPointer>
#include <QUrl>

class QIODevice;

namespace QCurl {

class QCNetworkAccessManager;
class QCNetworkDownloadToDeviceJobPrivate;
class QCNetworkRequest;

/**
 * @brief 将 GET 响应流式写入调用方持有的 QIODevice。
 *
 * 构造函数不启动网络请求。start() 会异步排队执行校验与请求启动，因此无效输入通过任务信号
 * 报告，而不是在构造期间同步失败。
 * 一旦响应数据交付设备，本次请求失败后不再透明重试，即使设备可以 seek 也不会读取、
 * 截断或回滚调用方数据。尚未交付响应体时仍遵循请求原有的重试安全策略。
 *
 * @note 错误生命周期：设备校验、打开或写入错误进入基类的唯一失败终态；成功时错误为空，
 * `finished()` 后状态固定，任务不会继续写入设备。
 * @note QObject 借用合同：`manager` 与 `device` 必须非空，均为 non-owning 借用；调用方须保活
 * 到任务完成。job、reply、manager 与 device 必须处于同一 owner thread，借用对象销毁后失效。
 */
class QCURL_EXPORT QCNetworkDownloadToDeviceJob final : public QCNetworkTransferJob
{
    Q_OBJECT

public:
    ~QCNetworkDownloadToDeviceJob() override;

    /**
     * @brief 基于完整请求创建下载任务。
     * @param manager 用于创建并启动 GET reply 的网络访问管理器。
     * @param request 下载请求配置。
     * @param device 借用的目标设备；任务完成前必须保持存活且可写。
     * @param parent 任务的可选 QObject parent。
     */
    explicit QCNetworkDownloadToDeviceJob(QCNetworkAccessManager *manager,
                                          const QCNetworkRequest &request,
                                          QIODevice *device,
                                          QObject *parent = nullptr);

    /**
     * @brief 基于 URL 创建下载任务。
     * @param manager 用于创建并启动 GET reply 的网络访问管理器。
     * @param url 下载 URL。
     * @param device 借用的目标设备；任务完成前必须保持存活且可写。
     * @param parent 任务的可选 QObject parent。
     */
    explicit QCNetworkDownloadToDeviceJob(QCNetworkAccessManager *manager,
                                          const QUrl &url,
                                          QIODevice *device,
                                          QObject *parent = nullptr);

    /**
     * @brief 排队执行异步校验并启动请求。
     *
     * 重复调用会被忽略。校验执行时，`manager`、当前 job 和 `device` 必须位于同一线程；
     * 若目标设备在传输期间销毁或变为不可写，任务会以可诊断错误结束。
     * reply 创建后会在 finished/error 后保持可通过 reply() 取得；调用方负责 deleteLater()。
     */
    void start();

private:
    Q_DISABLE_COPY_MOVE(QCNetworkDownloadToDeviceJob)

    /// 在 start() 之后执行校验、reply 创建和信号连接。
    void doStart();

    Q_DECLARE_PRIVATE(QCNetworkDownloadToDeviceJob)
    QScopedPointer<QCNetworkDownloadToDeviceJobPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKDOWNLOADTODEVICEJOB_H
