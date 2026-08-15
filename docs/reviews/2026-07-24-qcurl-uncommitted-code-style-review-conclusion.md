# QCurl 未提交代码审查最终综合结论

> **结论性质**：只读审查、一次复核与综合裁决的固化记录。本文是后续重构方案的唯一审查依据，不代表本轮已经修改源码。
>
> **审查日期**：2026-07-24
>
> **审查对象**：当前 QCurl 父仓未提交工作树，以及 `Qt6_CPP17_Coding_Style` 子模块指针变化。

## 1. 审查范围与权威基线

本轮按以下三份文档审查，专题规则优先于总纲摘要：

- [`Qt6_CPP17_Coding_Style.md`](../../Qt6_CPP17_Coding_Style/cn/Qt6_CPP17_Coding_Style.md)
- [`Qt6_KDE_API_Parameter_Style.md`](../../Qt6_CPP17_Coding_Style/cn/Qt6_KDE_API_Parameter_Style.md)
- [`Qt_Macro_Layout_Coding_Style.md`](../../Qt6_CPP17_Coding_Style/cn/Qt_Macro_Layout_Coding_Style.md)

本次实际审查边界包括：

- 父仓当前 147 个未提交路径，重点覆盖 `src/`、`tests/qcurl/`、public-header consumer smoke 和与本轮变更直接相关的配置文件。
- 子模块 `Qt6_CPP17_Coding_Style` 从父仓记录的 `335b07e` 移到 `fec2b3c`；子模块自身工作树干净，`fec2b3c` 的主题为 `docs(style): 明确 Qt PIMPL 成员命名规则`。
- 代码结构图只用于导航和影响面定位；最终判断以工作树中的源代码、差异和已有构建/测试证据为准。

规范中的关键分类已经冻结如下：

1. 普通类 `private`/`protected` 非静态成员使用 `m_`。
2. 只有满足 `Foo`/`FooPrivate` 配对、内部未导出、承担 PIMPL 职责等条件的私有类，其 `public:` 状态成员才使用无前缀小驼峰。
3. 数据型 `struct` 的 public 字段使用无前缀小驼峰；不能因为字段位于实现文件就套用普通类的 `m_`。
4. 静态成员使用 `s_`；`q_ptr`、`d_ptr` 以及 `Q_D`/`Q_Q` 生成的 `d`、`q` 是 Qt 固定名称，不参与普通成员命名检查。
5. `QObject` 及其派生类采用指针语义；释放使用 parent ownership 或所属线程的 `deleteLater()`，禁止手动 `delete`。

## 2. 最终总裁决

当前未提交代码**不能直接判定为规范完成，也不应在现状下作为最终提交点**。复核后仍有两项当前差异必须修复：

| 优先级 | 当前问题 | 结论 |
|---|---|---|
| 规范门禁阻断 | 合法 PIMPL public 状态成员和数据型 `struct` 字段被统一改成 `m_` | 必须按类型/访问级别重新分类；不能用全局 `m_` 规则覆盖例外 |
| 生命周期阻断 | `tst_QCNetworkActorThreadModel.cpp` 新增 `delete reply;` | 必须移除手动删除，恢复 parent ownership，并验证 deferred delete 在所属线程被处理 |

除此之外，上一轮复核确认的行为修复和 API 取舍应保留，不得为了处理上述两项而回退。全树格式化失败和大文件拆分属于已知维护性债务，不应被误报为本轮新回归，但本方案会给出有边界的后续拆分路线。

## 3. 必须修复的当前差异

### 3.1 PIMPL 与数据型字段的命名分类回归

**判断**：这是规范分类错误，不是普通成员命名风格选择。当前 diff 把本应无前缀的 public 状态字段批量改成了 `m_`，与三份权威文档的明确例外相反。

已确认的代表位置如下（实际修复前仍需做完整 AST/语义清单）：

| 文件 | 当前区域 | 需要恢复的语义 |
|---|---|---|
| `src/QCNetworkReply_p.h` | `QCNetworkReplyPrivate` public 状态区（约第 86-231 行） | `request`、`httpMethod`、`state`、缓冲区、配置缓存等 public PIMPL 状态不应以 `m_` 命名 |
| `src/QCWebSocket_p.h` | `QCWebSocketPrivate` public 状态区（约第 55-149 行） | WebSocket 状态、URL、计数、定时器、TLS/压缩缓存和 notifier 字段使用无前缀小驼峰 |
| `src/QCNetworkAccessManager_p.h` | `QCNetworkAccessManagerPrivate` public 状态区（约第 85-116 行） | cookie、scheduler、cache、notifier、middleware 等私有实现状态按 PIMPL 例外命名 |
| `src/QCNetworkMockHandler_p.h` | `QCNetworkMockHandlerPrivate` 及其数据描述类型 | PIMPL public 状态与数据型 `struct` 字段分别按对应例外处理 |
| `src/QCCurlMultiManager.h` | `SocketInfo`、`AddReplyResult`、`FinishedTransfer`、`ShareConfig`、`ShareContext` | `struct` public 字段不能被机械加上 `m_`；嵌套数据类型也要按字段语义分类 |
| `src/private/QCRequestPipeline_p.h` | `RequestBody`、`NormalizedRequest`、`CurlPlan`（约第 27-78 行） | 这些是数据快照/计划类型，字段恢复为无前缀小驼峰 |

修复合同：

- 先建立完整分类清单，再逐项重命名并更新所有引用；不能只修复表中示例。
- `FooPrivate` 的 `private:`/`protected:` 成员仍保留 `m_`；不能通过移动访问级别规避规则。
- 普通 public API 类、普通内部类和真正的 private/protected 成员继续使用原有 `m_` 约定。
- `q_ptr`、`d_ptr`、局部 `d`/`q` 保持 Qt 固定名称；不得将它们改写成 `m_`。
- 变更应限制在命名和引用更新，不得借机改变请求状态、线程归属、curl 资源所有权或信号时序。
- 对 private header 的白盒测试、digest/helper 和 consumer smoke 做编译回归；不得把“能编译”当作分类证明，必须有静态门禁或等价的 AST 检查。

风险说明：这批字段多数不属于稳定 public ABI，但命名会影响内部测试、模板访问和维护者对状态所有权的判断。错误地把普通成员恢复成无前缀，或把合法 PIMPL 继续保留为 `m_`，都会使规范门禁失真。

### 3.2 测试中的 QObject 手动删除

`tests/qcurl/tst_QCNetworkActorThreadModel.cpp:55-95` 的故障注入场景在 `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)` lambda 中取得 reply，并在第 87 行执行：

```cpp
delete reply;
```

该 reply 是异步网络对象，正常路径由 manager 的 parent ownership 管理；手动删除违反 Qt 生命周期规则，并可能与 parent 析构、queued/deferred 事件或后续清理产生双重释放和悬挂引用风险。

唯一修复路线：

1. 保持 reply 的 manager parent ownership，不在测试 lambda 中手动删除，也不为 reply 建立第二条独立释放路径。
2. 测试结束时在 manager 所属线程安排 `manager->deleteLater()`，由 parent tree 统一回收 reply；在退出 actor thread 前显式等待 manager 与 reply 的 deferred delete 被处理。
3. 使用 `QPointer`/`destroyed` 或等价可观察证据确认 manager 与 reply 均已回收，再结束线程；不能只依赖 `actorThread.quit()` 后“推测”事件已经处理。
4. 保留故障注入的行为断言：三个失败点都必须进入 `Error`，`error`/`finished` 各发射一次，active reply 数量和 running request 计数回到零。

## 4. 已复核为正确、不得撤销的改动

以下改动与规范和既有行为合同一致，后续重构必须保留：

- 可能失败的 public/internal API 增加 `[[nodiscard]]`，包括 cookie 操作、缓存删除、curl option 配置、pause mask、压缩/解压和 multi 注册辅助函数等。
- `QCNetworkRequest(const QUrl &)` 改为 `explicit`；这是已接受的源码兼容性变化，不增加隐式转换兼容层、deprecated 过渡重载或旁路工厂。直接构造仍应可用，并以 `std::is_constructible_v`/`!std::is_convertible_v` 编译期合同保护。
- `Q_EMIT`、`Q_SIGNALS:`、`Q_SLOTS` 的统一方向，以及将 `Q_DISABLE_COPY_MOVE` 放入 `private:` 的布局方向。
- `QCWebSocket::isValid` 的 `NOTIFY isValidChanged(bool)` 及其“仅在是否 Connected 的布尔值实际变化时发射”的逻辑；现有 `stateChanged(State)` 不得删除或改型。
- multi 初始化、回调配置、easy-handle 注册失败的显式错误终态和确定性故障注入覆盖；不能为了修测试生命周期而恢复“失败后 reply 留在 Running”的旧行为。
- slist 临时指针追加、部分链表失败时释放、header/关键 `curl_easy_setopt` 错误传播；cleanup/reset 的 best-effort 语义不能被误改成静默吞错。
- Public API 参数未发现新的 Borrow/view 生命周期问题；Qt meta-object/QML 边界仍使用 owning 类型，view 不应被保存、延迟捕获或跨线程传播。

## 5. 既有债务与本轮边界

以下项目在复核中确认存在，但**不是上述两项当前差异的新增回归**：

### 5.1 Qt 布局与测试旧写法

- `Q_DECLARE_PUBLIC`、`q_ptr` 的布局属于 Qt PIMPL 固定合同，不因字段命名修正而重排。
- 部分测试仍使用旧式 `private slots:`。若本轮触及同一测试类，可在局部改为 `private Q_SLOTS:`；不启动与本次目标无关的全仓宏迁移。
- `Q_DISABLE_COPY_MOVE` 的目标是显式诊断和规范一致性，不意味着当前 QObject 突然变成可复制类型。

### 5.2 格式化统计

- 143 个变更 C/C++ 文件中有 82 个整文件 `clang-format` 检查失败，主要是历史格式债务。
- `git clang-format --diff HEAD` 已通过，`git diff --check` 已通过。
- 当前方案只要求新改动行和新增拆分文件符合格式；不执行全树格式化，也不把 82/143 作为本轮阻断门禁。

### 5.3 文件规模与后续重构建议

- `src/private/QCNetworkReplyCurlOptions.cpp` 当前约 1685 行，多个配置函数约 241-476 行。建议按 base/network、method/body/header、proxy/auth、HTTP version、TLS、timeout/callback 拆成职责清晰的内部模块，每个模块保留统一的失败传播入口。
- `src/QCWebSocket.cpp` 当前约 1021 行。建议按生命周期与状态机、握手配置、帧收发、压缩统计、关闭与重连拆分，保持 `setState()` 作为状态和通知的单一入口。
- `src/private/QCCurlOptionAdapter_p.h` 当前约 307 行，测试 hook 暂不强制拆分；后续只在职责边界清楚且不引入第二套 option 包装时处理。
- 大文件拆分属于本方案的次级维护任务，必须在行为合同和 ABI 检查通过后进行；不能以“重构需要”掩盖命名或生命周期缺陷。

## 6. 已有验证证据与复核限制

审查/复核记录中已有以下证据：

- `git diff --check` 通过。
- `git clang-format --diff HEAD` 通过；整文件格式统计单独记录为历史债务。
- `QCurl`、`QCurlOtherExtras` 构建通过。
- `qcurl_public_header_self_compile`、Core/Other Extras consumer smoke 通过。
- `tst_QCNetworkActorThreadModel`、`tst_QCBlockingRequestConfig`、`tst_QCNetworkNetworkPath`、`tst_QCNetworkProxy`、`tst_QCWebSocket` 通过。
- 四个本地端口场景首次受沙箱 AF_INET 限制；在允许回环端口后均通过。该环境事实不应被误写成代码失败。

这些是审查阶段已有证据，不替代实施命名和生命周期修正后的回归。方案执行阶段必须重新跑适用的 GCC/Clang、Qt Test、public-header/consumer、ABI 和格式门禁，并保留失败点与环境前置条件。

## 7. 单一路线与完成门槛

后续执行必须按以下顺序推进：

1. 固定本审查文档和 `fec2b3c` gitlink 基线，生成完整字段分类清单。
2. 修正 PIMPL/data struct 命名并通过静态分类门禁。
3. 移除测试中的手动 QObject 删除，建立所属线程 deferred-delete 证据。
4. 仅在触及文件内收敛 `Q_EMIT`/`Q_SIGNALS`/`Q_SLOTS`、copy/move 宏位置和枚举尾逗号；不得开启全仓格式迁移。
5. 在行为修复稳定后拆分 `QCNetworkReplyCurlOptions.cpp` 和 `QCWebSocket.cpp`，不新增平行 curl option 抽象。
6. 执行深度 QA：GCC/Clang、定向故障矩阵、Qt Test、consumer smoke、public API/ABI、changed-lines format 和 `git diff --check`。
7. 只有所有强制门槛通过后，才形成父仓提交建议；本轮不提交 Git，子模块内容不单独再次提交。

完成必须同时满足：

- 合法 PIMPL public 状态和数据型 public 字段不再被错误使用 `m_`，普通 private/protected 成员仍保持 `m_`。
- 故障注入测试不再手动删除 QObject，deferred delete 在所属线程完成且无泄漏/双重释放证据。
- 已确认的 `[[nodiscard]]`、`explicit`、`isValidChanged(bool)`、multi 错误传播和 public 参数语义全部保留。
- 全部新增/修改行通过 changed-lines 格式检查，历史整文件格式债务单独记录。
- 深度 QA 和 ABI/consumer 检查通过；未通过时不得以刷新 baseline 或删除断言掩盖问题。

本文件只保存结论和执行边界；本轮没有修改 `src/`、`tests/` 实现代码，也没有执行 Git 提交。
