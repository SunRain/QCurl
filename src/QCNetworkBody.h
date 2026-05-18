/**
 * @file
 * @brief 声明常用 HTTP 请求体的值类型辅助接口。
 */

#ifndef QCNETWORKBODY_H
#define QCNETWORKBODY_H

#include "QCGlobal.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSharedDataPointer>
#include <QString>

class QJsonObject;

namespace QCurl {

class QCNetworkBodyData;

/**
 * @brief 持有小型内存 HTTP 请求体及其 Content-Type。
 *
 * 该值类型用于 JSON、URL-encoded form 等便捷构造入口。大文件或流式载荷应使用
 * 基于 QIODevice 的请求 API，避免把完整内容一次性载入内存。
 *
 * QCNetworkBody 使用 Qt 隐式共享：拷贝成本低，未来修改操作会按 QSharedDataPointer
 * 写时分离。对象不保存外部 view；只读共享实例可跨线程传递，并发写入不属于本类型合同。
 */
class QCURL_EXPORT QCNetworkBody final
{
public:
    QCNetworkBody();
    QCNetworkBody(const QCNetworkBody &other);
    QCNetworkBody(QCNetworkBody &&other) noexcept;
    ~QCNetworkBody();

    QCNetworkBody &operator=(const QCNetworkBody &other);
    QCNetworkBody &operator=(QCNetworkBody &&other) noexcept;

    /**
     * @brief 构造紧凑 JSON 请求体。
     * @param json 需要序列化的 JSON 对象。
     * @return 带 `application/json` Content-Type 的请求体。
     */
    [[nodiscard]] static QCNetworkBody fromJson(const QJsonObject &json);

    /**
     * @brief 构造 `application/x-www-form-urlencoded` 请求体。
     * @param fields 需要百分号编码的字段名和值。
     * @return 带 URL 编码内容和匹配 Content-Type 的请求体。
     *
     * 该主入口按传入顺序编码字段，并保留重复 key。
     */
    [[nodiscard]] static QCNetworkBody fromFormUrlEncoded(
        const QList<QPair<QString, QString>> &fields);

    /**
     * @brief 从 QMap 构造 `application/x-www-form-urlencoded` 请求体。
     *
     * 这是便捷重载。QMap 本身不保留插入顺序，也不能表达重复 key；需要严格顺序或重复
     * key 时使用 QList<QPair<QString, QString>> 主入口。
     */
    [[nodiscard]] static QCNetworkBody fromFormUrlEncoded(const QMap<QString, QString> &fields);

    /// 返回已持有的请求体字节。
    [[nodiscard]] QByteArray data() const;

    /// 返回与请求体匹配的 Content-Type 值。
    [[nodiscard]] QByteArray contentType() const;

    /// 请求体没有字节内容时返回 true。
    [[nodiscard]] bool isEmpty() const noexcept;

private:
    QCNetworkBody(QByteArray data, QByteArray contentType);

    QSharedDataPointer<QCNetworkBodyData> d;
};

} // namespace QCurl

#endif // QCNETWORKBODY_H
