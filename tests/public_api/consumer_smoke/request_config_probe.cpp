#include "contract_probes.h"

#include <QCNetworkRequest.h>

#include <QString>

int runRequestConfigProbe(QCurl::QCNetworkRequest &request)
{
    QCurl::QCNetworkRedirectConfig redirectConfig;
    redirectConfig.setFollowLocation(false);
    redirectConfig.setMaxRedirects(3);
    redirectConfig.setPostRedirectPolicy(QCurl::QCNetworkPostRedirectPolicy::KeepPost301);

    QCurl::QCNetworkTransferConfig transferConfig;
    transferConfig.setAcceptedEncodings({QStringLiteral("gzip")});
    transferConfig.setIpResolve(QCurl::QCNetworkIpResolve::Ipv4);
    transferConfig.setAllowedProtocols({QStringLiteral("https")});

    request.setRedirectConfig(redirectConfig);
    request.setTransferConfig(transferConfig);
    if (request.followLocation() || request.maxRedirects().value_or(-1) != 3
        || !request.autoDecompressionEnabled()
        || request.ipResolve().value_or(QCurl::QCNetworkIpResolve::Any)
            != QCurl::QCNetworkIpResolve::Ipv4
        || request.allowedProtocols()->size() != 1) {
        return 8;
    }

    return 0;
}
