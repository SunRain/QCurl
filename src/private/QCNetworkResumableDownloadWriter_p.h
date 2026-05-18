#ifndef QCNETWORKRESUMABLEDOWNLOADWRITER_P_H
#define QCNETWORKRESUMABLEDOWNLOADWRITER_P_H

#include "QCGlobal.h"

#include <QSharedPointer>
#include <QString>

#include <optional>

class QFile;
class QIODevice;
class QSaveFile;

namespace QCurl {

class QCNetworkReply;

namespace Internal {

struct ResumableDownloadWriteContext
{
    bool modeDecided = false;
    bool appendMode = false;
    bool safeOverwriteMode = false;
    bool writeFailed = false;
    QSharedPointer<QFile> appendFile;
    QSharedPointer<QFile> directFile;
    QSharedPointer<QSaveFile> overwriteFile;
};

QSharedPointer<ResumableDownloadWriteContext> makeResumableDownloadWriteContext(
    const QString &savePath);

void writeResumableDownloadChunk(QCNetworkReply *reply,
                                 const QSharedPointer<ResumableDownloadWriteContext> &context,
                                 const QString &savePath,
                                 qint64 existingSize,
                                 bool hadExistingFile);

[[nodiscard]] std::optional<QString> commitResumableDownloadIfNeeded(
    QCNetworkReply *reply,
    const QSharedPointer<ResumableDownloadWriteContext> &context,
    const QString &savePath,
    qint64 existingSize,
    bool hadExistingFile);

} // namespace Internal
} // namespace QCurl

#endif // QCNETWORKRESUMABLEDOWNLOADWRITER_P_H
