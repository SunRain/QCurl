#ifndef QCNETWORKMOCKHANDLER_P_H
#define QCNETWORKMOCKHANDLER_P_H

#include "QCNetworkMockHandler.h"
#include "private/QCNetworkMockProvider_p.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QUrl>

namespace QCurl {

namespace Internal {

/// 供执行链路访问私有 consumeMock，避免把 MockData 暴露为公开 API。
class QCNetworkMockHandlerAccess
{
public:
    [[nodiscard]] static bool consumeMock(QCNetworkMockHandler &handler,
                                          HttpMethod method,
                                          const QUrl &url,
                                          QCNetworkMockData &out);
};

} // namespace Internal

/// 同一 method/url 的 mock 序列；cursor 指向下一次消费的条目。
struct QCNetworkMockSequence
{
    QList<Internal::QCNetworkMockData> items;
    int cursor = 0;
};

/// MockHandler 共享状态；序列和捕获列表均由 mutex 保护。
class QCNetworkMockHandlerPrivate
{
public:
    mutable QMutex mutex;
    QMap<QString, QCNetworkMockSequence> sequences;
    int globalDelay                  = 0;
    bool captureEnabled              = false;
    int captureBodyPreviewLimitBytes = 4096;
    QList<QCNetworkCapturedRequest> capturedRequests;
};

} // namespace QCurl

#endif // QCNETWORKMOCKHANDLER_P_H
