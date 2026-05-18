#include "QCNetworkMultipartBody.h"

#include "QCMultipartFormData.h"
#include "private/QCSingleFileMultipartBodyDevice.h"

#include <QObject>
#include <QIODevice>

#include <memory>

namespace QCurl {

class QCNetworkMultipartBodyPrivate
{
public:
    QByteArray data;
    std::unique_ptr<QIODevice> device;
    QByteArray contentType;
    std::optional<qint64> sizeBytes;
    bool deviceTaken = false;
};

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

std::optional<qint64> resolveSingleFileSize(QIODevice *device,
                                            std::optional<qint64> sizeBytes,
                                            QString *error)
{
    if (device->isSequential()) {
        setError(error,
                 QStringLiteral("QCNetworkMultipartBody: 单文件 multipart 要求已知长度且设备可 seek"));
        return std::nullopt;
    }

    if (sizeBytes.has_value()) {
        if (sizeBytes.value() < 0) {
            setError(error,
                     QStringLiteral("QCNetworkMultipartBody: sizeBytes 不能为负数"));
            return std::nullopt;
        }
        return sizeBytes;
    }

    const qint64 basePos = device->pos();
    const qint64 totalSize = device->size();
    if (basePos < 0 || totalSize < 0 || totalSize < basePos) {
        setError(error,
                 QStringLiteral("QCNetworkMultipartBody: 无法从源 QIODevice 推导剩余长度"));
        return std::nullopt;
    }

    return totalSize - basePos;
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
    if (!device->isReadable()) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: 源 QIODevice 不可读"));
        return std::nullopt;
    }

    const auto resolvedSize = resolveSingleFileSize(device, sizeBytes, error);
    if (!resolvedSize.has_value()) {
        return std::nullopt;
    }

    QCMultipartFormData formData;
    auto data = std::make_unique<QCNetworkMultipartBodyPrivate>();
    data->contentType = formData.contentType().toUtf8();
    data->device = std::make_unique<Internal::QCSingleFileMultipartBodyDevice>(
        formData.boundary(),
        fieldName.toString(),
        device,
        fileName.toString(),
        mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : mimeType.toString(),
        resolvedSize.value());
    data->sizeBytes = data->device->size();

    return QCNetworkMultipartBody(data.release());
}

QByteArray QCNetworkMultipartBody::data() const
{
    if (!d_ptr) {
        return {};
    }
    return d_ptr->data;
}

QIODevice *QCNetworkMultipartBody::device() const noexcept
{
    if (!d_ptr) {
        return nullptr;
    }
    return d_ptr->device.get();
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
    if (!d_ptr || !d_ptr->device) {
        if (!d_ptr) {
            setError(error, QStringLiteral("QCNetworkMultipartBody: 请求体为空"));
        } else if (d_ptr->deviceTaken) {
            setError(error, QStringLiteral("QCNetworkMultipartBody: 流式设备已转移"));
        } else {
            setError(error, QStringLiteral("QCNetworkMultipartBody: 非流式请求体没有可转移设备"));
        }
        return nullptr;
    }

    QIODevice *device = d_ptr->device.get();
    if (parent && parent->thread() != device->thread()) {
        setError(error, QStringLiteral("QCNetworkMultipartBody: parent 与流式设备不在同一线程"));
        return nullptr;
    }
    device = d_ptr->device.release();
    d_ptr->deviceTaken = true;
    device->setParent(parent);
    return device;
}

} // namespace QCurl
