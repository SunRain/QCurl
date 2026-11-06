/**
 * @file
 * @brief 声明 owner-thread share/Cookie 行为模块。
 */
#ifndef QCCURLSHARECOOKIESTORE_P_H
#define QCCURLSHARECOOKIESTORE_P_H

#include "QCNetworkAccessManager.h"
#include "private/QCCurlShareCookieResult_p.h"

#include <QThread>

#include <memory>

namespace QCurl::Internal {

class QCCurlShareCookieStoreTestAccess;

/// 由 multi 托管；所有调用在 owner thread 串行执行，native 生命周期由显式 shutdown 结束。
class Q_DECL_HIDDEN QCCurlShareCookieStore final
{
public:
    /// 建立 owner thread 约束；运行时 lease 由调用方持有。
    explicit QCCurlShareCookieStore(QThread *ownerThread);
    /// 仅释放内存；原生资源必须已显式关闭或由整图隔离保活。
    ~QCCurlShareCookieStore();

    /// 首次登记逻辑身份返回 true，不创建 CURLSH，也不读取 manager。
    [[nodiscard]] bool openScope(const QCNetworkAccessManager *manager);
    /// 同步已有 context 的最新期望；尚未使用的 scope 不初始化原生资源。
    [[nodiscard]] ShareCallResult synchronizeScope(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config);
    /// 按当前期望配置绑定 HTTP easy；成功设置 SHARE 后才登记借用。
    [[nodiscard]] ShareCallResult bindHttp(const QCNetworkAccessManager *manager,
                                           const QCNetworkAccessManager::ShareHandleConfig &config,
                                           CURL *easy,
                                           bool hasCookiePersistence);
    /// 成功解绑后才提交用户退出；失败保留 binding、context 与回调 userdata。
    [[nodiscard]] ShareCallResult unbindHttp(CURL *easy, const char *operation);
    /// 移出 live identity，等原 context 的全部借用退出后清理。
    [[nodiscard]] ShareCallResult closeScope(const QCNetworkAccessManager *manager);

    /// 验证并事务性导入；返回事务结果及临时 easy 的独立退出结果。
    [[nodiscard]] CookieCommandResult importCookies(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config,
        const QList<QCCookie> &cookies,
        const QUrl &originUrl);
    /// 导出并按 URL 过滤共享 Cookie，空集合仍表示成功。
    [[nodiscard]] CookieCommandResult exportCookies(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config,
        const QUrl &filterUrl);
    /// 清空共享 Cookie 并显式 FLUSH；失败不会伪装为成功。
    [[nodiscard]] CookieCommandResult clearAllCookies(
        const QCNetworkAccessManager *manager,
        const QCNetworkAccessManager::ShareHandleConfig &config);

    /// 仅改变阶段；最后用户退出时不再推进 pending 或清理 share。
    void beginShutdown() noexcept;
    /// 在全部 easy 安全退出后关闭 live/retired scopes。
    [[nodiscard]] ShareCallResult shutdownScopes();
    /// 封闭所有原生入口，保留未被证明可释放的 ownership。
    void makeNonCallable() noexcept;

private:
    Q_DISABLE_COPY_MOVE(QCCurlShareCookieStore)
    friend class QCCurlShareCookieStoreTestAccess;
    class State;
    std::unique_ptr<State> m_state;
};

} // namespace QCurl::Internal

#endif // QCCURLSHARECOOKIESTORE_P_H
