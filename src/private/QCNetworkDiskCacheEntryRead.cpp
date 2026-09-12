#include "private/QCNetworkDiskCacheEntry_p.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTimeZone>
#include <QtEndian>

#include <limits>

namespace QCurl::Internal {
namespace {

constexpr quint32 kEnvelopeMagic        = 0x51434348U; // QCCH
constexpr quint16 kEnvelopeVersion      = 4;
constexpr quint16 kEnvelopeFlags        = 0;
constexpr quint32 kEnvelopeHeaderSize   = 52;
constexpr quint64 kMaxMetadataBytes     = 1024 * 1024;
constexpr quint64 kMaxBodyBytes         = 64 * 1024 * 1024;
constexpr quint64 kMaxPayloadBytes      = kMaxMetadataBytes + sizeof(quint64) + kMaxBodyBytes;
constexpr quint32 kMaxVaryHeaderCount   = 128;
constexpr quint32 kMaxRawHeaderCount    = 1024;
constexpr quint32 kMaxUrlBytes          = 64 * 1024;
constexpr quint32 kMaxHeaderNameBytes   = 8 * 1024;
constexpr quint32 kMaxHeaderValueBytes  = 64 * 1024;
constexpr qint64 kNullDateTime          = std::numeric_limits<qint64>::min();
constexpr qsizetype kChecksumOffset     = 20;
constexpr qsizetype kChecksumSize       = 32;
constexpr qsizetype kDigestSize         = 32;
constexpr qsizetype kChecksumChunkBytes = 64 * 1024;

class PayloadReader
{
public:
    PayloadReader(QFile &file, quint64 payloadLength)
        : m_file(file)
        , m_remaining(payloadLength)
    {}

    [[nodiscard]] std::optional<QByteArray> readBytes(quint64 length, quint64 maximumLength)
    {
        if (length > maximumLength || length > m_remaining
            || length > kMaxMetadataBytes - m_metadataBytesRead
            || length > quint64(std::numeric_limits<qsizetype>::max())) {
            return std::nullopt;
        }
        auto bytes = readAllocated(length);
        if (bytes.has_value()) {
            m_metadataBytesRead += length;
        }
        return bytes;
    }

    [[nodiscard]] std::optional<QByteArray> readBody(quint64 length)
    {
        return length <= kMaxBodyBytes ? readAllocated(length) : std::nullopt;
    }

    [[nodiscard]] std::optional<quint32> readU32()
    {
        const auto bytes = readBytes(sizeof(quint32), sizeof(quint32));
        return bytes.has_value()
                   ? std::optional<quint32>(qFromBigEndian<quint32>(bytes->constData()))
                   : std::nullopt;
    }

    [[nodiscard]] std::optional<quint64> readU64()
    {
        const auto bytes = readBytes(sizeof(quint64), sizeof(quint64));
        return bytes.has_value()
                   ? std::optional<quint64>(qFromBigEndian<quint64>(bytes->constData()))
                   : std::nullopt;
    }

    [[nodiscard]] std::optional<qint64> readI64()
    {
        const auto bytes = readBytes(sizeof(qint64), sizeof(qint64));
        return bytes.has_value() ? std::optional<qint64>(qFromBigEndian<qint64>(bytes->constData()))
                                 : std::nullopt;
    }

    [[nodiscard]] std::optional<QByteArray> readLengthPrefixed(quint32 maximumLength)
    {
        const auto length = readU32();
        return length.has_value() ? readBytes(*length, maximumLength) : std::nullopt;
    }

    [[nodiscard]] quint64 remaining() const noexcept { return m_remaining; }

private:
    [[nodiscard]] std::optional<QByteArray> readAllocated(quint64 length)
    {
        if (length > m_remaining || length > quint64(std::numeric_limits<qsizetype>::max())) {
            return std::nullopt;
        }
        QByteArray bytes(qsizetype(length), Qt::Uninitialized);
        qsizetype read = 0;
        while (read < bytes.size()) {
            const qint64 count = m_file.read(bytes.data() + read, bytes.size() - read);
            if (count <= 0) {
                return std::nullopt;
            }
            read += count;
        }
        m_remaining -= length;
        return bytes;
    }

    QFile &m_file;
    quint64 m_remaining;
    quint64 m_metadataBytesRead = 0;
};

[[nodiscard]] QDateTime deserializeDateTime(qint64 milliseconds)
{
    return milliseconds == kNullDateTime
               ? QDateTime()
               : QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone(QByteArrayLiteral("UTC")));
}

[[nodiscard]] bool verifyChecksum(QFile &file,
                                  quint64 payloadLength,
                                  const QByteArray &expectedChecksum)
{
    if (!file.seek(kEnvelopeHeaderSize)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    quint64 remaining = payloadLength;
    QByteArray chunk(kChecksumChunkBytes, Qt::Uninitialized);
    while (remaining > 0) {
        const qint64 requested = qMin<quint64>(remaining, quint64(chunk.size()));
        const qint64 count     = file.read(chunk.data(), requested);
        if (count <= 0) {
            return false;
        }
        hash.addData(QByteArrayView(chunk.constData(), count));
        remaining -= quint64(count);
    }
    return hash.result() == expectedChecksum;
}

struct ParsedHeaders
{
    QByteArray encodedUrl;
    QList<RawHeaderPair> rawHeaders;
};

[[nodiscard]] std::optional<ParsedHeaders> readIdentityAndHeaders(PayloadReader &reader,
                                                                  QCNetworkDiskCacheEntry &entry)
{
    const auto primaryDigest = reader.readBytes(kDigestSize, kDigestSize);
    const auto variantDigest = reader.readBytes(kDigestSize, kDigestSize);
    const auto varyCount     = reader.readU32();
    if (!primaryDigest.has_value() || !variantDigest.has_value() || !varyCount.has_value()
        || *varyCount > kMaxVaryHeaderCount) {
        return std::nullopt;
    }
    entry.primaryDigest = *primaryDigest;
    entry.variantDigest = *variantDigest;
    for (quint32 index = 0; index < *varyCount; ++index) {
        const auto name = reader.readLengthPrefixed(kMaxHeaderNameBytes);
        if (!name.has_value()) {
            return std::nullopt;
        }
        entry.varyHeaderNames.append(*name);
    }

    ParsedHeaders parsed;
    const auto encodedUrl     = reader.readLengthPrefixed(kMaxUrlBytes);
    const auto rawHeaderCount = reader.readU32();
    if (!encodedUrl.has_value() || !rawHeaderCount.has_value()
        || *rawHeaderCount > kMaxRawHeaderCount) {
        return std::nullopt;
    }
    parsed.encodedUrl = *encodedUrl;
    parsed.rawHeaders.reserve(int(*rawHeaderCount));
    for (quint32 index = 0; index < *rawHeaderCount; ++index) {
        const auto name  = reader.readLengthPrefixed(kMaxHeaderNameBytes);
        const auto value = reader.readLengthPrefixed(kMaxHeaderValueBytes);
        if (!name.has_value() || !value.has_value()) {
            return std::nullopt;
        }
        parsed.rawHeaders.append(qMakePair(*name, *value));
    }
    return parsed;
}

[[nodiscard]] std::optional<QCNetworkDiskCacheEntry> parsePayload(QFile &file, quint64 payloadLength)
{
    if (!file.seek(kEnvelopeHeaderSize)) {
        return std::nullopt;
    }
    PayloadReader reader(file, payloadLength);
    QCNetworkDiskCacheEntry entry;
    const auto parsedHeaders = readIdentityAndHeaders(reader, entry);
    if (!parsedHeaders.has_value()) {
        return std::nullopt;
    }

    const auto expirationDate = reader.readI64();
    const auto lastModified   = reader.readI64();
    const auto creationDate   = reader.readI64();
    const auto requestTime    = reader.readI64();
    const auto responseTime   = reader.readI64();
    const auto correctedAge   = reader.readI64();
    const auto declaredSize   = reader.readI64();
    const auto statusCode     = reader.readU32();
    const auto bodyLength     = reader.readU64();
    if (!expirationDate.has_value() || !lastModified.has_value() || !creationDate.has_value()
        || !requestTime.has_value() || !responseTime.has_value() || !correctedAge.has_value()
        || !declaredSize.has_value() || !statusCode.has_value() || !bodyLength.has_value()
        || *bodyLength > kMaxBodyBytes || *bodyLength != reader.remaining() || *declaredSize < 0
        || quint64(*declaredSize) != *bodyLength) {
        return std::nullopt;
    }
    const auto body = reader.readBody(*bodyLength);
    if (!body.has_value() || reader.remaining() != 0) {
        return std::nullopt;
    }

    entry.body = *body;
    entry.metadata.setUrl(QUrl::fromEncoded(parsedHeaders->encodedUrl));
    entry.metadata.setRawHeaders(parsedHeaders->rawHeaders);
    entry.metadata.setExpirationDate(deserializeDateTime(*expirationDate));
    entry.metadata.setLastModified(deserializeDateTime(*lastModified));
    entry.metadata.setCreationDate(deserializeDateTime(*creationDate));
    entry.metadata.setRequestTime(deserializeDateTime(*requestTime));
    entry.metadata.setResponseTime(deserializeDateTime(*responseTime));
    entry.metadata.setCorrectedInitialAgeSeconds(*correctedAge);
    entry.metadata.setSize(*declaredSize);
    entry.metadata.setStatusCode(qint32(*statusCode));
    entry.metadata.setVaryHeaderNames(entry.varyHeaderNames);
    return entry;
}

} // namespace

std::optional<QCNetworkDiskCacheEntry> readDiskCacheEntry(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    const qint64 fileSize   = file.size();
    const QByteArray header = file.read(kEnvelopeHeaderSize);
    const auto reject       = [&file, &filePath]() -> std::optional<QCNetworkDiskCacheEntry> {
        file.close();
        (void)removeDiskCacheFile(filePath);
        return std::nullopt;
    };
    if (fileSize < kEnvelopeHeaderSize || quint64(fileSize) > kEnvelopeHeaderSize + kMaxPayloadBytes
        || header.size() != kEnvelopeHeaderSize) {
        return reject();
    }

    const quint32 magic         = qFromBigEndian<quint32>(header.constData());
    const quint16 version       = qFromBigEndian<quint16>(header.constData() + 4);
    const quint16 flags         = qFromBigEndian<quint16>(header.constData() + 6);
    const quint32 headerLength  = qFromBigEndian<quint32>(header.constData() + 8);
    const quint64 payloadLength = qFromBigEndian<quint64>(header.constData() + 12);
    const QByteArray checksum   = header.mid(kChecksumOffset, kChecksumSize);
    if (magic != kEnvelopeMagic || version != kEnvelopeVersion || flags != kEnvelopeFlags
        || headerLength != kEnvelopeHeaderSize || payloadLength > kMaxPayloadBytes
        || payloadLength != quint64(fileSize - kEnvelopeHeaderSize)
        || !verifyChecksum(file, payloadLength, checksum)) {
        return reject();
    }

    const auto entry = parsePayload(file, payloadLength);
    return entry.has_value() ? entry : reject();
}

} // namespace QCurl::Internal
