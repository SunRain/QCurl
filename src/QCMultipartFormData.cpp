/**
 * @file QCMultipartFormData.cpp
 * @brief Multipart/form-data 编码器实现
 */

#include "QCMultipartFormData.h"

#include "private/QCMultipartHeaderEncoding_p.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRandomGenerator>
#include <QSharedData>

namespace QCurl {

namespace {

/// 表示一个 multipart part，文本字段和内存文件字段共用同一结构。
struct QCMultipartField
{
    QString name;
    // 文本字段使用 value；文件字段使用 fileName/mimeType/fileData。
    QString value;
    QString fileName;
    QString mimeType;
    QByteArray fileData;
    bool isFile = false;
};

QString generateBoundary()
{
    // 固定前缀便于调试，随机尾部降低与 payload 内容冲突的概率。
    QString randomPart;
    for (int i = 0; i < 16; ++i) {
        randomPart.append(QString::number(QRandomGenerator::global()->bounded(16), 16));
    }

    return QStringLiteral("----QCurlBoundary%1").arg(randomPart);
}

QString guessMimeType(const QString &fileName)
{
    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);

    if (mimeType.isValid()) {
        return mimeType.name();
    }

    return QStringLiteral("application/octet-stream");
}

QByteArray encodeTextField(const QString &boundary, const QCMultipartField &field)
{
    QByteArray result;

    result.append("--" + boundary.toUtf8() + "\r\n");
    result.append("Content-Disposition: form-data; name=\""
                  + Internal::encodeMultipartHeaderQuotedString(field.name) + "\"\r\n");
    result.append("\r\n");
    result.append(field.value.toUtf8());
    result.append("\r\n");

    return result;
}

QByteArray encodeFileField(const QString &boundary, const QCMultipartField &field)
{
    QByteArray result;

    result.append("--" + boundary.toUtf8() + "\r\n");
    result.append("Content-Disposition: form-data; name=\""
                  + Internal::encodeMultipartHeaderQuotedString(field.name) + "\"");
    result.append("; filename=\"" + Internal::encodeMultipartHeaderQuotedString(field.fileName)
                  + "\"\r\n");
    result.append(
        "Content-Type: "
        + Internal::sanitizeMultipartHeaderValue(field.mimeType,
                                                 QByteArrayLiteral("application/octet-stream"))
        + "\r\n");
    result.append("\r\n");
    result.append(field.fileData);
    result.append("\r\n");

    return result;
}

} // namespace

/// Multipart 表单的共享存储，支持值类型拷贝和编码缓存。
class QCMultipartFormDataData : public QSharedData
{
public:
    QString boundary = generateBoundary();
    QList<QCMultipartField> fields;
    // toByteArray() 可重复调用，dirty 表示字段或 boundary 已改变。
    mutable QByteArray cachedData;
    mutable bool dirty = true;
};

QCMultipartFormData::QCMultipartFormData()
    : d(new QCMultipartFormDataData)
{}

QCMultipartFormData::QCMultipartFormData(const QCMultipartFormData &other) = default;

QCMultipartFormData::QCMultipartFormData(QCMultipartFormData &&other) noexcept = default;

QCMultipartFormData::~QCMultipartFormData() = default;

QCMultipartFormData &QCMultipartFormData::operator=(const QCMultipartFormData &other) = default;

QCMultipartFormData &QCMultipartFormData::operator=(QCMultipartFormData &&other) noexcept = default;


void QCMultipartFormData::addTextField(const QString &name, const QString &value)
{
    QCMultipartField field;
    field.name   = name;
    field.value  = value;
    field.isFile = false;

    d->fields.append(field);
    d->dirty = true;
}

bool QCMultipartFormData::addFileField(const QString &fieldName,
                                       const QString &filePath,
                                       const QString &mimeType)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        qWarning() << "QCMultipartFormData: File not found or not readable:" << filePath;
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "QCMultipartFormData: Cannot open file:" << filePath;
        return false;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QString actualMimeType = mimeType;
    if (actualMimeType.isEmpty()) {
        actualMimeType = guessMimeType(fileInfo.fileName());
    }

    addFileField(fieldName, fileInfo.fileName(), fileData, actualMimeType);
    return true;
}

void QCMultipartFormData::addFileField(const QString &fieldName,
                                       const QString &fileName,
                                       const QByteArray &fileData,
                                       const QString &mimeType)
{
    QCMultipartField field;
    field.name       = fieldName;
    field.fileName   = fileName;
    field.fileData   = fileData;
    field.mimeType   = mimeType.isEmpty() ? "application/octet-stream" : mimeType;
    field.isFile     = true;

    d->fields.append(field);
    d->dirty = true;
}

QByteArray QCMultipartFormData::toByteArray() const
{
    if (!d->dirty && !d->cachedData.isEmpty()) {
        return d->cachedData;
    }

    QByteArray result;

    for (const QCMultipartField &field : d->fields) {
        if (field.isFile) {
            result.append(encodeFileField(d->boundary, field));
        } else {
            result.append(encodeTextField(d->boundary, field));
        }
    }

    result.append("--" + d->boundary.toUtf8() + "--\r\n");

    d->cachedData = result;
    d->dirty      = false;

    return result;
}

QString QCMultipartFormData::contentType() const
{
    return QStringLiteral("multipart/form-data; boundary=%1").arg(d->boundary);
}

QString QCMultipartFormData::boundary() const
{
    return d->boundary;
}

bool QCMultipartFormData::setBoundary(const QString &boundary)
{
    if (boundary.isEmpty()) {
        qWarning() << "QCMultipartFormData: boundary cannot be empty";
        return false;
    }
    if (boundary.size() > 70) {
        qWarning() << "QCMultipartFormData: boundary too long:" << boundary.size();
        return false;
    }
    if (boundary.contains('\r') || boundary.contains('\n')) {
        qWarning() << "QCMultipartFormData: boundary contains CR/LF";
        return false;
    }
    if (boundary.contains(' ') || boundary.contains('\t')) {
        qWarning() << "QCMultipartFormData: boundary contains whitespace";
        return false;
    }

    if (boundary == d->boundary) {
        return true;
    }

    d->boundary = boundary;
    d->cachedData.clear();
    d->dirty = true;
    return true;
}

qint64 QCMultipartFormData::size() const
{
    return toByteArray().size();
}

int QCMultipartFormData::fieldCount() const
{
    return d->fields.size();
}

void QCMultipartFormData::clear()
{
    d->fields.clear();
    d->cachedData.clear();
    d->dirty = true;
}

} // namespace QCurl
