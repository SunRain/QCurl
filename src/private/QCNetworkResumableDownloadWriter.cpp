#include "private/QCNetworkResumableDownloadWriter_p.h"

#include "QCNetworkError.h"
#include "QCNetworkReply.h"

#include <QFile>
#include <QIODevice>
#include <QSaveFile>

namespace QCurl::Internal {
namespace {

std::optional<qint64> parseContentRangeCompleteSize(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix = QByteArrayLiteral("bytes */");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    bool ok = false;
    const qint64 totalSize = trimmed.mid(prefix.size()).toLongLong(&ok);
    if (!ok || totalSize < 0) {
        return std::nullopt;
    }
    return totalSize;
}

struct ContentRangeInfo
{
    qint64 start = -1;
    qint64 end = -1;
    qint64 total = -1;
};

std::optional<ContentRangeInfo> parseContentRangeBytesSpec(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix = QByteArrayLiteral("bytes ");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    const int dashPos = trimmed.indexOf('-', prefix.size());
    const int slashPos = trimmed.indexOf('/', prefix.size());
    if (dashPos < 0 || slashPos < 0 || dashPos >= slashPos) {
        return std::nullopt;
    }

    bool startOk = false;
    bool endOk = false;
    bool totalOk = false;
    const qint64 start = trimmed.mid(prefix.size(), dashPos - prefix.size()).toLongLong(&startOk);
    const qint64 end = trimmed.mid(dashPos + 1, slashPos - dashPos - 1).toLongLong(&endOk);
    const qint64 total = trimmed.mid(slashPos + 1).toLongLong(&totalOk);
    if (!startOk || !endOk || !totalOk || start < 0 || end < start || total < 0) {
        return std::nullopt;
    }

    return ContentRangeInfo{start, end, total};
}

bool isAlreadyComplete(QCNetworkReply *reply, qint64 existingSize)
{
    return reply && existingSize > 0 && reply->httpStatusCode() == 416
           && parseContentRangeCompleteSize(reply->rawHeader(QByteArrayLiteral("Content-Range")))
                  .value_or(-1)
                  == existingSize;
}

void closeTargets(const QSharedPointer<ResumableDownloadWriteContext> &context)
{
    if (context->appendFile && context->appendFile->isOpen()) {
        context->appendFile->close();
    }
    if (context->directFile && context->directFile->isOpen()) {
        context->directFile->close();
    }
    if (context->overwriteFile && context->overwriteFile->isOpen()) {
        context->overwriteFile->cancelWriting();
    }
}

QIODevice *activeTarget(const QSharedPointer<ResumableDownloadWriteContext> &context)
{
    if (context->appendMode) {
        return context->appendFile.data();
    }
    if (context->safeOverwriteMode) {
        return context->overwriteFile.data();
    }
    return context->directFile.data();
}

std::optional<QString> decideWriteMode(QCNetworkReply *reply,
                                       const QSharedPointer<ResumableDownloadWriteContext> &context,
                                       const QString &savePath,
                                       qint64 existingSize,
                                       bool hadExistingFile)
{
    if (isAlreadyComplete(reply, existingSize)) {
        context->modeDecided = true;
        return std::nullopt;
    }

    const auto contentRange =
        parseContentRangeBytesSpec(reply->rawHeader(QByteArrayLiteral("Content-Range")));
    context->appendMode = existingSize > 0 && reply->httpStatusCode() == 206
                          && contentRange.has_value() && contentRange->start == existingSize;
    if (reply->httpStatusCode() == 206 && !context->appendMode) {
        return QStringLiteral(
                   "QCNetworkResumableDownloadJob: 206 响应的 Content-Range.start 与本地文件大小不匹配: %1")
            .arg(savePath);
    }
    context->safeOverwriteMode = !context->appendMode && hadExistingFile;
    context->modeDecided = true;
    return std::nullopt;
}

std::optional<QString> ensureWriteTarget(QCNetworkReply *reply,
                                         const QSharedPointer<ResumableDownloadWriteContext> &context,
                                         const QString &savePath,
                                         qint64 existingSize,
                                         bool hadExistingFile)
{
    if (!context->modeDecided) {
        if (const auto error = decideWriteMode(
                reply, context, savePath, existingSize, hadExistingFile);
            error.has_value()) {
            return error;
        }
    }
    if (isAlreadyComplete(reply, existingSize)) {
        return std::nullopt;
    }
    if (context->appendMode && !context->appendFile->isOpen()
        && !context->appendFile->open(QIODevice::Append)) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法以追加模式打开目标文件: %1")
            .arg(context->appendFile->fileName());
    }
    if (context->safeOverwriteMode) {
        if (!context->overwriteFile) {
            context->overwriteFile = QSharedPointer<QSaveFile>::create(savePath);
        }
        if (!context->overwriteFile->isOpen()
            && !context->overwriteFile->open(QIODevice::WriteOnly)) {
            return QStringLiteral(
                       "QCNetworkResumableDownloadJob: 无法以安全覆盖模式打开目标文件: %1")
                .arg(savePath);
        }
    } else if (!context->appendMode && !context->directFile->isOpen()
               && !context->directFile->open(QIODevice::WriteOnly)) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法创建目标文件: %1")
            .arg(savePath);
    }
    return std::nullopt;
}

} // namespace

QSharedPointer<ResumableDownloadWriteContext> makeResumableDownloadWriteContext(
    const QString &savePath)
{
    auto context = QSharedPointer<ResumableDownloadWriteContext>::create();
    context->appendFile = QSharedPointer<QFile>::create(savePath);
    context->directFile = QSharedPointer<QFile>::create(savePath);
    return context;
}

void writeResumableDownloadChunk(QCNetworkReply *reply,
                                 const QSharedPointer<ResumableDownloadWriteContext> &context,
                                 const QString &savePath,
                                 qint64 existingSize,
                                 bool hadExistingFile)
{
    if (const auto error =
            ensureWriteTarget(reply, context, savePath, existingSize, hadExistingFile);
        error.has_value()) {
        context->writeFailed = true;
        closeTargets(context);
        reply->abortWithError(NetworkError::InvalidRequest, error.value());
        return;
    }

    QIODevice *target = activeTarget(context);
    if (!target || !target->isWritable()) {
        context->writeFailed = true;
        closeTargets(context);
        reply->abortWithError(
            NetworkError::InvalidRequest,
            QStringLiteral("QCNetworkResumableDownloadJob: 目标文件不可写: %1").arg(savePath));
        return;
    }

    const auto data = reply->readAll();
    if (!data.has_value() || data->isEmpty()) {
        return;
    }

    const QByteArray &chunk = data.value();
    if (target->write(chunk) != chunk.size()) {
        context->writeFailed = true;
        closeTargets(context);
        reply->abortWithError(
            NetworkError::InvalidRequest,
            QStringLiteral("QCNetworkResumableDownloadJob: 写入目标文件失败: %1").arg(savePath));
    }
}

std::optional<QString> commitResumableDownloadIfNeeded(
    QCNetworkReply *reply,
    const QSharedPointer<ResumableDownloadWriteContext> &context,
    const QString &savePath,
    qint64 existingSize,
    bool hadExistingFile)
{
    if (isAlreadyComplete(reply, existingSize)) {
        return std::nullopt;
    }
    if (reply->error() != NetworkError::NoError) {
        closeTargets(context);
        return std::nullopt;
    }
    if (const auto error =
            ensureWriteTarget(reply, context, savePath, existingSize, hadExistingFile);
        error.has_value()) {
        context->writeFailed = true;
        closeTargets(context);
        return error;
    }
    if (context->appendFile && context->appendFile->isOpen()) {
        context->appendFile->close();
    }
    if (context->directFile && context->directFile->isOpen()) {
        context->directFile->close();
    }
    if (!context->safeOverwriteMode || !context->overwriteFile
        || !context->overwriteFile->isOpen()) {
        return std::nullopt;
    }
    if (context->overwriteFile->commit()) {
        return std::nullopt;
    }
    context->writeFailed = true;
    return QStringLiteral("QCNetworkResumableDownloadJob: 覆盖写入提交失败: %1").arg(savePath);
}

} // namespace QCurl::Internal
