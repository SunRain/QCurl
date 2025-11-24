//#ifndef QCNETWORKREPLY_P_H
//#define QCNETWORKREPLY_P_H

//#include <QtGlobal>
//#include <curl/curl.h>

//#include "QCNetworkRequest.h"
//#include "QCNetworkReply.h"
//#include "qbytedata_p.h"


//namespace QCurl {

//class QCNetworkAccessManager;
//class QCNetworkReplyPrivate
//{
//public:
//    QCNetworkReplyPrivate(QCNetworkReply *self)
//        : q_ptr(self)
//    {
//        manager         = Q_NULLPTR;
//        curlHandle      = Q_NULLPTR;
//        curlHttpHeaders = Q_NULLPTR;
//    }
//    ~QCNetworkReplyPrivate() {
//        qDebug()<<Q_FUNC_INFO<<"-----------";
//        if (curlHandle) {
//            curl_easy_cleanup(curlHandle);
//            curlHandle = Q_NULLPTR;
//        }
//        if (curlHttpHeaders) {
//            curl_slist_free_all(curlHttpHeaders);
//            curlHttpHeaders = Q_NULLPTR;
//        }
//    }

//    QCNetworkRequest        request;

//    QUrl                    url;

//    QCByteDataBuffer        buffer;

//    QList<RawHeaderPair>    rawHeaderPairs;

//    QCNetworkAccessManager  *manager;

//    CURL                    *curlHandle;

//    struct curl_slist       *curlHttpHeaders;

//    Q_DECLARE_PUBLIC(QCNetworkReply)

//protected:
//    QCNetworkReply *const q_ptr;
//};


//} //namespace QCurl
//#endif // QCNETWORKREPLY_P_H
