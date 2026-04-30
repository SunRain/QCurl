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

class QCURL_EXPORT QCSingleFileMultipartBodyDevice final : public QIODevice
{
    Q_OBJECT

public:
    QCSingleFileMultipartBodyDevice(const QString &boundary,
                                    const QString &fieldName,
                                    QIODevice *sourceDevice,
                                    const QString &fileName,
                                    const QString &mimeType,
                                    qint64 sourceSizeBytes,
                                    QObject *parent = nullptr);

    ~QCSingleFileMultipartBodyDevice() override;

    [[nodiscard]] bool isSequential() const override;
    [[nodiscard]] qint64 size() const override;
    bool seek(qint64 pos) override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    QPointer<QIODevice> m_sourceDevice;
    QByteArray m_prefix;
    QByteArray m_suffix;
    qint64 m_sourceBasePos   = 0;
    qint64 m_sourceSizeBytes = 0;
    qint64 m_virtualPos      = 0;
};

} // namespace QCurl::Internal

#endif // QCSINGLEFILEMULTIPARTBODYDEVICE_H
