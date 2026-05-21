#ifndef QCURL_PUBLIC_API_CONSUMER_CONTRACT_PROBES_H
#define QCURL_PUBLIC_API_CONSUMER_CONTRACT_PROBES_H

namespace QCurl {
class QCNetworkRequest;
}

int runRequestConfigProbe(QCurl::QCNetworkRequest &request);
int runCookieAsyncResultProbe();

#endif // QCURL_PUBLIC_API_CONSUMER_CONTRACT_PROBES_H
