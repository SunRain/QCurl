/**
 * @file
 * @brief 声明应用级 libcurl 进程运行时控制器。
 */

#ifndef QCURLRUNTIME_H
#define QCURLRUNTIME_H

#include "QCGlobal.h"

#include <QDeadlineTimer>
#include <QScopedPointer>
#include <QString>

namespace QCurl {

class QCurlRuntimePrivate;

/// 表示 QCurl 进程运行时的单向生命周期状态。
enum class QCurlRuntimeState {
    Uninitialized,
    Running,
    Stopping,
    Stopped,
    Failed,
};

/// 表示 `beginShutdown()` 的结构化启动结果。
enum class QCurlShutdownStartResult {
    Started,
    AlreadyStopping,
    AlreadyStopped,
    WrongThread,
    ExternalUsersUnverified,
    InternalFailure,
};

/// 表示 `shutdownAndWait()` 的结构化关闭结果。
enum class QCurlShutdownResult {
    Succeeded,
    AlreadyStopped,
    TimedOut,
    WrongThread,
    Poison,
    ExternalUsersUnverified,
    InternalFailure,
};

/**
 * @brief 控制进程唯一的 QCurl/libcurl 手动关闭协议。
 *
 * 构造对象不会触发全局 cleanup。应用必须在 owner thread 的事件循环仍可处理
 * teardown 时调用 `beginShutdown()`，再从不会阻塞这些 owner event loop 的协调线程
 * 调用 `shutdownAndWait()`。成功关闭只终止 libcurl 运行时，不表示 QCurl 动态库可卸载。
 *
 * @note 错误生命周期：每次关闭命令返回独立的枚举结果；成功结果没有诊断文本，失败或
 * 超时原因以返回枚举为准。`diagnostic()` 只描述当前 runtime 初始化或关闭失败，不替代
 * 单次命令结果；runtime 终态由 `state()` 查询。
 */
class QCURL_EXPORT QCurlRuntime final
{
public:
    /// 注册当前线程持有的进程 runtime controller；同一时刻仅一个对象可成为 owner。
    QCurlRuntime();

    /// 注销 controller；析构不会等待，也不会隐式调用 `curl_global_cleanup()`。
    ~QCurlRuntime();

    Q_DISABLE_COPY_MOVE(QCurlRuntime)

    /// 返回当前对象是否持有进程唯一 controller 身份。
    [[nodiscard]] bool isProcessOwner() const noexcept;

    /// 返回当前进程 runtime 状态。
    [[nodiscard]] QCurlRuntimeState state() const noexcept;

    /// 返回最近一次初始化或关闭失败诊断；无诊断时为空。
    [[nodiscard]] QString diagnostic() const;

    /**
     * @brief 确认全部外部 libcurl 使用者已经停止。
     * @return 仅 controller owner 在其构造线程、且尚未开始关闭时返回 `true`
     *
     * 该确认不为外部组件创建 lease；应用仍负责在调用前停止其线程、handle 和 callback。
     * 确认只属于当前 controller，controller 注销后必须由替代 controller 重新确认。
     */
    [[nodiscard]] bool confirmExternalUsersStopped();

    /**
     * @brief 停止 admission，并异步投递受管 owner-thread teardown。
     * @return 关闭启动结果；未确认外部使用者时 fail-closed 进入 `Failed`
     */
    [[nodiscard]] QCurlShutdownStartResult beginShutdown();

    /**
     * @brief 等待受管 lease 清零并至多执行一次 `curl_global_cleanup()`。
     * @param deadline 等待截止时间；超时保持 `Stopping`，允许协调线程再次等待
     * @return 结构化关闭结果
     *
     * 该函数不得在 controller owner thread 调用，避免阻塞仍需处理 teardown 的事件循环。
     */
    [[nodiscard]] QCurlShutdownResult shutdownAndWait(QDeadlineTimer deadline);

private:
    QScopedPointer<QCurlRuntimePrivate> d_ptr;
};

} // namespace QCurl

#endif // QCURLRUNTIME_H
