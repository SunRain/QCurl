#include "CurlGlobalConstructor.h"

#include <QDebug>

#include <curl/curl.h>

#include "SingletonPointer_p.h"

using namespace QCurl;

CurlGlobalConstructor::CurlGlobalConstructor(QObject *parent)
    : QObject(parent)
{
    CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
    if (code != CURLE_OK) {
        qWarning()<<Q_FUNC_INFO<<"curl_global_init error ["<<code<<"]";
    }
}

CurlGlobalConstructor::~CurlGlobalConstructor()
{
    curl_global_cleanup();
}

CurlGlobalConstructor *CurlGlobalConstructor::instance()
{
    return Singleton<CurlGlobalConstructor>::instance(CurlGlobalConstructor::createInstance);
}

CurlGlobalConstructor *CurlGlobalConstructor::createInstance()
{
    return new CurlGlobalConstructor();
}
