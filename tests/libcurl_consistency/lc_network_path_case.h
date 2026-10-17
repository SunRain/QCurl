#ifndef LC_NETWORK_PATH_CASE_H
#define LC_NETWORK_PATH_CASE_H

namespace QCurl {
class QCNetworkAccessManager;
}

/** 执行 pytest 提供的单请求网络路径配置，记录真实传输码并保存响应正文。 */
void runNetworkPathCase(QCurl::QCNetworkAccessManager &manager);

#endif // LC_NETWORK_PATH_CASE_H
