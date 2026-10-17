#ifndef QCURL_NATIVE_MULTI_PROBE_H
#define QCURL_NATIVE_MULTI_PROBE_H

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QStringList>
#include <QUrl>

#include <curl/curl.h>
#include <memory>

struct NativeProbeOptions
{
    QStringList resolve;
    QStringList connectTo;
    QByteArray caFile;
    QByteArray proxy;
    long ipResolve       = CURL_IPRESOLVE_WHATEVER;
    bool freshConnection = false;
};

struct NativeProbeResult
{
    CURLcode code       = CURLE_FAILED_INIT;
    long status         = 0;
    long newConnections = 0;
    QByteArray body;
};

// 与 QCurl 链接同一个 DSO；保留真实 multi/share，不能用独立 CLI 冒充暖缓存基线。
class NativeMultiProbe final
{
public:
    explicit NativeMultiProbe(bool cookies = false, bool ssl = false, bool useMulti = true)
        : m_globalReady(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
        , m_multi(useMulti && m_globalReady ? curl_multi_init() : nullptr, &cleanupMulti)
        , m_share((cookies || ssl) && m_globalReady ? curl_share_init() : nullptr, &cleanupShare)
        , m_cookies(cookies)
    {
        m_ready = m_globalReady && (!useMulti || m_multi) && (!(cookies || ssl) || m_share);
        if (m_ready && cookies) {
            m_ready = curl_share_setopt(m_share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE)
                      == CURLSHE_OK;
        }
        if (m_ready && ssl) {
            m_ready = curl_share_setopt(m_share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION)
                      == CURLSHE_OK;
        }
    }

    ~NativeMultiProbe()
    {
        m_multi.reset();
        m_share.reset();
        if (m_globalReady) {
            curl_global_cleanup();
        }
    }

    /** 替换 multi 并保留同一 manager share，用于跨域缓存对照。 */
    [[nodiscard]] bool replaceMulti()
    {
        m_multi.reset();
        m_multi.reset(curl_multi_init());
        return bool(m_multi);
    }

    /** 执行一次请求；保留本对象的缓存，列表与 easy 活到确认 detach 后才释放。 */
    [[nodiscard]] NativeProbeResult get(const QUrl &url, const NativeProbeOptions &options = {})
    {
        NativeProbeResult result;
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolve(makeList(
                                                                                options.resolve),
                                                                            &curl_slist_free_all);
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> connectTo(makeList(
                                                                                  options.connectTo),
                                                                              &curl_slist_free_all);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(),
                                                                 &curl_easy_cleanup);
        if (!m_ready || !easy || (!options.resolve.isEmpty() && !resolve)
            || (!options.connectTo.isEmpty() && !connectTo)
            || !configure(easy.get(), url, options, &result, resolve.get(), connectTo.get())) {
            return result;
        }
        if (m_multi) {
            result.code = performMulti(easy.get());
        } else {
            result.code = curl_easy_perform(easy.get());
        }
        if (curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &result.status) != CURLE_OK
            || curl_easy_getinfo(easy.get(), CURLINFO_NUM_CONNECTS, &result.newConnections)
                   != CURLE_OK) {
            result.code = CURLE_FAILED_INIT;
        }
        return result;
    }

private:
    Q_DISABLE_COPY_MOVE(NativeMultiProbe)

    static void cleanupMulti(CURLM *multi)
    {
        if (curl_multi_cleanup(multi) != CURLM_OK) {
            qFatal("原生对照 multi 清理失败");
        }
    }

    static void cleanupShare(CURLSH *share)
    {
        if (curl_share_cleanup(share) != CURLSHE_OK) {
            qFatal("原生对照 share 清理失败");
        }
    }

    static curl_slist *makeList(const QStringList &entries)
    {
        curl_slist *list = nullptr;
        for (const auto &entry : entries) {
            auto *next = curl_slist_append(list, entry.toUtf8().constData());
            if (!next) {
                curl_slist_free_all(list);
                return nullptr;
            }
            list = next;
        }
        return list;
    }

    static size_t writeBody(char *data, size_t size, size_t count, void *context)
    {
        const auto bytes = size * count;
        static_cast<NativeProbeResult *>(context)->body.append(data, static_cast<qsizetype>(bytes));
        return bytes;
    }

    bool configure(CURL *easy,
                   const QUrl &url,
                   const NativeProbeOptions &options,
                   NativeProbeResult *result,
                   curl_slist *resolve,
                   curl_slist *connectTo)
    {
        return curl_easy_setopt(easy, CURLOPT_URL, url.toEncoded().constData()) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_PROXY, options.proxy.constData()) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_NOPROXY, "") == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 2500L) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_IPRESOLVE, options.ipResolve) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_FRESH_CONNECT, options.freshConnection ? 1L : 0L)
                      == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_RESOLVE, resolve) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_CONNECT_TO, connectTo) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeBody) == CURLE_OK
               && curl_easy_setopt(easy, CURLOPT_WRITEDATA, result) == CURLE_OK
               && (!m_share || curl_easy_setopt(easy, CURLOPT_SHARE, m_share.get()) == CURLE_OK)
               && (!m_cookies || curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "") == CURLE_OK)
               && (options.caFile.isEmpty()
                   || curl_easy_setopt(easy, CURLOPT_CAINFO, options.caFile.constData())
                          == CURLE_OK);
    }

    CURLcode performMulti(CURL *easy)
    {
        if (curl_multi_add_handle(m_multi.get(), easy) != CURLM_OK) {
            return CURLE_FAILED_INIT;
        }
        const auto detach = qScopeGuard([this, easy]() {
            if (curl_multi_remove_handle(m_multi.get(), easy) != CURLM_OK) {
                qFatal("原生对照 easy 脱离 multi 失败");
            }
        });
        QElapsedTimer timer;
        timer.start();
        int running = 0;
        do {
            if (curl_multi_perform(m_multi.get(), &running) != CURLM_OK) {
                return CURLE_FAILED_INIT;
            }
            // 本机 Qt 服务与原生基线同线程；只驱动事件，不改变基线缓存/连接范围。
            QCoreApplication::processEvents();
            if (curl_multi_poll(m_multi.get(), nullptr, 0, 1, nullptr) != CURLM_OK) {
                return CURLE_FAILED_INIT;
            }
        } while (running && timer.elapsed() < 3000);
        int remaining = 0;
        while (const auto *message = curl_multi_info_read(m_multi.get(), &remaining)) {
            if (message->msg == CURLMSG_DONE && message->easy_handle == easy) {
                return message->data.result;
            }
        }
        return CURLE_OPERATION_TIMEDOUT;
    }

    bool m_globalReady = false;
    std::unique_ptr<CURLM, decltype(&cleanupMulti)> m_multi;
    std::unique_ptr<CURLSH, decltype(&cleanupShare)> m_share;
    bool m_cookies = false;
    bool m_ready   = false;
};

#endif // QCURL_NATIVE_MULTI_PROBE_H
