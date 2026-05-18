/**
 * @file
 * @brief 单文件 multipart 流式请求体设备。
 */

#ifndef QCSINGLEFILEMULTIPARTBODYDEVICE_H
#define QCSINGLEFILEMULTIPARTBODYDEVICE_H

#include "QCGlobal.h"

#include <QByteArray>
#include <QIODevice>
#include <QPointer>
#include <QString>

namespace QCurl::Internal {

/**
 * @brief 单文件 multipart 上传使用的只读虚拟设备。
 *
 * 该设备借用源 QIODevice，并把 multipart 头、源字节和结束 boundary 组合成可 seek
 * 的连续读取流。源设备在包装设备生命周期内必须保持存活、可读、可 seek，并保持
 * thread affinity 不变。
 */
class QCURL_EXPORT QCSingleFileMultipartBodyDevice final : public QIODevice
{
    Q_OBJECT

public:
    /**
     * @brief 围绕源设备创建 multipart 包装设备。
     * @param boundary 不带前导横线的 multipart boundary。
     * @param fieldName 写入 Content-Disposition 的字段名。
     * @param sourceDevice 借用的可读、可 seek 源设备。
     * @param fileName 写入 Content-Disposition 的文件名。
     * @param mimeType 文件 part 的 MIME 类型。
     * @param sourceSizeBytes 需要暴露的源设备字节数。
     * @param parent 包装设备的可选 QObject parent。
     */
    QCSingleFileMultipartBodyDevice(const QString &boundary,
                                    const QString &fieldName,
                                    QIODevice *sourceDevice,
                                    const QString &fileName,
                                    const QString &mimeType,
                                    qint64 sourceSizeBytes,
                                    QObject *parent = nullptr);

    ~QCSingleFileMultipartBodyDevice() override;

    /// 返回 false；该包装设备支持 seek()，可用于请求重放。
    [[nodiscard]] bool isSequential() const override;

    /// 返回虚拟 multipart 载荷的总字节数。
    [[nodiscard]] qint64 size() const override;

    /// 重新定位虚拟 multipart 流，用于重试或重定向后的 body 重放。
    bool seek(qint64 pos) override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    QPointer<QIODevice> m_sourceDevice;
    QByteArray m_prefix;
    QByteArray m_suffix;
    QThread *m_sourceThread = nullptr;
    qint64 m_sourceBasePos   = 0;
    qint64 m_sourceSizeBytes = 0;
    qint64 m_virtualPos      = 0;
};

} // namespace QCurl::Internal

#endif // QCSINGLEFILEMULTIPARTBODYDEVICE_H
