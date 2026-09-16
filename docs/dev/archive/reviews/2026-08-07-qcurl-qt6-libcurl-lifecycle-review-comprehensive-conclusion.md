# QCurl Qt6/libcurl 生命周期完整审核结论

> 历史记录：以下审查/执行结论仅适用于正文注明的候选与时点；不是当前工程状态、发布资格或远端 CI 证明。原始结论保留，统一从[历史索引](../README.md)查阅。

> 审查日期：2026-08-07
>
> 重点审查分支：`master-tmp`
>
> 重点审查提交：`a1bafb7ba89491a016645c99d90572afeb877580`
>
> 对比范围：`0960482eedbb968f5c1acfdccaabfccf060ab33e..a1bafb7ba89491a016645c99d90572afeb877580`
>
> 单一路线修订日期：2026-08-10
>
> 文档性质：Qt/std 智能指针、QObject 生命周期、Qt 线程与锁、DeferredDelete、libcurl easy/multi/share 绑定及进程级全局运行时的只读审核结论
>
> 验证边界：结论以指定 Git 快照和逐代码读取为依据；本轮未运行构建、QtTest、集成测试或 sanitizer，不构成修复完成证明或发布签署

## 1. 总裁决

本次审核的重点对象必须是 `master-tmp@a1bafb7`，不能继续把历史提交
`0d1e57dcd68edcca9c7dcac28841571a8a6e00ad` 作为主审核对象，也不能用
`a1bafb7` 中出现的局部 helper 反向证明生命周期问题已经闭合。

针对 `a1bafb7` 的最终分级为：

| 严重度 | 数量 | 裁决 |
| --- | ---: | --- |
| P0 | 0 | 当前证据未证明默认路径存在确定的立即内存破坏 |
| P1 | 7 | 生命周期、同步重入、failure path 或进程级运行时合同缺陷，必须整改 |
| P2 | 1 | 当前调用不变量下不可达的危险死分支，应删除并固化 owner-thread invariant |

本次确认的 7 项 P1 是：

1. CURLSH rollback poison 后，当前 add-reply 流程仍继续把 easy 加入 multi。
2. Scheduler 在同步信号返回后继续访问可能已经被 slot 删除的 sender/reply。
3. WebSocket、Scheduler 和 CancelToken 在析构期间执行会发业务信号的运行期清理。
4. `CurlGlobalConstructor` 的函数静态析构自动调用 `curl_global_cleanup()`，违反已确认的进程级生命周期合同。
5. Logger 在普通 mutex 内执行文件 I/O、Qt 日志和用户 callback。
6. Multipart 在验证当前执行线程前调用 source `QIODevice`，并可能在错误线程调用 `setParent()`。
7. 多个 `thread_local` manager 可无锁并发写入进程级 poison quarantine 容器。

因此，当前生命周期审核结论是：**不通过**。代码不能被描述为已经闭合 Qt
线程、所有权、DeferredDelete、CURLSH failure path 和进程级 libcurl 生命周期问题。

### 1.1 隐含路线分叉复核后的最终选择

| 分叉点 | 唯一选择 | 明确排除 |
| --- | --- | --- |
| 外部 libcurl 使用者 | 允许在应用正常运行期存在；应用必须在 `beginShutdown()` 前停止全部外部使用者、线程、callback 和 handle | QCurl 不提供 external lease API，不把外部组件纳入内部 lease |
| 关闭入口与状态 | 只提供 `beginShutdown()` 和 `shutdownAndWait(QDeadlineTimer)`；采用 `Uninitialized -> Running -> Stopping -> Stopped/Failed` 单向状态机 | 不提供别名入口，不使用嵌套事件循环、`processEvents()` 循环或 blocking queued cleanup |
| 指针与 QObject 所有权 | PIMPL 用 `QScopedPointer`；C 资源用 `std::unique_ptr` 与具名 deleter；event QObject 用 owner-thread parent；观察者用 `QPointer`；既有共享状态保持其已证明的 shared ownership | 不把 `unique_ptr<QObject>` 作为 QCurl event object 的实施路线，不做全仓指针替换 |
| transfer detach/rollback 失败 | 立即 poison、停止 admission，并把 ownership 不确定的完整对象图移入 `NonCallable` quarantine | 不强制 cleanup，不释放或继续驱动 ownership 不确定的资源 |
| 未证实问题分类 | 统一记为当前非 finding 与测试缺口；新证据出现后重新打开为新的 finding | 不再同时标记为“待证实风险”，不计入当前 finding 数量 |

## 2. 审查目标、范围与证据规则

### 2.1 审查目标

本文件回答以下问题：

- Qt/std 智能指针分别能表达什么，不能表达什么。
- QObject parent-child、thread affinity 与 DeferredDelete 如何共同决定析构正确性。
- mutex 能保护哪些普通状态，为什么不能替代 QObject affinity 或 libcurl handle owner。
- QCurl 如何绑定 libcurl easy、multi、share、callback 与 Qt event-driven 对象。
- 多个 QCurl 实例并存时，析构单个实例与进程级 libcurl cleanup 的边界是什么。
- QCurl 如何提供可验证的手动 libcurl 关闭，并与不受支持的 QCurl 动态卸载严格分离。

### 2.2 覆盖范围

逐代码复核覆盖：

- `CurlGlobalConstructor`
- `QCCurlMultiManager` 及 share/event-driver/failure-path helper
- `QCNetworkRequestScheduler`
- `QCWebSocket`、`QCWebSocketPrivate`、WebSocket pool
- `QCNetworkCancelToken`
- `QCNetworkDefaultLogger`
- `QCNetworkMultipartBody`
- reply pause/resume flow-control
- 当前代码中的 `std::unique_ptr`、`QSharedPointer`、`QPointer`、`QMutex`、
  `QRecursiveMutex` 和 `BlockingQueuedConnection` 相关路径

本文件不替代 HTTP 可观察一致性、ABI、安装包、性能、安全或完整发布审查。

### 2.3 证据规则

结论按以下顺序取信：

1. `a1bafb7:<path>:<line>` 的实际代码。
2. 从 public API 到 private helper 的可达调用路径。
3. Qt6 QObject/thread/event-loop 稳定规则。
4. libcurl easy/multi/share 的返回码与 ownership 规则。
5. 已明确确认的 QCurl 进程级生命周期合同。

代码图谱对 `0960482..a1bafb7` 的历史 delta 返回空。该结果只能说明图谱没有提供
该差异的符号索引，不能解释为提交没有修改。最终结论以 Git 快照、源码行号和调用路径为准。

## 3. Qt/std 智能指针、QObject 与锁模型的适用边界

### 3.1 必须独立回答的六个问题

| 维度 | 必须回答的问题 | 不能由什么替代 |
| --- | --- | --- |
| 所有权 | 谁负责最终释放资源 | `QPointer`、裸指针、mutex |
| 观察关系 | 被观察对象销毁后如何停止访问 | shared ownership、thread affinity |
| 线程归属 | 哪个线程允许操作和销毁对象 | 引用计数、mutex |
| 异步删除 | 哪个事件循环消费 DeferredDelete | `deleteLater()` 调用本身 |
| 数据同步 | 哪些线程可以并发访问哪份普通数据 | QObject parent、智能指针 |
| 外部状态机 | libcurl 操作成功后何时提交内部账本 | Qt 容器状态、QCurl 自己的计数 |

正确审核顺序是：

```text
资源图 -> owner thread -> shutdown protocol -> observer -> pointer type -> lock
```

不能从 `unique_ptr`、`QSharedPointer` 或 `QMutex` 的拼写倒推实现是否正确。

### 3.2 智能指针适用矩阵

| 类型 | 表达的语义 | 最终释放位置 | 适用范围 | QObject 边界 |
| --- | --- | --- | --- | --- |
| `std::unique_ptr<T>` | 单一、可移动所有权 | 析构或 `reset()` 所在线程 | 纯 C++ 对象、C handle RAII、明确单 owner 的资源 | 一般情况下可以拥有无 parent 的 QObject；QCurl 的 event-driven QObject 不采用该路线 |
| `QScopedPointer<T>` | 词法作用域单一所有权 | wrapper 析构线程 | QCurl public class 的 PIMPL、局部非 QObject 资源 | 不提供 affinity；不能与 QObject parent 同时拥有同一对象 |
| `std::shared_ptr<T>` | shared control block 所有权 | 最后一个 owning reference 释放线程 | QCurl 私有 `QPromise` operation state 等真实多 owner 的纯 C++ 状态 | 控制块不把 QObject 析构封送到 affinity thread |
| `QSharedPointer<T>` | Qt shared ownership | 最后一个 owning reference 释放线程 | `QCCurlMultiTransferRecord` 等现有 Qt 共享状态 | 原子引用计数不保护 pointee，也不固定 QObject 析构线程 |
| `std::weak_ptr<T>` / `QWeakPointer<T>` | 对 shared control block 的弱观察 | 不负责释放 | 与对应 shared ownership 配套 | 只有成功提升为强引用后才临时延寿 |
| `QPointer<T>` | QObject guarded observer | 不负责释放 | 观察由 owner-thread QObject parent 持有的 event object | 不延寿、不加锁、不封送线程调用，也不识别派生类析构已经开始 |

QCurl 的实施职责固定如下，不做全仓指针类型替换：

- public class PIMPL 保持 `QScopedPointer`，现状见 `src/QCNetworkAccessManager.h:367` 和
  `src/QCNetworkReply.h:353`。
- `CURL*`、`curl_slist` 等纯 C 资源使用 `std::unique_ptr` 与具名 deleter；现有
  `CurlSlistOwner` 见 `src/private/QCCurlOptionAdapter_p.h:38`。
- timer、socket notifier、reply 和运行期 wrapper 必须由 owner-thread QObject parent 持有。
- QObject 的外部非 owning 观察统一使用 `QPointer`。
- `QCCurlMultiTransferRecord` 等现有 Qt 共享状态保持 `QSharedPointer`，见
  `src/QCCurlMultiManager.h:268`。
- 私有 `QPromise` operation state 保持现有 `std::shared_ptr`，见
  `src/private/QCWebSocketPoolPrivate_p.h:52`。

### 3.3 `unique_ptr<QObject>` 的一般理论边界

以下条件全部成立时，`std::unique_ptr<QObject>` 或 `QScopedPointer<QObject>` 可以正确：

- QObject 没有 parent，智能指针是唯一 owner。
- wrapper 不会被移动到无法证明的析构线程。
- `reset()`、异常回滚和 wrapper 析构发生在 QObject affinity thread。
- 析构前已经停止 timer、socket notifier、queued work 和外部 callback。

真正错误的是：

- parent ownership 与 owning smart pointer 重叠。
- movable value object 携带 affinity QObject 到任意析构线程。
- custom deleter 只调用 `deleteLater()`，却未证明目标事件循环仍会消费事件。
- 用 mutex 保护 QObject 指针后直接跨线程调用对象方法。

这些条件只说明 C++/Qt 一般情况下何时可以安全使用 `unique_ptr<QObject>`，不构成 QCurl
的并行实施路线。QCurl 的 timer、socket notifier、reply 和运行期 wrapper 已统一选择
owner-thread QObject parent-child；`unique_ptr` 只承担非 QObject 状态和纯 C 资源所有权。

### 3.4 shared ownership 不提供线程正确性

`std::shared_ptr` 和 `QSharedPointer` 的原子引用计数只保护 control block。它们不保证：

- pointee 成员读写线程安全。
- 同一个 shared-pointer 变量可无锁并发修改。
- 最后一个引用在哪个线程释放。
- QObject parent、affinity 和 event loop 仍有效。
- 同一个 `CURL*` 没有被其他线程同时使用。

因此，共享指针适合真实共享的纯 C++ 状态，不适合回避资源 owner 决策。

### 3.5 `QPointer` 只回答“QObject 是否已经析构”

`QPointer` 不提供所有权、延寿、同步、取消、join 或 shutdown。它在
`QObject::~QObject()` 阶段清空，不能识别派生类析构函数已经开始执行。因此：

- 运行期同步 signal 后，可用 `QPointer` 判断对象是否被 slot 完整删除。
- 析构函数内部不能用 `QPointer(this)` 证明派生对象仍可安全重入。
- `QPointer<QCNetworkReply>` 只适合作为观察者；transfer 的真实 owner 仍应是
  manager-owned record/token 和明确 teardown 状态机。

### 3.6 QObject parent-child 是事件对象的首选所有权模型

QObject parent 析构会同步删除 child。该模型仍有以下边界：

- parent 与 child 必须具有兼容的 thread affinity。
- `setParent()` 不是跨线程迁移工具。
- parent ownership 不授权其他线程直接调用 child 方法。
- parent 同步删除不依赖 child 的 DeferredDelete 被事件循环消费。
- 对象成员指针不会自动成为 child；QCurl event object 必须显式设置 owner-thread parent，
  其他 owning pointer 只用于非 QObject 状态。

QCurl 中 reply、timer、socket notifier 和流式 wrapper 必须挂到固定 owner-thread
actor 的 QObject 树。跨线程只传不可变值、stable token、错误和完成结果。

### 3.7 DeferredDelete 的准确边界

`QObject::deleteLater()` 投递 `QEvent::DeferredDelete`，不是立即析构，也不是无条件最终析构：

- 目标线程事件循环运行时，对象在合适的事件循环层级返回后删除。
- 事件循环启动前投递，通常会在循环启动后处理。
- 主事件循环已经停止后调用，Qt 不再替调用方完成删除。
- 对象线程没有运行事件循环时，删除依赖线程结束路径。
- 跨线程调用 `deleteLater()` 只解决删除事件投递，不授权其他跨线程方法调用。

因此，运行期可以使用 DeferredDelete；析构期、线程退出期和进程关闭期必须有同步、
幂等、不依赖未来事件的 teardown。parent-owned child 上的 `deleteLater()` 可能只是冗余，
没有具体证据时不能直接判定为双删或泄漏。

### 3.8 锁模型的准确边界

`QMutex`、`std::mutex` 和 `QRecursiveMutex` 只保护指定普通数据。它们不会：

- 改变 QObject affinity。
- 把 QObject 方法变成跨线程安全 API。
- 固定 shared pointer 的最后释放线程。
- 允许多个线程同时驱动同一个 easy/multi handle。

状态锁内可以执行短小、可预测的容器和字段事务。状态锁内不应执行：

- 用户 callback、虚函数或未知外部代码。
- 公开 Qt signal；AutoConnection 在同线程会同步执行 slot。
- Qt 日志；自定义 message handler 可以同步回流。
- `BlockingQueuedConnection`。
- 不受控长时 I/O，除非这是已证明不可拆分的文件事务。

`QRecursiveMutex` 也不是自动错误。libcurl 的 multi/share 回调可在驱动调用栈中同步
回流；若 manager 合同允许同线程内部重入，recursive mutex 有现实依据。正确动作是固定
允许的重入边，不能机械替换为普通 mutex。

### 3.9 libcurl handle 与 Qt actor 的边界

`CURL*`、`CURLM*`、`CURLSH*` 不是 QObject，没有 Qt affinity，但这不等于可以并发使用：

- 同一个 easy 或 multi handle 不得被多个线程同时驱动。
- share data 必须按 libcurl lock/unlock callback 合同同步。
- easy/share 的绑定和解绑只能按 libcurl 成功返回提交内部状态。
- callback userdata 的寿命必须覆盖 libcurl 可能回调的完整时间窗。
- libcurl callback 进入 Qt 层后仍须遵守 owner-thread 和锁外用户代码规则。

QCurl 的最佳实践是把 manager、multi handle、easy record、socket notifier 和 timer 固定
到一个 owner-thread actor。跨线程 API 只排队 value/token command，不传裸 `CURL*`、
`reply->d_func()`、栈引用或 owning QObject pointer。

## 4. QCurl 进程级生命周期合同

### 4.1 多个 QCurl 实例实际共享什么

多个 QCurl-backed manager/reply 在同一进程中共享 libcurl 的进程级全局运行时，但它们：

- 不共享同一个 easy handle。
- 不必共享同一个 share handle。
- 不共享同一个 multi manager；`QCCurlMultiManager::instance()` 是 `thread_local`。
- 每个实例仍须独立清理自己的 reply、easy、transfer record 和 QObject child。

### 4.2 受支持与不受支持场景

| 场景 | 合同裁决 |
| --- | --- |
| 创建多个 QCurl 实例并执行网络操作 | 支持 |
| 只析构其中一个 QCurl 实例 | 支持；只清理该实例资源，不执行全局 cleanup |
| 其他 QCurl 实例继续工作 | 必须支持 |
| 外部组件仍使用 libcurl 时析构单个 QCurl 实例 | 支持；不得触发全局 cleanup |
| 活动 QCurl/libcurl 对象、线程、handle 或 callback 存在时卸载 QCurl | 不支持 |
| 其他线程或组件仍使用 libcurl 时执行 QCurl 静态全局析构 | 不支持 |
| 应用调用进程唯一 runtime 的 `shutdownAndWait()` | 支持；仅在第 4.4 节全部关闭条件满足时执行 cleanup |
| `shutdownAndWait()` 失败、超时或仍有未知使用者 | 不执行 cleanup，QCurl 保持加载 |
| QCurl 自动调用 `curl_global_cleanup()` | 不支持；普通实例析构、静态析构和 `atexit()` 均无此权限 |
| `shutdownAndWait()` 成功后继续创建 QCurl 工作 | 不支持；runtime 已进入终态 |
| `shutdownAndWait()` 成功后动态卸载 QCurl | 不支持；手动 libcurl 关闭不等于动态库可卸载 |

核心等式是：

```text
析构一个 QCurl 实例
!= 卸载 QCurl 动态库
!= curl_global_cleanup()
```

### 4.3 正式合同文本

> QCurl 支持独立创建和销毁 QCurl 实例。实例销毁只终止并清理该实例拥有的操作，
> 不关闭进程级 libcurl 运行时。应用可以通过进程唯一的 `QCurlRuntime` 手动关闭
> libcurl；只有 `shutdownAndWait()` 能够在全部受管使用者、线程、handle 和 callback
> 已停止，且应用确认不存在外部 libcurl 使用者时调用一次 `curl_global_cleanup()`。
> 任一条件无法证明时不执行 cleanup。关闭成功后 QCurl runtime 进入终态，但 QCurl
> 动态库仍保持加载到进程结束。

QCurl 内部实例计数无法证明整个进程已经没有 libcurl 使用者，因为外部组件可能绕过
QCurl 直接使用 libcurl。因此，组件局部的最后一个实例不能获得全局 cleanup 权限，
QCurl 也不提供 external lease API。应用运行期间可以有其他 libcurl 组件；但在调用
`beginShutdown()` 前，应用必须先停止全部外部 libcurl 使用者、线程、callback 和 handle。
应用无法确认这一条件时，关闭必须失败且不得执行 cleanup。

### 4.4 手动 libcurl 关闭合同

手动 cleanup 必须由应用级、进程唯一且不可复制移动的 `QCurlRuntime` 提供。该 runtime
拥有 QCurl 的 `curl_global_init()` 配对责任。状态机固定如下；`Stopped` 与 `Failed` 均为
不可重新初始化的终态：

```text
Uninitialized -> Running -> Stopping -> Stopped
                               |
                               +-> Failed
```

runtime 同时维护：

- 覆盖 manager、reply、WebSocket、transfer、handle、callback 和工作线程的内部 lease。
- 应用对全部外部 libcurl 使用者已经停止的显式确认；外部使用者不进入内部 lease。
- 拒绝新工作的 admission gate、活动计数和有截止时间的等待协议。
- 成功、已停止、超时、错误线程、poison、外部使用者未确认和内部失败的结构化结果。

公开关闭入口固定为：

```cpp
[[nodiscard]] QCurlShutdownStartResult beginShutdown();

[[nodiscard]] QCurlShutdownResult
shutdownAndWait(QDeadlineTimer deadline);
```

调用顺序也固定：应用先在 owner event loop 仍运行时调用 `beginShutdown()`，停止 admission
并投递各 owner-thread teardown；随后只在不会阻塞这些 event loop 的协调线程调用
`shutdownAndWait()`。不得保留名称不同但语义重叠的第二套异步入口。

`shutdownAndWait()` 只有在内部 lease、handle、callback 和线程计数全部为零，且应用已经
确认全部外部使用者停止后，才能在不持有 QCurl 状态锁的条件下调用一次
`curl_global_cleanup()`。结果与状态映射固定如下：

- `WrongThread`：状态不变，不执行 cleanup。
- `TimedOut`：保持 `Stopping`，不执行 cleanup，允许协调线程再次等待。
- `Poison`、`ExternalUsersUnverified`、`InternalFailure`：进入 fail-closed `Failed` 终态，
  不执行 cleanup，也不释放 ownership 不确定的资源。
- `Succeeded`：执行一次 cleanup 并进入 `Stopped`。
- `AlreadyStopped`：保持 `Stopped`，不重复执行 cleanup。

错误线程调用必须返回结构化失败，不得使用嵌套 `QEventLoop`、循环
`processEvents()` 或 `BlockingQueuedConnection` 模拟完成。`aboutToQuit` 最多用于启动
关闭，不能单独作为全部 DeferredDelete、callback 和线程已经停止的证明。

普通 QCurl 实例调用者不负责手动关闭。只有应用级 runtime owner 可以调用该接口。
析构函数不得阻塞等待，也不得把失败路径退化为自动 cleanup。关闭成功后 runtime 保持
`Stopped`，所有新 QCurl 工作确定性失败；当前合同不支持重新初始化。

### 4.5 QCurl 动态卸载不是本合同目标

手动关闭只管理 libcurl 运行时，不证明 Qt 动态库卸载安全。即使 `shutdownAndWait()`
成功，仍可能存在 Qt 元类型注册、queued metacall、函数指针、事件过滤器或其他代码地址
引用 QCurl 动态库。本项目当前不提供 `canUnload()`，不承诺 `QPluginLoader::unload()` 或
`QLibrary::unload()` 成功，也不把动态装卸测试列为本次实现门禁。QCurl 保持加载到进程结束。

## 5. P1-1：share rollback poison 后仍继续注册 transfer

### 5.1 定位

- `src/QCCurlMultiManager.cpp:224-273`
- `src/private/QCCurlMultiManagerShareLifecycle.cpp:188-236`
- `src/private/QCCurlMultiManagerEventDriver.cpp:101-102`
- `src/private/QCCurlMultiManagerEventDriver.cpp:125-126`
- `tests/qcurl/tst_QCCurlMultiDetachOwnership.cpp:32-130`

### 5.2 代码事实

`tryAddReplyOnOwnerThread()` 在进入 share 配置前检查 manager readiness。随后：

1. `prepareShareForReplyLocked()` 取得或更新 share context。
2. `applyShareToEasyLocked()` 先设置 `CURLOPT_SHARE`。
3. 当 `CURLOPT_COOKIEFILE` setup 失败时，代码尝试 rollback `CURLOPT_SHARE=nullptr`。
4. rollback 失败时，`detachShareBindingLocked()` poison manager 并保留绑定账本。
5. `applyShareToEasyLocked()` 返回 `void`，调用方无法区分成功、降级和 poison。
6. `tryAddReplyOnOwnerThread()` 没有再次检查 poison，继续调用 `addEasyToMultiLocked()`。
7. 成功后继续登记 `m_activeTransfers` 并增加 `m_runningRequests`。

event-driver 入口发现 poison 后 fail-closed，不再驱动 multi。结果是一个在 poison 产生后
仍被登记的新 transfer 可能永久停留，没有网络推进，也没有确定性完成。

### 5.3 为什么现有测试没有关闭问题

`tst_QCCurlMultiDetachOwnership` 先通过 test helper 创建并登记 transfer，再触发 detach 或
rollback failure。它证明已登记对象图会在 poison 后被保留，并证明后续 test-helper admission
会被拒绝，但没有覆盖真实 add-reply 中“本次 apply 产生 poison，本次调用仍继续 add easy”
的顺序。

### 5.4 裁决与修复边界

该问题是 P1 release blocker。修复必须满足：

- share prepare/apply 返回结构化结果，而不是 `void`。
- apply/rollback 产生 poison 后，本次 easy 不得进入 multi。
- 不得登记 active transfer，不得增加 running count。
- 尚未注册的 record、easy、share context 和 callback userdata 必须作为完整图隔离。
- reply 必须确定性终止且恰好完成一次。

## 6. P1-2：Scheduler 同步信号后的对象失效没有处理

### 6.1 定位

- `src/QCNetworkRequestScheduler.cpp:73-74`
- `src/QCNetworkRequestScheduler.cpp:126-131`
- `src/QCNetworkRequestScheduler.cpp:217-224`
- `src/QCNetworkRequestScheduler.cpp:278-286`
- `src/QCNetworkRequestScheduler.cpp:326-328`

### 6.2 代码事实

Scheduler 已做到先解锁再发 signal，但这只消除了“持锁同步 slot”死锁，并未处理对象寿命：

- `requestQueued` 返回后立即调用 `processQueue()`。
- `bandwidthThrottled` 返回后继续遍历 `toStart` 并启动 reply。
- 批量 `requestCancelled` 返回后继续处理剩余 reply。
- `changePriority()` 发射 `requestQueued` 后继续调用 `processQueue()`。

Qt AutoConnection 在同线程会同步执行 DirectConnection。slot 可以合法删除 reply，也可能
删除 Scheduler 的 owning object tree。signal 返回后继续使用 `this`、reply 或缓存的裸指针，
会进入失效对象。

### 6.3 裁决与修复边界

该问题是 P1。正确处理不是把 signal 改成强制 queued，而是：

- signal 前建立 `QPointer<QCNetworkRequestScheduler>` sender guard。
- 对将继续使用的 reply 建立 `QPointer<QCNetworkReply>` guard。
- 每次同步 signal 返回后重新验证 guard，失效则立即停止当前流程。
- 批量操作每轮重新验证 sender 和对应 reply。
- 状态提交仍须在 signal 前完成，signal 后不得依赖未持有的容器 iterator 或裸指针。

## 7. P1-3：析构期间发射业务信号

### 7.1 定位

- `src/QCWebSocket.cpp:42-48`
- `src/QCWebSocket_p.h:31-42`
- `src/private/QCWebSocketLifecycle.cpp:268-291`
- `src/private/QCWebSocketLifecycle.cpp:357-413`
- `src/private/QCNetworkRequestSchedulerLifecycle.cpp:47-50`
- `src/QCNetworkCancelToken.cpp:30-33`
- `src/QCNetworkCancelToken.cpp:76-106`

### 7.2 WebSocket 代码事实

`QCWebSocket::~QCWebSocket()` 在 Connected、Connecting 或 Closing 状态调用 public `abort()`。
`abort()` 进入 `cleanupConnection()`，该路径可能：

- 发射 `stateChanged`。
- 发射 `isValidChanged`。
- 发射 `disconnected`。
- 根据 close/reconnect policy 进入运行期重连处理。

`emitWebSocketSignal()` 使用 `QPointer<QCWebSocket>` 判断 signal 中对象是否被删除。但在
`QCWebSocket::~QCWebSocket()` 执行期间，派生类析构已经开始，而 `QObject::~QObject()`
尚未执行，`QPointer` 仍为非空。它不能识别 slot 正在重入一个部分析构的 QCWebSocket。

### 7.3 同类路径

- Scheduler 析构直接调用 `cancelAllRequests()`，该运行期 API 发射取消和队列信号。
- CancelToken 析构直接调用 `cancel()`，该运行期 API 调用 reply cancel 并发射 `cancelled()`。

这些 destructor-to-public-operation 路径均把可重入业务协议带入派生类析构阶段。

### 7.4 DeferredDelete 关系

WebSocket private cleanup 对 parent-owned timer/notifier 调用 `deleteLater()`。对象最终可由
QObject parent 同步析构，因此单独看该调用不能证明双删或泄漏；真正的缺陷是析构流程仍
执行运行期信号、重连决策并依赖可重入状态机。

### 7.5 裁决与修复边界

该问题是 P1。必须拆分：

- 运行期 `abort/cancel/clear`：允许按 public contract 发 signal。
- 析构期 teardown：同步、幂等、不发业务 signal、不重连、不依赖 DeferredDelete 完成。

析构期 teardown 必须按确定状态规则处理 transfer：

- 正常 transfer 且 detach 成功：执行 cancel/detach，确认 easy/share/context/callback 已解绑后释放。
- detach 或 rollback 失败：立即 poison manager 并停止 admission。
- ownership 无法证明：把完整 easy/share/context/callback 对象图移入 quarantine。
- quarantine 对象标记为 `NonCallable`，以后不得再进入任何 libcurl API。
- 禁止为了完成析构而强制 cleanup。

完成 transfer 处理后，最后让 QObject parent 同步销毁 timer/notifier 等 child。不得用
`QPointer(this)` 作为派生析构安全证明。

## 8. P1-4：自动静态 `curl_global_cleanup()` 违反生命周期合同

### 8.1 定位

- `src/CurlGlobalConstructor.cpp:37-57`
- `src/private/CurlGlobalConstructor_p.h:21-56`
- `src/QCCurlMultiManager.cpp:64-76`
- `src/QCNetworkAccessManager.cpp:114`
- `src/QCCurlHandleManager.cpp:15`

### 8.2 代码事实

`CurlGlobalConstructor::instance()` 返回函数静态 QObject。构造时调用 `curl_global_init()`，
静态析构时在初始化成功条件下自动调用 `curl_global_cleanup()`。

该对象只能观察 QCurl 自己的初始化结果，不能证明：

- 其他 QCurl `thread_local` manager 已全部析构。
- 所有 callback、timer、socket notifier 和 queued work 已停止。
- 其他动态库或应用组件已经不再使用 libcurl。
- 跨翻译单元和跨动态库静态析构顺序满足 QCurl 假设。

### 8.3 与“析构一个 QCurl 实例”的区别

普通 `QCNetworkAccessManager` 或其他 QCurl 实例析构，只应清理自身对象图。它不能触发
`CurlGlobalConstructor` 的静态析构，也不能取得进程级 cleanup 权限。删除一个实例必须
允许其他 QCurl 实例和外部 libcurl 使用者继续工作。

### 8.4 裁决与修复边界

在本文件已确认的生命周期合同下，该实现是 P1 生命周期设计缺陷。唯一实施路线是：

- 全局初始化状态不继承 QObject。
- 使用应用持有、进程唯一且不可复制移动的 `QCurlRuntime` 管理初始化状态。
- 删除组件局部的自动静态 cleanup。
- 所有 QCurl 工作通过内部 runtime lease 进入，`Stopping` 后拒绝新 lease。
- 只提供第 4.4 节固定签名的 `beginShutdown()` 与 `shutdownAndWait(QDeadlineTimer)`。
- 只在全部受管状态清零且应用确认没有外部 libcurl 使用者后调用一次 cleanup。
- cleanup 后进入不可重新初始化的终态，但不据此支持动态卸载 QCurl。
- runtime 析构、普通 QObject 析构和 `atexit()` 都不得自动 cleanup。

## 9. P1-5：Logger 在状态锁内执行可重入外部代码

### 9.1 定位

- `src/QCNetworkDefaultLogger.cpp:146-189`

### 9.2 代码事实

普通 `QMutex` 的临界区从状态更新一直覆盖到：

- 日志 entry append 和上限维护。
- 文件打开、写入和轮转。
- Qt debug/info/warning/critical 输出。
- 用户提供的 `customCallback`。

用户 callback 可以同步再次调用 logger；Qt message handler 也可以把 Qt 日志同步回送到
同一 logger。普通 `QMutex` 在同线程重入时会自死锁。把它改成 recursive mutex 只会掩盖
用户代码在 logger 中间状态重入的问题。

### 9.3 裁决与修复边界

该问题是 P1。锁内只允许：

- 分配顺序号。
- 更新 bounded in-memory entries。
- 复制配置、writer 和 callback 快照。

文件输出、轮转、Qt logging 和用户 callback 必须在锁外执行。若要求全局顺序，应使用
不可变 `LogRecord`、序号和单一串行 writer 保序，不能用状态锁覆盖全部副作用。

## 10. P1-6：Multipart source affinity 与 `setParent()` 执行线程未证明

### 10.1 定位

- `src/QCNetworkMultipartBody.cpp:109-143`
- `src/QCNetworkMultipartBody.cpp:178-206`
- `src/private/QCSingleFileMultipartBodyDevice.cpp`

### 10.2 代码事实

`fromSingleFileDevice()` 在任何当前线程检查前调用：

- `device->isReadable()`。
- 用于解析 size 的 source `QIODevice` API。
- wrapper 构造逻辑。

`takeDevice()` 只比较 parent 与 wrapper 的 affinity 是否相同，未验证
`QThread::currentThread()` 是否就是 device/parent 的 owner thread，随后直接调用
`device->setParent(parent)`。

问题不是 `std::unique_ptr<QIODevice>` 这一类型本身非法，而是 movable value object 持有
affinity QObject，且 source 访问、wrapper 创建、parent 移交和最终析构线程没有被统一编码。

### 10.3 裁决与修复边界

该问题是 P1。推荐边界是：

- 内存 multipart body 保持纯值数据。
- 流式描述只保存 metadata 和明确的 borrowed-source observer。
- 在 source/reply 的共同 owner thread 验证 affinity 后创建实际 wrapper。
- wrapper 创建后立即 parent 到 transfer/reply owner。
- borrowed source 用 `QPointer` 观察，销毁后确定性失败，不形成第二 owner。
- wrong-affinity source 或无法 parent 的输入同步拒绝，不偷偷跨线程读取。

## 11. P1-7：process-wide poison quarantine 存在数据竞争

### 11.1 定位

- `src/QCCurlMultiManager.cpp:64-69`
- `src/QCCurlMultiManager.cpp:366-389`

### 11.2 代码事实

`QCCurlMultiManager::instance()` 是 `thread_local`。poisoned shutdown 为避免错误释放未知
ownership 的对象图，把 transfer、share context、binding、socket 和 multi handle 追加到
进程级静态 `QList`。

多个线程可以同时析构各自的 manager，并同时进入 `retainPoisonedObjectGraph()`。每个
manager 的 `m_mutex` 只保护本 manager，不能保护这些进程共享静态列表。当前 append 没有
process-wide mutex，因此存在真实数据竞争。

### 11.3 裁决与修复边界

该问题是 P1 failure-path 缺陷。正确路线是：

- 建立 process-lifetime quarantine registry。
- 使用专用非递归 mutex 保护跨线程 append/move。
- 锁内只移动容器，不调用 Qt signal、日志 callback 或 libcurl API。
- 条目显式标记 `NonCallable`，后续驱动入口全部 fail-closed。
- registry 生命周期必须覆盖全部 `thread_local` destructor。
- 对无法成功解绑的对象图不做“为了整洁”的 cleanup。

## 12. P2：pause/resume blocking fallback 是不可达危险分支

### 12.1 定位

- `src/private/QCNetworkReplyFlowControl.cpp:47-66`
- `src/private/QCNetworkReplyControls.cpp`

### 12.2 代码事实

`pauseCurlEasy()` 在非 manager thread 分支使用 `BlockingQueuedConnection`，lambda 捕获裸
`CURL*` 和栈上结果引用，且不检查 invoke 投递结果。单独看该分支，目标事件循环停止会
造成永久等待。

但 `QCCurlMultiManager::instance()` 返回当前线程自己的 `thread_local` manager。按当前公开
pause/resume owner-thread 封送不变量，helper 取得的 manager 就属于当前线程，因此该
blocking 分支不可达。当前没有证据证明运行时实际进入该分支。

### 12.3 裁决

该项为 P2 清理项，不应描述成已发生的死锁。应删除 blocking fallback，在 owner-only helper
首行断言当前线程并直接调用 `curl_easy_pause()`。跨线程 public API 只排队 operation token，
并对对象销毁、投递失败、shutdown 和 poison 返回明确结果。

## 13. 经复核不能认定为当前缺陷的内容

本节条目统一归类为**当前非 finding，并记录测试缺口**，不再同时标记为“待证实风险”。
未来只有 sanitizer、故障注入或具体可达调用路径提供新证据时，才把对应条目重新打开为
新的 finding；在此之前不得把它们计入当前缺陷数量。

### 13.1 WebSocket pool parent-owned child

parent-owned WebSocket child 最终由 QObject parent 同步删除。pending DeferredDelete 在对象
同步析构时不会单独证明双删或泄漏。runtime clear 与 destructor teardown 仍应拆分，但不能
在没有复现和 sanitizer 证据时把 pool child ownership 定为 correctness bug。

该结论不影响第 7 节的 `QCWebSocket::~QCWebSocket()` 析构期业务信号 finding。两者是不同问题。

### 13.2 `QSharedPointer<QCCurlMultiTransferRecord>`

transfer record 是纯 C++ record，easy handle 也没有 QObject affinity。当前调用图没有证明
最后一个 shared reference 在错误线程释放，也没有证明释放时 easy 仍被并发驱动。因此不能
仅凭 `QSharedPointer` 拼写判定错误线程析构。

### 13.3 `QSharedPointer<QFile/QSaveFile>`

当前 resumable writer 调用图未证明 shared reference 跨线程保留或错误线程成为最后
releaser。后续可以在证明单 owner 后简化，但不是当前 correctness 修复前置条件。

### 13.4 wrong-thread reply deletion

QObject 调用方必须在 affinity thread 停止工作并销毁对象。对象已经进入 destructor 后，
排队捕获 `this` 会产生新的 UAF，blocking self-rescue 又依赖可能已经停止的 event loop。
因此，wrong-thread direct delete 首先是调用方合同违规；析构器只应诊断并执行最保守的
无回调 cleanup，不能承诺自救。

### 13.5 poisoned stale callback UAF

当前 event-driver 入口检查 poisoned/shutdown，且 poison 路径保留对象图。尚未找到绕过
检查并解引用已释放 userdata 的确定路径。因此 stale callback UAF 未被证实。隔离状态仍应
显式编码为 NonCallable，只有 sanitizer、故障注入或具体调用路径证明 dereference 后才升级。

## 14. Qt 锁逐代码裁决

| 组件 | 锁内内容 | 裁决 | 处理原则 |
| --- | --- | --- | --- |
| `QCNetworkDefaultLogger` | 状态、文件 I/O、Qt 日志、callback | P1 | 状态快照锁内，全部外部副作用锁外 |
| `QCNetworkRequestScheduler` | 队列、计数、状态转换 | 锁外 signal 已完成，但同步 signal 后寿命未处理 | 每次 emit 后重新验证 sender/reply |
| `QCNetworkMemoryCache` | entry、size、淘汰 | 普通内存临界区 | 当前 mutex 合理，无证据支持机械换读写锁 |
| `QCNetworkDiskCache` | metadata 与文件事务 | 主要是吞吐风险 | 先保持事务一致性，再依据 contention 数据拆 writer |
| `QCNetworkMockHandler` | mock 配置、队列、计数 | 普通测试状态 | 外部执行继续保持锁外 |
| CURLSH domain mutex | libcurl share lock callback | 合同要求 | 每个 domain 独立，callback 内不执行 Qt/user code |
| manager recursive mutex | manager 状态与 libcurl 同步回流 | 有现实依据 | 固定允许的内部重入边，不能机械改普通 mutex |
| poison quarantine | 进程级静态容器 append | P1 race | process-wide registry + 专用 mutex |

本审核不支持以下机械操作：

- 全仓 `QMutex -> QReadWriteLock`。
- 全仓 `QRecursiveMutex -> QMutex`。
- 全仓 `QSharedPointer -> std::shared_ptr`。
- 全仓 shared ownership 改 unique ownership。
- 不分析事务语义就把所有锁内 I/O 移出。

## 15. 唯一重构路线

重构必须按以下顺序执行，不能先做大范围 pointer churn。

### 阶段 1：固定进程级与实例级生命周期合同

- 文档化第 4 节合同。
- 区分 instance teardown、thread-local manager teardown 和 application-owned runtime shutdown。
- 删除自动静态 global cleanup，新增进程唯一 `QCurlRuntime` 与内部 lease。
- 只提供 `beginShutdown()` 与 `shutdownAndWait(QDeadlineTimer)` 两个关闭入口。
- 固化 `Uninitialized -> Running -> Stopping -> Stopped/Failed` 单向状态机和第 4.4 节结果映射。
- cleanup 成功进入 `Stopped`；poison、外部使用者未确认和内部失败进入 `Failed`。
- 不提供 external lease API；应用在 `beginShutdown()` 前停止全部外部 libcurl 使用者。
- 明确手动关闭只关闭 libcurl，不支持 QCurl 动态卸载。

### 阶段 2：关闭 share admission/rollback 事务缺口

- share prepare/apply 返回结构化状态。
- poison 后立即停止当前 admission。
- 只有 libcurl 成功后才提交 binding/map/refcount。
- 正常 detach 成功后才释放 transfer 对象图。
- detach/rollback 失败时 poison，并将 ownership 不确定的完整
  easy/share/context/callback 对象图移入 `NonCallable` quarantine。
- 禁止强制 cleanup quarantine 中的资源。

### 阶段 3：修复同步 signal 与析构重入

- Scheduler 每次 emit 后验证 sender/reply guard。
- WebSocket runtime abort 与 destructor teardown 分离。
- Scheduler destructor 使用无信号 internal cancel/drain。
- CancelToken destructor 使用无信号 internal detach/cancel。

### 阶段 4：收敛 Multipart QObject 边界

- 描述阶段不拥有运行期 wrapper QObject。
- source/reply 共同 owner thread 创建并 parent wrapper。
- wrong-affinity 输入确定性失败。

### 阶段 5：重构 Logger 和 quarantine

- Logger 锁内只生成 immutable record 与快照。
- 文件、Qt logging、callback 移到锁外串行 writer。
- quarantine 使用 process-wide mutex 和 NonCallable 状态。

### 阶段 6：删除 dead blocking fallback

- owner-only helper 直接操作 easy handle。
- public 跨线程 API 只投递 token/value command。
- 删除裸 `CURL*` 和栈结果的 blocking capture。

### 阶段 7：落实固定指针与 QObject 职责

- public class PIMPL 保持 `QScopedPointer`。
- `CURL*`、slist 等纯 C 资源使用 `std::unique_ptr` 与具名 deleter。
- timer、notifier、reply 和运行期 wrapper 使用 owner-thread QObject parent-child。
- QObject 外部观察使用 `QPointer`。
- `QCCurlMultiTransferRecord` 等现有 Qt 共享状态保持 `QSharedPointer`。
- 私有 `QPromise` operation state 保持现有 `std::shared_ptr`。
- 不执行全仓智能指针类型替换。

## 16. 测试与证据要求

### 16.1 首个证明点

必须通过真实 add-reply 路径强制：

```text
setup:CURLOPT_COOKIEFILE,rollback:CURLOPT_SHARE
```

测试必须证明：

- manager 进入 poison。
- 当前 easy 未加入 multi。
- active/running count 不增加。
- reply 恰好进入一次终态。
- 没有 socket/timer action 再驱动 poisoned handle。
- 尚未注册的 record/easy/share/callback 图仍保持安全可达。

### 16.2 其余确定性测试

- Scheduler direct slot 删除 scheduler 后，signal 返回路径立即停止。
- Scheduler direct slot 删除 reply 后，不再启动或取消该裸指针。
- QCWebSocket 析构不发运行期业务 signal，不触发重连。
- Scheduler/CancelToken 析构不发 public cancel signal。
- Logger custom callback 和 Qt message handler 同步重入不死锁。
- Multipart wrong-affinity source、source 提前析构和跨线程移动确定性失败。
- 多线程同时 teardown poisoned `thread_local` manager 时 TSan 无 race。
- 多个 QCurl 实例中只析构一个实例后，其他实例继续完成网络操作。
- 外部 libcurl 使用者存在时，普通 QCurl 实例析构不触发 global cleanup。
- 进程生命周期运行期间销毁最后一个 QCurl 局部实例也不触发 global cleanup。
- `Running -> Stopping` 后新请求和新 lease 确定性失败。
- 活动 transfer、callback、handle、worker 或 DeferredDelete 存在时不执行 cleanup。
- 外部 libcurl 使用者在 `beginShutdown()` 前全部停止；未确认时返回
  `ExternalUsersUnverified`，进入 `Failed` 且不执行 cleanup。
- owner event loop 仍承担 teardown 时调用阻塞等待，返回 WrongThread 而不死锁。
- `beginShutdown()` 期间 queued teardown 和 DeferredDelete 在所属线程完成。
- `WrongThread` 保持原状态；`TimedOut` 保持 `Stopping` 并允许再次等待。
- `Poison` 与 `InternalFailure` 进入 `Failed`，不执行 cleanup。
- 多线程重复调用 shutdown 只执行一次 cleanup；成功后返回 `AlreadyStopped` 的调用不重复 cleanup。
- cleanup 成功后所有新 QCurl 工作确定性失败，不发生隐式重新初始化。
- `shutdownAndWait()` 成功不触发或暗示 QCurl 动态卸载。

### 16.3 动态分析

- ASan/UBSan：同步 signal 删除 sender/reply、析构重入和 callback userdata。
- LSan：实例和 transfer 正常路径无泄漏；手动 cleanup 成功路径释放 QCurl 拥有的 libcurl
  全局初始化责任；poison quarantine 以 intentional-retention 合同解释。
- TSan：quarantine、Logger、Scheduler 和 share lock 路径。
- stopped-event-loop 测试：析构 teardown 不依赖 DeferredDelete 被处理。

本轮没有运行这些测试。任何“已修复”结论都必须绑定修订后提交和新生成的测试证据。

## 17. 证伪条件、裁剪清单与止损规则

### 17.1 证伪条件

以下证据出现时，应按新证据修订本文：

- 真实 add-reply 故障注入证明 poison 后 easy 从未进入 multi：P1-1 才可关闭。
- 证明 Scheduler 所有相关 signal 均不可能同步执行删除对象的 slot：P1-2 可重审，但必须有 API 合同和测试。
- 证明析构期 public signal 不可重入任何派生状态：P1-3 可重审，不能只凭调用意图。
- 删除自动静态 cleanup，建立进程唯一 runtime 与完整 lease，并证明只有成功的
  `shutdownAndWait()` 执行一次 global cleanup：P1-4 才可关闭。
- 证明 Logger 外存在不可绕过的 serialization 且 callback 不会同步回流：P1-5 可重审。
- TSan 证明所有 thread-local teardown 都被更外层全局串行：P1-7 可重审。
- sanitizer、故障注入或具体可达调用路径证实当前非 finding 中的 UAF、双删或
  wrong-thread release：对应条目重新打开为新的 finding。

### 17.2 裁剪清单

实施时应删除或拒绝：

- 自动静态 `curl_global_cleanup()`。
- 普通 QCurl 实例上的 cleanup/shutdown 方法和直接暴露的 raw `curl_global_cleanup()`。
- runtime 析构、QObject 析构或 `atexit()` 中的隐式 cleanup。
- cleanup 失败后强制释放、重新初始化或继续卸载 QCurl 的路径。
- 把 `shutdownAndWait()` 成功描述成 QCurl 动态卸载保证的 API 或文档。
- share apply 产生 poison 后仍继续 add easy 的路径。
- 析构函数调用会发业务 signal 的 public runtime API。
- pause/resume `BlockingQueuedConnection` fallback。
- 跨线程 lambda 中裸 `CURL*` 和栈结果引用。
- movable value object 内无法证明析构线程的 owning QObject。
- Logger 状态锁内 Qt 日志和用户 callback。
- 无锁 process-wide quarantine append。
- 无证据的全仓智能指针和锁类型替换。

### 17.3 止损规则

- 无法先写出确定性 failure injection 时，不做跨模块 pointer churn。
- 修复改变 public signal、promise、错误码或同步/异步语义时，单独进行 API 合同审查。
- share detach/cleanup 失败时，不为追求“释放干净”销毁未知 ownership context。
- owner event loop 已停止时，不新增 `deleteLater()` 作为唯一 cleanup。
- sanitizer 失败先缩小复现，不以关闭 sanitizer 或增加 suppression 代替修复。
- 不提供 external lease；应用未确认全部外部使用者已在 `beginShutdown()` 前停止时，
  返回 `ExternalUsersUnverified` 并进入 `Failed`。
- `shutdownAndWait()` 失败或超时时，保持 QCurl 加载并保留未知所有权资源。
- cleanup 成功也不允许调用方据此推断可以动态卸载 QCurl。

## 18. 最终结论

Qt/std 智能指针只表达所有权的一部分；QObject 生命周期还由 parent、thread affinity 和
event loop 决定；`QPointer` 只观察已经完成 QObject 析构的对象；mutex 只保护指定普通
数据；libcurl handle 虽无 QObject affinity，但同一个 handle 仍必须由单一 owner-thread
状态机驱动。

多个 QCurl 实例可以共享进程级 libcurl 运行时。析构其中一个实例是受支持场景，且不得
影响其他 QCurl 或外部 libcurl 使用者。QCurl 不支持其他线程或组件仍使用 libcurl 时卸载
动态库或执行静态全局清理。当前唯一实施路线是删除自动 `curl_global_cleanup()`，由应用
持有进程唯一 `QCurlRuntime`；普通实例只释放自身资源，只有可验证的 `shutdownAndWait()`
可以手动关闭 libcurl。

手动关闭必须同时具备进程级状态机、完整内部 lease、应用对外部 libcurl 使用者的独占
保证和结构化失败结果。QCurl 不提供 external lease；应用必须在 `beginShutdown()` 前停止
全部外部 libcurl 使用者。任一条件无法证明时不执行 cleanup。关闭成功后 runtime 进入
`Stopped` 终态；poison、外部使用者未确认或内部失败进入 `Failed` 终态；超时保持
`Stopping` 以便再次等待。这些结果都不证明 QCurl 动态库可以卸载，动态卸载仍明确不受支持。

基于 `master-tmp@a1bafb7` 的实际代码，当前仍存在 7 项 P1：share poison admission、
Scheduler 同步信号寿命、析构期业务信号、自动静态 global cleanup、Logger 锁内外部代码、
Multipart affinity 和 quarantine 数据竞争。pause/resume blocking fallback 是应删除的 P2
死分支。WebSocket pool parent ownership、shared transfer record、
`QSharedPointer<QFile/QSaveFile>`、wrong-thread reply deletion 以及 stale callback UAF
统一属于当前非 finding 与测试缺口。
只有新的 sanitizer、故障注入或具体可达调用路径证据才能将对应条目重新打开为新的 finding。

唯一正确路线是：**应用持有的进程唯一 runtime + 固定的 `beginShutdown()` /
fail-closed `shutdownAndWait(QDeadlineTimer)` + 关闭前停止全部外部 libcurl 使用者 +
无动态卸载承诺 + owner-thread actor + libcurl 事务状态机 + 同步 signal guard +
无信号析构 teardown + 可并发 poison quarantine + 锁外外部副作用**。
在故障注入、同步重入、跨线程和 sanitizer 证据完成前，不得宣称 QCurl 生命周期修复已经闭合。
