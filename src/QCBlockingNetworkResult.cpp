#include "QCBlockingNetworkResult.h"
#include "QCBlockingCookieStore.h"

#include <QSharedData>

#include <utility>

namespace QCurl {

class QCBlockingNetworkResultData : public QSharedData
{
public:
    NetworkError error = NetworkError::InvalidRequest;
    QString errorMessage;
    int statusCode = 0;
    QCBlockingNetworkResult::HeaderList headers;
    QByteArray body;
    QCCookieDelta cookieDelta;
};

QCBlockingNetworkResult::QCBlockingNetworkResult()
    : d(new QCBlockingNetworkResultData)
{
}

QCBlockingNetworkResult::QCBlockingNetworkResult(const QCBlockingNetworkResult &other) = default;

QCBlockingNetworkResult::QCBlockingNetworkResult(QCBlockingNetworkResult &&other) noexcept = default;

QCBlockingNetworkResult::~QCBlockingNetworkResult() = default;

QCBlockingNetworkResult &QCBlockingNetworkResult::operator=(
    const QCBlockingNetworkResult &other) = default;

QCBlockingNetworkResult &QCBlockingNetworkResult::operator=(
    QCBlockingNetworkResult &&other) noexcept = default;

QCBlockingNetworkResult QCBlockingNetworkResult::success(int statusCode,
                                                         QByteArray body,
                                                         HeaderList headers)
{
    return success(statusCode, std::move(body), std::move(headers), QCCookieDelta());
}

QCBlockingNetworkResult QCBlockingNetworkResult::success(int statusCode,
                                                         QByteArray body,
                                                         HeaderList headers,
                                                         QCCookieDelta cookieDelta)
{
    QCBlockingNetworkResult result;
    result.d->error = NetworkError::NoError;
    result.d->statusCode = statusCode;
    result.d->body = std::move(body);
    result.d->headers = std::move(headers);
    result.d->cookieDelta = std::move(cookieDelta);
    return result;
}

QCBlockingNetworkResult QCBlockingNetworkResult::failure(NetworkError error,
                                                         QString errorMessage,
                                                         int statusCode)
{
    QCBlockingNetworkResult result;
    result.d->error = error;
    result.d->errorMessage = std::move(errorMessage);
    result.d->statusCode = statusCode;
    return result;
}

bool QCBlockingNetworkResult::isSuccess() const noexcept
{
    return d->error == NetworkError::NoError;
}

NetworkError QCBlockingNetworkResult::error() const noexcept
{
    return d->error;
}

QString QCBlockingNetworkResult::errorMessage() const
{
    return d->errorMessage;
}

int QCBlockingNetworkResult::statusCode() const noexcept
{
    return d->statusCode;
}

QByteArray QCBlockingNetworkResult::body() const
{
    return d->body;
}

QCBlockingNetworkResult::HeaderList QCBlockingNetworkResult::headers() const
{
    return d->headers;
}

QCCookieDelta QCBlockingNetworkResult::cookieDelta() const
{
    return d->cookieDelta;
}

} // namespace QCurl
