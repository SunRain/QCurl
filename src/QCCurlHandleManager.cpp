#include "QCCurlHandleManager.h"
#include <utility> // for std::exchange

QT_BEGIN_NAMESPACE

namespace QCurl {

QCCurlHandleManager::QCCurlHandleManager()
    : m_curlHandle(curl_easy_init())
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
{
}

QCCurlHandleManager& QCCurlHandleManager::operator=(QCCurlHandleManager &&other) noexcept
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
    QByteArray headerBytes = header.toUtf8();
    m_headerList = curl_slist_append(m_headerList, headerBytes.constData());
}

} // namespace QCurl
QT_END_NAMESPACE
