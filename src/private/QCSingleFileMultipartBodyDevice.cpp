#include "private/QCSingleFileMultipartBodyDevice.h"

#include "private/QCMultipartHeaderEncoding_p.h"

#include <QThread>

#include <cstring>

namespace QCurl::Internal {

QCSingleFileMultipartBodyDevice::QCSingleFileMultipartBodyDevice(const QString &boundary,
                                                                 const QString &fieldName,
                                                                 QIODevice *sourceDevice,
                                                                 const QString &fileName,
                                                                 const QString &mimeType,
                                                                 qint64 sourceSizeBytes,
                                                                 QObject *parent)
    : QIODevice(parent)
    , m_sourceDevice(sourceDevice)
    , m_prefix(
          QByteArray("--") + boundary.toUtf8() + "\r\nContent-Disposition: form-data; name=\""
          + encodeMultipartHeaderQuotedString(fieldName) + "\"; filename=\""
          + encodeMultipartHeaderQuotedString(fileName) + "\"\r\nContent-Type: "
          + sanitizeMultipartHeaderValue(mimeType, QByteArrayLiteral("application/octet-stream"))
          + "\r\n\r\n")
    , m_suffix(QStringLiteral("\r\n--%1--\r\n").arg(boundary).toUtf8())
    , m_sourceThread(sourceDevice ? sourceDevice->thread() : nullptr)
    , m_sourceBasePos(sourceDevice ? sourceDevice->pos() : 0)
    , m_sourceSizeBytes(sourceSizeBytes)
{
    open(QIODevice::ReadOnly);
}

QCSingleFileMultipartBodyDevice::~QCSingleFileMultipartBodyDevice() = default;

bool QCSingleFileMultipartBodyDevice::isSequential() const
{
    return false;
}

qint64 QCSingleFileMultipartBodyDevice::size() const
{
    return m_prefix.size() + m_sourceSizeBytes + m_suffix.size();
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
    if (QThread::currentThread() != thread()
        || m_sourceDevice->thread() != m_sourceThread
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
                setErrorString(detail.isEmpty()
                                   ? QStringLiteral("multipart source read failed")
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
    if (QThread::currentThread() != thread()
        || m_sourceDevice->thread() != m_sourceThread
        || m_sourceDevice->thread() != thread()) {
        setErrorString(QStringLiteral("multipart source device thread affinity changed"));
        return false;
    }

    m_virtualPos = pos;
    return QIODevice::seek(pos);
}

} // namespace QCurl::Internal
