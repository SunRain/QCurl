/**
 * @file
 * @brief 声明 curl multi manager 的测试专用访问面。
 */

#ifndef QCCURLMULTIMANAGERTESTACCESS_P_H
#define QCCURLMULTIMANAGERTESTACCESS_P_H

#include "QCCurlMultiManager.h"

#ifdef QCURL_ENABLE_TEST_HOOKS

namespace QCurl {

/**
 * @brief 集中提供确定性故障注入所需的 manager 私有访问能力。
 *
 * 该类型只在 test-internals target 中生成，不属于正式库 ABI。
 */
class QCCurlMultiManagerTestAccess final
{
public:
    /**
     * @brief 为 share detach 故障注入绑定临时 share handle。
     *
     * @param manager 执行故障注入的 manager
     * @param token 已注册的测试传输 token
     * @param error 失败时写入诊断信息
     * @return 绑定成功返回 true；测试前置不成立返回 false
     */
    [[nodiscard]] static bool prepareShareDetach(QCCurlMultiManager *manager,
                                                 QCCurlMultiManager::TransferToken token,
                                                 QString *error);

    /**
     * @brief 触发 cookie engine setup 失败后的 share rollback 故障注入。
     *
     * @param manager 执行故障注入的 manager
     * @param token 已注册的测试传输 token
     * @param error 失败时写入诊断信息
     * @return 成功执行预期故障路径返回 true；测试前置不成立返回 false
     */
    [[nodiscard]] static bool prepareShareRollback(QCCurlMultiManager *manager,
                                                   QCCurlMultiManager::TransferToken token,
                                                   QString *error);

    /**
     * @param manager 待查询的 manager
     * @return 当前 easy handle 到 share context 的绑定数量
     */
    [[nodiscard]] static int activeShareBindings(QCCurlMultiManager *manager);

    /**
     * @param manager 待查询的 manager
     * @return 当前 manager 保留的 share context 数量
     */
    [[nodiscard]] static int activeShareContexts(QCCurlMultiManager *manager);

    /**
     * @param manager 待查询的 manager
     * @return 仍持有 share handle 的 context 数量
     */
    [[nodiscard]] static int activeShareContextsWithHandle(QCCurlMultiManager *manager);

    /**
     * @param manager 待查询的 manager
     * @return 全部 share context 的 activeUsers 总数
     */
    [[nodiscard]] static int activeShareUsers(QCCurlMultiManager *manager);

private:
    QCCurlMultiManagerTestAccess() = delete;
};

} // namespace QCurl

#endif // QCURL_ENABLE_TEST_HOOKS

#endif // QCCURLMULTIMANAGERTESTACCESS_P_H
