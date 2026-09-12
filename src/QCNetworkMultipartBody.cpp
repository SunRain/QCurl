#include "QCNetworkMultipartBody.h"

#include "QCMultipartFormData.h"
#include "private/QCSingleFileMultipartBodyDevice.h"

#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <QThread>

#include <memory>

namespace QCurl {

/**
 * @brief 保存 multipart 请求体的内存数据或单文件流式描述。
 *
 * 流式描述只借用 sourceDevice；实际 wrapper 在 takeDevice() 的 owner thread 内创建并立即
 * 转移，避免 movable value object 持有具有 thread affinity 的 QObject。
 */
class QCNetworkMultipartBodyPrivate
{
public:
    QByteArray data;
    QByteArray contentType;
    std::optional<qint64> sizeBytes;  ///< 空值表示无法预先确定请求体长度。
    QPointer<QIODevice> sourceDevice; ///< 借用源设备，不延长其生命周期。
    QString boundary;
    QString fieldName;
    QString fileName;
    QString mimeType;
    QThread *sourceThread  = nullptr;
    qint64 sourceBasePos   = 0;
    qint64 sourceSizeBytes = 0;
    bool streaming         = false;
    bool deviceTaken       = false; ///< 防止同一描述重复创建 wrapper。
};

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

std::optional<qint64> resolveSingleFileSize(QIODevice *device,
                                            qint64 sourceBasePos,
                                            std::optional<qint64> sizeBytes,
                                            QString *error)
{
    if (device->isSequential()) {
        setError(error,
                 QStringLiteral(
                     "QCNetworkMultipartBody: 单文件 multipart 要求已知长度且设备可 seek"));
        return std::nullopt;
    }

    if (sizeBytes.has_value()) {
        if (sizeBytes.value() < 0) {
            setError(error, QStringLiteral("QCNetworkMultipartBody: sizeBytes 不能为负数"));
            return std::nullopt;
        }
        return sizeBytes;
    }

    const qint64 totalSize = device->size();
    if (totalSize < 0 || totalSize < sourceBasePos) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: 无法从源 QIODevice 推导剩余长度"));
        return std::nullopt;
    }

    return totalSize - sourceBasePos;
}

} // namespace

QCNetworkMultipartBody::QCNetworkMultipartBody()
    : d_ptr(new QCNetworkMultipartBodyPrivate)
{}

QCNetworkMultipartBody::QCNetworkMultipartBody(QCNetworkMultipartBodyPrivate *d)
    : d_ptr(d)
{}

QCNetworkMultipartBody::QCNetworkMultipartBody(QCNetworkMultipartBody &&other) noexcept
{
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    d_ptr.swap(other.d_ptr);
    QT_WARNING_POP
}

QCNetworkMultipartBody::~QCNetworkMultipartBody() = default;

QCNetworkMultipartBody &QCNetworkMultipartBody::operator=(QCNetworkMultipartBody &&other) noexcept
{
    if (this == &other) {
        return *this;
    }

    d_ptr.reset();
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    d_ptr.swap(other.d_ptr);
    QT_WARNING_POP
    return *this;
}

QCNetworkMultipartBody QCNetworkMultipartBody::fromFormData(const QCMultipartFormData &formData)
{
    auto *data        = new QCNetworkMultipartBodyPrivate;
    data->data        = formData.toByteArray();
    data->contentType = formData.contentType().toUtf8();
    data->sizeBytes   = data->data.size();
    return QCNetworkMultipartBody(data);
}

std::optional<QCNetworkMultipartBody> QCNetworkMultipartBody::fromSingleFileDevice(
    QIODevice *device,
    QAnyStringView fieldName,
    QAnyStringView fileName,
    QAnyStringView mimeType,
    std::optional<qint64> sizeBytes,
    QString *error)
{
    if (!device) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: 源 QIODevice 为空"));
        return std::nullopt;
    }
    QThread *const sourceThread = device->thread();
    if (QThread::currentThread() != sourceThread) {
        setError(error,
                 QStringLiteral("QCNetworkMultipartBody: 必须在源 QIODevice 的当前线程创建描述"));
        return std::nullopt;
    }
    if (!device->isReadable()) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: 源 QIODevice 不可读"));
        return std::nullopt;
    }

    const qint64 sourceBasePos = device->pos();
    if (sourceBasePos < 0) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: 无法获取源 QIODevice 的当前位置"));
        return std::nullopt;
    }

    const auto resolvedSize = resolveSingleFileSize(device, sourceBasePos, sizeBytes, error);
    if (!resolvedSize.has_value()) {
        return std::nullopt;
    }

    QCMultipartFormData formData;
    auto data              = std::make_unique<QCNetworkMultipartBodyPrivate>();
    data->contentType      = formData.contentType().toUtf8();
    data->sourceDevice     = device;
    data->boundary         = formData.boundary();
    data->fieldName        = fieldName.toString();
    data->fileName         = fileName.toString();
    data->mimeType         = mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                                : mimeType.toString();
    data->sourceThread     = sourceThread;
    data->sourceBasePos    = sourceBasePos;
    data->sourceSizeBytes  = resolvedSize.value();
    const auto encodedSize = Internal::QCSingleFileMultipartBodyDevice::encodedSize(
        data->boundary, data->fieldName, data->fileName, data->mimeType, data->sourceSizeBytes);
    if (!encodedSize.has_value()) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: multipart 总长度超出 qint64 范围"));
        return std::nullopt;
    }
    data->sizeBytes = encodedSize.value();
    data->streaming = true;

    return QCNetworkMultipartBody(data.release());
}

QByteArray QCNetworkMultipartBody::data() const
{
    if (!d_ptr) {
        return {};
    }
    return d_ptr->data;
}

QByteArray QCNetworkMultipartBody::contentType() const
{
    if (!d_ptr) {
        return {};
    }
    return d_ptr->contentType;
}

std::optional<qint64> QCNetworkMultipartBody::sizeBytes() const noexcept
{
    if (!d_ptr) {
        return std::nullopt;
    }
    return d_ptr->sizeBytes;
}

QIODevice *QCNetworkMultipartBody::takeDevice(QObject *parent)
{
    return takeDevice(parent, nullptr);
}

QIODevice *QCNetworkMultipartBody::takeDevice(QObject *parent, QString *error)
{
    if (!d_ptr || !d_ptr->streaming || d_ptr->deviceTaken) {
        if (!d_ptr) {
            setError(error, QStringLiteral("QCNetworkMultipartBody: 请求体为空"));
        } else if (d_ptr->deviceTaken) {
            setError(error, QStringLiteral("QCNetworkMultipartBody: 流式设备已转移"));
        } else {
            setError(error, QStringLiteral("QCNetworkMultipartBody: 非流式请求体没有可转移设备"));
        }
        return nullptr;
    }

    if (QThread::currentThread() != d_ptr->sourceThread) {
        setError(error,
                 QStringLiteral("QCNetworkMultipartBody: 必须在源 QIODevice 的当前线程转移设备"));
        return nullptr;
    }
    if (!d_ptr->sourceDevice) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: 源 QIODevice 已析构"));
        return nullptr;
    }
    if (d_ptr->sourceDevice->thread() != d_ptr->sourceThread) {
        setError(error,
                 QStringLiteral("QCNetworkMultipartBody: 源 QIODevice 的 thread affinity 已改变"));
        return nullptr;
    }
    if (parent && parent->thread() != d_ptr->sourceThread) {
        setError(error,
                 QStringLiteral("QCNetworkMultipartBody: parent 与源 QIODevice 不在同一线程"));
        return nullptr;
    }

    auto *device       = new Internal::QCSingleFileMultipartBodyDevice(d_ptr->boundary,
                                                                       d_ptr->fieldName,
                                                                       d_ptr->sourceDevice,
                                                                       d_ptr->fileName,
                                                                       d_ptr->mimeType,
                                                                       d_ptr->sourceSizeBytes,
                                                                       d_ptr->sourceBasePos,
                                                                       d_ptr->sourceThread,
                                                                       parent);
    d_ptr->deviceTaken = true;
    d_ptr->sourceDevice.clear();
    return device;
}

} // namespace QCurl
