/**
 * @file
 * @brief 声明 multipart/form-data 请求体辅助接口。
 */

#ifndef QCNETWORKMULTIPARTBODY_H
#define QCNETWORKMULTIPARTBODY_H

#include "QCGlobal.h"

#include <QAnyStringView>
#include <QByteArray>
#include <QScopedPointer>
#include <QString>

#include <optional>

class QIODevice;
class QObject;

namespace QCurl {

class QCMultipartFormData;
class QCNetworkMultipartBodyPrivate;

/**
 * @brief 持有已准备好的 multipart/form-data 请求体。
 *
 * 请求体可持有完整内存编码结果，也可持有为单文件字段生成的流式 QIODevice。流式设备
 * 通过 takeDevice() 转移给发送路径，类型自身不暴露内部存储布局。
 */
class QCURL_EXPORT QCNetworkMultipartBody final
{
public:
    /// 构造一个有效的空 multipart 请求体。
    QCNetworkMultipartBody();
    QCNetworkMultipartBody(QCNetworkMultipartBody &&other) noexcept;
    ~QCNetworkMultipartBody();

    QCNetworkMultipartBody &operator=(QCNetworkMultipartBody &&other) noexcept;
    Q_DISABLE_COPY(QCNetworkMultipartBody)

    /**
     * @brief 将已有 multipart 表单编码为内存请求体。
     * @param formData 需要序列化的 multipart 表单数据。
     * @return 有效请求体，包含完整编码数据和已知长度。
     */
    [[nodiscard]] static QCNetworkMultipartBody fromFormData(const QCMultipartFormData &formData);

    /**
     * @brief 为单文件字段构造流式 multipart 请求体。
     * @param device 借用的可读源设备。
     * @param fieldName multipart 字段名。
     * @param fileName 写入 Content-Disposition 的文件名。
     * @param mimeType 文件 MIME 类型；为空时使用 `application/octet-stream`。
     * @param sizeBytes 从当前设备位置开始读取的字节数。
     * @param error 可选错误输出；返回空值时写入构造失败原因。
     * @return 构造成功的请求体；源设备不可用时返回空值。
     *
     * 返回对象只持有生成的包装设备，不接管源 `device` 所有权。调用方必须保证源设备在请求
     * 结束前保持存活、可读、可 seek，且 thread affinity 不变。包装设备发送时仍会按
     * QIODevice 线程规则拒绝跨线程读取。
     */
    [[nodiscard]] static std::optional<QCNetworkMultipartBody> fromSingleFileDevice(
        QIODevice *device,
        QAnyStringView fieldName,
        QAnyStringView fileName,
        QAnyStringView mimeType = {},
        std::optional<qint64> sizeBytes = std::nullopt,
        QString *error = nullptr);

    /// 返回非流式 multipart 请求体的内存载荷。
    [[nodiscard]] QByteArray data() const;

    /// 返回生成的流式设备；内存请求体返回 nullptr。
    [[nodiscard]] QIODevice *device() const noexcept;

    /// 返回包含 boundary 参数的 multipart Content-Type。
    [[nodiscard]] QByteArray contentType() const;

    /// 返回已知载荷长度；无法确定时返回空值。
    [[nodiscard]] std::optional<qint64> sizeBytes() const noexcept;

    /**
     * @brief 转移生成的流式包装设备所有权。
     * @param parent 可选 QObject parent；非空时必须与包装设备处于同一线程。
     * @return 已转移所有权的包装设备；无流式设备或 parent 线程不匹配时返回 nullptr。
     *
     * 返回值是 QCurl 生成的 wrapper device，不是调用方传入的 source device。传入 parent 后，
     * wrapper device 会挂接到该 parent；parent 为空时调用方负责在 wrapper 所在线程销毁，
     * 跨线程销毁应使用 deleteLater() 且目标线程需要事件循环。
     */
    [[nodiscard]] QIODevice *takeDevice(QObject *parent = nullptr);

    /**
     * @brief 转移生成的流式包装设备所有权，并返回可诊断错误。
     * @param parent 可选 QObject parent；非空时必须与包装设备处于同一线程。
     * @param error 可选错误输出；返回 nullptr 时写入失败原因。
     * @return 已转移所有权的包装设备；失败时返回 nullptr。
     */
    [[nodiscard]] QIODevice *takeDevice(QObject *parent, QString *error);

private:
    explicit QCNetworkMultipartBody(QCNetworkMultipartBodyPrivate *d);

    QScopedPointer<QCNetworkMultipartBodyPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKMULTIPARTBODY_H
