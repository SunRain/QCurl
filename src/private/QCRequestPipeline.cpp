#include "private/QCRequestPipeline_p.h"

#include "QCNetworkTimeoutConfig.h"

#include <QIODevice>
#include <QStringList>

namespace QCurl::Internal {

namespace {

#ifdef QCURL_ENABLE_TEST_HOOKS
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
#endif

} // namespace

RequestBody makeEmptyRequestBody()
{
    return {};
}

RequestBody makeInlineRequestBody(const QByteArray &inlineBody)
{
    RequestBody body;
    if (!inlineBody.isEmpty()) {
        body.kind        = RequestBodyKind::InlineBytes;
        body.inlineBytes = inlineBody;
        body.sizeBytes   = inlineBody.size();
    }
    return body;
}

RequestBody makeDeviceRequestBody(QIODevice *device,
                                  std::optional<qint64> sizeBytes,
                                  bool allowChunkedPost,
                                  bool inferDeviceSize)
{
    RequestBody body;
    if (!device) {
        return body;
    }

    body.kind             = RequestBodyKind::Device;
    body.device           = device;
    body.allowChunkedPost = allowChunkedPost;
    body.inferDeviceSize  = inferDeviceSize;
    body.sizeBytes        = sizeBytes.value_or(-1);
    return body;
}

NormalizedRequest normalizeRequest(const QCNetworkRequest &request,
                                   HttpMethod method,
                                   ExecutionMode mode,
                                   const RequestBody &body)
{
    NormalizedRequest normalized;
    normalized.request       = request;
    normalized.method        = method;
    normalized.executionMode = mode;
    normalized.body          = body;

    return normalized;
}

CurlPlan compileRequest(const NormalizedRequest &normalized)
{
    CurlPlan plan;
    plan.normalized             = normalized;
    const bool hasBodySource = normalized.body.hasBodySource();
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
            // POST 允许内联 body 与请求体来源两条路径；后者会在执行期走 read callback。
            plan.transferMode = hasBodySource ? CurlTransferMode::RequestBodySource
                                              : CurlTransferMode::InlineBytes;
            plan.bodySizeBytes = hasBodySource ? normalized.body.sizeBytes : inlineBodySize;
            break;
        case HttpMethod::Put:
            // PUT 只有在外部请求体来源存在时才打开 CURLOPT_UPLOAD；内联 body 走自定义请求路径。
            plan.setUpload = hasBodySource;
            plan.customRequest = QByteArrayLiteral("PUT");
            if (hasBodySource) {
                plan.transferMode = CurlTransferMode::RequestBodySource;
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

    plan.hasRequestBody = hasBodySource || inlineBodySize > 0;
    return plan;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
QByteArray buildCurlPlanDigestForTest(const CurlPlan &plan)
{
    const auto &normalized = plan.normalized;
    QStringList parts;
    parts << QStringLiteral("method=%1").arg(methodName(normalized.method));
    parts << QStringLiteral("url=%1").arg(normalized.request.url().toString());
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
    if (timeout.connectTimeout().has_value()) {
        parts << QStringLiteral("connect_timeout_ms=%1").arg(timeout.connectTimeout()->count());
    }
    if (timeout.totalTimeout().has_value()) {
        parts << QStringLiteral("total_timeout_ms=%1").arg(timeout.totalTimeout()->count());
    }

    return parts.join(QStringLiteral("|")).toUtf8();
}
#endif

} // namespace QCurl::Internal
