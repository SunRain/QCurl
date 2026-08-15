#include "private/QCSingleFileMultipartBodyDevice.h"

#include "private/QCMultipartHeaderEncoding_p.h"

#include <QThread>

#include <cstring>
#include <limits>
#include <optional>

namespace QCurl::Internal {

namespace {

QByteArray multipartPrefix(const QString &boundary,
                           const QString &fieldName,
                           const QString &fileName,
                           const QString &mimeType)
{
    return QByteArray("--") + boundary.toUtf8() + "\r\nContent-Disposition: form-data; name=\""
           + encodeMultipartHeaderQuotedString(fieldName) + "\"; filename=\""
           + encodeMultipartHeaderQuotedString(fileName) + "\"\r\nContent-Type: "
           + sanitizeMultipartHeaderValue(mimeType, QByteArrayLiteral("application/octet-stream"))
           + "\r\n\r\n";
}

QByteArray multipartSuffix(const QString &boundary)
{
    return QStringLiteral("\r\n--%1--\r\n").arg(boundary).toUtf8();
}

/// 使用逐段上界检查计算总长度，避免任何中间 qint64 加法溢出。
std::optional<qint64> checkedEncodedSize(qint64 prefixSize, qint64 sourceSize, qint64 suffixSize)
{
    if (prefixSize < 0 || sourceSize < 0 || suffixSize < 0) {
        return std::nullopt;
    }

    constexpr qint64 kMaximumSize = std::numeric_limits<qint64>::max();
    if (prefixSize > kMaximumSize - sourceSize) {
        return std::nullopt;
    }
    const qint64 prefixAndSourceSize = prefixSize + sourceSize;
    if (suffixSize > kMaximumSize - prefixAndSourceSize) {
        return std::nullopt;
    }
    return prefixAndSourceSize + suffixSize;
}

} // namespace

std::optional<qint64> QCSingleFileMultipartBodyDevice::encodedSize(const QString &boundary,
                                                                   const QString &fieldName,
                                                                   const QString &fileName,
                                                                   const QString &mimeType,
                                                                   qint64 sourceSizeBytes)
{
    const QByteArray prefix = multipartPrefix(boundary, fieldName, fileName, mimeType);
    const QByteArray suffix = multipartSuffix(boundary);
    return checkedEncodedSize(static_cast<qint64>(prefix.size()),
                              sourceSizeBytes,
                              static_cast<qint64>(suffix.size()));
}

QCSingleFileMultipartBodyDevice::QCSingleFileMultipartBodyDevice(const QString &boundary,
                                                                 const QString &fieldName,
                                                                 QIODevice *sourceDevice,
                                                                 const QString &fileName,
                                                                 const QString &mimeType,
                                                                 qint64 sourceSizeBytes,
                                                                 qint64 sourceBasePos,
                                                                 QThread *sourceThread,
                                                                 QObject *parent)
    : QIODevice(parent)
    , m_sourceDevice(sourceDevice)
    , m_prefix(multipartPrefix(boundary, fieldName, fileName, mimeType))
    , m_suffix(multipartSuffix(boundary))
    , m_sourceThread(sourceThread)
    , m_sourceBasePos(sourceBasePos)
    , m_sourceSizeBytes(sourceSizeBytes)
    , m_encodedSize(checkedEncodedSize(static_cast<qint64>(m_prefix.size()),
                                       sourceSizeBytes,
                                       static_cast<qint64>(m_suffix.size()))
                        .value_or(-1))
{
    Q_ASSERT(sourceDevice);
    Q_ASSERT(sourceThread);
    Q_ASSERT(QThread::currentThread() == sourceThread);
    Q_ASSERT(sourceDevice->thread() == sourceThread);
    Q_ASSERT(!parent || parent->thread() == sourceThread);
    Q_ASSERT(m_encodedSize >= 0);
    if (m_encodedSize < 0) {
        setErrorString(QStringLiteral("multipart encoded size exceeds qint64"));
        return;
    }
    open(QIODevice::ReadOnly);
}

QCSingleFileMultipartBodyDevice::~QCSingleFileMultipartBodyDevice() = default;

bool QCSingleFileMultipartBodyDevice::isSequential() const
{
    return false;
}

qint64 QCSingleFileMultipartBodyDevice::size() const
{
    return m_encodedSize;
}

qint64 QCSingleFileMultipartBodyDevice::readData(char *data, qint64 maxlen)
{
    if (maxlen == 0) {
        return 0;
    }
    if (!m_sourceDevice || maxlen < 0) {
        setErrorString(QStringLiteral("multipart source device is unavailable"));
        return -1;
    }
    if (QThread::currentThread() != thread() || m_sourceDevice->thread() != m_sourceThread
        || m_sourceDevice->thread() != thread()) {
        setErrorString(QStringLiteral("multipart source device thread affinity changed"));
        return -1;
    }

    qint64 written         = 0;
    const qint64 totalSize = size();

    while (written < maxlen && m_virtualPos < totalSize) {
        if (m_virtualPos < m_prefix.size()) {
            const qint64 remaining = m_prefix.size() - m_virtualPos;
            const qint64 chunk     = qMin(maxlen - written, remaining);
            std::memcpy(data + written,
                        m_prefix.constData() + static_cast<int>(m_virtualPos),
                        static_cast<size_t>(chunk));
            written += chunk;
            m_virtualPos += chunk;
            continue;
        }

        const qint64 bodyStart = m_prefix.size();
        const qint64 bodyEnd   = bodyStart + m_sourceSizeBytes;
        if (m_virtualPos < bodyEnd) {
            const qint64 sourceOffset = m_virtualPos - bodyStart;
            if (m_sourceDevice->pos() != m_sourceBasePos + sourceOffset
                && !m_sourceDevice->seek(m_sourceBasePos + sourceOffset)) {
                setErrorString(QStringLiteral("multipart source seek failed"));
                return written > 0 ? written : -1;
            }

            const qint64 remaining = bodyEnd - m_virtualPos;
            const qint64 chunk     = qMin(maxlen - written, remaining);
            const qint64 n         = m_sourceDevice->read(data + written, chunk);
            if (n < 0) {
                const QString detail = m_sourceDevice->errorString();
                setErrorString(detail.isEmpty() ? QStringLiteral("multipart source read failed")
                                                : detail);
                return written > 0 ? written : -1;
            }
            if (n == 0) {
                setErrorString(QStringLiteral("multipart source ended before declared size"));
                return written > 0 ? written : -1;
            }

            written += n;
            m_virtualPos += n;
            continue;
        }

        const qint64 suffixOffset = m_virtualPos - bodyEnd;
        const qint64 remaining    = m_suffix.size() - suffixOffset;
        const qint64 chunk        = qMin(maxlen - written, remaining);
        std::memcpy(data + written,
                    m_suffix.constData() + static_cast<int>(suffixOffset),
                    static_cast<size_t>(chunk));
        written += chunk;
        m_virtualPos += chunk;
    }

    return written;
}

qint64 QCSingleFileMultipartBodyDevice::writeData(const char *, qint64)
{
    return -1;
}

bool QCSingleFileMultipartBodyDevice::seek(qint64 pos)
{
    if (!m_sourceDevice || pos < 0 || pos > size()) {
        return false;
    }
    if (QThread::currentThread() != thread() || m_sourceDevice->thread() != m_sourceThread
        || m_sourceDevice->thread() != thread()) {
        setErrorString(QStringLiteral("multipart source device thread affinity changed"));
        return false;
    }

    m_virtualPos = pos;
    return QIODevice::seek(pos);
}

} // namespace QCurl::Internal
