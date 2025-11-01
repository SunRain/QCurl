/**
 * @file QCMultipartFormData.h
 * @brief Multipart/form-data 编码器
 * @author QCurl Project
 * @date 2025-11-07
 *
 * 提供 multipart/form-data 格式的编码支持，用于文件上传场景。
 * 符合 RFC 7578 规范。
 */

#ifndef QCMULTIPARTFORMDATA_H
#define QCMULTIPARTFORMDATA_H

#include <QByteArray>
#include <QString>
#include <QMap>
#include <QIODevice>
#include <QSharedPointer>

namespace QCurl {

/**
 * @brief Multipart/form-data 编码器
 *
 * 用于构建 multipart/form-data 格式的 HTTP 请求体，常用于文件上传。
 *
 * @par 使用示例：
 * @code
 * QCMultipartFormData formData;
 * formData.addTextField("username", "alice");
 * formData.addTextField("email", "alice@example.com");
 * formData.addFileField("avatar", "/path/to/avatar.jpg", "image/jpeg");
 *
 * QCNetworkRequest request(QUrl("https://api.example.com/upload"));
 * auto *reply = manager->post(request, formData.toByteArray(), formData.contentType());
 * @endcode
 *
 */
class QCMultipartFormData
{
public:
    /**
     * @brief 构造函数
     *
     * 自动生成随机 boundary 字符串
     */
    QCMultipartFormData();

    /**
     * @brief 析构函数
     */
    ~QCMultipartFormData();

    // ========== 添加字段 ==========

    /**
     * @brief 添加文本字段
     *
     * @param name 字段名称
     * @param value 字段值
     *
     * @par 示例：
     * @code
     * formData.addTextField("username", "alice");
     * formData.addTextField("age", "25");
     * @endcode
     */
    void addTextField(const QString &name, const QString &value);

    /**
     * @brief 添加文件字段（从文件路径）
     *
     * @param fieldName 字段名称（如 "file", "avatar", "document"）
     * @param filePath 本地文件路径
     * @param mimeType MIME 类型（如 "image/jpeg", "application/pdf"）
     *                 如果为空，将尝试自动检测
     *
     * @return 如果文件存在且可读返回 true，否则返回 false
     *
     * @par 示例：
     * @code
     * formData.addFileField("avatar", "/home/user/photo.jpg", "image/jpeg");
     * formData.addFileField("document", "/tmp/report.pdf", "application/pdf");
     * @endcode
     */
    bool addFileField(const QString &fieldName, const QString &filePath, const QString &mimeType = QString());

    /**
     * @brief 添加文件字段（从 QByteArray）
     *
     * @param fieldName 字段名称
     * @param fileName 文件名（会出现在请求中）
     * @param fileData 文件数据
     * @param mimeType MIME 类型
     *
     * @par 示例：
     * @code
     * QByteArray imageData = loadImageFromMemory();
     * formData.addFileField("avatar", "photo.jpg", imageData, "image/jpeg");
     * @endcode
     */
    void addFileField(const QString &fieldName, const QString &fileName,
                      const QByteArray &fileData, const QString &mimeType);

    /**
     * @brief 添加文件字段（从 QIODevice 流）
     *
     * 用于大文件上传，避免一次性加载到内存。
     *
     * @param fieldName 字段名称
     * @param device IO 设备（如 QFile）
     * @param fileName 文件名
     * @param mimeType MIME 类型
     *
     * @return 如果设备可读返回 true，否则返回 false
     *
     * @note device 必须在请求完成前保持有效
     *
     * @par 示例：
     * @code
     * QFile *largeFile = new QFile("/path/to/video.mp4");
     * if (largeFile->open(QIODevice::ReadOnly)) {
     *     formData.addFileFieldStream("video", largeFile, "video.mp4", "video/mp4");
     * }
     * @endcode
     */
    bool addFileFieldStream(const QString &fieldName, QIODevice *device,
                            const QString &fileName, const QString &mimeType);

    // ========== 编码和获取 ==========

    /**
     * @brief 编码为 multipart/form-data 格式的字节数组
     *
     * @return 编码后的请求体（包含所有字段和文件）
     *
     * @note 对于流式字段，不会包含在返回的字节数组中
     */
    QByteArray toByteArray() const;

    /**
     * @brief 获取 Content-Type 头的值
     *
     * @return 完整的 Content-Type 字符串，包含 boundary
     *
     * @par 示例：
     * @code
     * QString contentType = formData.contentType();
     * // 返回: "multipart/form-data; boundary=----QCurlBoundary1234567890"
     *
     * request.setHeader("Content-Type", contentType.toUtf8());
     * @endcode
     */
    QString contentType() const;

    /**
     * @brief 获取 boundary 字符串
     *
     * @return boundary 字符串（不包含前导 "--"）
     */
    QString boundary() const;

    /**
     * @brief 计算编码后的总大小（字节）
     *
     * @return 请求体的总大小
     *
     * @note 用于设置 Content-Length 头
     */
    qint64 size() const;

    // ========== 查询和清理 ==========

    /**
     * @brief 检查是否包含流式字段
     *
     * @return 如果包含流式字段返回 true
     */
    bool hasStreamFields() const;

    /**
     * @brief 获取字段数量
     *
     * @return 字段总数（文本字段 + 文件字段）
     */
    int fieldCount() const;

    /**
     * @brief 清空所有字段
     */
    void clear();

private:
    // ========== 内部数据结构 ==========

    struct Field {
        QString name;           ///< 字段名
        QString value;          ///< 文本值（文本字段）
        QString fileName;       ///< 文件名（文件字段）
        QString mimeType;       ///< MIME 类型
        QByteArray fileData;    ///< 文件数据（内存文件）
        QIODevice *fileStream;  ///< 文件流（流式文件）
        bool isFile;            ///< 是否为文件字段

        Field() : fileStream(nullptr), isFile(false) {}
    };

    // ========== 私有方法 ==========

    /**
     * @brief 生成随机 boundary 字符串
     */
    QString generateBoundary() const;

    /**
     * @brief 从文件扩展名推断 MIME 类型
     */
    QString guessMimeType(const QString &fileName) const;

    /**
     * @brief 编码单个文本字段
     */
    QByteArray encodeTextField(const Field &field) const;

    /**
     * @brief 编码单个文件字段（内存文件）
     */
    QByteArray encodeFileField(const Field &field) const;

    // ========== 私有成员 ==========

    QString m_boundary;                 ///< Boundary 字符串
    QList<Field> m_fields;              ///< 所有字段列表
    mutable QByteArray m_cachedData;    ///< 缓存的编码数据
    mutable bool m_dirty;               ///< 是否需要重新编码
};

} // namespace QCurl

#endif // QCMULTIPARTFORMDATA_H
