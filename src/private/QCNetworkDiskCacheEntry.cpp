#include "private/QCNetworkDiskCacheEntry_p.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>

#include <limits>

namespace QCurl::Internal {
namespace {

constexpr quint32 kEnvelopeMagic       = 0x51434348U; // QCCH
constexpr quint16 kEnvelopeVersion     = 4;
constexpr quint16 kEnvelopeFlags       = 0;
constexpr quint32 kEnvelopeHeaderSize  = 52;
constexpr quint64 kMaxMetadataBytes    = 1024 * 1024;
constexpr quint64 kMaxBodyBytes        = 64 * 1024 * 1024;
constexpr quint64 kMaxPayloadBytes     = kMaxMetadataBytes + sizeof(quint64) + kMaxBodyBytes;
constexpr quint32 kMaxVaryHeaderCount  = 128;
constexpr quint32 kMaxRawHeaderCount   = 1024;
constexpr quint32 kMaxUrlBytes         = 64 * 1024;
constexpr quint32 kMaxHeaderNameBytes  = 8 * 1024;
constexpr quint32 kMaxHeaderValueBytes = 64 * 1024;
constexpr qint64 kNullDateTime         = std::numeric_limits<qint64>::min();
constexpr qsizetype kDigestSize        = 32;

void appendU16(QByteArray &bytes, quint16 value)
{
    const qsizetype offset = bytes.size();
    bytes.resize(offset + qsizetype(sizeof(value)));
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void appendU32(QByteArray &bytes, quint32 value)
{
    const qsizetype offset = bytes.size();
    bytes.resize(offset + qsizetype(sizeof(value)));
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void appendU64(QByteArray &bytes, quint64 value)
{
    const qsizetype offset = bytes.size();
    bytes.resize(offset + qsizetype(sizeof(value)));
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void appendI64(QByteArray &bytes, qint64 value)
{
    const qsizetype offset = bytes.size();
    bytes.resize(offset + qsizetype(sizeof(value)));
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

[[nodiscard]] bool appendLengthPrefixed(QByteArray &bytes,
                                        const QByteArray &value,
                                        quint32 maximumLength)
{
    if (value.size() > maximumLength) {
        return false;
    }
    appendU32(bytes, quint32(value.size()));
    bytes.append(value);
    return quint64(bytes.size()) <= kMaxMetadataBytes;
}

[[nodiscard]] qint64 serializedDateTime(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toMSecsSinceEpoch() : kNullDateTime;
}

[[nodiscard]] QByteArray serializeMetadata(const QCNetworkDiskCacheEntry &entry)
{
    if (entry.primaryDigest.size() != kDigestSize || entry.variantDigest.size() != kDigestSize
        || entry.varyHeaderNames.size() > kMaxVaryHeaderCount
        || entry.metadata.rawHeaders().size() > kMaxRawHeaderCount) {
        return {};
    }

    QByteArray payload;
    payload.append(entry.primaryDigest);
    payload.append(entry.variantDigest);

    appendU32(payload, quint32(entry.varyHeaderNames.size()));
    for (const QByteArray &name : entry.varyHeaderNames) {
        if (!appendLengthPrefixed(payload, name, kMaxHeaderNameBytes)) {
            return {};
        }
    }

    if (!appendLengthPrefixed(payload, entry.metadata.url().toEncoded(), kMaxUrlBytes)) {
        return {};
    }

    const auto rawHeaders = entry.metadata.rawHeaders();
    appendU32(payload, quint32(rawHeaders.size()));
    for (const auto &[name, value] : rawHeaders) {
        if (!appendLengthPrefixed(payload, name, kMaxHeaderNameBytes)
            || !appendLengthPrefixed(payload, value, kMaxHeaderValueBytes)) {
            return {};
        }
    }

    appendI64(payload, serializedDateTime(entry.metadata.expirationDate()));
    appendI64(payload, serializedDateTime(entry.metadata.lastModified()));
    appendI64(payload, serializedDateTime(entry.metadata.creationDate()));
    appendI64(payload, serializedDateTime(entry.metadata.requestTime()));
    appendI64(payload, serializedDateTime(entry.metadata.responseTime()));
    appendI64(payload, entry.metadata.correctedInitialAgeSeconds());
    appendI64(payload, entry.body.size());
    appendU32(payload, quint32(qint32(entry.metadata.statusCode())));
    return quint64(payload.size()) <= kMaxMetadataBytes ? payload : QByteArray();
}

[[nodiscard]] bool writeAll(QIODevice &device, QByteArrayView bytes)
{
    qsizetype written = 0;
    while (written < bytes.size()) {
        const qint64 count = device.write(bytes.data() + written, bytes.size() - written);
        if (count <= 0) {
            return false;
        }
        written += count;
    }
    return true;
}

} // namespace

bool writeDiskCacheEntry(const QString &filePath, const QCNetworkDiskCacheEntry &entry)
{
    if (entry.body.size() > qint64(kMaxBodyBytes)) {
        return false;
    }
#if defined(QCURL_ENABLE_TEST_HOOKS)
    if (qEnvironmentVariableIsSet("QCURL_TEST_DISK_CACHE_COMMIT_FAILURE")) {
        return false;
    }
#endif

    const QByteArray metadata = serializeMetadata(entry);
    if (metadata.isEmpty()) {
        return false;
    }

    QByteArray bodyLengthBytes;
    appendU64(bodyLengthBytes, quint64(entry.body.size()));
    const quint64 payloadLength = quint64(metadata.size() + bodyLengthBytes.size())
                                  + quint64(entry.body.size());
    if (payloadLength > kMaxPayloadBytes) {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(metadata));
    hash.addData(QByteArrayView(bodyLengthBytes));
    hash.addData(QByteArrayView(entry.body));

    QByteArray header;
    header.reserve(kEnvelopeHeaderSize);
    appendU32(header, kEnvelopeMagic);
    appendU16(header, kEnvelopeVersion);
    appendU16(header, kEnvelopeFlags);
    appendU32(header, kEnvelopeHeaderSize);
    appendU64(header, payloadLength);
    header.append(hash.result());
    if (header.size() != kEnvelopeHeaderSize) {
        return false;
    }

    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !writeAll(file, header) || !writeAll(file, metadata)
        || !writeAll(file, bodyLengthBytes) || !writeAll(file, entry.body)) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool removeDiskCacheFile(const QString &filePath)
{
#if defined(QCURL_ENABLE_TEST_HOOKS)
    const QByteArray failureBasename = qgetenv("QCURL_TEST_DISK_CACHE_REMOVE_FAILURE_BASENAME");
    if (!failureBasename.isEmpty() && QFileInfo(filePath).fileName().toUtf8() == failureBasename) {
        return false;
    }
#endif
    return QFile::remove(filePath);
}

} // namespace QCurl::Internal
