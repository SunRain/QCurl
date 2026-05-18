#include "QCNetworkBody.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedData>
#include <QUrl>

#include <utility>

namespace QCurl {

class QCNetworkBodyData : public QSharedData
{
public:
    QByteArray data;
    QByteArray contentType;
};

QCNetworkBody::QCNetworkBody()
    : d(new QCNetworkBodyData)
{}

QCNetworkBody::QCNetworkBody(QByteArray data, QByteArray contentType)
    : d(new QCNetworkBodyData)
{
    d->data        = std::move(data);
    d->contentType = std::move(contentType);
}

QCNetworkBody::QCNetworkBody(const QCNetworkBody &other) = default;

QCNetworkBody::QCNetworkBody(QCNetworkBody &&other) noexcept = default;

QCNetworkBody::~QCNetworkBody() = default;

QCNetworkBody &QCNetworkBody::operator=(const QCNetworkBody &other) = default;

QCNetworkBody &QCNetworkBody::operator=(QCNetworkBody &&other) noexcept = default;

QCNetworkBody QCNetworkBody::fromJson(const QJsonObject &json)
{
    const QJsonDocument document(json);
    return QCNetworkBody(document.toJson(QJsonDocument::Compact),
                         QByteArrayLiteral("application/json"));
}

QCNetworkBody QCNetworkBody::fromFormUrlEncoded(const QList<QPair<QString, QString>> &fields)
{
    QByteArray encoded;
    for (const auto &field : fields) {
        if (!encoded.isEmpty()) {
            encoded += '&';
        }
        encoded += QUrl::toPercentEncoding(field.first);
        encoded += '=';
        encoded += QUrl::toPercentEncoding(field.second);
    }

    return QCNetworkBody(encoded, QByteArrayLiteral("application/x-www-form-urlencoded"));
}

QCNetworkBody QCNetworkBody::fromFormUrlEncoded(const QMap<QString, QString> &fields)
{
    QList<QPair<QString, QString>> orderedFields;
    orderedFields.reserve(fields.size());
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        orderedFields.append({it.key(), it.value()});
    }
    return fromFormUrlEncoded(orderedFields);
}

QByteArray QCNetworkBody::data() const
{
    return d ? d->data : QByteArray();
}

QByteArray QCNetworkBody::contentType() const
{
    return d ? d->contentType : QByteArray();
}

bool QCNetworkBody::isEmpty() const noexcept
{
    return !d || d->data.isEmpty();
}

} // namespace QCurl
