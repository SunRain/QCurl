#include "contract_probes.h"

#include <QCNetworkContentEncoding.h>
#include <QCNetworkHttpHeaders.h>
#include <QCNetworkRequest.h>
#include <QString>

#include <type_traits>

namespace {

int checkResolveIsolation(QCurl::QCNetworkRequest &request)
{
    using Isolation  = QCurl::QCNetworkResolveIsolation;
    using Update     = QCurl::QCNetworkConfigUpdateResult;
    const auto rules = QStringList{QStringLiteral("example.invalid:443:127.0.0.1")};
    if (request.resolveIsolation() != Isolation::Isolated
        || request.setResolveOverride(rules) != Update::Applied
        || request.setResolveIsolation(Isolation::NativeShared) != Update::Applied
        || request.resolveOverride() != rules) {
        return 8;
    }
    auto copy = request;
    if (copy.setResolveIsolation(static_cast<Isolation>(-1)) != Update::InvalidArgument
        || copy.resolveIsolation() != Isolation::NativeShared || copy.resolveOverride() != rules
        || copy.setResolveIsolation(Isolation::Isolated) != Update::Applied
        || copy.setResolveOverride({}) != Update::Applied || copy.resolveOverride()
        || request.resolveIsolation() != Isolation::NativeShared
        || request.resolveOverride() != rules) {
        return 8;
    }
    if (request.setResolveOverride({}) != Update::Applied
        || request.resolveIsolation() != Isolation::NativeShared) {
        return 8;
    }
    return 0;
}

int checkResolveRules(QCurl::QCNetworkRequest &request)
{
    const auto resolve = QStringList{QStringLiteral("example.invalid:443:127.0.0.1")};
    using Update       = QCurl::QCNetworkConfigUpdateResult;
    if (request.setResolveOverride(resolve) != Update::Applied
        || request.resolveOverride() != resolve
        || request.setResolveOverride({QStringLiteral("invalid")}) != Update::InvalidArgument
        || request.resolveOverride() != resolve) {
        return 8;
    }
    auto copy = request;
    if (copy.setResolveOverride({}) != Update::Applied || copy.resolveOverride()
        || request.resolveOverride() != resolve) {
        return 8;
    }
    if (request.setResolveOverride({QStringLiteral("BÜCHER.INVALID:443:127.0.0.1")})
            != Update::Applied
        || request.resolveOverride()
               != QStringList{QStringLiteral("xn--bcher-kva.invalid:443:127.0.0.1")}) {
        return 8;
    }
    return 0;
}

} // namespace

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
    if (redirectConfig.setMaxRedirects(3) != QCurl::QCNetworkConfigUpdateResult::Applied) {
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

    if (checkResolveIsolation(request) != 0 || checkResolveRules(request) != 0) {
        return 8;
    }
    return 0;
}
