#ifndef QCREQUESTPIPELINE_P_H
#define QCREQUESTPIPELINE_P_H

#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QPointer>
#include <QString>

class QIODevice;

namespace QCurl::Internal {

enum class RequestBodyKind {
    Empty,
    InlineBytes,
    UploadDevice,
    UploadFilePath,
};

struct RequestBody
{
    RequestBodyKind kind = RequestBodyKind::Empty;
    QByteArray inlineBytes;
    QPointer<QIODevice> device;
    QString filePath;
    qint64 sizeBytes      = 0;
    qint64 basePos        = 0;
    bool seekable         = false;
    bool allowChunkedPost = false;

    [[nodiscard]] bool hasInlineBytes() const noexcept { return kind == RequestBodyKind::InlineBytes; }
    [[nodiscard]] bool hasUploadSource() const noexcept
    {
        return kind == RequestBodyKind::UploadDevice || kind == RequestBodyKind::UploadFilePath;
    }
    [[nodiscard]] bool hasKnownSize() const noexcept { return sizeBytes >= 0; }
};

struct NormalizedRequest
{
    QCNetworkRequest request;
    HttpMethod method                = HttpMethod::Get;
    ExecutionMode executionMode      = ExecutionMode::Async;
    RequestBody body;
};

enum class CurlTransferMode {
    None,
    InlineBytes,
    UploadSource,
};

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

    [[nodiscard]] bool usesInlineBytes() const noexcept
    {
        return transferMode == CurlTransferMode::InlineBytes;
    }

    [[nodiscard]] bool usesUploadSource() const noexcept
    {
        return transferMode == CurlTransferMode::UploadSource;
    }
};

NormalizedRequest normalizeRequest(const QCNetworkRequest &request,
                                   HttpMethod method,
                                   ExecutionMode mode,
                                   const QByteArray &inlineBody);

CurlPlan compileRequest(const NormalizedRequest &normalized);

#ifdef QCURL_ENABLE_TEST_HOOKS
QByteArray buildCurlPlanDigestForTest(const CurlPlan &plan);
#endif

} // namespace QCurl::Internal

#endif // QCREQUESTPIPELINE_P_H
