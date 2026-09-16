# QCurl 企业级 Qt6/libcurl 工程审核结论：发布阻断与唯一修订路线

> 历史记录：以下审查/执行结论仅适用于正文注明的候选与时点；不是当前工程状态、发布资格或远端 CI 证明。原始结论保留，统一从[历史索引](../README.md)查阅。

> 日期：2026-07-31
>
> 性质：初次工程评审与逐代码复核的综合结论
>
> 当前状态：**BLOCKED / 需要重大修订**
>
> 审查范围：Qt6 网络编程、libcurl 的 Qt6 绑定、Qt6/KDE 应用库工程、公开 API/ABI、持久化、发布证据
>
> 最高代码规范：`Qt6_CPP17_Coding_Style/cn/Qt6_CPP17_Coding_Style.md`

本文是当前 dirty tree 的综合审核结论。本文只记录已经由源码、构建规则、测试代码和 vendored libcurl 文档支持的判断，不表示缺陷已经修复，也不表示任何历史构建、CTest、ABI、sanitizer、UCE 或 release gate 对当前工作树仍然有效。

本文中的路线均为唯一实施路线。被否决的方案只用于说明边界，不构成可选项。任何实现若重新引入本文明确否决的路线，审核结果自动恢复为 `BLOCKED`。

## 目录

1. [最终裁决](#一最终裁决)
2. [证据基准与有效性边界](#二证据基准与有效性边界)
3. [发布单元与兼容承诺](#三发布单元与兼容承诺)
4. [不可变工程合同](#四不可变工程合同)
5. [P0：内存安全、死锁与所有权阻断](#五p0内存安全死锁与所有权阻断)
6. [P1：协议、持久化、ABI 与证据阻断](#六p1协议持久化abi-与证据阻断)
7. [P2：公开 API 与工程一致性阻断](#七p2公开-api-与工程一致性阻断)
8. [隐含路线分叉的唯一裁决](#八隐含路线分叉的唯一裁决)
9. [固定实施顺序](#九固定实施顺序)
10. [完整验证矩阵](#十完整验证矩阵)
11. [发布完成门禁](#十一发布完成门禁)
12. [最终结论](#十二最终结论)

## 一、最终裁决

“工程评审：需要重大修订”结论准确。逐代码复核没有推翻初次评审中的任何缺陷类别，反而确认原文档存在事实表述不精确、finding 混项、发布范围不清和实施路线分叉。

当前 QCurl dirty tree 不得进入发布候选，不得维持 `implementation-complete`，不得引用历史本地 release gate `PASS` 作为当前代码证据。

阻断依据如下：

1. `QCNetworkReply` 与 `QCWebSocket` 在公开信号返回后继续访问可能已经销毁的 sender/private，形成真实释放后使用风险。
2. `QCWebSocketPool` 在 mutex 内执行局部事件循环、QObject 操作和公开信号，存在确定性自锁、重入死锁和线程亲和性破坏。
3. multi detach 在 `curl_multi_remove_handle()` 未成功时仍释放或转移 easy、record、share 和 callback ownership，直接破坏 libcurl 生命周期合同。
4. HTTP 缓存仍让有损 `QMap` 参与安全决策，无法正确表达重复头、有效 `Vary`、冲突 `Date/Age` 和饱和年龄计算。
5. 磁盘缓存把本地文件当作可信 `QDataStream` 容器输入，在校验前触发长度驱动分配。
6. Cookie、Diagnostics、UCE 与 ABI 路径仍存在吞错、嵌套事件循环、证据 fail-open、导出面失控和 baseline promotion 不受保护。
7. 当前版本是首个 `1.0.0` Stable 候选，仍保留双 API、迁移 shim 和下一 major 再 hard-break 不具备合理性。
8. 默认安装产物包含 Core 与 Other Extras。Preview 标签只能限制 ABI 承诺，不能豁免 UAF、死锁、线程错误和证据不可信。

本文修订后的原子 finding 共 20 项：

- P0：4 项，涉及释放后使用、死锁和 multi/easy 所有权。
- P1：10 项，涉及缓存、磁盘格式、WebSocket 保活、Cookie、UCE、ABI、发布单元与平台承诺。
- P2：6 项，涉及 public result、异步完成、Diagnostics、Qt 宏、死字段和重复 ABI。

**本文保留的 20 项全部是 1.0 发布阻断。P0、P1、P2 只决定修复顺序，不表示任何条目允许延期。**

## 二、证据基准与有效性边界

### 2.1 当前 Git 与工作树基准

本轮逐代码复核使用以下基准：

- 当前分支：`master`
- 当前 HEAD：`ebfb45b42c6b866064d894acf710b58d6a6d6003`
- 参考祖先：`54f0fc144eb27264cb8d58a5da24c99ab9d429b5`
- 当前 tracked 差异：144 个文件，6741 行新增，5814 行删除
- 本文路径：`docs/reviews/2026-07-31-qcurl-engineering-review-release-blockers.md`
- 本文当前属于 untracked 文件，普通 `git diff HEAD` 不包含其内容

初次评审记录的 `143 files / 6740 additions` 已经漂移，证明只记录 shortstat 不能唯一标识 dirty tree。

后续所有 QA artifact 必须绑定以下完整快照：

1. 完整 HEAD SHA。
2. tracked patch 的稳定内容摘要。
3. 全部 untracked 相对路径、文件类型与逐文件内容摘要。
4. 子模块 SHA 与 dirty 状态。
5. CMake cache 中影响能力和 ABI 的选项。
6. Qt、编译器、CMake、libcurl、libabigail 和 sanitizer 版本。
7. 操作系统、架构、时间和执行命令。
8. 本文内容摘要。

仅记录 HEAD、`git diff --shortstat`、文件数量或人工状态文本均不满足证据身份要求。

### 2.2 历史方案与 QA 的使用边界

以下材料只用于追溯设计意图：

- `.helloagents/archive/2026-07/202607271049_qcurl_network_architecture_release_blockers_remediation/`
- `.helloagents/archive/2026-07/202607222333_qcurl_src_review_remediation/`
- `.helloagents/archive/2026-07/202607240132_qcurl_uncommitted_style_refactor/`
- `docs/reviews/2026-07-27-qcurl-final-integrated-architecture-review-conclusion.md`

历史方案仍然有效的方向包括：

- easy handle 从成功加入 multi 到安全 detach 期间由 transfer record 持有。
- HTTP Core 只承担 HTTP/HTTPS。
- 重试必须经过 method、幂等性和 body 可重放门禁。
- 缓存使用结构化请求 key、认证分区、有效出站 `Vary` 和原子文件提交。
- WebSocket 保持 Preview，复用现有 multi 事件驱动。
- persistent CONNECT_ONLY 在 `curl_easy_send()` 与 `curl_easy_recv()` 使用期间留在 `CURLM`。
- 手工 `permessage-deflate` 保持删除。

历史完成状态已经被当前源码反证：

- `contract.json` 仍写有 `implementation-complete` 和本地 gate 通过。
- `plan.md` 又声明 T8、全量 sanitizer 和真实一致性门禁未完成。
- 当前代码仍存在本文列出的 P0。

因此，历史 `78/78`、`101/101`、sanitizer manifest、ABI clean diff 和 closeout artifact 只能描述旧快照，不得为当前 dirty tree 放行。

### 2.3 本轮验证边界

本轮完成的是逐代码静态复核：

- 使用项目代码图谱定位架构关系，图谱查询失败或未覆盖 untracked 时回退到精确源码读取。
- 读取相关 Qt/C++ 实现、测试、CMake、ABI 脚本、release contract 和 vendored libcurl 文档。
- `git diff --check HEAD` 通过。
- `git diff --cached --check` 通过。

本轮没有重新构建，没有运行 CTest、libcurl consistency、public API consumer、ABI diff、UCE、ASan、UBSan、LSan 或 TSan。现有 `build/` 仍可能包含已经从源码删除的目标和对象，不能证明当前代码状态。

## 三、发布单元与兼容承诺

### 3.1 唯一发布单元

`CMakeLists.txt` 的默认 `install(TARGETS ...)` 同时包含：

- `QCurl`
- `QCurlBlockingExtras`
- `QCurlTestSupport`
- `QCurlOtherExtras`

`QCurlOtherExtras` 又编译 Diagnostics、Middleware Extras、WebSocket 和 `QCWebSocketPool`。

因此，本轮发布单元固定为：**默认安装与默认打包实际携带的全部 first-party target**。

所有进入默认包的运行时代码必须满足以下安全门禁：

- 无已知 UAF、double free、悬空 callback 和错误所有权转移。
- 无局部事件循环造成的公开 API 重入风险。
- 无锁内公开信号、锁内 QObject 生命周期操作和线程亲和性破坏。
- 无吞错后报告成功。
- 无畸形输入导致的无界分配。
- sanitizer 与 consumer gate 覆盖真实安装产物。

不得通过只检查 Core、关闭 WebSocket、跳过 Other Extras 或把缺陷标成 Preview 来形成 release PASS。

### 3.2 Stable API/ABI 范围

兼容承诺与安全门禁是两个不同维度：

- Core `QCurl::QCurl`：进入 `1.0.0` Stable API/ABI 合同。
- Blocking Extras：显式 opt-in，保持源码合同和安装 consumer gate，不进入 Core ABI baseline。
- Test Support：显式 opt-in，只承担测试用途和 consumer gate。
- Other Extras：保持 Preview，不承诺 1.0 二进制兼容，但必须通过发布安全、线程、生命周期和安装验证。

### 3.3 平台承诺

当前 ABI 工具链明确依赖 Linux ELF：

- 产物名为 `libQCurl.so.1.0.0`。
- 动态符号检查使用 `nm -D`。
- ABI 快照和比较使用 `abidw`、`abidiff`。
- CPack 主要产出 TGZ、DEB 和 RPM。

因此，`1.0.0` 的唯一 ABI 承诺固定为：**Linux ELF 平台上的 Core ABI**。

Windows 与 macOS 在对应原生 shared library、导出符号、consumer、工具链矩阵和 ABI 证据建立前，只承诺源码可移植性与构建兼容，不承诺二进制兼容。README 中“跨平台支持”必须明确这一边界。

## 四、不可变工程合同

### 4.1 Qt6 网络和 QObject 合同

1. 公开信号是同步可重入边界。直接连接槽能够立即修改状态、销毁 sender、销毁 parent、启动嵌套事件处理或调用同一对象的其他 API。
2. 信号前必须完成内部状态提交，并把信号 payload 拷贝或移动到局部值。
3. 每次信号返回后必须检查 sender guard。sender 已销毁时，当前 helper 与全部上层调用栈立即结束。
4. QObject 网络对象只在 owner thread 中创建、驱动和销毁。
5. Core 和 Other Extras 的异步主路径不得使用局部 `QEventLoop::exec()` 模拟同步脚本。
6. 每个异步操作必须恰好完成一次，成功、业务失败、调度失败、取消和 owner 销毁均映射为结构化结果。

### 4.2 libcurl binding 合同

1. `curl_multi_add_handle()` 成功后，transfer record 独占 easy、callback userdata、buffer、upload source、option backing storage 和 share 使用权。
2. `QCNetworkReply` 与 `QCWebSocket` 是可失效 observer，不拥有已注册到 multi 的 easy。
3. 只有 `curl_multi_remove_handle()==CURLM_OK` 才能证明 easy 已脱离该 multi。libcurl 对“已经移除”的 easy 同样返回 `CURLM_OK`。
4. `CURLM_RECURSIVE_API_CALL` 表示当前位于 libcurl callback，必须在 callback 完成后由 owner thread 重试。
5. 其他 remove 错误表示 ownership 不可证明，manager 必须进入 poisoned 状态并保留整个对象图。
6. persistent CONNECT_ONLY 在 send/recv 使用期持续留在 multi，不建立第二 transport。
7. 每个影响请求语义和安全性的 `curl_easy_setopt()`、`curl_multi_setopt()` 与 `curl_share_setopt()` 都必须检查结果。

### 4.3 HTTP 与缓存合同

1. 网络响应的外部可观察基准由 direct libcurl 提供：status、body、ordered raw headers、重复项和字段名大小写。
2. cache admission、freshness、revalidation、304 merge 和 replay 由 RFC 9111 与 QCurl 安全政策裁决，direct libcurl 不承担缓存 oracle。
3. ordered raw response headers 是唯一存储事实；规范化索引只用于查找，不得重建对外响应。
4. `Set-Cookie` 保持网络可观察并交给 cookie engine 处理，但禁止存储到 response cache，禁止 cache hit 重放。
5. `Vary` 使用实际有效出站 header。维度不可重现时 non-storable 且 non-reusable。
6. `Date`、`Age`、lifetime 和 expiration 使用饱和运算，不允许有符号溢出。

### 4.4 Qt6/KDE 应用库合同

1. 首个 Stable 版本前直接完成必要 hard-break，不创建临时双 API。
2. 所有 shared library target 默认 hidden visibility，仅 export macro 声明的公共 API 对外可见。
3. public 值结果统一使用 `QSharedDataPointer<Data>` 隐式共享，避免 public data member 和实现类型泄漏。
4. 默认发布门禁只读取受控 baseline，不得写入或刷新 baseline。
5. first-party target 使用 `QT_NO_KEYWORDS` 编译，源代码统一使用 `Q_SIGNALS`、`Q_SLOTS` 和 `Q_EMIT`。
6. 发布证据 fail closed；任何 malformed、缺字段、身份不匹配和工具失败均阻塞发布。

## 五、P0：内存安全、死锁与所有权阻断

### P0-1 QCNetworkReply 全信号调用栈存在释放后使用

**复核结论：准确，原 finding 范围需要扩大。**

**代码证据**

- `src/private/QCNetworkReplyState.cpp:117-140`：`stateChanged()` 后继续清理 flow control、解析 headers 并发射终态信号。
- `src/private/QCNetworkReplyState.cpp:92-111`：`error()` 与 `cancelled()` 后继续发 `finished()`。
- `src/private/QCNetworkReplyCache.cpp:205-229`：304 cache restore 发 `readyRead()` 后返回上层终态流程。
- `src/private/QCNetworkReplyRuntime.cpp:105-130`：`retryAttempt()` 后上层仍使用 reply private。
- `src/private/QCNetworkReplyCallbacks.cpp:95-115`：`readyRead()`、download progress 和 upload progress 位于真实 libcurl callback 栈。

**缺陷机制**

直接连接槽能够同步 `delete reply` 或删除其 parent。`QPointer` 只保护被 guard 的那一层；若 helper 返回后上层继续持有 raw private，仍然发生 UAF。仅在终态 helper 内增加一次 guard 不能闭合整个调用栈。

**唯一修订路线**

1. 定义内部 `SignalEmissionResult { Alive, Destroyed }`。
2. 每个信号前完成与该信号对应的全部状态提交。
3. payload 使用局部值，不在信号返回后从 private 重新读取。
4. 每次 emit 使用 `QPointer<QCNetworkReply>` guard。
5. emit helper 返回 `Destroyed` 时，上层逐层立即返回。
6. libcurl callback 只访问 transfer-record-owned state；reply observer 失效后不得解引用 reply private。
7. 对象被用户删除后不再尝试补发 `finished()`，该行为写入 public lifecycle 文档。

**布尔完成标准**

- 所有 Reply 公开信号都有同步删除 sender 与 parent 的测试。
- 任一信号删除对象后，没有后续公开信号、cache 操作、retry 安排或 multi 注册。
- 真实 local HTTP server、cache hit、304、错误、取消和重试均通过 ASan/UBSan。
- 测试断言 observable 顺序，而非只断言“没有崩溃一次”。

### P0-2 QCWebSocket 全状态机存在同类释放后使用

**复核结论：准确，必须按完整状态机修复。**

**代码证据**

- `src/private/QCWebSocketFrameAssembler.cpp:133-168`：消息信号后清理 fragment state。
- `src/private/QCWebSocketFrameAssembler.cpp:220-247`：Pong、Ping、close 信号后继续自动响应、切换状态和 cleanup。
- `src/private/QCWebSocketFrames.cpp:90-123`：frame processor 返回后继续读取 `d->state`。
- `src/private/QCWebSocketFrames.cpp:205-226`：`errorOccurred()` 后继续 queue close 或 cleanup。
- `src/private/QCWebSocketLifecycle.cpp:235-276`：`stateChanged()` 后继续发 `isValidChanged()`，error path 继续访问 private。
- `src/private/QCWebSocketReconnect.cpp:15-52`：`reconnectAttempt()` 后继续创建和启动 timer。

**缺陷机制**

任一直接连接槽同步删除 socket 或 parent 后，当前 helper 与上层 frame drain、handshake、reconnect 和 close 流程继续使用 `this/d/q`。

**唯一修订路线**

1. 与 Reply 共用 `SignalEmissionResult` 模型。
2. 完整 message payload 在 emit 前移到局部值，并提前清空 fragment state。
3. Ping/Pong/close payload 与 close code 在 emit 前冻结。
4. 每个 emit 后检查 guard；`Destroyed` 逐层传播到 receive drain、handshake、reconnect 与 cleanup caller。
5. 自动 Pong、close reply、timer 创建和重连安排只在 sender 仍存活时执行。
6. 不建立 worker WebSocket，不延迟全部信号，不增加第二 transport。

**布尔完成标准**

- `stateChanged`、`isValidChanged`、`connected`、消息、Ping、Pong、close、error、reconnect 和 `disconnected` 全部覆盖同步删除。
- partial control frame、自动 Pong、close timeout、远端 close 和 reconnect 均覆盖。
- persistent CONNECT_ONLY 在整个收发期间留在 multi。
- 真实 libcurl WebSocket 通过 ASan/UBSan/LSan。

### P0-3 QCWebSocketPool 同步 API、mutex 和局部事件循环形成死锁

**复核结论：准确，线程安全模型必须整体删除重建。**

**代码证据**

- `src/QCWebSocketPool.cpp:56-109`：`acquire()` 持 mutex，调用 `createNewConnection()`，并在锁内发信号。
- `src/QCWebSocketPoolMaintenance.cpp:25-61`：创建连接后运行局部 `QEventLoop` 等待信号。
- `src/QCWebSocketPool.cpp:155-188`：锁内执行 `close()`、`deleteLater()` 和 `connectionClosed()`。
- `src/QCWebSocketPoolMaintenance.cpp:135-143`：锁内发送所谓 keepalive。
- `src/QCWebSocketPoolMaintenance.cpp:179-212`：断线槽重取同一 mutex，并在锁内删除与发信号。

**缺陷机制**

连接失败、断线、clear、公开信号重入和 deferred delete 均能在局部事件循环内回到同一 pool。非递归 mutex 随即自锁。mutex 又不能使 socket、timer 和 notifier 跨线程安全，因此当前“线程安全”既不正确也不可证明。

**唯一修订路线**

1. `QCWebSocketPool` 固定为 owner-thread-only QObject。
2. 删除 pool mutex；所有容器和 QObject 操作只在 owner thread 执行。
3. 删除阻塞 `acquire()` 和 `preWarm()` public contract。
4. public API 固定为 `QFuture<QCWebSocketAcquireResult>` 与 `QFuture<QCWebSocketPreWarmResult>` 异步结果。
5. result 明确成功、容量上限、连接失败、取消、pool 销毁和非 owner thread 调用。
6. 所有公开信号在状态提交后发射，信号期间不持有任何锁。
7. cross-thread 调用立即完成为结构化 `WrongThread`，不透明阻塞投递。

**布尔完成标准**

- 源码中不存在 pool `QMutex`、`QMutexLocker` 和局部 `QEventLoop`。
- 每个 pool signal 中同步重入 acquire、release 和 clear 都不会死锁。
- 非 owner thread 调用没有 socket 副作用并返回 `WrongThread`。
- 断线、clear、pool 析构和连接超时均通过 watchdog。
- Clang ThreadSanitizer 作为强制并发门禁，不以普通压力测试替代。

### P0-4 multi detach 失败后仍释放 easy ownership

**复核结论：准确，原文把 `CURLM_BAD_EASY_HANDLE` 当作安全状态是错误的。**

**代码证据**

- `src/private/QCCurlMultiManagerTransfers.cpp:302-320`：remove 非成功后仍 take handle/completion、删除 record、递减 running count 和释放 share。
- `src/private/QCCurlMultiManagerShare.cpp:301-337`：cleanup path 具有相同释放行为。
- `curl/lib/multi.c:768-786`：已经移除返回 `CURLM_OK`；wrong multi 与坏 easy 返回 `CURLM_BAD_EASY_HANDLE`；callback 内 remove 返回 `CURLM_RECURSIVE_API_CALL`。
- `curl/docs/libcurl/curl_multi_remove_handle.md`：禁止在 libcurl callback 中 remove。
- `curl/docs/libcurl/curl_multi_cleanup.md`：顺序固定为 remove easy、cleanup easy、cleanup multi。

**缺陷机制**

remove 失败时 `CURLM` 仍可能引用 easy。此时释放 easy、record、callback userdata 或 share 会让下一次 socket action、timeout 和 multi cleanup 进入 UAF、double free 或 stale callback。

**唯一修订路线**

1. `CURLM_OK`：提交 detach，随后才允许转移或销毁 ownership。
2. `CURLM_RECURSIVE_API_CALL`：不改变 record、running count、observer、easy、share 和 completion；owner thread 在 callback 返回后重试。
3. 其他 `CURLMcode`：manager 永久进入 `Poisoned`。
4. `Poisoned` 后停止 socket/timer driver、拒绝新请求、拒绝继续驱动 multi，并向所有尚可观察的调用方返回统一内部错误。
5. poisoned multi、全部 easy、record、callback backing storage 和 share context 保持存活到进程退出；不得调用无法满足前置顺序的 `curl_multi_cleanup()`。
6. 未知 `CURLMSG_DONE` 只记录 invariant failure 并 poison manager，不猜测 easy ownership，不执行二次 cleanup。
7. 库不得 abort host process，不创建替代 `CURLM`，不恢复 easy perform 路线。

**布尔完成标准**

- 为 remove 注入 `CURLM_RECURSIVE_API_CALL`、`CURLM_INTERNAL_ERROR`、`CURLM_BAD_EASY_HANDLE` 和 `CURLM_BAD_HANDLE`。
- 只有 `CURLM_OK` 导致 record 数量、running count 与 share user count减少。
- callback 内删除 reply/socket 后，remove 在 callback 外完成。
- poison 后没有 socket action、新请求、multi cleanup、easy cleanup 和 share cleanup。
- ASan/UBSan/LSan 证明无 UAF 和 double free；poison 测试允许受控的进程期保留。

## 六、P1：协议、持久化、ABI 与证据阻断

### P1-1 缓存安全决策仍依赖有损单值 header map

**复核结论：准确。**

**代码证据**

- `src/private/QCNetworkReplyAccessors.cpp:334-383`：重复 header 通过 `QMap::insert()` 覆盖。
- `src/QCNetworkCache.cpp:15-44`：cache-control 与敏感 header helper 读取单值 map。
- `src/QCNetworkCache.cpp:130-150`：`setRawHeaders()` 虽保存 ordered raw list，仍同步生成最后值覆盖的 map。
- `src/QCNetworkCache.cpp:348-369`：cacheability 与 `Vary` 使用单值 map。
- `src/private/QCNetworkCacheIntegration.cpp:212-247`：metadata freshness 与 `Vary` 从 map 决策。

**缺陷机制**

较早的 `Cache-Control: no-store/no-cache`、多行 `Vary` 和冲突 `Date/Age` 能被后行覆盖。`Age + responseDelay` 还存在 `qint64` 溢出风险。缓存随后可能存储禁止响应、丢失 variant 维度或错误判定 freshness。

**唯一修订路线**

1. cache policy parser 直接消费 ordered raw header list。
2. 字段名查找使用大小写不敏感索引，索引值是原始位置列表，不是单值。
3. list 型字段按 RFC 字段语法合并全部行。
4. `Set-Cookie` 等非 list 字段保留每一行、顺序和原始值。
5. `Date`、`Age` 等单值字段出现冲突、重复或非法值时，响应 non-storable；已有 entry 立即 stale。
6. age、lifetime 和 expiration 全部使用显式饱和运算。
7. `QMap` 只保留为标注 lossy 的兼容观察视图，不参与 cache admission、freshness、Vary、304 merge 和 replay。

**布尔完成标准**

- 多行大小写混合 Cache-Control、Vary、重复 Set-Cookie、冲突 Date/Age 和 `Age=INT64_MAX` 均有具名测试。
- 任何较早行包含 `no-store` 时绝不 insert。
- 冲突单值字段不命中 fresh cache。
- sanitizer 下无整数溢出和越界。

### P1-2 cache oracle 与敏感响应政策未被准确区分

**复核结论：初次评审的风险方向正确，验证合同必须重写。**

**代码证据**

- `src/QCNetworkCache.cpp:39-44` 明确把 `Set-Cookie`、`Set-Cookie2`、`Authorization` 和 `Proxy-Authorization` 视为敏感响应头。
- `src/QCNetworkCache.cpp:348-360` 明确拒绝存储含敏感响应头的响应。

**缺陷机制**

把 direct libcurl raw response、cache insert、cache hit 和 304 merge 全部要求为同一 oracle，会与 QCurl 的安全政策冲突。libcurl 没有替 QCurl 定义 cache admission；响应 cache 也不得重放 `Set-Cookie`。

**唯一修订路线**

1. network path：QCurl 与 direct libcurl 对比 status、body、ordered raw headers、重复项和大小写。
2. cache admission：只按 RFC 9111 与 QCurl 敏感响应政策。
3. `Set-Cookie`：在网络响应上可观察并进入 cookie engine；该响应不得进入 cache。
4. cache hit：不得生成或重放 `Set-Cookie`。
5. 304 revalidation：网络收到的 cookie 由 cookie engine 处理，不持久化到 cached representation。
6. 测试报告分别记录 network parity 与 cache policy，禁止合并为一个布尔比较。

**布尔完成标准**

- direct libcurl 与 QCurl 的 network raw response 对重复 Set-Cookie 一致。
- 插入测试确认含 Set-Cookie 的响应无 cache entry。
- 后续请求没有来自 response cache 的 Set-Cookie 重放。
- 304 路径只更新允许合并的 representation metadata。

### P1-3 磁盘缓存 v3 在验证前执行无界容器分配

**复核结论：准确。**

**代码证据**

- `src/private/QCNetworkDiskCacheEntry.cpp:13-20`：当前 envelope version 为 3。
- `src/private/QCNetworkDiskCacheEntry.cpp:31-63`：inner payload 使用 `QDataStream >> QByteArray/QList/QMap`。
- `src/private/QCNetworkDiskCacheEntry.cpp:101-116`：outer payload 和 checksum 先通过长度编码容器读取，随后才检查 magic、version 和 checksum。

**缺陷机制**

QDataStream 容器操作符根据不可信长度申请内存。损坏、截断和人工放置的 `.qce` 文件能在 checksum 与配额验证前触发巨大分配、OOM 或长时间解析。

**唯一修订路线**

1. 格式固定为 envelope v4。
2. outer header 使用固定宽度字段：magic、version、flags、header length、payload length、SHA-256。
3. 读取固定 header 前先检查最小文件长度和最大文件配额。
4. payload length 必须等于文件剩余布局并小于 entry 配额。
5. checksum 使用受限长度流式计算，通过后才解析字段。
6. inner format 逐字段读取固定宽度 length/count；每次分配前校验字段配额、总配额、剩余字节和整数溢出。
7. 禁止对不可信 inner payload 使用 QDataStream 容器提取操作符。
8. v3 与其他版本固定为 miss，并尝试删除；删除失败仍拒绝再次解析。
9. 写入继续使用 `QSaveFile` 且禁止 direct-write fallback。

**布尔完成标准**

- 巨大 outer length、巨大 inner length、截断字段、过多 header、超大 body 和正确 checksum 的畸形 payload 全部 fail closed。
- 坏 entry 始终表现为 miss，不影响其他 entry。
- 内存峰值受测试配额约束。
- v3 不触发迁移代码。
- 原子提交中断后不存在可见半文件。

### P1-4 WebSocketPool keepalive 实际发送空文本业务帧

**复核结论：准确。**

**代码证据**

- `src/QCWebSocketPoolMaintenance.cpp:135-143`：`sendKeepAlive()` 调用 `sendTextMessage(QString())`。
- `src/QCWebSocket.cpp:100`：公共 API 已提供真实 `ping()`。

**缺陷机制**

空文本是应用数据，不是 Ping。服务端可能把它交给业务层；pool 又没有 nonce、Pong 匹配和 deadline，无法识别半开连接。

**唯一修订路线**

1. 每次 keepalive 使用有界随机 nonce 的真实 Ping control frame。
2. 每个 idle connection 记录 outstanding nonce、发送时刻和 deadline。
3. 只有完全匹配的 Pong 清除 outstanding 状态并更新 RTT。
4. 错误 nonce 只作为普通 Pong 事件，不刷新该 keepalive。
5. deadline 到期先从 pool 状态摘除，再异步关闭 socket。
6. 所有 frame 发送和 Pong 处理在 owner thread、无锁环境执行。
7. 不保留空文本 heartbeat compatibility。

**布尔完成标准**

- 应用层观察不到 keepalive text frame。
- 正确 Pong、延迟 Pong、错误 nonce、无 Pong 和断线均有测试。
- 超时连接从 pool 和统计中一致移除。
- keepalive 与 clear/disconnect 重入不死锁。

### P1-5 Cookie/share option 错误被吞掉且批量副作用不完整

**复核结论：准确，必须补充事务边界。**

**代码证据**

- `src/private/QCCurlMultiManagerCookies.cpp:188-189`：忽略 `CURLOPT_SHARE` 与 `CURLOPT_COOKIEFILE`。
- 同文件 `:231-241`：批量 import 中间失败前已经可能写入前序 cookies，FLUSH 又被忽略。
- 同文件 `:278-279`：export 继续忽略 SHARE 与 COOKIEFILE。
- 同文件 `:351-367`：clear 的 SHARE、COOKIEFILE 与最终 FLUSH 未完整检查。

**缺陷机制**

函数能够在配置失败、部分 cookie 已写入或持久化失败时返回成功，日志与实际 cookie store 状态分离。单纯统一 setopt helper 不能解决批量操作的部分提交。

**唯一修订路线**

1. SHARE、COOKIEFILE、COOKIELIST ALL 和请求的 FLUSH 全部标记为该操作的 required step。
2. 所有调用经统一 adapter 返回 `CURLcode`、option 名、稳定 policy code 和脱敏消息。
3. import 前完成全部 cookie 语法和 origin 校验，并快照当前 libcurl cookie list。
4. apply 中任一步失败时恢复快照。
5. 快照恢复失败时 poison 对应 share cookie context，后续操作统一失败，不报告不可信状态。
6. result 明确区分 `Applied`、`RejectedBeforeMutation`、`RolledBack`、`PersistenceFailed` 与 `StorePoisoned`。
7. 日志、同步内部入口和异步 public result 使用同一结果对象。

**布尔完成标准**

- 每个 required option 都有定点故障注入。
- 失败前后 cookie snapshot 可比较。
- rollback 失败进入 poisoned 状态。
- FLUSH 失败不得报告完整成功。
- 错误和日志不泄漏 cookie value、认证信息和本地隐私路径。

### P1-6 UCE timeline 解析 fail-open

**复核结论：准确，必须与状态 SSOT 分离。**

**代码证据**

- `tests/uce/timeline/common.py:48-55`：UTF-8 使用 `errors="replace"`，JSON 失败直接 `continue`。
- `tests/uce/timeline/validate.py:53-60`：具有相同 fail-open 行为。

**缺陷机制**

截断尾行、非法 UTF-8、非法 JSON 和非 object event 能从证据流消失，剩余事件继续通过合同，形成发布假绿。

**唯一修订路线**

1. UTF-8 严格解码。
2. 每个非空行必须解析为 JSON object。
3. 每个 event 必须通过版本化 schema 和 provider contract。
4. 任一错误生成结构化 `timeline_evidence_parse_error`，包含相对路径、行号、稳定 policy code 和脱敏摘要。
5. 解析器返回 value result；CLI 聚合所有错误后非零退出。
6. release gate 不提供 legacy 宽松模式，不提供 malformed evidence 转换工具。

**布尔完成标准**

- 非法 UTF-8、截断 JSON、array/scalar、缺 provider、未知 schema 和重复 identity 均失败。
- 错误定位稳定且不包含凭据。
- 任一坏行都使整个 artifact 不可接受。

### P1-7 发布状态与 artifact 身份存在双重真相

**复核结论：准确，原 P1-5 中的第二个独立 finding。**

**代码证据**

- 归档 `contract.json` 声称当前 dirty checkout 通过本地 Core release gate。
- 同方案 `plan.md` 又声明完整 release gate、sanitizer 和真实一致性证据未完成。
- 当前源码存在 P0，进一步证明人工状态文本已经失效。

**缺陷机制**

plan、contract、STATE、closeout 和 review 文档均能独立声明完成，且 artifact 没有绑定完整 dirty tree。人工文本能够覆盖真实 QA 结果。

**唯一修订路线**

1. 机器生成的 current QA manifest 是运行结果唯一 SSOT。
2. manifest 绑定第 2.1 节定义的完整源码和工具链身份。
3. contract 只保存不变量、required gates 和期望，不保存人工 PASS。
4. plan 与 STATE 只引用 manifest identity，不复制测试数字。
5. closeout 从 manifest 派生；identity 不匹配时自动失效。
6. 任何源码、测试、CMake、baseline、contract 或验证脚本变化都使旧 manifest 失效。

**布尔完成标准**

- 修改一个 tracked 字节、增加一个 untracked 文件、修改工具链版本都会使旧 artifact 被拒绝。
- plan、contract、STATE 和 closeout 不再存在可手工维护的并行 PASS 字段。
- 缺 artifact、坏 schema、identity mismatch 和 gate 缺失均 fail closed。

### P1-8 Core 动态导出面包含内部 helper

**复核结论：准确。**

**代码证据**

- `abi/baseline/qcurl-core-v1.abi.xml:246` 包含默认可见的 `QCurl::jitterFraction()`。
- 同文件 `:352` 包含默认可见的 `QCurl::equalJitterDelay(...)`。
- `src/CMakeLists.txt:251-263` 未设置 `CXX_VISIBILITY_PRESET hidden` 与 `VISIBILITY_INLINES_HIDDEN`。
- 当前动态符号检查主要针对 `QCCurlMultiTransferRecord` token，不是完整公共导出 allowlist。

**缺陷机制**

编译器默认可见性把实现 helper 纳入 ELF dynamic symbol table。baseline 随后把偶然导出冻结成 ABI，增加维护和兼容负担。

**唯一修订路线**

1. `QCurl` 与 `QCurlOtherExtras` shared target 设置 `CXX_VISIBILITY_PRESET hidden` 和 `VISIBILITY_INLINES_HIDDEN YES`。
2. Core 仅允许 `QCURL_EXPORT` 声明的 public API 导出。
3. Other Extras 仅允许 `QCURL_OTHER_EXTRAS_EXPORT` 声明的 opt-in API 导出。
4. Linux gate 从 public surface manifest 生成 demangled/mangled allowlist，与 `nm -D --defined-only` 比较。
5. helper、private type、moc 内部实现和未安装 header symbol 不得进入 Core allowlist。
6. 当前是首个 Stable 候选，直接 hard-break 清除泄漏 symbol，不提供 shim。

**布尔完成标准**

- 两个 jitter helper 与 transfer record 不再具有默认动态可见性。
- public shared consumer 仍能链接全部 manifest API。
- static consumer 不依赖 visibility 假设。
- allowlist 外任一 first-party symbol 导出都会使 gate 失败。

### P1-9 ABI baseline promotion 与 release gate 职责混合

**复核结论：原结论部分准确，必须修正事实表述。**

**代码证据**

- `scripts/qcurl_abi_gate.py:146-152` 的 `baseline` 命令能写 baseline，默认输出指向 tracked baseline。
- `scripts/qcurl_abi_gate.py:156-170` 的 `diff` 只生成 current snapshot 并比较。
- `scripts/release_gate_steps.py:191-202` 正式 full gate 调用 `diff`，不会自动调用 `baseline`。
- release contract 又要求从当前候选生成 baseline，再让当前候选对该 baseline clean diff，容易把自比较误写成兼容证明。

**缺陷机制**

问题不是 release gate 自动写 baseline，而是 baseline promotion 命令默认能覆盖受控文件，且首个 Stable 的 baseline 建立、export surface 审查与后续只读兼容 gate 没有明确分层。

**唯一修订路线**

1. release gate 永远只读受控 baseline。
2. 普通 `baseline` 命令默认只写 build 目录临时 snapshot，禁止默认写 tracked 路径。
3. baseline promotion 使用独立、显式命令，并要求 clean、不可变且已记录的 release-candidate commit、固定工具链和 Linux ELF Core 产物。
4. promotion 先生成临时 snapshot、动态导出 allowlist 报告和 old-to-new abidiff。
5. 人工审查 public surface 与 hard-break 报告后，以 baseline-only commit 提交受控文件；final release tag 只能创建在该提交及其后续全量 gate 均通过的 revision 上。
6. dirty tree、untracked source、未记录的 source revision、工具链漂移和未通过 consumer gate 时拒绝 promotion。
7. 首个 1.0 baseline 只证明冻结起点；真正发布证据还必须包含 allowlist、consumer、API manifest 和全量运行门禁。

**布尔完成标准**

- full release gate 无任何 tracked write。
- 不传显式 promotion 参数时无法修改 `abi/baseline/`。
- dirty tree 与未记录的 release-candidate commit 无法 promotion。
- baseline-only commit 附带可审计 old-to-new report 与 public surface 审查结果。
- final release tag 晚于 baseline-only commit，并绑定最终全量 gate artifact。

### P1-10 默认包安全门禁与 Preview 标签脱节

**复核结论：初次文档遗漏的发布合同阻断。**

**代码证据**

- `src/CMakeLists.txt:57-78` 把 Diagnostics 与 WebSocketPool 编入 `QCurlOtherExtras`。
- `CMakeLists.txt:187-198` 默认 install 同时安装 `QCurlOtherExtras`。
- `CMakeLists.txt:137-149` 提供强制关闭 WebSocket 的构建选项。

**缺陷机制**

若 gate 只检查 Core，默认包仍可能携带已知死锁和 UAF。若通过关闭 WebSocket 形成 PASS，gate 证明的是裁剪构建，不是默认能力构建。

**唯一修订路线**

1. 默认 package manifest 是发布安全 gate 的输入。
2. manifest 中每个运行时 target 必须有 shared/static consumer、生命周期测试和 sanitizer 证据。
3. WebSocket-capable libcurl 环境必须以默认启用配置通过 WebSocket 与 Pool gate。
4. force-disable 构建只用于能力矩阵的 negative variant，不得作为正式 release candidate。
5. Other Extras 保持 Preview ABI 标签，但不得带已知安全和线程缺陷发布。

**布尔完成标准**

- 默认 `cmake --install` 产物逐文件进入 manifest。
- 安装后的 Core、Blocking Extras、Test Support 和 Other Extras consumer 均通过。
- release manifest 不接受通过关闭默认能力得到的替代 PASS。

## 七、P2：公开 API 与工程一致性阻断

### P2-1 `QCNetworkCache::clear()` 无法表达失败

**复核结论：缺陷准确，原过渡 API 路线错误。**

**代码证据**

- `src/QCNetworkCache.h:191`：public virtual `clear()` 返回 `void`。
- `src/QCNetworkDiskCache.cpp:258-266`：忽略每个 `QFile::remove()` 结果并无条件设置 `currentSize=0`。

**缺陷机制**

权限与 I/O 失败后，旧 entry 仍存在并可能命中，但统计已经归零。调用方无法判断完整成功、部分失败和剩余容量。

**唯一修订路线**

1. 在 1.0 baseline 前直接改为 `[[nodiscard]] virtual QCNetworkCacheClearResult clear() = 0`。
2. 不增加 `clearChecked()`，不保留 void canonical API，不等待下一 major。
3. result 至少包含 `Status { Success, PartialFailure, Failure }`、removed count、failed count、remaining bytes、稳定错误码和脱敏消息。
4. disk clear 对每个 entry 记录结果，完成后重新扫描真实容量。
5. memory cache 返回确定的 success result。
6. custom cache consumer compile gate 必须实现新 virtual contract。

**布尔完成标准**

- 只读目录、单文件失败、部分成功和完整成功均返回正确 result。
- `cacheSize()` 与磁盘实际文件一致。
- public surface 中不存在 `clearChecked()` 和 void `clear()`。

### P2-2 Cookie async 在 manager 销毁窗口违反恰好一次完成

**复核结论：准确，signal 与 Future 双 completion 必须收敛。**

**代码证据**

- `src/QCNetworkAccessManagerCookies.cpp:107-137`：promise 只被以 manager 为 context 的 queued functor 持有。
- manager 在 functor 执行前销毁时，queued call 被移除，Future 没有结构化业务结果。
- `src/QCNetworkAccessManager.h:95-103` 返回 Future，`:370-372` 又为同一操作暴露三个结果 signal。

**缺陷机制**

并发调用缺少稳定关联，manager 销毁时又不可能发实例 signal。双 completion 使成功与销毁窗口使用不同观察方式，形成第二真相。

**唯一修订路线**

1. public per-call completion 只保留 `QFuture<Result>`。
2. 删除 `cookiesImported`、`cookiesExported` 和 `cookiesCleared` 结果 signal。
3. shared completion state 独立于 manager QObject 生命周期。
4. command 与 manager-destroyed 路径竞争原子完成同一 promise。
5. manager 销毁返回稳定 `ManagerDestroyed`，调度失败返回 `DispatchFailed`，业务错误保留具体 policy code。
6. operation 只完成一次；销毁后不执行 libcurl 副作用。

**布尔完成标准**

- queued 后立即销毁 manager，Future 正常 finished 且包含 `ManagerDestroyed`。
- 正常、调度失败、业务失败、取消和销毁竞争均恰好一个 result。
- public meta-object 与 ABI manifest 不再包含三个重复结果 signal。

### P2-3 Diagnostics 使用局部事件循环模拟同步脚本

**复核结论：准确，必须在首次发布前直接收敛。**

**代码证据**

- `src/QCNetworkDiagnostics.cpp:32-65` 与 `:103-136`：DNS 和 reverse DNS 运行局部 event loop。
- `src/QCNetworkDiagnosticsConnectivity.cpp:53-90`：TCP probe 运行局部 event loop。
- 同文件 `:111-151`：SSL probe 运行局部 event loop。
- 同文件 `:203-245`：HTTP probe 等待 reply 的局部 event loop。

**缺陷机制**

GUI/owner thread 调用会阻塞界面，并在同步函数内部处理任意 queued slot、timer 和 deferred delete。状态与对象生命周期能在调用返回前被意外改变。

**唯一修订路线**

1. Diagnostics public API 固定为 `QFuture<DiagResult>`。
2. DNS 使用异步 `QHostInfo::lookupHost()` 与可取消 completion state。
3. TCP/SSL 使用 socket signals 与 `QTimer` deadline。
4. HTTP 使用现有 `QCNetworkAccessManager/QCNetworkReply`，不建立 QtNetwork HTTP transport。
5. ping/traceroute 的 process 路径使用异步 `QProcess` signals。
6. 取消时终止对应 host lookup、socket、reply 或 process，并完成结构化 `Cancelled`。
7. 删除同步 Diagnostics public API，不新增 Blocking Diagnostics facade。

**布尔完成标准**

- Diagnostics 源码中不存在 `QEventLoop::exec()` 和 `waitFor*()`。
- GUI heartbeat 在全部诊断期间持续运行。
- timeout、cancel、manager 销毁、并发 diagnostics 和 process failure 均完成一次。
- HTTP probe 只经过 QCurl Core transport。

### P2-4 first-party Qt 宏规范没有编译期门禁

**复核结论：准确，原文列举范围严重不足。**

**代码证据**

tests、examples 与 benchmarks 中仍存在大量 `signals:`、`slots:` 和裸 `emit`。当前 first-party target 没有统一启用 `QT_NO_KEYWORDS`。

**缺陷机制**

依赖人工 regex 与示例清单会持续漏报，新 target 也能绕过规范。最高规范与真实可编译合同不一致。

**唯一修订路线**

1. 所有 first-party Core、Extras、测试、示例和 benchmark target 编译时启用 `QT_NO_KEYWORDS`。
2. 全部源码改为 `Q_SIGNALS:`、访问限定符加 `Q_SLOTS`、`Q_EMIT`。
3. `curl/` 与 `node_modules/` 明确排除，因为它们不是 first-party target。
4. regex 只用于检测未进入 CMake 的孤立源码，不替代编译门禁。

**布尔完成标准**

- 所有 first-party target 在 `QT_NO_KEYWORDS` 下构建。
- 排除第三方目录后的残留扫描为零。
- 新 target 未继承门禁时 CMake contract test 失败。

### P2-5 `QCNetworkAccessManagerPrivate` 保留废弃 multi 字段

**复核结论：准确，必须独立于宏清理。**

**代码证据**

`src/QCNetworkAccessManager_p.h:93-105` 仍保留：

- `replyList`
- `curlMultiHandle`
- `timer`
- `socketDescriptor`
- `readNotifier`
- `writeNotifier`
- `errorNotifier`

全树精确引用只出现在构造初始化与字段声明；真实 multi driver 已由 `QCCurlMultiManager` 和 socket-info 结构承担。

**缺陷机制**

废弃字段制造第二 ownership 叙事，误导维护者认为 manager private 仍持有 multi、timer 和 notifier，并增加初始化与审查噪音。

**唯一修订路线**

删除字段、构造初始化和过时注释，不增加 alias，不保留占位符。

**布尔完成标准**

- 字段名在 `QCNetworkAccessManagerPrivate` 中全部消失。
- manager 初始化、析构、scheduler、cache 和 middleware 测试不回归。

### P2-6 `QCNetworkReply::deleteLater()` 重复扩大 public ABI

**复核结论：准确，必须在 1.0 baseline 前删除。**

**代码证据**

- `src/QCNetworkReply.h:308` 重复声明 QObject 已提供的 public slot。
- `src/private/QCNetworkReplyAccessors.cpp:257-260` 仅转发 `QObject::deleteLater()`。

**缺陷机制**

wrapper 没有增加语义，却产生派生类符号与 meta-object entry，扩大 public API/ABI 和文档维护面。

**唯一修订路线**

直接删除派生声明与实现。调用方继续通过继承的 `QObject::deleteLater()` 使用同一源码调用形式，不提供 wrapper、alias 或 shim。

**布尔完成标准**

- public header、moc metadata 与动态符号中不存在派生 wrapper。
- consumer 仍能编译 `reply->deleteLater()`。
- ABI hard-break report 明确记录删除项。

## 八、隐含路线分叉的唯一裁决

下表中的“被否决路线”不得保留为当前实现、兼容开关、迁移期实现或 release 例外。

| 问题 | 被否决路线 | 唯一路线 |
| --- | --- | --- |
| multi remove 失败 | 把 `BAD_EASY_HANDLE` 当安全成功；销毁或重建 multi；abort host | 仅 `CURLM_OK` 释放；recursive 延迟重试；其他错误 poison 并保留完整图 |
| Reply/WebSocket signal | 只在单个 emit 加 `QPointer`；延迟全部信号 | 状态先提交、局部 payload、逐 emit guard、`Alive/Destroyed` 逐层传播 |
| WebSocketPool | 保留同步 API再新增 async；mutex 继续存在 | owner-thread-only、异步 value result、删除同步 API与 mutex |
| Pool 并发证据 | 普通压力测试替代 TSan | Clang TSan 加 watchdog，二者均为门禁 |
| cache oracle | direct libcurl 同时裁决网络与缓存 | direct libcurl 只裁决网络可观察；RFC 9111 与安全政策裁决缓存 |
| Set-Cookie | 为一致性写入 cache | 网络可观察并进入 cookie engine；response cache 永不存储与重放 |
| disk format | v3 兼容迁移；坏 entry 隔离后继续解析 | 固定 v4；非 v4 一律 miss并删除；删除失败仍拒绝解析 |
| keepalive | 空文本 heartbeat；失败后长期禁用验证 | 真实 Ping nonce、匹配 Pong、固定 deadline |
| Cookie completion | signal 与 Future 并行 | `QFuture<Result>` 是逐调用完成 SSOT |
| Diagnostics | async 与同步 facade 并存；新增 Blocking Diagnostics | 仅保留异步 `QFuture<DiagResult>` |
| cache clear | `clearChecked()` 过渡到下一 major | 1.0 前直接 hard-break canonical `clear()` |
| symbol control | 把 export allowlist 与 version script 保留为并列方案 | target hidden + export macro；Linux allowlist作为验证，不增加 version script |
| ABI 泄漏 | 临时 versioned shim | 首个 Stable 前直接删除泄漏 symbol |
| baseline | gate 自动刷新 baseline；dirty build promotion | gate 只读；clean tagged commit 走独立 promotion |
| 发布范围 | 只发 Core；关闭 WebSocket后放行；Preview 豁免安全门禁 | 默认安装产物是安全 gate 单元；Core 单独承担 Stable ABI |
| 平台 ABI | 用 Linux ELF 证据宣称跨平台 ABI | 1.0 只承诺 Linux ELF Core ABI |
| Qt 宏门禁 | 把 regex 与编译门禁保留为并列方案 | `QT_NO_KEYWORDS` 编译门禁为主，regex只补孤立源码 |
| P1/P2 处置 | 按优先级延期 | 本文全部 finding 在 1.0 发布前关闭 |

## 九、固定实施顺序

### 9.1 冻结证据身份

1. 生成第 2.1 节定义的完整 dirty-tree manifest。
2. 记录当前 20 项 finding 的测试映射。
3. 将历史 artifact 标记为 expired，不修改其历史内容。

### 9.2 先建立失败测试

在修改实现前先加入以下红测：

1. Reply 每个公开信号同步删除 sender 与 parent。
2. WebSocket 每个公开信号同步删除 sender 与 parent。
3. Pool 信号重入、disconnect、clear 与 watchdog。
4. multi remove 全部错误码注入。
5. duplicate raw headers、冲突单值字段和 Age 饱和边界。
6. Set-Cookie network parity 与 cache rejection。
7. v4 hostile length、checksum、截断和字段配额。
8. Cookie required option、partial apply 与 rollback failure。
9. malformed UCE 与 artifact identity mismatch。
10. manager 销毁窗口下 Cookie Future 完成。

### 9.3 关闭 P0

严格按以下顺序实施：

1. multi detach 状态机、callback 外重试与 poison containment。
2. Reply 全信号 `Alive/Destroyed` 传播。
3. WebSocket 全状态机 `Alive/Destroyed` 传播。
4. WebSocketPool owner-thread 异步模型。

P0 未在真实 multi/WebSocket、ASan、UBSan、LSan 和 TSan 下通过前，不修改发布状态。

### 9.4 修复协议与持久化

1. ordered raw cache policy parser 与饱和时间运算。
2. network parity/cache policy 双 oracle 测试。
3. bounded disk envelope v4。
4. Ping/Pong nonce 与 deadline。
5. Cookie required option、snapshot、rollback 与 poison。

### 9.5 收敛异步和公开 API

1. `QCNetworkCacheClearResult clear()` hard-break。
2. Cookie `QFuture<Result>` 单 completion。
3. Diagnostics `QFuture<DiagResult>` 异步主 API。
4. 删除 manager private 废弃字段。
5. 删除 Reply `deleteLater()` wrapper。
6. 全 first-party target 启用 `QT_NO_KEYWORDS`。

### 9.6 冻结导出面与证据链

1. Core 与 Other Extras target hidden visibility。
2. export macro 与 Linux dynamic symbol allowlist。
3. UCE timeline fail closed。
4. QA manifest 绑定完整 dirty-tree identity。
5. public API manifest、consumer 与 hard-break report 通过。
6. clean tagged candidate 执行独立 baseline promotion。

### 9.7 fresh deep QA

必须创建全新 shared、static、Clang、sanitizer 和安装验证目录，不复用当前 build tree。

首个证明点固定为：真实 libcurl multi 下同步删除 Reply/WebSocket，并注入 remove failure；ASan/UBSan 通过，且只有 `CURLM_OK` 释放 ownership。

证伪条件固定为：任一路径允许 easy 在成功 detach 前释放，任一公开信号返回后继续访问已销毁 sender/private，任一默认安装目标未进入安全 gate。

止损规则固定为：实现切片未满足不变量时保持发布 `BLOCKED` 并撤销该切片，不启用第二 transport、兼容 API、宽松解析、feature-disable 放行、ABI shim 或 baseline 刷新来掩盖失败。

## 十、完整验证矩阵

### 10.1 生命周期与所有权

- 真实 local HTTP server 与 libcurl multi。
- Reply callback/signal 中同步删除 sender 和 parent。
- WebSocket callback/signal 中同步删除 sender 和 parent。
- `CURLM_RECURSIVE_API_CALL` 延迟重试。
- 非预期 multi remove error poison containment。
- persistent CONNECT_ONLY 始终留在 `CURLM`。
- ASan、UBSan、LSan。

### 10.2 Pool 与线程

- owner-thread 正常 acquire/preWarm/release/clear。
- non-owner-thread 结构化失败且无副作用。
- 每个公开 pool signal 中重入所有 pool API。
- connect timeout、disconnect、clear 与析构 watchdog。
- Clang ThreadSanitizer。
- 源码静态禁止 `QEventLoop`、`QMutex` 和锁内 QObject 操作。

### 10.3 HTTP/cache/libcurl 一致性

- direct libcurl 与 QCurl network status/body/ordered raw headers。
- 重复 headers、大小写和不可合并字段。
- 请求与响应 `no-store/no-cache/max-age=0`。
- 多行 `Vary` 与实际有效出站 header。
- `Date/Age/current-age` 和饱和边界。
- 304 merge、HEAD representation、streaming body 与 PreferNetwork。
- Set-Cookie network 可观察、cache non-storable 和 non-replay。

### 10.4 Disk cache

- v4 golden round-trip。
- v3 与未知版本 miss/delete。
- 固定 header、文件配额和逐字段配额。
- 巨大长度、截断、checksum mismatch 和正确 checksum 的畸形 inner payload。
- QSaveFile 原子提交中断。
- clear 完整成功、部分失败和容量重扫。
- ASan/UBSan/LSan 与内存峰值断言。

### 10.5 Cookie

- SHARE、COOKIEFILE、COOKIELIST ALL 与 FLUSH 定点失败。
- import 前校验、部分 apply、rollback 成功和 rollback 失败 poison。
- export、clear 与持久化失败结果。
- manager queued 后销毁、调度失败、取消和并发完成。
- Future 恰好一次 result。
- 日志与错误脱敏。

### 10.6 Diagnostics

- DNS、reverse DNS、TCP、SSL、HTTP、ping、traceroute 异步成功与失败。
- timeout、cancel、owner 销毁和并发操作。
- GUI heartbeat 不停顿。
- 不存在 nested event loop、waitFor API 和第二 HTTP transport。

### 10.7 Public API、ABI 与安装

- Core public API fast/slow consumer。
- Blocking Extras、Test Support 与 Other Extras consumer。
- shared/static build 与 install-tree smoke。
- public surface manifest 和 Doxygen。
- `QT_NO_KEYWORDS` 全 first-party target 编译。
- dynamic symbol allowlist。
- Linux ELF Core baseline `abidiff`。
- deleteLater、Cookie signals、clear hard-break consumer checks。
- 默认能力构建与 negative capability matrix 分开记录。

### 10.8 发布证据

- malformed JSONL 非零退出。
- QA manifest 绑定 HEAD、tracked digest、untracked digest、子模块和工具链。
- artifact identity mismatch 自动失效。
- contract、plan、STATE 和 closeout 不维护并行 PASS。
- `git diff --check HEAD`。
- untracked 文本和 C/C++ 文件独立执行空白与格式检查。
- full release gate、UCE、libcurl consistency、ABI gate 在最终源码变化之后重新执行。

## 十一、发布完成门禁

只有以下项目全部为真，才允许把状态从 `BLOCKED` 改为候选 `PASS`：

- [ ] 20 项 finding 均有独立提交范围、具名测试和布尔完成证据。
- [ ] 四个 P0 在真实 libcurl multi/WebSocket 下通过 ASan、UBSan、LSan 和 TSan。
- [ ] multi ownership 只有 `CURLM_OK` 触发释放，poison path 不破坏对象图。
- [ ] Reply 与 WebSocket 全部公开信号通过同步删除测试。
- [ ] WebSocketPool 不含 mutex、局部事件循环和同步 acquire/preWarm。
- [ ] cache 决策不再读取 lossy QMap，Set-Cookie 不进入 response cache。
- [ ] disk cache 仅接受有界 v4，v3 不迁移。
- [ ] Cookie required option、rollback、poison 和 Future completion 全部闭合。
- [ ] Diagnostics public API 全异步且无第二 transport。
- [ ] public API hard-break 在 1.0 baseline 前完成，不存在双 canonical API。
- [ ] Core 与 Other Extras 默认 hidden，Linux dynamic symbol allowlist 通过。
- [ ] baseline promotion 与 release gate 完全分离，gate 无 tracked write。
- [ ] 默认安装产物的全部 first-party target 通过 shared/static consumer 与安全门禁。
- [ ] ABI 承诺明确限制为 Linux ELF Core。
- [ ] UCE 与 QA artifact fail closed，并绑定完整 dirty-tree identity。
- [ ] fresh build 产生的新证据晚于最后一次源码、测试、CMake、文档合同和 baseline 变化。
- [ ] 当前 commit/tag/CI run 与 release asset、checksum、ABI report 和 QA manifest 可追溯关联。

任何一项未满足，发布状态保持 `BLOCKED`。

## 十二、最终结论

QCurl 当前已经形成较完整的 Qt6/libcurl 网络库主体，现有单 multi、HTTP/HTTPS Core、统一重试、结构化 cache key、persistent CONNECT_ONLY 和 WebSocket Preview 方向应继续保留。

当前阻断不在功能数量，而在企业级库最关键的可证明合同仍未闭合：

- QObject 公开信号的同步重入安全。
- libcurl multi/easy/share/callback 的唯一 ownership。
- owner-thread 网络对象与无嵌套事件循环的异步模型。
- HTTP ordered multi-value semantics 与安全缓存政策。
- 不可信磁盘格式的分配前边界。
- 每个失败路径的结构化结果与恰好一次 completion。
- 首个 Stable API/ABI 的最小导出面和受控 baseline。
- 默认安装产物与真实源码快照绑定的 fail-closed 发布证据。

唯一正确路线是：冻结证据身份，先建立 P0 红测，依次关闭 multi ownership、Reply/WebSocket 重入和 Pool 线程模型，再修复缓存、磁盘、Cookie 与 Diagnostics，随后一次性完成 1.0 public API/ABI hard-break，最后使用全新构建目录对默认安装产物执行完整 deep QA。

在该路线全部通过之前，历史 PASS、旧 build tree、Core-only gate、关闭 WebSocket、Preview 标签、兼容 shim、宽松证据解析和刷新 baseline 都不能替代发布闭环。当前发布裁决保持 **BLOCKED / 需要重大修订**。
