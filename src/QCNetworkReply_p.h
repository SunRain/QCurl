/**
 * @file
 * @brief 声明 QCNetworkReply 的内部状态与辅助接口。
 */

#ifndef QCNETWORKREPLY_P_H
#define QCNETWORKREPLY_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the QCurl API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "QCCurlHandleManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "private/QCCurlPersistentTransferBridge_p.h"
#include "private/QCNetworkReplyCache_p.h"
#include "private/QCNetworkReplySignal_p.h"
#include "private/QCNetworkReplyTransferState_p.h"
#include "private/QCReplyRetry_p.h"
#include "private/QCRequestPipeline_p.h"

#include <QMap>
#include <QPointer>
#include <QString>
#include <QStringList>

class QFile;

namespace QCurl {

namespace Internal {
QByteArray testCurlPlanDigest(const QCNetworkReply *reply);
}

/**
 * @brief QCNetworkReply 私有实现类
 *
 * 使用 Pimpl 模式隐藏实现细节，提供 curl 封装和数据管理。
 *
 * @note Qt 的 Q_DECLARE_PRIVATE 宏要求私有类命名为 QCNetworkReplyPrivate
 *
 * @internal
 */
class Q_DECL_HIDDEN QCNetworkReplyPrivate
{
    Q_DECLARE_PUBLIC(QCNetworkReply)

public:
    /**
     * @brief 返回公共对象的非拥有型回指。
     *
     * 供不属于私有类成员的 libcurl 回调读取 QObject 线程和信号边界。
     */
    QCNetworkReply *qObject() const noexcept { return q_ptr; }

    /**
     * @brief 构造函数
     *
     * @param q 公共类指针（q_ptr）
     * @param req 网络请求配置
     * @param method HTTP 方法
     * @param body 请求体数据（用于 POST/PUT/PATCH）
     */
    explicit QCNetworkReplyPrivate(QCNetworkReply *q,
                                   const QCNetworkRequest &req,
                                   HttpMethod method,
                                   const Internal::RequestBody &requestBodySource,
                                   const QByteArray &body);

    /**
     * @brief 析构函数
     *
     * 确保资源正确清理。
     */
    ~QCNetworkReplyPrivate();

    // 配置信息

    QCNetworkRequest request; ///< 网络请求配置
    HttpMethod httpMethod;    ///< HTTP 方法（HEAD/GET/POST等）
    QSharedPointer<QCNetworkReplyTransferState> transferState;

    // Curl 管理

    QCCurlHandleManager curlManager;       ///< add 前以及 multi detach 后的句柄管理器
    QCCurlTransferToken transferToken = 0; ///< 仅 completion 或 admission 回滚可清零。
    bool transferRemovalRequested     = false;
    bool poolRequestActive            = false;

    // 重试机制

    Internal::QCReplyRetry retry;
    // 仅独占文件 writer 可恢复本次输出；通用消费者没有回滚合同。
    std::function<std::optional<QString>()> restoreResponseBeforeRetry;

    // 缓存集成

    Internal::QCReplyCache cache;

    // Manager service snapshots

    QCNetworkLoggerHandle &logger;
    bool &debugTraceEnabled;

    // 内部方法

    /// 在公开终态或析构前结束网络请求统计，仅执行一次。
    void finishPoolRequest();

    /**
     * @brief 配置 curl 选项
     *
     * 按原插点协调非 TLS 配置、设备准备、TLS 与 callback 绑定。
     * 规则和原生借用存储由共享 holder 中的领域对象拥有。
     *
     * @return true 配置成功，false 失败
     */
    [[nodiscard]] bool configureCurlOptions();

    /// 返回当前 attempt 的 easy handle；multi 在途时句柄由 transfer record 持有。
    [[nodiscard]] CURL *activeCurlHandle() const noexcept;

    /**
     * @brief 设置状态并发射信号
     *
     * 集中管理状态转换，自动发射 stateChanged()、finished()、error() 等信号。
     *
     * @param newState 新状态
     */
    [[nodiscard]] Internal::SignalEmissionResult setState(ReplyState newState);

    /**
     * @brief 设置错误信息
     *
     * @param error 错误码
     * @param message 错误描述
     */
    void setError(NetworkError error, const QString &message);

    /**
     * @brief 异步 multi 完成入口（点对点投递到 reply 线程）
     *
     * reply-side completion adapter 先回收 detached handle、清零 token，再在
     * reply 所在线程调用本方法，处理完成、重试和终态收敛。
     *
     * @note 必须在 Reply 线程执行；调用方应保证在解锁 multi manager 后再投递回调。
     */
    void onCurlMultiFinished(CURLcode curlCode, long httpStatusCode);
    /// 网络或本地到期分类后的共同收敛入口，保留既有重试与缓存回退。
    void finishAttempt(const Internal::ReplyAttemptErrorInfo &info);

    // Curl 静态回调函数（C 接口）

    /**
     * @brief 写回调（接收响应体数据）
     *
     * curl 调用此函数传递下载的数据。
     *
     * @param ptr 数据指针
     * @param size 数据大小（通常为 1）
     * @param nmemb 数据块数量
     * @param userdata 用户数据（Private* 指针）
     * @return size_t 处理的字节数（size * nmemb），返回其他值会中止传输
     */
    static size_t curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata);

    /**
     * @brief 响应头回调
     *
     * curl 调用此函数传递响应头数据。
     *
     * @param ptr 数据指针
     * @param size 数据大小（通常为 1）
     * @param nmemb 数据块数量
     * @param userdata 用户数据（Private* 指针）
     * @return size_t 处理的字节数
     */
    static size_t curlHeaderCallback(char *ptr, size_t size, size_t nmemb, void *userdata);

    /**
     * @brief 读回调（发送请求体数据，用于 PUT 等上传操作）
     *
     * @param ptr 缓冲区指针
     * @param size 缓冲区大小（通常为 1）
     * @param nmemb 缓冲区块数量
     * @param userdata 用户数据（Private* 指针）
     * @return size_t 实际写入的字节数
     */
    static size_t curlReadCallback(char *ptr, size_t size, size_t nmemb, void *userdata);

    /**
     * @brief 定位回调（用于重新定位数据流）
     *
     * @param userdata 用户数据（Private* 指针）
     * @param offset 偏移量
     * @param origin 起始位置（SEEK_SET/SEEK_CUR/SEEK_END）
     * @return int CURL_SEEKFUNC_OK（成功）或其他错误码
     */
    static int curlSeekCallback(void *userdata, curl_off_t offset, int origin);

    /**
     * @brief 进度回调
     *
     * curl 调用此函数报告上传/下载进度。
     *
     * @param userdata 用户数据（Private* 指针）
     * @param dltotal 下载总字节数
     * @param dlnow 已下载字节数
     * @param ultotal 上传总字节数
     * @param ulnow 已上传字节数
     * @return int 返回 0 继续传输，非 0 中止传输
     */
    static int curlProgressCallback(
        void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    /**
     * @brief libcurl debug 回调（仅在显式启用 verbose/debug trace 时调用）
     *
     * @note 必须对敏感信息（Authorization/Cookie 等）做强制脱敏
     */
    static int curlDebugCallback(
        CURL *handle, curl_infotype type, char *data, size_t size, void *userptr);

private:
    /// 指向公共接口对象的非拥有型回指。
    QCNetworkReply *q_ptr;
};

} // namespace QCurl

#endif // QCNETWORKREPLY_P_H
