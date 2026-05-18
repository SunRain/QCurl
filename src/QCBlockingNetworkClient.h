/**
 * @file
 * @brief 声明 Blocking Extras 的显式同步请求入口。
 */

#ifndef QCBLOCKINGNETWORKCLIENT_H
#define QCBLOCKINGNETWORKCLIENT_H

#include "QCBlockingNetworkResult.h"
#include "QCBlockingCookieStore.h"
#include "QCNetworkHttpMethod.h"
#include "QCNetworkRequest.h"

#include <QByteArray>
#include <QSharedDataPointer>
#include <QUrl>

#include <optional>

class QIODevice;

namespace QCurl {

class QCBlockingNetworkClientData;
class QCBlockingNetworkClientOptionsData;

/**
 * @brief Blocking Extras 的同步请求客户端。
 *
 * 该客户端属于显式启用的 Blocking Extras surface，不属于默认 Core。
 * 它返回值结果，不返回 `QCNetworkReply *`，也不暴露 live manager。
 */
class QCURL_EXPORT QCBlockingNetworkClient
{
public:
    enum class ApplicationThreadPolicy {
        Reject,
        AllowForCliOrTests,
    };

    class QCURL_EXPORT Options
    {
    public:
        Options();
        Options(const Options &other);
        Options(Options &&other) noexcept;
        ~Options();

        Options &operator=(const Options &other);
        Options &operator=(Options &&other) noexcept;

        [[nodiscard]] ApplicationThreadPolicy applicationThreadPolicy() const noexcept;
        void setApplicationThreadPolicy(ApplicationThreadPolicy policy) noexcept;

    private:
        QSharedDataPointer<QCBlockingNetworkClientOptionsData> d;
    };

    QCBlockingNetworkClient();
    explicit QCBlockingNetworkClient(Options options);
    QCBlockingNetworkClient(const QCBlockingNetworkClient &other);
    QCBlockingNetworkClient(QCBlockingNetworkClient &&other) noexcept;
    ~QCBlockingNetworkClient();

    QCBlockingNetworkClient &operator=(const QCBlockingNetworkClient &other);
    QCBlockingNetworkClient &operator=(QCBlockingNetworkClient &&other) noexcept;

    [[nodiscard]] Options options() const;
    void setOptions(const Options &options);

    [[nodiscard]] QCBlockingNetworkResult sendGet(const QCNetworkRequest &request) const;
    [[nodiscard]] QCBlockingNetworkResult sendGet(const QCNetworkRequest &request,
                                                  const QCCookieSnapshot &cookies) const;
    [[nodiscard]] QCBlockingNetworkResult sendPost(const QCNetworkRequest &request,
                                                   const QByteArray &body) const;
    [[nodiscard]] QCBlockingNetworkResult sendPost(const QCNetworkRequest &request,
                                                   const QByteArray &body,
                                                   const QCCookieSnapshot &cookies) const;
    [[nodiscard]] QCBlockingNetworkResult sendPost(
        const QCNetworkRequest &request,
        QIODevice *body,
        std::optional<qint64> sizeBytes = std::nullopt) const;
    [[nodiscard]] QCBlockingNetworkResult sendPut(const QCNetworkRequest &request,
                                                  const QByteArray &body) const;
    [[nodiscard]] QCBlockingNetworkResult sendPut(
        const QCNetworkRequest &request,
        QIODevice *body,
        std::optional<qint64> sizeBytes = std::nullopt) const;

private:
    [[nodiscard]] QCBlockingNetworkResult perform(const QCNetworkRequest &request,
                                                  HttpMethod method,
                                                  const QByteArray &body,
                                                  const QCCookieSnapshot &cookies = {}) const;
    [[nodiscard]] QCBlockingNetworkResult perform(const QCNetworkRequest &request,
                                                  HttpMethod method,
                                                  QIODevice *body,
                                                  std::optional<qint64> sizeBytes) const;
    [[nodiscard]] bool applicationThreadRejected() const;

    QSharedDataPointer<QCBlockingNetworkClientData> d;
};

} // namespace QCurl

#endif // QCBLOCKINGNETWORKCLIENT_H
