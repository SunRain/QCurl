/**
 * @file QCMultipartFormData.cpp
 * @brief Multipart/form-data 编码器实现
 */

#include "QCMultipartFormData.h"

#include "private/QCMultipartHeaderEncoding_p.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRandomGenerator>

namespace QCurl {

// ========== 构造和析构 ==========

QCMultipartFormData::QCMultipartFormData()
    : m_boundary(generateBoundary())
    , m_dirty(true)
{}

QCMultipartFormData::~QCMultipartFormData() {}

// ========== 公共方法：添加字段 ==========

void QCMultipartFormData::addTextField(const QString &name, const QString &value)
{
    Field field;
    field.name   = name;
    field.value  = value;
    field.isFile = false;

    m_fields.append(field);
    m_dirty = true;
}

bool QCMultipartFormData::addFileField(const QString &fieldName,
                                       const QString &filePath,
                                       const QString &mimeType)
{
    // 检查文件是否存在和可读
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        qWarning() << "QCMultipartFormData: File not found or not readable:" << filePath;
        return false;
    }

    // 读取文件内容
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "QCMultipartFormData: Cannot open file:" << filePath;
        return false;
    }

    QByteArray fileData = file.readAll();
    file.close();

    // 推断 MIME 类型
    QString actualMimeType = mimeType;
    if (actualMimeType.isEmpty()) {
        actualMimeType = guessMimeType(fileInfo.fileName());
    }

    // 添加字段
    addFileField(fieldName, fileInfo.fileName(), fileData, actualMimeType);
    return true;
}

void QCMultipartFormData::addFileField(const QString &fieldName,
                                       const QString &fileName,
                                       const QByteArray &fileData,
                                       const QString &mimeType)
{
    Field field;
    field.name       = fieldName;
    field.fileName   = fileName;
    field.fileData   = fileData;
    field.mimeType   = mimeType.isEmpty() ? "application/octet-stream" : mimeType;
    field.isFile     = true;

    m_fields.append(field);
    m_dirty = true;
}

// ========== 公共方法：编码和获取 ==========

QByteArray QCMultipartFormData::toByteArray() const
{
    if (!m_dirty && !m_cachedData.isEmpty()) {
        return m_cachedData;
    }

    QByteArray result;

    // 编码所有字段
    for (const Field &field : m_fields) {
        if (field.isFile) {
            result.append(encodeFileField(field));
        } else {
            result.append(encodeTextField(field));
        }
    }

    // 添加结束 boundary
    result.append("--" + m_boundary.toUtf8() + "--\r\n");

    m_cachedData = result;
    m_dirty      = false;

    return result;
}

QString QCMultipartFormData::contentType() const
{
    return QStringLiteral("multipart/form-data; boundary=%1").arg(m_boundary);
}

QString QCMultipartFormData::boundary() const
{
    return m_boundary;
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

    if (boundary == m_boundary) {
        return true;
    }

    m_boundary = boundary;
    m_cachedData.clear();
    m_dirty = true;
    return true;
}

qint64 QCMultipartFormData::size() const
{
    return toByteArray().size();
}

// ========== 公共方法：查询和清理 ==========

int QCMultipartFormData::fieldCount() const
{
    return m_fields.size();
}

void QCMultipartFormData::clear()
{
    m_fields.clear();
    m_cachedData.clear();
    m_dirty = true;
}

// ========== 私有方法 ==========

QString QCMultipartFormData::generateBoundary() const
{
    // 生成随机 boundary 字符串
    // 格式: ----QCurlBoundary + 16位随机十六进制数
    QString randomPart;
    for (int i = 0; i < 16; ++i) {
        randomPart.append(QString::number(QRandomGenerator::global()->bounded(16), 16));
    }

    return QStringLiteral("----QCurlBoundary%1").arg(randomPart);
}

QString QCMultipartFormData::guessMimeType(const QString &fileName) const
{
    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);

    if (mimeType.isValid()) {
        return mimeType.name();
    }

    // 默认 MIME 类型
    return "application/octet-stream";
}

QByteArray QCMultipartFormData::encodeTextField(const Field &field) const
{
    QByteArray result;

    // Boundary
    result.append("--" + m_boundary.toUtf8() + "\r\n");

    // Content-Disposition 头
    result.append("Content-Disposition: form-data; name=\""
                  + Internal::encodeMultipartHeaderQuotedString(field.name) + "\"\r\n");

    // 空行
    result.append("\r\n");

    // 字段值
    result.append(field.value.toUtf8());

    // 换行
    result.append("\r\n");

    return result;
}

QByteArray QCMultipartFormData::encodeFileField(const Field &field) const
{
    QByteArray result;

    // Boundary
    result.append("--" + m_boundary.toUtf8() + "\r\n");

    // Content-Disposition 头
    result.append("Content-Disposition: form-data; name=\""
                  + Internal::encodeMultipartHeaderQuotedString(field.name) + "\"");
    result.append("; filename=\"" + Internal::encodeMultipartHeaderQuotedString(field.fileName)
                  + "\"\r\n");

    // Content-Type 头
    result.append(
        "Content-Type: "
        + Internal::sanitizeMultipartHeaderValue(field.mimeType,
                                                 QByteArrayLiteral("application/octet-stream"))
        + "\r\n");

    // 空行
    result.append("\r\n");

    result.append(field.fileData);

    // 换行
    result.append("\r\n");

    return result;
}

} // namespace QCurl
