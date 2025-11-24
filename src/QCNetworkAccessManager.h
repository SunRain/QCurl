#ifndef QCNETWORKACCESSMANAGER_H
#define QCNETWORKACCESSMANAGER_H

#include <curl/curl.h>

#include <QObject>
#include <QSet>

class QTimer;
class QSocketNotifier;

namespace QCurl {

class QCNetworkReply;
class QCNetworkRequest;
class QCNetworkAccessManagerPrivate;
class QCNetworkAccessManager : public QObject
{
    Q_OBJECT
    friend class QCNetworkReply;
public:
    explicit QCNetworkAccessManager(QObject *parent = nullptr);
    virtual ~QCNetworkAccessManager();

    QString cookieFilePath() const;

    void setCookieFilePath(const QString &cookieFilePath);

    QCNetworkReply *head(const QCNetworkRequest &request);

    QCNetworkReply *get(const QCNetworkRequest &request);


signals:

public slots:

private:
    QCNetworkAccessManagerPrivate *const d_ptr;
    Q_DECLARE_PRIVATE(QCNetworkAccessManager)
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

