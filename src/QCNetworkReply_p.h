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
#include "QCNetworkReply.h"
#include "private/QCRequestPipeline_p.h"
#include "qbytedata_p.h"

#include <QElapsedTimer>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QStringList>

class QFile;

namespace QCurl {

namespace Internal {
QByteArray testCurlPlanDigest(const QCNetworkReply *reply);
}

class CurlMultiHandleProcesser; // 前向声明

/**
 * @brief QCNetworkReply 私有实现类
 *
 * 使用 Pimpl 模式隐藏实现细节，提供 curl 封装和数据管理。
 *
 * @note Qt 的 Q_DECLARE_PRIVATE 宏要求私有类命名为 QCNetworkReplyPrivate
 *
 * @internal
 */
class QCNetworkReplyPrivate
{
public:
    /**
     * @brief 构造函数
     *
     * @param q 公共类指针（q_ptr）
     * @param req 网络请求配置
     * @param method HTTP 方法
     * @param mode 执行模式（同步/异步）
     * @param body 请求体数据（用于 POST/PUT/PATCH）
     */
    explicit QCNetworkReplyPrivate(QCNetworkReply *q,
                                   const QCNetworkRequest &req,
                                   HttpMethod method,
                                   ExecutionMode mode,
                                   const QByteArray &body);

    /**
     * @brief 析构函数
     *
     * 确保资源正确清理。
     */
    ~QCNetworkReplyPrivate();

    // ==================
    // 公共类指针（Qt Pimpl 模式）
    // ==================

    QCNetworkReply *q_ptr;
    Q_DECLARE_PUBLIC(QCNetworkReply)

    // ==================
    // 配置信息
    // ==================

    QCNetworkRequest request;    ///< 网络请求配置
    HttpMethod httpMethod;       ///< HTTP 方法（HEAD/GET/POST等）
    ExecutionMode executionMode; ///< 执行模式（异步/同步）
    QByteArray requestBody;      ///< 请求体数据（POST/PUT/PATCH使用）
    Internal::NormalizedRequest normalizedRequest;
    Internal::CurlPlan curlPlan;

    // ==================
    // Curl 管理
    // ==================

    QCCurlHandleManager curlManager;          ///< RAII curl 句柄管理器
    CurlMultiHandleProcesser *multiProcessor; ///< 多句柄管理器（异步模式使用）

    // ==================
    // 数据缓冲
    // ==================

    QCByteDataBuffer bodyBuffer;      ///< 响应体缓冲区（异步模式使用）
    QByteArray headerData;            ///< 原始响应头数据
    QMap<QString, QString> headerMap; ///< 解析后的响应头键值对

    // ==================
    // 状态管理
    // ==================

    ReplyState state;           ///< 当前状态（Idle/Running/Paused/Finished等）
    NetworkError errorCode;     ///< 错误码（NetworkNoError = 0）
    QString errorMessage;       ///< 错误描述信息
    int httpStatusCode = 0;     ///< HTTP 状态码（0 表示未知/未返回）
    qint64 durationMs  = -1;    ///< 总耗时（毫秒，-1 表示未知/未完成）
    QElapsedTimer elapsedTimer; ///< 耗时统计（跨重试/延迟）
    bool elapsedTimerStarted = false;

    // ==================
    // 传输级 pause/resume mask（P2）
    // ==================

    int userPauseMask     = 0; ///< 用户显式 pause（CURLPAUSE_* mask）
    int internalPauseMask = 0; ///< 内部流控 pause（CURLPAUSE_* mask）
    int appliedPauseMask  = 0; ///< 最近一次已应用到 libcurl 的 mask（用于幂等）

    // ==================
    // 下载 backpressure（P2）
    // ==================

    qint64 backpressureLimitBytes        = 0; ///< <=0 表示禁用
    qint64 backpressureResumeBytes       = 0; ///< <=0 表示默认（limit/2）
    qint64 backpressurePeakBufferedBytes = 0; ///< 生命周期内 bodyBuffer 峰值（bytes）
    bool backpressureActive              = false;

    // ==================
    // 上传 source pause/resume（P2）
    // ==================

    bool uploadSendPaused = false; ///< 上传发送方向内部 pause（source not ready；仅 Async）

    // ==================
    // 能力探测/可诊断 warning（不含敏感信息）
    // ==================

    QStringList capabilityWarnings;

    qint64 bytesDownloaded; ///< 已下载字节数
    qint64 bytesUploaded;   ///< 已上传字节数
    qint64 downloadTotal;   ///< 下载总字节数（-1 表示未知）
    qint64 uploadTotal;     ///< 上传总字节数

    // ==================
    // 重试机制
    // ==================

    int attemptCount;

    // ==================
    // 缓存集成
    // ==================

    bool fallbackToCache = false;

    // ==================
    // Cookie 配置（从 QCNetworkAccessManager 传递）
    // ==================

    QString cookieFilePath; ///< Cookie 文件路径
    int cookieMode;         ///< Cookie 模式标志

    // ==================
    // HSTS/Alt-Svc cache 持久化（LC-50，可选，默认关闭）
    // ==================

    QByteArray hstsCachePathBytes;   ///< CURLOPT_HSTS 文件路径缓存
    QByteArray altSvcCachePathBytes; ///< CURLOPT_ALTSVC 文件路径缓存

    // ==================
    // 同步模式回调函数
    // ==================

    DataFunction writeCallback;        ///< 数据接收回调（同步模式）
    DataFunction headerCallback;       ///< 响应头接收回调（同步模式）
    SeekFunction seekCallback;         ///< 数据定位回调（同步模式）
    ProgressFunction progressCallback; ///< 进度回调（同步模式）

    // ==================
    // 代理配置缓存（保持 QByteArray 生命周期）
    // ==================

    QByteArray proxyHostBytes;     ///< 代理主机名缓存
    QByteArray proxyUserBytes;     ///< 代理用户名缓存
    QByteArray proxyPasswordBytes; ///< 代理密码缓存

    QByteArray httpAuthUserBytes;     ///< HTTP 认证用户名缓存
    QByteArray httpAuthPasswordBytes; ///< HTTP 认证密码缓存

    QByteArray refererBytes;        ///< Referer 缓存
    QByteArray acceptEncodingBytes; ///< Accept-Encoding(由 libcurl 托管) 缓存

    // ==================
    // 网络路径与 DNS 控制（M4）
    // ==================

    QByteArray interfaceBytes;            ///< CURLOPT_INTERFACE 缓存
    QByteArray dnsServersBytes;           ///< CURLOPT_DNS_SERVERS 缓存
    QByteArray dohUrlBytes;               ///< CURLOPT_DOH_URL 缓存
    curl_slist *resolveSlist   = nullptr; ///< CURLOPT_RESOLVE 列表（Reply 生命周期内有效）
    curl_slist *connectToSlist = nullptr; ///< CURLOPT_CONNECT_TO 列表（Reply 生命周期内有效）

    // ==================
    // 协议白名单（M5，安全）
    // ==================

    QByteArray allowedProtocolsBytes;         ///< CURLOPT_PROTOCOLS_STR 缓存
    QByteArray allowedRedirectProtocolsBytes; ///< CURLOPT_REDIR_PROTOCOLS_STR 缓存

    QByteArray sslCaCertPathBytes;        ///< CA 证书路径缓存
    QByteArray sslClientCertPathBytes;    ///< 客户端证书路径缓存
    QByteArray sslClientKeyPathBytes;     ///< 客户端私钥路径缓存
    QByteArray sslClientKeyPasswordBytes; ///< 客户端私钥密码缓存

    QByteArray sslPinnedPublicKeyBytes; ///< CURLOPT_PINNEDPUBLICKEY 缓存
    QByteArray sslCipherListBytes;      ///< CURLOPT_SSL_CIPHER_LIST 缓存
    QByteArray sslTls13CiphersBytes;    ///< CURLOPT_TLS13_CIPHERS 缓存

    QByteArray proxySslCaCertPathBytes;   ///< CURLOPT_PROXY_CAINFO 缓存
    QByteArray proxySslCipherListBytes;   ///< CURLOPT_PROXY_SSL_CIPHER_LIST 缓存
    QByteArray proxySslTls13CiphersBytes; ///< CURLOPT_PROXY_TLS13_CIPHERS 缓存

    // ==================
    // 流式上传（M2）
    // ==================

    QPointer<QIODevice> uploadDevice; ///< 上传来源（调用方/内部文件，所有权不在 Reply）
    QPointer<QFile>
        ownedUploadFile; ///< 若来源为 uploadFilePath，则由 Reply 打开并持有（父子树/事件循环析构）
    qint64 uploadDeviceBasePos = 0;     ///< 首次执行时记录的起始位置（支持非 0 起点）
    qint64 uploadBodySizeBytes = -1;    ///< 约定：-1 表示未知/未设置
    qint64 uploadBytesRead     = 0;     ///< 已从 uploadDevice 读取的字节数（相对 basePos）
    bool uploadDeviceSeekable  = false; ///< uploadDevice 是否可 seek（用于重发 body）

    bool hasUploadErrorOverride          = false;
    NetworkError uploadErrorOverrideCode = NetworkError::NoError;
    QString uploadErrorOverrideMessage;

    // ==================
    // 内部方法
    // ==================

    [[nodiscard]] int desiredPauseMask() const { return userPauseMask | internalPauseMask; }

    bool applyPauseMask(int desiredMask);
    void setBackpressureActive(bool active);
    void setUploadSendPaused(bool paused);
    void maybeResumeRecvFromBackpressure();
    void resumeSendFromUploadSourceIfNeeded();

    /**
     * @brief 配置 curl 选项
     *
     * 根据 httpMethod 和 request 配置 curl 句柄的各种选项。
     * 这是核心方法，替代了旧版本中 6 个 Reply 子类的 createEasyHandle()。
     *
     * @return true 配置成功，false 失败
     */
    bool configureCurlOptions();

    /**
     * @brief 设置状态并发射信号
     *
     * 集中管理状态转换，自动发射 stateChanged()、finished()、error() 等信号。
     *
     * @param newState 新状态
     */
    void setState(ReplyState newState);

    /**
     * @brief 设置错误信息
     *
     * @param error 错误码
     * @param message 错误描述
     */
    void setError(NetworkError error, const QString &message);

    /**
     * @brief 解析响应头
     *
     * 将原始响应头数据（headerData）解析为键值对（headerMap）。
     */
    void parseHeaders();

    // ==================
    // Curl 静态回调函数（C 接口）
    // ==================

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

    /**
     * @brief 生成脱敏后的 debug trace 文本
     *
     * 供 libcurl debug 回调与单元测试共用，确保 trace 分类与脱敏规则只有一处实现。
     */
    [[nodiscard]] static QString formatDebugTraceMessage(curl_infotype type,
                                                         const QByteArray &raw);
};

} // namespace QCurl

#endif // QCNETWORKREPLY_P_H
