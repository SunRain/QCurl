#include "contract_probes.h"

#include <QCNetworkContentEncoding.h>
#include <QCNetworkHttpHeaders.h>
#include <QCNetworkRequest.h>
#include <QString>

#include <type_traits>

int runRequestConfigProbe(QCurl::QCNetworkRequest &request)
{
    static_assert(!std::is_invocable_v<decltype(&QCurl::QCNetworkRequest::setAllowedProtocols),
                                       QCurl::QCNetworkRequest &,
                                       QStringList>);
    request.setRawHeader(QCurl::httpheaders::kContentType, QByteArrayLiteral("application/json"));
    if (request.rawHeader(QByteArrayLiteral("content-type"))
        != QByteArrayLiteral("application/json")) {
        return 8;
    }
    QCurl::QCNetworkRedirectConfig redirectConfig;
    redirectConfig.setFollowLocation(false);
    if (redirectConfig.setMaxRedirects(3)
        != QCurl::QCNetworkConfigUpdateResult::Applied) {
        return 8;
    }
    redirectConfig.setPostRedirectPolicy(QCurl::QCNetworkPostRedirectPolicy::KeepPost301);

    QCurl::QCNetworkTransferConfig transferConfig;
    transferConfig.setAcceptedEncodings({QCurl::contentencoding::kGzip});
    transferConfig.setIpResolve(QCurl::QCNetworkIpResolve::Ipv4);
    transferConfig.setAllowedProtocols(QCurl::QCNetworkProtocol::Https);

    request.setRedirectConfig(redirectConfig);
    request.setTransferConfig(transferConfig);
    if (request.followLocation() || request.maxRedirects().value_or(-1) != 3
        || !request.autoDecompressionEnabled()
        || request.ipResolve().value_or(QCurl::QCNetworkIpResolve::Any)
               != QCurl::QCNetworkIpResolve::Ipv4
        || request.allowedProtocols()
               != QCurl::QCNetworkProtocols(QCurl::QCNetworkProtocol::Https)) {
        return 8;
    }

    return 0;
}
