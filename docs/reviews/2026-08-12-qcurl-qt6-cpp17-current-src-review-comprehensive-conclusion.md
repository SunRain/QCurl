# QCurl 当前 `src/` Qt6/C++17 规范完整审核结论

> 初始审查日期：2026-08-12
>
> `39325eaa` 专项复核日期：2026-08-13
>
> QCurl 基线：`master-tmp@c70cef9f1211f6d7303bb1b1012314e222a37761` 加专项复核时的
> 现有 dirty WIP
>
> 中文规范权威：`Qt6_CPP17_Coding_Style/cn/`
>
> 规范子仓快照：`39325eaa8a0384b9158d89f2883b9de25214b69d`
>
> 本轮聚焦：只复核提交 `39325eaa` 改变的规则边界，并据此校准旧报告；不重新开展
> 全维度源码审核
>
> 文档性质：当前源码整改 authority；不是源码修复完成证明、deep-QA、ABI promotion、
> 发布候选签署或动态卸载支持证明

## 1. 最终结论

旧版本文记录的 `P0=0、P1=6、P2=6` 已不再准确。原因有两类：一是当前 dirty WIP 已经
关闭多项旧 finding；二是 `39325eaa` 明确区分 Qt/KDE 技术硬要求、项目严格门禁和通用默认
建议，不能继续把推荐项或条件性规则机械计为缺陷。

按当前源码逐代码复核后的裁决如下：

| 严重度 | 数量 | 当前裁决 |
| --- | ---: | --- |
| P0 | 0 | 未取得默认可达路径上的立即内存破坏或已复现 UAF 证据 |
| P1 | 1 | `QCCurlMultiManager` 的跨线程 QObject 访问合同错误 |
| P2 | 2 | Scheduler 错误线程 fallback；progress tracking 固定 queued 语义无证据 |
| 总体 | - | **REMEDIATION INCOMPLETE；三项确认源码缺陷仍需修复** |

当前确认缺陷只有三项：

1. **P1：`QCCurlMultiManager::addReply()`/`removeReply()` 的跨线程合同错误。**
2. **P2：Scheduler owner-thread-only 路径仍在错误线程读取或捕获 `QPointer`。**
3. **P2：progress tracking 显式 `Qt::QueuedConnection` 缺少固定异步合同。**

Runtime participant、CancelToken、WebSocket lease、resumable writer、Logger、Cookie bridge、
QObject 宏和系统性公共失败表达等旧条目，已改为“历史已关闭”或“重新盘点验证缺口”，
不再计入当前 P1/P2。

## 2. 权威、范围与证据边界

### 2.1 当前文档的权威关系

本文覆盖并替代同一路径下基于旧规范快照形成的现行数量和整改顺序。以下材料仍保留其
历史用途，但不得覆盖本次裁决：

- `master-tmp@a1bafb7` 的生命周期旧报告，只描述当时源码。
- 本文旧版的 `P1=6、P2=6`，只描述整改前的中间状态。
- T1-T6 的实现和 focused test 记录，可证明对应历史整改发生过，但不能替代当前 fresh QA。

后续实施必须同时消费本文与已归档的绑定方案包：

- `.helloagents/archive/2026-08/202608121517_qcurl_qt6_cpp17_current_src_remediation/`

该方案包的归档状态表示本轮审核 authority 与实施规划已修订完成，不表示其中 R1-R5 已实施。
后续取得源码修改授权时，应从归档 authority 建立新的活跃执行包，不得把本轮文档级 QA
复用为源码修复证据。

### 2.2 `39325eaa` 对本轮裁决的直接影响

本轮只应用提交 `39325eaa` 涉及的规则变化：

- `cn/` 是中文规范权威；根目录文件是兼容入口，`en/` 是翻译。
- 只有明确标记为“必须”“禁止”或项目门禁的规则构成阻断。
- `Qt::AutoConnection` 是默认路线；只有合同要求同线程也强制异步时，才显式使用
  `Qt::QueuedConnection`，并说明原因。
- QObject 同一实例跨线程访问只允许作为受控例外，必须具备明确 API 合同、稳定生命周期、
  完整同步和评审依据。
- 自定义 queued payload 必须在首次相关连接前完成可审计注册；具名注册的兼容路径要按实际
  Qt 基线和动态名称需求验证，不能只看调用形式下结论。
- `Q_PROPERTY` 是否需要 `NOTIFY` 取决于可观察变化合同；构造后只读或无需观察的属性不一律
  要求 `NOTIFY`。
- 文档生成器必须先识别真实 profile。存在 Doxygen 注释不等于 Doxygen、Doxyqml 或 QCH
  已启用；Doxyqml 是 Doxygen 的 QML 扩展，不是 C++ 注释的独立生成器。

与上述变化无直接关系的文件体积、命名、性能或其他历史 finding，不纳入本次专项计数。

### 2.3 源码基线说明

父仓 HEAD 固定为 `c70cef9f1211f6d7303bb1b1012314e222a37761`，但专项复核以该 HEAD 加
现有 dirty WIP 为真实源码基线。报告中的符号与当前代码优先于旧行号；后续源码继续变化时，
应按符号重新定位，不能只照搬本文行号。

专项复核期间代码图谱统计为 594 个文件、3911 个节点、30700 条边，embeddings 为 0。
图谱只用于结构导航，最终裁决以当前源码、规范原文和确定性测试为准。

### 2.4 本轮未取得的证据

本轮没有运行：

- GCC/Clang configure 或 build；
- QtTest、完整 CTest 或真实网络集成；
- Doxygen 或 Doxyqml 生成；
- ABI gate、installed consumer 或 baseline diff；
- ASan/UBSan/LSan、TSan 或动态并发 shutdown 验证。

因此，本文只能确认静态源码缺陷、已关闭代码形态和验证缺口，不能声称整改或发布就绪。

## 3. 当前 finding 总表

| ID | 严重度 | 当前代码位置 | 问题 | 唯一整改方向 |
| --- | --- | --- | --- | --- |
| F1 | P1 | `src/QCCurlMultiManager.h:46-116`、`src/QCCurlMultiManager.cpp:274-288,376-386,584-606` | API 声称跨线程安全，但错误线程创建/capture `QPointer`，或直接读取 reply 私有状态 | `addReply()`/`removeReply()` 固定 manager owner-thread-only；wrong-thread 同步 fail-closed，不捕获 QObject 指针 |
| F2 | P2 | `src/QCNetworkRequestScheduler.cpp:229-250`、`src/private/QCNetworkRequestSchedulerFinalize.cpp:16-37,94-112` | owner-thread-only 内部入口仍保留 wrong-thread `QPointer` marshal | 删除 defensive marshal；不变量违约时断言并 fail-closed |
| F3 | P2 | `src/private/QCNetworkRequestSchedulerPrivate.cpp:14-54` | progress tracking 显式 queued，但没有强制异步、避免重入或节流合同 | 默认改回 `Qt::AutoConnection`；只有先用时序测试证明固定异步必要性，才保留 queued 并记录合同 |

## 4. F1 / P1：MultiManager 跨线程 QObject 访问合同错误

### 4.1 当前代码事实

`QCCurlMultiManager` 的类说明写明 manager “接受跨线程投递的管理操作”。`addReply()` 和
`removeReply()` 的 Doxygen 又分别标注“线程安全”。这形成了公开的内部调用合同，而不是
偶发 defensive branch。

实际实现与该合同不安全：

- `addReply()` 在 wrong-thread 路径进入 `marshalAddReplyIfNeeded()`。
- `marshalAddReplyIfNeeded()` 在调用线程构造 `QPointer<QCNetworkReply>`，再把它捕获到 queued
  lambda；这违反 guarded pointer 只能在目标 QObject affinity thread 判空、复制或解引用的门禁。
- `queueAddReplyFailure()` 继续围绕同一个 `QPointer` 形成二次异步投递，扩大生命周期推断面。
- `removeReply()` 没有线程检查，直接读取 `reply->d_func()->multiTransferRecord`。在文档允许任意
  线程调用的前提下，这是直接的跨线程 QObject 私有状态访问。

mutex、原子 shutdown 标记和再次判空都不能修复该问题。风险根源是跨线程 API 仍以 live
QObject 指针作为 command payload。

### 4.2 分级理由

该问题定为 P1，而不是 P0：

- 当前 API 合同明确允许跨线程调用，错误路径可达。
- 代码静态上违反 QObject affinity 和 `QPointer` 使用门禁。
- 但本轮没有取得已复现 UAF、内存破坏或数据竞争报告，不能夸大为 P0。

### 4.3 唯一重构路线

现有两个入口不再保留跨线程兼容：

1. 将 `addReply()` 与 `removeReply()` 的合同改为 manager owner-thread-only。
2. 入口先验证 manager owner thread；wrong-thread 调用同步 fail-closed，并在内部构建启用断言。
3. wrong-thread 分支不得读取、复制、判空、捕获或投递 `reply`。
4. 删除 `marshalAddReplyIfNeeded()` 和围绕 reply `QPointer` 的兼容路径。
5. owner thread 内仍可使用 `QPointer` 处理同步 signal 重入或延迟失败通知，但必须把使用理由
   约束在 owner-thread 代码块中。
6. 若未来确有跨线程业务需求，另建只携带稳定 transfer token 的 command API；不得重新把
   `QCNetworkReply *` 或 `QPointer<QCNetworkReply>` 放入跨线程 payload。

### 4.4 关闭条件

F1 只有同时满足以下条件才能关闭：

- public/internal Doxygen 明确 owner-thread-only，且不再声称 `addReply()`/`removeReply()` 线程安全；
- wrong-thread 测试证明调用同步失败、无 reply/manager/curl multi 状态变化；
- 静态 gate 证明两个入口的 wrong-thread 分支不出现 reply 访问、`QPointer` 或 queued marshal；
- owner-thread add/remove、完成、取消和 shutdown focused QtTest 通过；
- TSan 或等价动态竞态证据覆盖 wrong-thread 拒绝与并发 shutdown。

## 5. F2 / P2：Scheduler wrong-thread fallback 与 owner-thread 合同冲突

### 5.1 当前代码事实

Scheduler 的 public command 已逐步固定为 owner-thread-only，并以 `CommandResult::WrongThread`
表达拒绝；但下列内部路径仍保留旧的 defensive marshal：

- `QCNetworkRequestScheduler::startRequest()`：wrong-thread 构造并捕获
  `QPointer<QCNetworkReply>`。
- `QCNetworkRequestScheduler::onRequestFinished()`：wrong-thread 构造并捕获
  `QPointer<QCNetworkReply>`。
- `QCNetworkRequestScheduler::onReplyDestroyed()`：wrong-thread 对 destroyed 参数构造并捕获
  `QPointer<QObject>`。
- `updateBandwidthStats()` 等无 QObject payload 的 owner-thread 调度，应按其独立时序合同审查，
  不能与上述 live QObject fallback 混为一项。

`processQueue()` 内的 `QPointer<QCNetworkRequestScheduler>` 与 `QPointer<QCNetworkReply>` 不属于
F2。它们在 scheduler owner thread 中保护同步 signal 发射后的重入销毁，是合法的 owner-thread
guard，不能被静态正则一并禁止。

### 5.2 唯一重构路线

1. 删除 `startRequest()`、`onRequestFinished()` 和 `onReplyDestroyed()` 的 wrong-thread marshal。
2. 在任何 reply/obj 访问前验证 scheduler owner thread。
3. 不变量违约时内部断言并立即 fail-closed，不触碰 queue、reply、curl 或统计状态。
4. 保留 `processQueue()` owner-thread signal 重入保护，并在静态 gate 中精确排除该合法模式。
5. 不新增第二套兼容入口，不使用裸地址、`QPointer` 或临时 QObject token 模拟跨线程支持。

### 5.3 关闭条件

- 三个目标函数不再包含 `invokeOnSchedulerOwnerThread` 或 QObject `QPointer` fallback；
- wrong-thread 行为测试证明无状态变化、无 signal、无后续 queued command；
- owner-thread finished/destroyed/processQueue 重入测试保持通过；
- 静态 gate 只匹配 wrong-thread pre-owner 分支，不误报 `processQueue()`；
- focused actor/thread QtTest 与 TSan 通过。

## 6. F3 / P2：progress tracking 固定 queued 语义无证据

### 6.1 当前代码事实

`QCNetworkRequestScheduler::Impl::connectProgressTracking()` 为 download/upload progress 两个
functor connection 都提供了 scheduler context，这一部分符合规范；问题只在显式连接类型：

- 两处均写死 `Qt::QueuedConnection`。
- 邻近代码和现有注释没有说明同线程也必须异步投递。
- 本轮没有发现避免重入、事件合并、节流或固定时序的回归测试。

`39325eaa` 已把默认规则改为 `Qt::AutoConnection`。sender/receiver 可能跨线程本身不是显式
queued 的充分理由，Qt 会在 signal 发射时按 receiver affinity 选择投递方式。

### 6.2 唯一重构路线

当前缺少固定异步证据，因此实施默认是把两处连接改为 `Qt::AutoConnection`，并补充同线程与
跨线程 progress 行为测试。只有在修改前先建立一个会因 direct delivery 破坏真实不变量的失败
时序测试，才允许保留 `Qt::QueuedConnection`；保留时必须在代码中说明强制异步的原因，并把该
测试设为回归门禁。

这不是两条长期并存的实现路线。裁决顺序固定为：**先用测试证明固定异步必要性；证明失败则
使用 AutoConnection。**

### 6.3 关闭条件

- 默认路线下，两处显式 queued 已删除，同线程和跨线程累计值均正确；或
- 固定异步必要性已有可复现测试，代码注释记录真实不变量，queued 时序测试稳定通过；
- finished/cancel/disconnect 后不再累计旧 progress；
- 无重复 connection、锁重入或 stale key 更新。

## 7. 历史已关闭或改判条目

下表只保存历史状态，不参与当前 `P1=1、P2=2` 计数：

| 旧条目 | 当前代码事实 | 当前裁决 |
| --- | --- | --- |
| Runtime participant 跨线程 `QPointer` | 已改为 registration token 和 owner endpoint，协调线程只处理纯值 id | 源码形态已关闭；并发 shutdown 动态 fence 仍是验证缺口 |
| CancelToken affinity/destroyed 裸地址 | 已有 owner-thread、affinity、registration id 和 destroyed 清理 | 已关闭 |
| WebSocket acquire result 携带 `QPointer` | acquire result 已只保存纯值 `leaseId`，owner thread resolve/release | 已关闭 |
| Resumable writer shared-own `QFile/QSaveFile` | writer 已直接持有设备，job 以唯一所有权持有 writer | 已关闭 |
| Logger ABI 暴露 `QSharedPointer` | 已改为 `QCNetworkLoggerHandle`，公共 ABI 不再暴露 logger owning smart pointer | 已关闭 |
| Cookie bridge 无 context functor | 已使用 manager context 和 `Qt::DirectConnection` | 已关闭 |
| 三个 QObject 派生类缺宏 | `Q_OBJECT` 与 `Q_DISABLE_COPY_MOVE` 已存在 | 已关闭 |
| 公共失败表达系统性缺口 | RetryPolicy、ConnectionPool、WebSocket command、Logger 等已有结构化结果 | 旧系统性 P1 已过时；改为刷新 inventory 内容和覆盖门禁 |
| 公共错误/借用注释整体缺失 | 当前 public headers 已补入相应合同注释 | 不再作为整体 P2；按 inventory 逐项验证真实性 |
| 九个生产文件超过 400 行 | 与 `39325eaa` 的规则变更无直接关系 | 移出本次专项 finding 和绑定整改主线 |

“已关闭”只表示当前源码已不再呈现旧缺陷形态，不表示本轮重新跑过构建、QtTest、sanitizer
或发布验证。

## 8. 条件性例外

### 8.1 具名 `qRegisterMetaType<T>("CanonicalName")`

当前 `QCurl::initialize()` 和 `registerQCNetworkRequestPriorityMetaType()` 使用具名注册，并有
`QMetaType::fromName()` consumer smoke 验证 canonical name。该行为可能服务已发布名称、插件、
动态调用或 static consumer 兼容，不能仅因使用具名重载就判为源码缺陷。

后续必须补齐的证据是：

- 在元类型 inventory 中记录每个 canonical name 的兼容理由和实际消费者；
- 证明 `QCurl::initialize()` 或等价初始化路径在首次相关 queued/AutoConnection 前执行；
- 分离“无参数注册满足 typed queued payload”与“具名注册满足动态名称兼容”的测试；
- 若某个名称没有真实兼容消费者，按当前 Qt 基线迁移为无参数注册并删除伪兼容承诺。

### 8.2 `QCWebSocket::url` 没有 `NOTIFY`

`QCWebSocket::url` 是构造后只读属性，当前没有 setter，也没有 BINDABLE 路线。调用方不依赖
构造后的变化通知，因此缺少 `NOTIFY` 不构成违规。若未来允许 URL 变化，必须重新设计通知和
连接周期合同，不能沿用本例外。

### 8.3 普通 Doxygen 与 Doxyqml

仓库存在 `Doxyfile`、manifest-driven 输入脚本和 `doxygen Doxyfile` 手工生成入口，故当前
C++ 文档 profile 是普通 Doxygen。仓库未发现 `.qml`、`qmldir`、Doxyqml 配置、Doxyqml 构建
入口或生成产物。

因此：

- 现有中文 `/** ... */` 注释可以按普通 Doxygen 语法审查；
- “注释看起来兼容”不能证明 Doxyqml 已启用或已生成；
- 当前没有 QML public API，不能为了验证 C++ 注释而机械引入 Doxyqml；
- 若后续新增或发布 QML API，再把 Doxyqml 作为同一 Doxygen 路线的 QML 扩展配置并运行。

旧方案中“普通 Doxygen/Doxyqml 共同语法已通过”的表述只能降级为静态注释形态检查，不能
作为 Doxyqml profile 或生成证据。

## 9. 当前验证缺口与门禁问题

### 9.1 public contract inventory 内容陈旧

`tests/public_api/test_public_contract_inventory.py` 当前通过 `1/1`，但验证器主要检查 schema、
header 覆盖和分类结构，不能证明 `currentContract` 与当前源码一致。至少以下内容已陈旧：

- ConnectionPool 条目仍写成“仅 qWarning”等整改前行为。
- WebSocket command 条目仍写成 `void`/`-1` 和 payload 截断等整改前行为。

因此，inventory gate 通过只能证明清单结构有效，不能证明合同盘点完成。后续必须更新当前
合同描述，并增加对已落地签名/结果类型的语义锚点检查。

### 9.2 Qt contract guard 存在误报

`tests/test_qt_contract_guards.py` 当前结果为 `3/4`。失败原因不是出现第四个源码缺陷，而是
`test_owner_thread_internal_paths_do_not_marshal_qobject_pointers()` 的测试实现有两层错误：

1. 测试把每个目标函数与每个 Scheduler 源文件做笛卡尔组合，首先在
   `QCNetworkRequestSchedulerFinalize.cpp` 中查找不存在的 `processQueue()` 并失败。
2. 修正“函数到定义文件”的映射后，现有宽泛正则仍会禁止目标函数中的全部
   `invokeOnSchedulerOwnerThread` 和 reply/object `QPointer`，无法区分 `processQueue()` 的无
   QObject payload 调度、owner-thread signal 重入保护，以及真正捕获 live reply/obj 的违规
   wrong-thread fallback。

门禁必须收窄为：

- 为每个函数建立唯一且真实的定义文件映射，缺失符号必须报告映射错误；
- 只检查目标函数进入 owner-thread invariant 之前的 wrong-thread 分支；
- 区分无 live QObject payload 的 owner-thread 调度、捕获 reply/obj 的违规 fallback，以及
  owner-thread signal reentrancy guard；
- 失败消息报告具体函数、分支和违规 capture，不以“函数中出现 QPointer”代替语义检查。

### 9.3 动态与发布证据缺失

下列内容仍待 fresh 证据，不得引用旧运行结果代替：

- Runtime participant 并发 unregister/shutdown fence；
- MultiManager/Scheduler wrong-thread fail-closed；
- progress 同线程/跨线程时序；
- GCC/Clang、full/strict offline CTest；
- Doxygen 实际生成与 warning 记录；
- public consumer、ABI diagnostics、ASan/UBSan/LSan、TSan；
- 绑定最终 dirty worktree 或候选 commit 的 qa-review/closeout fingerprint。

## 10. 唯一整改顺序

整改只保留以下五个阶段，必须串行推进：

### 阶段 1：MultiManager 线程合同

- 先补 wrong-thread 拒绝测试和精确静态 gate。
- 将 `addReply()`/`removeReply()` 固定为 owner-thread-only。
- 删除 live reply 跨线程 marshal，不新增兼容分支。

### 阶段 2：Scheduler wrong-thread fallback

- 删除三个 internal reply/object fallback。
- 保留 owner-thread `processQueue()` signal 重入保护。
- 收窄静态 guard，证明它只拦截真实 wrong-thread payload。

### 阶段 3：progress connection 语义

- 先建立同线程和跨线程时序测试。
- 没有固定异步证据时改用 `Qt::AutoConnection`。
- 只有测试证明真实不变量时才保留 queued，并记录原因。

### 阶段 4：元类型、inventory 与文档门禁

- 刷新 ConnectionPool/WebSocket 等 inventory 当前合同。
- 审计 canonical metatype name、初始化顺序和首次连接前注册。
- 修正 Qt guard 的函数/文件笛卡尔组合错误，以及对 `processQueue()` 无 payload 调度和
  owner-thread 重入保护的误报。
- 将普通 Doxygen、Doxyqml 未配置/当前无 QML API 的事实写入文档和门禁；不伪造 Doxyqml
  生成证据。

### 阶段 5：fresh 动态 QA

- 运行 focused QtTest、GCC/Clang、full/strict offline CTest 和 public consumers。
- 运行 Doxygen、ABI diagnostics、ASan/UBSan/LSan、TSan 及图谱/diff hygiene。
- 证据绑定最终工作树；不自动 promotion ABI baseline，不修改 Git index，不提交、tag、push
  或发布。

## 11. 实施边界

### 11.1 最小第一步与首个证明点

最小第一步是为 MultiManager 建立 wrong-thread 红测：从非 manager owner thread 调用
`addReply()`/`removeReply()`，证明当前代码会进入跨线程兼容或触碰 reply。首个通过证明必须是：
wrong-thread 同步 fail-closed，且没有 reply 读取、queued command、signal、transfer registry 或
curl multi 状态变化。

### 11.2 证伪条件

以下证据可以关闭对应 finding：

- 代码结构消除 wrong-thread QObject/QPointer 访问，并由行为测试证明无副作用；
- progress 固定 queued 有可复现的不变量测试，或已改为 AutoConnection 且两种线程关系通过；
- inventory 的描述与当前签名、结果类型和行为锚点一致；
- 元类型注册顺序由初始化调用链和首次连接前测试共同证明。

以下内容不能关闭 finding：多一次判空、扩大 mutex、只补注释、只通过宽泛正则、引用历史
focused tests，或把错误线程访问称为“正常不会发生”。

### 11.3 裁剪清单

本轮后续实施不包含：

- 已关闭 T1-T6 的重复重写；
- 与 `39325eaa` 无关的大文件拆分、全仓命名迁移或格式化；
- QCurl 动态卸载支持；
- 新增兼容开关或第二套 live QObject 跨线程 API；
- 为没有 QML public API 的当前项目机械引入 Doxyqml；
- ABI baseline promotion、Git 暂存、提交、tag、push 或发布。

### 11.4 止损规则

出现以下任一情况时，停止当前实现并回到合同设计：

- 为保留 wrong-thread 兼容再次捕获 live QObject 或 `QPointer`；
- 静态 gate 只能通过禁止所有 owner-thread `QPointer`，破坏 signal 重入保护；
- progress 连接语义只能靠主观推断，无法形成确定时序测试；
- 具名元类型注册被删除，但动态名称或 static consumer 兼容测试随之失败；
- 文档声称 Doxyqml、ABI、sanitizer 或 release gate 已通过，却没有当前 profile 和实际产物。

## 12. 最终裁决

当前源码基线的确认缺陷为 **P0=0、P1=1、P2=2**。旧版 `P1=6、P2=6` 已由当前代码和
`39325eaa` 规则边界共同推翻，不能继续作为整改完成标准。

后续唯一执行路线是：

1. MultiManager owner-thread 合同；
2. Scheduler wrong-thread fallback；
3. progress connection 语义；
4. 元类型、inventory、Qt guard 与文档 profile 门禁；
5. fresh 动态 QA。

T1-T6 只保留为历史整改证据。绑定方案中的 R1-R5 均未实施，当前 remediation 仍未完成；
本报告及本轮方案修订不授权源码/测试修改、ABI baseline promotion、Git 副作用、tag、push
或发布。
