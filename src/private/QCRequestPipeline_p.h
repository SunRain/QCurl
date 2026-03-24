/**
 * @file
 * @brief 声明请求归一化与 curl 计划编译接口。
 */

#ifndef QCREQUESTPIPELINE_P_H
#define QCREQUESTPIPELINE_P_H

#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QPointer>
#include <QString>

class QIODevice;

namespace QCurl::Internal {

/// 描述请求体来源的归一化类型。
enum class RequestBodyKind {
    Empty,          ///< 无请求体
    InlineBytes,    ///< 使用内联 QByteArray
    UploadDevice,   ///< 使用 QIODevice 上传源
    UploadFilePath, ///< 使用文件路径作为上传源
};

/// 归一化后的请求体描述。
struct RequestBody
{
    RequestBodyKind kind = RequestBodyKind::Empty;
    QByteArray inlineBytes;   ///< InlineBytes 模式下的内联 body
    QPointer<QIODevice> device; ///< UploadDevice 模式下的设备句柄
    QString filePath;         ///< UploadFilePath 模式下的文件路径
    qint64 sizeBytes      = 0; ///< 已知请求体大小；未知时为负值
    qint64 basePos        = 0; ///< 上传开始时的设备起始偏移
    bool seekable         = false; ///< 上传源是否支持 seek
    bool allowChunkedPost = false; ///< POST 上传源是否允许 chunked 传输

    /// 返回当前 body 是否使用内联字节。
    [[nodiscard]] bool hasInlineBytes() const noexcept { return kind == RequestBodyKind::InlineBytes; }

    /// 返回当前 body 是否依赖外部上传源。
    [[nodiscard]] bool hasUploadSource() const noexcept
    {
        return kind == RequestBodyKind::UploadDevice || kind == RequestBodyKind::UploadFilePath;
    }

    /// 返回当前 body 是否具有可用的长度信息。
    [[nodiscard]] bool hasKnownSize() const noexcept { return sizeBytes >= 0; }
};

/// 归一化后的请求与执行模式快照。
struct NormalizedRequest
{
    QCNetworkRequest request;
    HttpMethod method                = HttpMethod::Get;
    ExecutionMode executionMode      = ExecutionMode::Async;
    RequestBody body;
};

/// curl 侧最终使用的数据传输模式。
enum class CurlTransferMode {
    None,         ///< 无请求体或无需额外传输配置
    InlineBytes,  ///< 直接使用内联内存块
    UploadSource, ///< 使用设备或文件流式上传
};

/// 编译后用于驱动 curl_easy_setopt 的请求计划。
struct CurlPlan
{
    NormalizedRequest normalized;
    bool setNoBody                = false;
    bool setHttpGet               = false;
    bool setPost                  = false;
    bool setUpload                = false;
    QByteArray customRequest;
    CurlTransferMode transferMode = CurlTransferMode::None;
    qint64 bodySizeBytes          = 0;
    bool hasRequestBody           = false;

    /// 返回是否使用内联字节作为传输源。
    [[nodiscard]] bool usesInlineBytes() const noexcept
    {
        return transferMode == CurlTransferMode::InlineBytes;
    }

    /// 返回是否使用设备或文件作为传输源。
    [[nodiscard]] bool usesUploadSource() const noexcept
    {
        return transferMode == CurlTransferMode::UploadSource;
    }
};

/**
 * @brief 归一化请求体来源和执行模式
 *
 * 把 request 中可能的 uploadDevice / uploadFilePath / inlineBody
 * 收敛为统一的 RequestBody 描述。
 */
NormalizedRequest normalizeRequest(const QCNetworkRequest &request,
                                   HttpMethod method,
                                   ExecutionMode mode,
                                   const QByteArray &inlineBody);

/**
 * @brief 根据归一化结果编译 curl 侧请求计划
 *
 * 输出值描述 curl method flag、请求体传输模式和最终 body size。
 */
CurlPlan compileRequest(const NormalizedRequest &normalized);

#ifdef QCURL_ENABLE_TEST_HOOKS
QByteArray buildCurlPlanDigestForTest(const CurlPlan &plan);
#endif

} // namespace QCurl::Internal

#endif // QCREQUESTPIPELINE_P_H
