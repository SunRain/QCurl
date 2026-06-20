#include "QCCurlHandleManager.h"

#include "private/CurlGlobalConstructor_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QDebug>

#include <utility> // for std::exchange

namespace QCurl {
namespace {

CURL *createEasyHandle()
{
    CurlGlobalConstructor::instance();

    CURL *handle = curl_easy_init();
    if (!handle) {
        return nullptr;
    }

    const CURLcode rc = Internal::CurlOptions::setEnabled(handle, CURLOPT_NOSIGNAL, true);
    if (rc != CURLE_OK) {
        qWarning() << "QCCurlHandleManager: failed to apply CURLOPT_NOSIGNAL:"
                   << curl_easy_strerror(rc);
        curl_easy_cleanup(handle);
        return nullptr;
    }

    return handle;
}

} // namespace

QCCurlHandleManager::QCCurlHandleManager()
    : m_curlHandle(createEasyHandle())
    , m_headerList(nullptr)
{
    // curl_easy_init() 可能返回 nullptr（内存不足等情况）
    // 调用者应该通过 isValid() 或检查 handle() != nullptr 来验证
}

QCCurlHandleManager::~QCCurlHandleManager()
{
    // 清理 header 列表
    if (m_headerList) {
        curl_slist_free_all(m_headerList);
        m_headerList = nullptr;
    }

    // 清理 curl 句柄
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
}

QCCurlHandleManager::QCCurlHandleManager(QCCurlHandleManager &&other) noexcept
    : m_curlHandle(std::exchange(other.m_curlHandle, nullptr))
    , m_headerList(std::exchange(other.m_headerList, nullptr))
{}

QCCurlHandleManager &QCCurlHandleManager::operator=(QCCurlHandleManager &&other) noexcept
{
    if (this != &other) {
        // 清理当前资源
        if (m_headerList) {
            curl_slist_free_all(m_headerList);
        }
        if (m_curlHandle) {
            curl_easy_cleanup(m_curlHandle);
        }

        // 转移所有权
        m_curlHandle = std::exchange(other.m_curlHandle, nullptr);
        m_headerList = std::exchange(other.m_headerList, nullptr);
    }
    return *this;
}

void QCCurlHandleManager::appendHeader(const QString &header)
{
    if (header.isEmpty()) {
        return;
    }

    // curl_slist_append 会复制字符串，所以临时的 QByteArray 生命周期没问题
    QByteArray headerBytes   = header.toUtf8();
    curl_slist *newHeaderList = curl_slist_append(m_headerList, headerBytes.constData());
    if (!newHeaderList) {
        qWarning() << "QCCurlHandleManager::appendHeader: curl_slist_append failed";
        return;
    }

    m_headerList = newHeaderList;
}

} // namespace QCurl
