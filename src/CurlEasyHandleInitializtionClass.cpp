#include "CurlEasyHandleInitializtionClass.h"

#include <QDebug>

#include "QCUtility.h"

#include "QCNetworkAccessManager.h"

namespace QCurl {

class WriteHandle
{
public:
    enum HandleType {
        TYPE_HEADER = 0,
        TYPE_BODY,
        TYPE_NONE
    };
    WriteHandle(CurlEasyHandleInitializtionClass *c, HandleType type) : m_class(c), m_type(type)
    {

    }
    ~WriteHandle() {}
    size_t writeFunc(char *data, size_t size, size_t nitems)
    {
        if (m_type == TYPE_HEADER) {
//            qDebug()<<Q_FUNC_INFO<<"----------------------------- TYPE_HEADER "<<size*nitems;
            return m_class->headerFunc(data, size, nitems);
//            return size * nitems;
//            if (!m_class->m_buffer.isEmpty()) {
//                //FIXME dirty hack
//                qWarning()<<Q_FUNC_INFO<<"Dirty hack to seprate body/header data incase of mixed boyd/header";
//                const QByteArray header(data, static_cast<int>(size * nitems));
//                const int pos = m_class->m_buffer.indexOf(header);
//                if (pos >= 0) {
//                    const int headerSize = static_cast<int>(size * nitems);
//                    m_class->m_buffer.remove(pos, headerSize);
//                }
//                if (!m_class->m_buffer.isEmpty()) {
//                    qWarning()<<Q_FUNC_INFO<<"remain buffer size "<<m_class->m_buffer.size()
//                             <<" data "<<m_class->m_buffer;
//                }
//            }
//            return m_class->headerFunc(data, size, nitems);
        } else if (m_type == TYPE_BODY) {
//            qDebug()<<Q_FUNC_INFO<<"................................... TYPE_BODY ";
            return m_class->writeFunc(data, size, nitems);
//            if (!m_class->m_buffer.isEmpty()) {
//                return m_class->writeFunc(m_class->m_buffer.data(), static_cast<size_t>(m_class->m_buffer.size()), 1);
//            }
//            //FIXME dirty hack
//            qWarning()<<Q_FUNC_INFO<<"Dirty hack to seprate body/header data incase of mixed boyd/header";
//            m_class->m_buffer.append(data, static_cast<int>(size * nitems));
//            qDebug()<<Q_FUNC_INFO<<"buffer size "<<m_class->m_buffer.size()<<" data "<<m_class->m_buffer;
//            return size * nitems;
        } else {
            qWarning()<<Q_FUNC_INFO<<"Inivalid handle type";
        }
        return 0;
    }
private:
    CurlEasyHandleInitializtionClass *m_class;
    HandleType m_type;
};


CurlEasyHandleInitializtionClass::CurlEasyHandleInitializtionClass(QObject *parent)
    : QObject(parent),
      manager(Q_NULLPTR),
      curlHandle(Q_NULLPTR),
      m_headerWriter(new WriteHandle(this, WriteHandle::HandleType::TYPE_HEADER)),
      m_bodyWriter(new WriteHandle(this, WriteHandle::HandleType::TYPE_BODY))
{

}

CurlEasyHandleInitializtionClass::~CurlEasyHandleInitializtionClass()
{
    qDebug()<<Q_FUNC_INFO<<"~~~~~~~~~~~~~~~~~~~~~~~~~";
    if (curlHandle) {
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
    }
    if (m_headerWriter) {
        delete  m_headerWriter;
        m_headerWriter = Q_NULLPTR;
    }
    if (m_bodyWriter) {
        delete  m_bodyWriter;
        m_bodyWriter = Q_NULLPTR;
    }
}

void CurlEasyHandleInitializtionClass::setReceivedContentType(CurlEasyHandleInitializtionClass::ReceivedContentType type)
{
    if (type == BodyOnly) {
//        set(curlHandle, CURLOPT_NOBODY, long(0));
//        set(curlHandle, CURLOPT_HEADER, long(0));
    } else if (type == HeaderOnly) {
        set(curlHandle, CURLOPT_NOBODY, long(1));
//        set(curlHandle, CURLOPT_HEADER, long(1));
    } else if (type == BodyAndHeader) {
        set(curlHandle, CURLOPT_NOBODY, long(0));
//        set(curlHandle, CURLOPT_HEADER, long(1));
    }
}

bool CurlEasyHandleInitializtionClass::createEasyHandle(QCNetworkAccessManager *mgr, const QCNetworkRequest &req)
{
    //TODO use share_handle for DNS_CACHE
    if (!mgr) {
        return false;
    }

    if (curlHandle) {
        qDebug()<<Q_FUNC_INFO<<"exist curl handle";
        return true;
    }

    manager = mgr;

    curlHandle = curl_easy_init();
    Q_ASSERT(curlHandle != Q_NULLPTR);
    qDebug()<<Q_FUNC_INFO<<" curlHandle "<<curlHandle
           <<" this addr "<<this;

    m_request = req;

    if (!set(curlHandle, CURLOPT_PRIVATE, this)
            || !set(curlHandle, CURLOPT_NOPROGRESS, long(0))
            || !set(curlHandle, CURLOPT_XFERINFODATA, this)) {
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
        return false;
    }

    //set timeout for multi-thread
    if (!set(curlHandle, CURLOPT_NOSIGNAL, long(1))) {
        //TODO
    }
    if (!set(curlHandle, CURLOPT_LOW_SPEED_LIMIT, 1)) {
        //TODO
    }
    if (!set(curlHandle, CURLOPT_LOW_SPEED_TIME, 10)) {
        //TODO
    }
    //end set timeout for multi-thread

    qDebug()<<Q_FUNC_INFO<<" url is "<<req.url();

    if (!set(curlHandle, CURLOPT_URL, req.url())) {
        qDebug()<<Q_FUNC_INFO<<" set url error ";
        curl_easy_cleanup(curlHandle);
        curlHandle = Q_NULLPTR;
        return false;
    }

    if (!mgr->cookieFilePath().isEmpty()) {
        qDebug()<<Q_FUNC_INFO<<" set cookie "<<mgr->cookieFilePath();

//        set(curlHandle, CURLOPT_COOKIEFILE, mgr->cookieFilePath());
//        set(curlHandle, CURLOPT_COOKIEJAR, mgr->cookieFilePath());
        QCNetworkAccessManager::CookieFileModeFlag flag = mgr->cookieFileMode();
        qDebug()<<Q_FUNC_INFO<<" CookieFileModeFlag "<<flag;
        if ((flag & QCNetworkAccessManager::ReadOnly) == QCNetworkAccessManager::ReadOnly) {
            qDebug()<<Q_FUNC_INFO<<" set cookie read";
            set(curlHandle, CURLOPT_COOKIEFILE, mgr->cookieFilePath());
        }
        if ((flag & QCNetworkAccessManager::WriteOnly) == QCNetworkAccessManager::WriteOnly) {
            qDebug()<<Q_FUNC_INFO<<" set cookie write";
            set(curlHandle, CURLOPT_COOKIEJAR, mgr->cookieFilePath());
        }
    }

    if (req.followLocation()) {
        set(curlHandle, CURLOPT_FOLLOWLOCATION, long(1));
    }

    // Do not return CURL_OK in case valid server responses reporting errors.
    set(curlHandle, CURLOPT_FAILONERROR, long(1));

    if (!set(curlHandle, CURLOPT_XFERINFOFUNCTION, staticXferinfoFunc)) {
        //TODO:
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//        return false;
    }

//    if (!set(curlHandle, CURLOPT_HEADER, long(0))) {

//    }

    if (!set(curlHandle, CURLOPT_HEADERDATA ,m_headerWriter)) {
        qDebug()<<Q_FUNC_INFO<<"set CURLOPT_HEADERDATA error ";
//        return false;
        //TODO:
    }

    if (!set(curlHandle, CURLOPT_HEADERFUNCTION, staticHeaderFunc)) {
        qDebug()<<Q_FUNC_INFO<<"set CURLOPT_HEADERFUNCTION error ";
        //TODO:
//        curl_easy_cleanup(curlHandle);
//        curlHandle = Q_NULLPTR;
//        return false;
    }
    if (!set(curlHandle, CURLOPT_WRITEDATA, m_bodyWriter)) {
        //TODO:
    }
//    qDebug()<<Q_FUNC_INFO<<"--------------------------- header "<<m_headerWriter
//           <<" body  "<<m_bodyWriter;

    //NOTE: header and body will use same write func
    if (!set(curlHandle, CURLOPT_WRITEFUNCTION, staticWriteFunc)) {
        //TODO:
    }

    if (!set(curlHandle, CURLOPT_READDATA, this)) {
        //TODO:
    }
    if (!set(curlHandle, CURLOPT_READFUNCTION, staticReadFunc)) {
        //TODO:
    }
    if (!set(curlHandle, CURLOPT_SEEKDATA, this)) {
        //TODO:
    }
    if (!set(curlHandle, CURLOPT_SEEKFUNCTION, staticSeekFunc)) {
        //TODO:
    }

    if (!set(curlHandle, CURLOPT_SSL_VERIFYHOST, long(0))) {

    }
    if (!set(curlHandle, CURLOPT_SSL_VERIFYPEER, long(0))) {

    }

    if (req.rangeStart() >= 0 && (req.rangeEnd() > req.rangeStart())) {
        QString range = QString("%1-%2").arg(req.rangeStart()).arg(req.rangeEnd());
        if (!set(curlHandle, CURLOPT_RANGE, range.toUtf8().data())) {

        }
    }

    this->setReceivedContentType(BodyAndHeader);

    // set header
    struct curl_slist *list = Q_NULLPTR;
    foreach(const QByteArray &data, m_request.rawHeaderList()) {
        list = curl_slist_append(list, data.constData());
    }
    set(curlHandle, CURLOPT_HEADER, list);

    return true;
}

void CurlEasyHandleInitializtionClass::xferinfoFunc(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{

}

size_t CurlEasyHandleInitializtionClass::headerFunc(char *data, size_t size, size_t nitems)
{
    return 0;//size * nitems;
}

size_t CurlEasyHandleInitializtionClass::writeFunc(char *data, size_t size, size_t nitems)
{
    return 0;//size * nitems;
}

size_t CurlEasyHandleInitializtionClass::readFunc(char *data, size_t size, size_t nitems)
{
    return 0;
}

int CurlEasyHandleInitializtionClass::seekFunc(curl_off_t offset, int origin)
{
    return 0;
}

QCNetworkRequest CurlEasyHandleInitializtionClass::request() const
{
    return m_request;
}

int CurlEasyHandleInitializtionClass::staticXferinfoFunc(void *reply, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
    CurlEasyHandleInitializtionClass *r = static_cast<CurlEasyHandleInitializtionClass*>(reply);
    Q_ASSERT(r != Q_NULLPTR);
    r->xferinfoFunc(dltotal, dlnow, ultotal, ulnow);
    return 0;
}

size_t CurlEasyHandleInitializtionClass::staticHeaderFunc(char *data, size_t size, size_t nitems, void *ptr)
{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
    WriteHandle *r = static_cast<WriteHandle*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->writeFunc(data, size, nitems);
}

size_t CurlEasyHandleInitializtionClass::staticWriteFunc(char *data, size_t size, size_t nitems, void *ptr)
{
//    qDebug()<<Q_FUNC_INFO<<"-----------------";
    WriteHandle *r = static_cast<WriteHandle*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
//    HeaderWriter *header = dynamic_cast<HeaderWriter*>(ptr);
////    if (header) {
////        qDebug()<<Q_FUNC_INFO<<"--------------------------- HeaderWriter";
////        header->writeFunc(data, size, nitems);
////    }
//    BodyWWriter *body = dynamic_cast<BodyWWriter*>(ptr);
//    qDebug()<<Q_FUNC_INFO<<"--------------------------- header "<<header
//           <<" body  "<<body;

//    if (header) {
//        qDebug()<<Q_FUNC_INFO<<"--------------------------- HeaderWriter";
//        return header->writeFunc(data, size, nitems);
//    }
//    if (body) {
//        qDebug()<<Q_FUNC_INFO<<"--------------------------- BodyWWriter";
//       return  body->writeFunc(data, size, nitems);
//    }

    return r->writeFunc(data, size, nitems);
}

size_t CurlEasyHandleInitializtionClass::staticReadFunc(char *data, size_t size, size_t nitems, void *ptr)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    CurlEasyHandleInitializtionClass *r = static_cast<CurlEasyHandleInitializtionClass*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->readFunc(data, size, nitems);
}

int CurlEasyHandleInitializtionClass::staticSeekFunc(void *ptr, curl_off_t offset, int origin)
{
    qDebug()<<Q_FUNC_INFO<<"-----------------";
    CurlEasyHandleInitializtionClass *r = static_cast<CurlEasyHandleInitializtionClass*>(ptr);
    Q_ASSERT(r != Q_NULLPTR);
    return r->seekFunc(offset, origin);
}


}

