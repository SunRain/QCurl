#include "private/QCRequestPipeline_p.h"

#include "QCNetworkTimeoutConfig.h"

#include <QFileInfo>
#include <QIODevice>
#include <QStringList>

namespace QCurl::Internal {

namespace {

#ifdef QCURL_ENABLE_TEST_HOOKS
QString bodyKindName(RequestBodyKind kind)
{
    switch (kind) {
        case RequestBodyKind::Empty:
            return QStringLiteral("empty");
        case RequestBodyKind::InlineBytes:
            return QStringLiteral("inline");
        case RequestBodyKind::UploadDevice:
            return QStringLiteral("device");
        case RequestBodyKind::UploadFilePath:
            return QStringLiteral("file");
    }

    return QStringLiteral("unknown");
}

QString methodName(HttpMethod method)
{
    switch (method) {
        case HttpMethod::Head:
            return QStringLiteral("HEAD");
        case HttpMethod::Get:
            return QStringLiteral("GET");
        case HttpMethod::Post:
            return QStringLiteral("POST");
        case HttpMethod::Put:
            return QStringLiteral("PUT");
        case HttpMethod::Delete:
            return QStringLiteral("DELETE");
        case HttpMethod::Patch:
            return QStringLiteral("PATCH");
    }

    return QStringLiteral("UNKNOWN");
}

QString transferModeName(CurlTransferMode mode)
{
    switch (mode) {
        case CurlTransferMode::None:
            return QStringLiteral("none");
        case CurlTransferMode::InlineBytes:
            return QStringLiteral("inline");
        case CurlTransferMode::UploadSource:
            return QStringLiteral("upload");
    }

    return QStringLiteral("unknown");
}
#endif

} // namespace

NormalizedRequest normalizeRequest(const QCNetworkRequest &request,
                                   HttpMethod method,
                                   ExecutionMode mode,
                                   const QByteArray &inlineBody)
{
    NormalizedRequest normalized;
    normalized.request       = request;
    normalized.method        = method;
    normalized.executionMode = mode;

    if (QIODevice *device = request.uploadDevice()) {
        // QIODevice 优先级最高：它携带当前位置、可 seek 性与潜在的流式上传语义。
        normalized.body.kind             = RequestBodyKind::UploadDevice;
        normalized.body.device           = device;
        normalized.body.basePos          = device->pos();
        normalized.body.seekable         = !device->isSequential();
        normalized.body.allowChunkedPost = (method == HttpMethod::Post)
                                           && request.allowChunkedUploadForPost();
        normalized.body.sizeBytes        = -1;
        if (const auto sizeOpt = request.uploadBodySizeBytes(); sizeOpt.has_value()) {
            normalized.body.sizeBytes = sizeOpt.value();
        } else if (normalized.body.seekable) {
            const qint64 totalSize = device->size();
            if (totalSize >= 0 && totalSize >= normalized.body.basePos) {
                normalized.body.sizeBytes = totalSize - normalized.body.basePos;
            }
        }
        return normalized;
    }

    if (const auto filePath = request.uploadFilePath(); filePath.has_value()) {
        // 文件路径模式在执行期再打开文件，这里只归一化路径和可推断的大小。
        normalized.body.kind             = RequestBodyKind::UploadFilePath;
        normalized.body.filePath         = filePath.value();
        normalized.body.allowChunkedPost = (method == HttpMethod::Post)
                                           && request.allowChunkedUploadForPost();
        normalized.body.sizeBytes        = -1;
        if (const auto sizeOpt = request.uploadBodySizeBytes(); sizeOpt.has_value()) {
            normalized.body.sizeBytes = sizeOpt.value();
        } else {
            const QFileInfo info(normalized.body.filePath);
            if (info.exists()) {
                normalized.body.sizeBytes = info.size();
            }
        }
        return normalized;
    }

    if (!inlineBody.isEmpty()) {
        normalized.body.kind        = RequestBodyKind::InlineBytes;
        normalized.body.inlineBytes = inlineBody;
        normalized.body.sizeBytes   = inlineBody.size();
    }

    return normalized;
}

CurlPlan compileRequest(const NormalizedRequest &normalized)
{
    CurlPlan plan;
    plan.normalized             = normalized;
    const bool hasUploadSource = normalized.body.hasUploadSource();
    const qint64 inlineBodySize = normalized.body.inlineBytes.size();

    switch (normalized.method) {
        case HttpMethod::Head:
            plan.setNoBody = true;
            break;
        case HttpMethod::Get:
            plan.setHttpGet = true;
            break;
        case HttpMethod::Post:
            plan.setPost = true;
            // POST 允许内联 body 与上传源两条路径；后者会在执行期走 read callback。
            plan.transferMode = hasUploadSource ? CurlTransferMode::UploadSource
                                                : CurlTransferMode::InlineBytes;
            plan.bodySizeBytes = hasUploadSource ? normalized.body.sizeBytes : inlineBodySize;
            break;
        case HttpMethod::Put:
            // PUT 只有在外部上传源存在时才打开 CURLOPT_UPLOAD；内联 body 走自定义请求路径。
            plan.setUpload = hasUploadSource;
            plan.customRequest = QByteArrayLiteral("PUT");
            if (hasUploadSource) {
                plan.transferMode = CurlTransferMode::UploadSource;
                plan.bodySizeBytes = normalized.body.sizeBytes;
            } else if (normalized.body.hasInlineBytes()) {
                plan.transferMode = CurlTransferMode::InlineBytes;
                plan.bodySizeBytes = inlineBodySize;
            }
            break;
        case HttpMethod::Delete:
            plan.customRequest = QByteArrayLiteral("DELETE");
            if (normalized.body.hasInlineBytes()) {
                plan.transferMode = CurlTransferMode::InlineBytes;
                plan.bodySizeBytes = inlineBodySize;
            }
            break;
        case HttpMethod::Patch:
            plan.customRequest = QByteArrayLiteral("PATCH");
            if (normalized.body.hasInlineBytes()) {
                plan.transferMode = CurlTransferMode::InlineBytes;
                plan.bodySizeBytes = inlineBodySize;
            }
            break;
    }

    plan.hasRequestBody = hasUploadSource || inlineBodySize > 0;
    return plan;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
QByteArray buildCurlPlanDigestForTest(const CurlPlan &plan)
{
    const auto &normalized = plan.normalized;
    QStringList parts;
    parts << QStringLiteral("method=%1").arg(methodName(normalized.method));
    parts << QStringLiteral("url=%1").arg(normalized.request.url().toString());
    parts << QStringLiteral("body_kind=%1").arg(bodyKindName(normalized.body.kind));
    parts << QStringLiteral("transfer=%1").arg(transferModeName(plan.transferMode));
    parts << QStringLiteral("body_size=%1").arg(plan.bodySizeBytes);
    parts << QStringLiteral("priority=%1").arg(static_cast<int>(normalized.request.priority()));
    parts << QStringLiteral("follow=%1").arg(normalized.request.followLocation() ? 1 : 0);

    const auto headerNames = normalized.request.rawHeaderList();
    QStringList headerParts;
    headerParts.reserve(headerNames.size());
    for (const auto &name : headerNames) {
        headerParts << QStringLiteral("%1=%2")
                           .arg(QString::fromUtf8(name),
                                QString::fromUtf8(normalized.request.rawHeader(name)));
    }
    parts << QStringLiteral("headers=%1").arg(headerParts.join(QStringLiteral(";")));

    const auto timeout = normalized.request.timeoutConfig();
    if (timeout.connectTimeout.has_value()) {
        parts << QStringLiteral("connect_timeout_ms=%1").arg(timeout.connectTimeout->count());
    }
    if (timeout.totalTimeout.has_value()) {
        parts << QStringLiteral("total_timeout_ms=%1").arg(timeout.totalTimeout->count());
    }

    return parts.join(QStringLiteral("|")).toUtf8();
}
#endif

} // namespace QCurl::Internal
