#include "private/QCBlockingResponseSink_p.h"

#include <QIODevice>

#include <limits>

namespace QCurl::Internal {
namespace {

bool calculateCallbackBytes(size_t size, size_t count, qint64 *bytes)
{
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
        return false;
    }

    const size_t totalSize = size * count;
    if (totalSize > static_cast<size_t>(std::numeric_limits<qint64>::max())) {
        return false;
    }

    *bytes = static_cast<qint64>(totalSize);
    return true;
}

bool canAppendToMemory(const QCBlockingResponseSink &sink, qint64 totalSize)
{
    if (totalSize > std::numeric_limits<qsizetype>::max()) {
        return false;
    }
    if (sink.bytesReceived > std::numeric_limits<qint64>::max() - totalSize) {
        return false;
    }

    return sink.maxInMemoryBytes < 0 || totalSize <= sink.maxInMemoryBytes - sink.bytesReceived;
}

bool writeAllToDevice(QIODevice *device, const char *data, qint64 size, QString *failureMessage)
{
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        const qint64 remaining = size - writtenTotal;
        const qint64 written   = device->write(data + writtenTotal, remaining);
        if (written == 0) {
            *failureMessage = QStringLiteral(
                "Blocking Extras output device write made no progress");
            return false;
        }
        if (written < 0) {
            const QString deviceError = device->errorString().trimmed();
            *failureMessage = deviceError.isEmpty()
                                  ? QStringLiteral("Blocking Extras output device write failed")
                                  : QStringLiteral("Blocking Extras output device write failed: %1")
                                        .arg(deviceError);
            return false;
        }
        if (written > remaining) {
            *failureMessage = QStringLiteral(
                "Blocking Extras output device reported an invalid write count");
            return false;
        }
        writtenTotal += written;
    }
    return true;
}

} // namespace

size_t QCBlockingResponseSink::write(char *data, size_t size, size_t count)
{
    qint64 totalSize = 0;
    if (!calculateCallbackBytes(size, count, &totalSize)) {
        failureMessage = QStringLiteral("Blocking Extras response body chunk is too large");
        return 0;
    }
    if (totalSize <= 0) {
        return 0;
    }

    qint64 bytesToAccept = totalSize;
    if (abortAfterBytes >= 0) {
        const qint64 remainingBytes = qMax<qint64>(0, abortAfterBytes - bytesReceived);
        bytesToAccept               = qMin(totalSize, remainingBytes);
    }

    if (device && bytesToAccept > 0) {
        if (!writeAllToDevice(device, data, bytesToAccept, &failureMessage)) {
            return 0;
        }
    } else if (body && bytesToAccept > 0) {
        if (!canAppendToMemory(*this, bytesToAccept)) {
            failureMessage = QStringLiteral(
                "Blocking Extras response body exceeds maxInMemoryBodyBytes");
            return 0;
        }
        body->append(data, static_cast<qsizetype>(bytesToAccept));
    } else if (!body && !device) {
        return 0;
    }

    bytesReceived += bytesToAccept;
    if (abortAfterBytes >= 0 && bytesReceived >= abortAfterBytes) {
        cancelledByProgress = true;
        return 0;
    }

    return static_cast<size_t>(bytesToAccept);
}

size_t writeBlockingResponseBody(char *data, size_t size, size_t count, void *userdata)
{
    auto *sink = static_cast<QCBlockingResponseSink *>(userdata);
    return sink ? sink->write(data, size, count) : 0;
}

size_t writeBlockingResponseHeader(char *data, size_t size, size_t count, void *userdata)
{
    auto *sink = static_cast<QCBlockingHeaderSink *>(userdata);
    if (!sink || !sink->headers) {
        return 0;
    }

    qint64 totalSize = 0;
    if (!calculateCallbackBytes(size, count, &totalSize)) {
        sink->failureMessage = QStringLiteral("Blocking Extras response header chunk is too large");
        return 0;
    }
    if (totalSize <= 0) {
        return 0;
    }
    if (totalSize > std::numeric_limits<qsizetype>::max()) {
        sink->failureMessage = QStringLiteral(
            "Blocking Extras response header exceeds Qt size limit");
        return 0;
    }

    QByteArray line(data, static_cast<qsizetype>(totalSize));
    line            = line.trimmed();
    const int colon = line.indexOf(':');
    if (colon > 0) {
        sink->headers->append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
    }
    return static_cast<size_t>(totalSize);
}

int invokeBlockingProgress(
    void *userdata, qint64 downloadTotal, qint64 downloaded, qint64 uploadTotal, qint64 uploaded)
{
    auto *state = static_cast<QCBlockingProgressState *>(userdata);
    if (!state || !state->callback) {
        return 0;
    }

    const QCTransferProgress progress(downloaded, downloadTotal, uploaded, uploadTotal);
    if (state->callback(progress, state->userData)) {
        return 0;
    }

    state->failureMessage = QStringLiteral("Blocking Extras progress callback cancelled request");
    return 1;
}

} // namespace QCurl::Internal
