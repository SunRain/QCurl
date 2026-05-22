/**
 * @file
 * @brief 声明请求归一化与 curl 计划编译接口。
 */

#ifndef QCREQUESTPIPELINE_P_H
#define QCREQUESTPIPELINE_P_H

#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"

#include <QPointer>
#include <QString>

class QIODevice;

namespace QCurl::Internal {

/// 描述请求体来源的归一化类型。
enum class RequestBodyKind {
    Empty,       ///< 无请求体
    InlineBytes, ///< 使用内联 QByteArray
    Device,      ///< 使用 QIODevice 请求体来源
};

/// 归一化后的请求体描述。
struct RequestBody
{
    RequestBodyKind kind = RequestBodyKind::Empty;
    QByteArray inlineBytes;    ///< InlineBytes 模式下的内联 body
    QPointer<QIODevice> device; ///< Device 模式下的设备句柄
    QByteArray customMethod; ///< Custom 方法的已校验 HTTP token
    qint64 sizeBytes      = 0; ///< 已知请求体大小；未知时为负值
    qint64 basePos        = 0; ///< 通过线程检查后采样的请求体读取起始偏移
    bool seekable         = false; ///< 通过线程检查后采样的请求体来源 seek 能力
    bool allowChunkedPost = false; ///< POST 请求体来源是否允许 chunked 传输
    bool inferDeviceSize  = true; ///< 是否允许从 seekable device 推导剩余长度

    /// 返回当前 body 是否使用内联字节。
    [[nodiscard]] bool hasInlineBytes() const noexcept { return kind == RequestBodyKind::InlineBytes; }

    /// 返回当前 body 是否依赖外部请求体来源。
    [[nodiscard]] bool hasBodySource() const noexcept
    {
        return kind == RequestBodyKind::Device;
    }

    /// 返回当前 body 是否具有可用的长度信息。
    [[nodiscard]] bool hasKnownSize() const noexcept { return sizeBytes >= 0; }
};

/// 归一化后的请求快照。
struct NormalizedRequest
{
    QCNetworkRequest request;
    HttpMethod method = HttpMethod::Get;
    RequestBody body;
};

/// curl 侧最终使用的数据传输模式。
enum class CurlTransferMode {
    None,              ///< 无请求体或无需额外传输配置
    InlineBytes,       ///< 直接使用内联内存块
    RequestBodySource, ///< 使用设备流式发送请求体
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

    /// 返回是否使用设备作为请求体传输源。
    [[nodiscard]] bool usesRequestBodySource() const noexcept
    {
        return transferMode == CurlTransferMode::RequestBodySource;
    }
};

/**
 * @brief 归一化请求体来源
 *
 * 把运行期的 device/inline-body 请求体来源
 * 收敛为统一的 RequestBody 描述。
 */
NormalizedRequest normalizeRequest(const QCNetworkRequest &request,
                                   HttpMethod method,
                                   const RequestBody &body);

/// 构造空请求体描述。
[[nodiscard]] RequestBody makeEmptyRequestBody();

/// 构造内联字节请求体描述。
[[nodiscard]] RequestBody makeInlineRequestBody(const QByteArray &inlineBody);

/// 构造 custom request 的空请求体描述。
[[nodiscard]] RequestBody makeCustomRequestBody(const QByteArray &method);

/// 构造 custom request 的内联字节请求体描述。
[[nodiscard]] RequestBody makeCustomInlineRequestBody(const QByteArray &method,
                                                      const QByteArray &inlineBody);

/// 构造外部 QIODevice 请求体描述。
[[nodiscard]] RequestBody makeDeviceRequestBody(QIODevice *device,
                                                std::optional<qint64> sizeBytes,
                                                bool allowChunkedPost,
                                                bool inferDeviceSize = true);

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
