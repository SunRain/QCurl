#ifndef QCNETWORKACCESSMANAGER_H
#define QCNETWORKACCESSMANAGER_H

#include <curl/curl.h>

#include <QObject>
#include <QSet>

class QTimer;
class QSocketNotifier;

namespace QCurl {

class QCNetworkAsyncReply;
class QCNetworkSyncReply;
class QCNetworkRequest;
class QCNetworkAccessManagerPrivate;
class QCNetworkAccessManager : public QObject
{
    Q_OBJECT
    friend class QCNetworkAsyncReply;
public:
    explicit QCNetworkAccessManager(QObject *parent = nullptr);
    virtual ~QCNetworkAccessManager();

    enum CookieFileModeFlag {
        NotOpen = 0x0,
        ReadOnly = 0x1,
        WriteOnly = 0x2,
        ReadWrite = ReadOnly | WriteOnly
    };

    QString cookieFilePath() const;

    CookieFileModeFlag cookieFileMode() const;

    void setCookieFilePath(const QString &cookieFilePath, CookieFileModeFlag flag = CookieFileModeFlag::ReadWrite);

    QCNetworkAsyncReply *head(const QCNetworkRequest &request);

    QCNetworkAsyncReply *get(const QCNetworkRequest &request);

    QCNetworkAsyncReply *post(const QCNetworkRequest &request, const QByteArray &data);

    QCNetworkSyncReply *create(const QCNetworkRequest &request);

signals:

public slots:

private:
    QCNetworkAccessManagerPrivate *const d_ptr;
    Q_DECLARE_PRIVATE(QCNetworkAccessManager)
    CookieFileModeFlag m_cookieModeFlag;
    QString                     m_cookieFilePath;


//    QSet<QCNetworkReply*>       replyList;

//    CURLM                       *curlMultiHandle;

//    QTimer                      *timer;

//    curl_socket_t               socketDescriptor;

//    QSocketNotifier             *readNotifier;

//    QSocketNotifier             *writeNotifier;

//    QSocketNotifier             *errorNotifier;
};

} //namespace QCurl
#endif // QCNETWORKACCESSMANAGER_H

