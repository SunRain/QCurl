#include "QCNetworkReply.h"
#include "private/QCNetworkResumableDownloadWriter_p.h"

#include <QIODevice>
#include <QThread>

#ifdef QCURL_ENABLE_TEST_HOOKS
#include <atomic>
#endif

namespace QCurl::Internal {
namespace {

#ifdef QCURL_ENABLE_TEST_HOOKS
std::atomic<int> s_activeWriterCount{0};
std::atomic<QThread *> s_lastWriterDestructionThread{nullptr};
#endif

std::optional<qint64> parseContentRangeCompleteSize(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix  = QByteArrayLiteral("bytes */");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    bool ok                = false;
    const qint64 totalSize = trimmed.mid(prefix.size()).toLongLong(&ok);
    if (!ok || totalSize < 0) {
        return std::nullopt;
    }
    return totalSize;
}

/// 表示解析后的 HTTP Content-Range 字节区间。
struct ContentRangeInfo
{
    qint64 start = -1;
    qint64 end   = -1;
    qint64 total = -1;
};

std::optional<ContentRangeInfo> parseContentRangeBytesSpec(const QByteArray &headerValue)
{
    const QByteArray trimmed = headerValue.trimmed();
    const QByteArray prefix  = QByteArrayLiteral("bytes ");
    if (!trimmed.startsWith(prefix)) {
        return std::nullopt;
    }

    const int dashPos  = trimmed.indexOf('-', prefix.size());
    const int slashPos = trimmed.indexOf('/', prefix.size());
    if (dashPos < 0 || slashPos < 0 || dashPos >= slashPos) {
        return std::nullopt;
    }

    bool startOk       = false;
    bool endOk         = false;
    bool totalOk       = false;
    const qint64 start = trimmed.mid(prefix.size(), dashPos - prefix.size()).toLongLong(&startOk);
    const qint64 end   = trimmed.mid(dashPos + 1, slashPos - dashPos - 1).toLongLong(&endOk);
    const qint64 total = trimmed.mid(slashPos + 1).toLongLong(&totalOk);
    if (!startOk || !endOk || !totalOk || start < 0 || end < start || end >= total) {
        return std::nullopt;
    }

    return ContentRangeInfo{start, end, total};
}

} // namespace

ResumableDownloadWriter::ResumableDownloadWriter(QString savePath,
                                                 const qint64 existingSize,
                                                 const bool hadExistingFile)
    : m_savePath(std::move(savePath))
    , m_existingSize(existingSize)
    , m_hadExistingFile(hadExistingFile)
    , m_ownerThread(QThread::currentThread())
    , m_file(m_savePath)
    , m_overwriteFile(m_savePath)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    s_activeWriterCount.fetch_add(1, std::memory_order_relaxed);
#endif
}

ResumableDownloadWriter::~ResumableDownloadWriter()
{
    assertOwnerThread();
    closeTargets();
#ifdef QCURL_ENABLE_TEST_HOOKS
    s_lastWriterDestructionThread.store(QThread::currentThread(), std::memory_order_relaxed);
    s_activeWriterCount.fetch_sub(1, std::memory_order_relaxed);
#endif
}

bool ResumableDownloadWriter::isAlreadyComplete(QCNetworkReply *reply) const
{
    return reply && m_existingSize > 0 && reply->httpStatusCode() == 416
           && parseContentRangeCompleteSize(reply->rawHeader(QByteArrayLiteral("Content-Range")))
                      .value_or(-1)
                  == m_existingSize;
}

std::optional<QString> ResumableDownloadWriter::decideWriteMode(QCNetworkReply *reply)
{
    if (isAlreadyComplete(reply)) {
        m_modeDecided = true;
        return std::nullopt;
    }

    if (reply->httpStatusCode() == 206) {
        if (const auto error = validateRange(reply); error.has_value()) {
            return error;
        }
        m_appendMode = m_existingSize > 0;
    }
    m_safeOverwriteMode = !m_appendMode && m_hadExistingFile;
    m_modeDecided       = true;
    return std::nullopt;
}

std::optional<QString> ResumableDownloadWriter::validateRange(QCNetworkReply *reply)
{
    const auto range = parseContentRangeBytesSpec(
        reply->rawHeader(QByteArrayLiteral("Content-Range")));
    const auto encoding = reply->rawHeader(QByteArrayLiteral("Content-Encoding")).trimmed();
    if (!range) {
        return QStringLiteral("QCNetworkResumableDownloadJob: Content-Range 无效: %1")
            .arg(m_savePath);
    }
    if (range->start != m_existingSize) {
        return QStringLiteral(
                   "QCNetworkResumableDownloadJob: Content-Range.start 不匹配本地长度: %1")
            .arg(m_savePath);
    }
    if (range->end != range->total - 1) {
        return QStringLiteral("QCNetworkResumableDownloadJob: Content-Range 未覆盖完整目标: %1")
            .arg(m_savePath);
    }
    if (!encoding.isEmpty() && encoding.compare("identity", Qt::CaseInsensitive) != 0) {
        return QStringLiteral(
                   "QCNetworkResumableDownloadJob: 206 只支持 identity Content-Encoding: %1")
            .arg(m_savePath);
    }
    m_rangeBytes            = range->end - range->start + 1;
    m_rangeTotal            = range->total;
    const QByteArray length = reply->rawHeader(QByteArrayLiteral("Content-Length"));
    if (!length.isEmpty()) {
        bool ok                    = false;
        const qint64 contentLength = length.toLongLong(&ok);
        if (!ok || contentLength != m_rangeBytes) {
            return QStringLiteral(
                       "QCNetworkResumableDownloadJob: Content-Length 与范围长度矛盾: %1")
                .arg(m_savePath);
        }
    }
    return std::nullopt;
}

std::optional<QString> ResumableDownloadWriter::ensureWriteTarget(QCNetworkReply *reply)
{
    if (!m_modeDecided) {
        if (const auto error = decideWriteMode(reply); error.has_value()) {
            return error;
        }
    }
    if (isAlreadyComplete(reply)) {
        return std::nullopt;
    }
    if (m_appendMode && !m_file.isOpen() && !m_file.open(QIODevice::Append)) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法以追加模式打开目标文件: %1")
            .arg(m_file.fileName());
    }
    if (m_safeOverwriteMode && !m_overwriteFile.isOpen()
        && !m_overwriteFile.open(QIODevice::WriteOnly)) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法以安全覆盖模式打开目标文件: %1")
            .arg(m_savePath);
    }
    if (!m_appendMode && !m_safeOverwriteMode && !m_file.isOpen()
        && !m_file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法创建目标文件: %1").arg(m_savePath);
    }
    return std::nullopt;
}

QIODevice *ResumableDownloadWriter::activeTarget() noexcept
{
    return m_safeOverwriteMode ? static_cast<QIODevice *>(&m_overwriteFile)
                               : static_cast<QIODevice *>(&m_file);
}

void ResumableDownloadWriter::closeTargets()
{
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (m_overwriteFile.isOpen()) {
        m_overwriteFile.cancelWriting();
        // cancelWriting 标记失败，commit 关闭并丢弃临时文件，绝不替换目标。
        static_cast<void>(m_overwriteFile.commit());
    }
}

void ResumableDownloadWriter::assertOwnerThread() const
{
    Q_ASSERT(QThread::currentThread() == m_ownerThread);
}

std::optional<QString> ResumableDownloadWriter::writeChunk(QCNetworkReply *reply)
{
    assertOwnerThread();
    if (reply->httpStatusCode() >= 400) {
        static_cast<void>(reply->readAll());
        return std::nullopt;
    }
    if (const auto error = ensureWriteTarget(reply); error.has_value()) {
        closeTargets();
        return error;
    }

    QIODevice *target = activeTarget();
    if (!target || !target->isWritable()) {
        closeTargets();
        return QStringLiteral("QCNetworkResumableDownloadJob: 目标文件不可写: %1").arg(m_savePath);
    }

    const auto data = reply->readAll();
    if (!data.has_value() || data->isEmpty()) {
        return std::nullopt;
    }

    const QByteArray &chunk = data.value();
    if (m_rangeBytes >= 0 && chunk.size() > m_rangeBytes - m_writtenBytes) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 响应体超过范围长度: %1")
            .arg(m_savePath);
    }
    if (target->write(chunk) != chunk.size()) {
        // 保留目标直到终态处理，追加模式仍需尝试截回原长度。
        return QStringLiteral("QCNetworkResumableDownloadJob: 写入目标文件失败: %1").arg(m_savePath);
    }
    m_writtenBytes += chunk.size();
    return std::nullopt;
}

std::optional<QString> ResumableDownloadWriter::restoreAppend()
{
    if (m_appendMode && m_file.isOpen() && (!m_file.flush() || !m_file.resize(m_existingSize))) {
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法恢复追加前长度: %1")
            .arg(m_savePath);
    }
    return std::nullopt;
}

std::optional<QString> ResumableDownloadWriter::restoreBeforeRetry()
{
    assertOwnerThread();
#ifdef QCURL_ENABLE_TEST_HOOKS
    if (qEnvironmentVariableIsSet("QCURL_TEST_FORCE_DOWNLOAD_RESTORE_ERROR")) {
        return QStringLiteral("QCNetworkResumableDownloadJob: forced output restore failure");
    }
#endif
    if (const auto error = restoreAppend(); error.has_value()) {
        closeTargets();
        return error;
    }
    if (!m_hadExistingFile && m_file.isOpen() && (!m_file.flush() || !m_file.resize(0))) {
        closeTargets();
        return QStringLiteral("QCNetworkResumableDownloadJob: 无法清空失败尝试的新文件: %1")
            .arg(m_savePath);
    }
    closeTargets();
    m_modeDecided       = false;
    m_appendMode        = false;
    m_safeOverwriteMode = false;
    m_writtenBytes      = 0;
    m_rangeBytes        = -1;
    m_rangeTotal        = -1;
    return std::nullopt;
}

std::optional<QString> ResumableDownloadWriter::commitIfNeeded(QCNetworkReply *reply)
{
    assertOwnerThread();
    if (isAlreadyComplete(reply)) {
        return std::nullopt;
    }
    if (reply->error() != NetworkError::NoError) {
        const auto error = restoreAppend();
        closeTargets();
        return error;
    }
    if (const auto error = ensureWriteTarget(reply); error.has_value()) {
        closeTargets();
        return error;
    }
    if (m_rangeBytes >= 0
        && (m_writtenBytes != m_rangeBytes || activeTarget()->size() != m_rangeTotal)) {
        const auto error = restoreAppend();
        closeTargets();
        return error.value_or(
            QStringLiteral("QCNetworkResumableDownloadJob: 续传最终长度不完整: %1").arg(m_savePath));
    }
    if (m_file.isOpen()) {
        if (!m_file.flush()) {
            const auto error = restoreAppend();
            closeTargets();
            if (error.has_value()) {
                return error;
            }
            return QStringLiteral("QCNetworkResumableDownloadJob: 文件刷新失败: %1").arg(m_savePath);
        }
        m_file.close();
    }
    if (!m_safeOverwriteMode || !m_overwriteFile.isOpen()) {
        return std::nullopt;
    }
    if (m_overwriteFile.commit()) {
        return std::nullopt;
    }
    return QStringLiteral("QCNetworkResumableDownloadJob: 覆盖写入提交失败: %1").arg(m_savePath);
}

#ifdef QCURL_ENABLE_TEST_HOOKS
int ResumableDownloadWriter::activeInstanceCountForTesting() noexcept
{
    return s_activeWriterCount.load(std::memory_order_relaxed);
}

QThread *ResumableDownloadWriter::lastDestructionThreadForTesting() noexcept
{
    return s_lastWriterDestructionThread.load(std::memory_order_relaxed);
}
#endif

} // namespace QCurl::Internal
