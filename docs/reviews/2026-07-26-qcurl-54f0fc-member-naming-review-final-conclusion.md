# commit `54f0fc144eb27264cb8d58a5da24c99ab9d429b5` 成员命名审核与逐代码复核最终结论

> 日期：2026-07-26  
> 性质：只读审核结论  
> 状态：最终修订版  
> 说明：本文综合初次审核和逐代码复核结果，并取代此前两轮零散结论。本文不代表相关源码、测试或门禁已经完成修复。

## 一、最终裁决

提交中的 `m_` 字段不能按“位于 `public` 就统一去前缀”的方式处理。正确规则是：

1. **先判断类型是否获准采用 public 直接字段模型。**
2. **再依据访问级别决定命名。**
3. record-like、内部 PIMPL、内部 shared-data 等获准类型的 public 字段使用无前缀小驼峰。
4. 普通行为类必须封装状态；其 private/protected 状态继续使用 `m_`。
5. 不能通过扩大 `public` 作用域规避成员封装规则。

本次对 136 个变更 C/C++ 文件进行扫描，共识别出 **92 个 `m_` 非静态字段定义**：

| 分类 | 数量 | 最终裁决 |
|---|---:|---|
| private 非静态状态字段 | 52 | 命名正确，保留 `m_` |
| record-like 类型的 public 字段 | 30 | 改为无前缀小驼峰 |
| `SchedulerQueues` 行为类的 public 状态字段 | 10 | 不直接改名；先封装并转为 private，继续保留 `m_` |

此外还发现：

- `RequestOptionStorage` 存在**声明与使用名称不一致的真实编译错误**。
- `tst_QCNetworkProxy.cpp` 存在 **10 处错误 PIMPL 字段引用**。
- 当前命名门禁虽然通过，但不能发现全部未登记 record-like 类型和普通行为类的 public 状态。
- 10 个待登记 record-like 类型中，有 9 个缺少类型级中文 Doxygen 注释。

因此，提交当前状态**不能认定为已完成 Qt 成员命名规范收敛，也不能认定为覆盖全部 libcurl 配置的可编译状态**。

## 二、审查对象与权威基线

### 2.1 Git 范围

- 目标提交：`54f0fc144eb27264cb8d58a5da24c99ab9d429b5`
- 父提交：`0960482eedbb968f5c1acfdccaabfccf060ab33e`
- 审核时 `HEAD`：目标提交
- 审核时工作树：干净
- `git diff --check`：通过
- 规范子模块提交：`870c84e79501a9afe02ba7abd67006912f6be31d`

### 2.2 权威规范

- `Qt6_CPP17_Coding_Style/cn/Qt6_CPP17_Coding_Style.md`
- `Qt6_CPP17_Coding_Style/cn/Qt6_KDE_API_Parameter_Style.md`
- `Qt6_CPP17_Coding_Style/cn/Qt_Macro_Layout_Coding_Style.md`
- 注释补充规范：`Qt6_CPP17_Coding_Style/cn/CPP_Code_Comment_Guidelines.md`

其中：

- 总纲负责成员访问模型和命名规则。
- 参数规范补充内部类型、Public API、源码兼容和 ABI 边界。
- 宏布局规范确认 PIMPL、`Q_DECLARE_PRIVATE`、`Q_DECLARE_PUBLIC` 等 Qt 固定边界。
- 注释规范要求类、结构体和非直观数据结构具有简洁、用途明确的 Doxygen 中文说明。

## 三、最终采用的成员命名判定规则

### 3.1 获准采用 public 直接字段的类型

只有以下类型经过语义确认后，才允许直接公开字段：

- 纯数据承载、解析结果、执行结果、快照或配置存储类型。
- 合法的内部 `FooPrivate` PIMPL 类型。
- 合法的内部 Qt shared-data 实现类型。
- 已明确批准为字段式接口的 record-like 类型。

这些类型的 public 非静态字段使用：

```cpp
QString errorMessage;
qint64 bytesReceived = 0;
```

不得使用：

```cpp
QString m_errorMessage;
qint64 m_bytesReceived = 0;
```

### 3.2 普通行为类

manager、scheduler、controller、service、worker 等具有状态不变量和状态变更行为的类型，默认必须封装。

其 private/protected 状态使用：

```cpp
QList<Request> m_pendingRequests;
```

不能仅因为字段当前位于 `public:`，就改成：

```cpp
QList<Request> pendingRequests;
```

### 3.3 固定例外

以下 Qt 固定名称保持不变：

- `q_ptr`
- `d_ptr`
- 局部变量 `q`
- 局部变量 `d`

静态成员使用 `s_`。本次没有发现需要重新分类的 static `m_` 字段，也没有 protected 字段偏差。

## 四、内部 PIMPL 的最终结论

当前门禁识别并检查了 15 个 PIMPL 类型。逐代码复核没有发现新的 PIMPL public `m_` 字段定义缺口。

`QCNetworkReplyPrivate` 是本次最明确的验证案例。其 public 状态字段已经正确使用无前缀小驼峰：

- `src/QCNetworkReply_p.h:116`：`errorCode`
- `src/QCNetworkReply_p.h:117`：`errorMessage`
- `src/QCNetworkReply_p.h:150`：`capabilityWarnings`

因此，测试代码中对 `m_errorCode`、`m_errorMessage` 和 `m_capabilityWarnings` 的访问是测试错误，不能据此反向要求 PIMPL 字段改回 `m_`。

同时必须明确：

- `SchedulerQueues` 不是 PIMPL。
- `RequestOptionStorage`、`HeaderSink` 等是 record-like 数据类型，不是 PIMPL。
- 类型位于 `private/`、名称带 `Private` 或使用 `struct`，都不能单独决定字段模型。

## 五、30 个应去除 `m_` 前缀的字段

以下 10 个类型已经逐代码确认属于内部 record-like 类型。它们只承担数据承载、解析结果、执行结果或消息快照职责，不维护复杂行为不变量。

| 类型与位置 | 当前字段 | 目标字段 | 数量 |
|---|---|---|---:|
| `QCMultipartField`，`src/QCMultipartFormData.cpp:24-33` | `m_name` | `name` | 1 |
| `RequestOptionStorage`，`src/private/QCBlockingCurlRequestSetup_p.h:22-40` | `m_connectToList` | `connectToList` | 1 |
| `HeaderSink`，`src/private/QCBlockingCurlAdapter.cpp:37-41` | `m_failureMessage` | `failureMessage` | 1 |
| `BlockingExecution`，`src/private/QCBlockingCurlAdapter.cpp:171-178` | `m_responseBody`、`m_responseHeaders`、`m_bytesReceived`、`m_httpStatus`、`m_code` | `responseBody`、`responseHeaders`、`bytesReceived`、`httpStatus`、`code` | 5 |
| `ContentRangeInfo`，`src/private/QCNetworkResumableDownloadWriter.cpp:29-34` | `m_start`、`m_end`、`m_total` | `start`、`end`、`total` | 3 |
| `ReplySnapshot`，`src/private/QCNetworkRequestSchedulerQueue_p.h:35-41` | `m_lane`、`m_hostKey`、`m_priority` | `lane`、`hostKey`、`priority` | 3 |
| `ReplyOutcome`，同文件 `:43-47` | `m_cancelled`、`m_bytesReceived` | `cancelled`、`bytesReceived` | 2 |
| `FinalizeResult`，同文件 `:55-62` | `m_snapshot`、`m_wasTracked`、`m_emitCancelled`、`m_emitFinished`、`m_shouldKickQueue` | `snapshot`、`wasTracked`、`emitCancelled`、`emitFinished`、`shouldKickQueue` | 5 |
| `ReplyProgressState`，同文件 `:64-70` | `m_lastBytesReceived`、`m_lastBytesSent`、`m_downloadConnection`、`m_uploadConnection` | `lastBytesReceived`、`lastBytesSent`、`downloadConnection`、`uploadConnection` | 4 |
| `SchedulerQueues::QueuedRequest`，同文件 `:138-145` | `m_requestId`、`m_key`、`m_reply`、`m_snapshot`、`m_queueTime` | `requestId`、`key`、`reply`、`snapshot`、`queueTime` | 5 |

合计：**30 个字段**。

这些重命名必须同步处理：

- 字段声明。
- 全部成员访问。
- lambda 和模板中的访问。
- 聚合初始化后的读取。
- 单元测试和辅助测试。
- 命名门禁授权清单。

这些类型均为内部实现类型，不应增加旧名称别名或双字段兼容层。

## 六、`RequestOptionStorage` 编译阻断

`src/private/QCBlockingCurlRequestSetup_p.h:37` 当前声明为：

```cpp
curl_slist *m_connectToList = nullptr;
```

实际实现却访问 `connectToList`：

- `src/private/QCBlockingCurlAdapter.cpp:259-260`
- `src/private/QCBlockingCurlRequestSetup.cpp:343`
- `src/private/QCBlockingCurlRequestSetup.cpp:349`
- `src/private/QCBlockingCurlRequestSetup.cpp:353`

这不是单纯的命名风格问题，而是声明与调用方已经发生分裂。

正确修复是把声明统一为：

```cpp
curl_slist *connectToList = nullptr;
```

理由：

1. `RequestOptionStorage` 是 record-like 类型，目标命名本来就应无前缀。
2. 现有使用点已经采用正确目标名称。
3. 不应反向把全部使用点改成 `m_connectToList`。
4. 该字段负责保存需要在请求执行期间持续存活并最终释放的 `curl_slist`，修复时不得改变清理顺序和所有权。

该问题必须作为最高优先级编译阻断处理。

## 七、`SchedulerQueues` 的修订裁决

`src/private/QCNetworkRequestSchedulerQueue_p.h:133-232` 中的 `SchedulerQueues` 是行为类，不是 record-like 类型。

它负责：

- pending、deferred、running 队列维护。
- lane 配置管理。
- host 与 lane 连接计数。
- reservation 选择。
- DRR 调度状态。
- 运行状态清理和重置。
- 请求入队、取出和运行状态迁移。

其以下 10 个 public 字段不能机械去除 `m_`：

```cpp
m_pendingRequests
m_deferredRequests
m_runningRequests
m_hostConnectionCount
m_runningLaneCount
m_runningLaneHostCount
m_laneConfigs
m_laneOrder
m_laneDeficit
m_laneLastStartedHost
```

最终目标应是：

```cpp
private:
    QList<QueuedRequest> m_pendingRequests;
    QList<QueuedRequest> m_deferredRequests;
    // ...
```

但不能只在第 173 行之前插入 `private:`。复核发现约 110 个活动直接访问点，分布在调度器主实现、Control、Private、Queries、Queue、LanePolicy 和 RuntimePruner 等实现切片。

唯一正确路线是：

1. 建立最小内部查询和变更接口。
2. 将入队、出队、计数、lane 状态更新收敛到 `SchedulerQueues`。
3. 逐个迁移外部实现切片的直接字段访问。
4. 仅为确有紧密算法耦合的策略保留必要 friend。
5. 所有外部直接访问清零后，将 10 个字段转为 private。
6. 字段转为 private 后继续保留 `m_`。

`SchedulerQueues::QueuedRequest` 与其外层类不同。`QueuedRequest` 只保存一次队列项的数据，属于 record-like 类型，其 5 个字段应去除前缀。

## 八、条件编译测试中的 10 处错误 PIMPL 引用

错误位于：

```text
tests/qcurl/tst_QCNetworkProxy.cpp:433-491
```

具体数量：

- `m_errorCode`：4 处。
- `m_errorMessage`：3 处。
- `m_capabilityWarnings`：3 处。

应分别恢复为：

```cpp
errorCode
errorMessage
capabilityWarnings
```

这些错误位于以下条件分支：

- 未定义 `CURLPROXY_HTTPS`。
- libcurl 版本低于 `7.52.0`。
- 对应能力缺失的警告和失败路径。

现代 libcurl 环境可能只编译已经使用正确字段名的 `#else` 分支，从而掩盖问题。因此：

- 当前主机测试通过不能证明这些分支可编译。
- 这属于 libcurl 配置矩阵敏感的编译回归。
- 正确修复对象是测试引用，不是 `QCNetworkReplyPrivate` 字段声明。

## 九、52 个应保留的 private 字段

复核确认 52 个 private 非静态字段符合 `m_` 命名要求，不应去除前缀。

代表位置包括：

- `QCCurlHandleManager`：`src/QCCurlHandleManager.h:76-77`
- `QCCurlMultiManager`：`src/QCCurlMultiManager.h:325-356`
- `QCNetworkRequestScheduler::m_impl`：`src/QCNetworkRequestScheduler.h:368`
- `SchedulerQueues` 的 private 调度游标：
  - `m_hostReservationCursor`
  - `m_globalReservationCursor`
  - `m_bestEffortCursor`
- `QCSingleFileMultipartBodyDevice` 的 private 设备状态。
- `QCByteDataBuffer`：`src/qbytedata_p.h:73-75`
- 测试夹具中的 private 测试状态。
- `QCErrorHandlingMiddleware`
- `QCSigningMiddleware`
- `QCUnifiedRetryPolicyMiddleware`

这些字段属于封装状态、资源句柄、运行计数、缓存或测试夹具状态。去除 `m_` 会直接违反规范。

## 十、命名门禁的准确评价

审核时执行：

```bash
python3 scripts/check_qt_member_naming.py --source-root .
```

结果为：

```text
Qt 直接字段授权门禁通过: 15 个 PIMPL、11 个 record-like、44 个 shared-data 类型
```

该结果应准确解释为：

> `qcurl_qt_member_naming_guard` 正确检查了现有授权清单，但其项目级覆盖不完整。它自动发现 PIMPL 和 shared-data，却不自动发现未登记的 record-like 类型，也不全局拒绝普通行为类的 public `m_` 状态。因此 PASS 不能证明整个项目完成了字段语义分类。

不能把问题表述为“脚本错误地放行”。脚本按当前规则正确工作，缺陷在于覆盖模型不完整。

后续门禁必须增加全局 AST 扫描：

1. 枚举所有 public 非静态 `m_` 字段。
2. 每个字段必须进入语义分类。
3. 获准的 record-like、PIMPL、shared-data 类型应报告命名违规。
4. 普通行为类应报告 public 状态封装违规。
5. 禁止仅根据 `struct`、`Private`/`Data` 后缀、内部路径或 `QSharedData` 继承自动授权。
6. 不允许存在“未分类但未报错”的 public `m_` 字段。

## 十一、中文 Doxygen 注释依赖

把上述 10 个 record-like 类型临时加入授权检查后，得到：

- 10 组命名违规。
- 30 个待重命名字段。
- 9 个类型级中文 Doxygen 注释缺口。

只有 `QCMultipartField` 已有合格注释：

```cpp
/// 表示一个 multipart part，文本字段和内存文件字段共用同一结构。
```

其余类型应补充简洁的类型用途说明。建议表达的语义如下：

- `RequestOptionStorage`：保存一次阻塞请求配置期间必须保持存活的 libcurl 选项数据。
- `HeaderSink`：承接 libcurl 响应头回调输出及失败信息。
- `BlockingExecution`：保存一次阻塞请求执行累计的响应数据和 libcurl 结果。
- `ContentRangeInfo`：表示解析后的 HTTP `Content-Range` 字节区间。
- `ReplySnapshot`：保存请求入队时供调度和信号使用的稳定快照。
- `ReplyOutcome`：保存 reply 结束或取消时提取的结果摘要。
- `FinalizeResult`：保存 reply 收尾后需要执行的通知和队列动作。
- `ReplyProgressState`：保存单个 reply 的进度值及相关信号连接。
- `SchedulerQueues::QueuedRequest`：保存队列项的标识、reply、快照和入队时间。

普通 `//` 成员注释或文件级 `@file` 注释不能替代类型级 `///` Doxygen 说明。

## 十二、兼容性与风险边界

### 12.1 Public API 与 ABI

上述字段均位于匿名命名空间、`.cpp`、private header 或内部实现类型中。只要：

- 不改变字段类型。
- 不改变字段顺序。
- 不改变对象持有关系。
- 不把内部类型暴露到 public header。
- 同步修改全部内部引用。

则字段重命名通常不改变 QCurl Public API 或公开 ABI。

### 12.2 聚合初始化

多个 record-like 类型使用聚合初始化。字段重命名本身不改变聚合初始化顺序，但重构不得顺便调整字段顺序，否则可能改变初始化语义。

### 12.3 `SchedulerQueues`

`SchedulerQueues` 封装属于内部架构调整，公开 ABI 风险低，但调度行为风险高。必须验证：

- 请求顺序。
- lane 公平性。
- host 连接上限。
- reservation。
- DRR deficit。
- pending/deferred/running 状态转换。
- 取消和完成后的计数回收。

### 12.4 禁止兼容别名

不应保留 `m_name` 与 `name`、`m_snapshot` 与 `snapshot` 等双字段或引用别名。它们会制造重复状态、初始化顺序风险和门禁歧义。

## 十三、唯一修复顺序

1. **先修复编译阻断**
   - `m_connectToList` 声明统一为 `connectToList`。
   - 修复 `tst_QCNetworkProxy.cpp` 的 10 处错误 PIMPL 引用。

2. **完成 30 个 record-like 字段重命名**
   - 按类型原子化修改声明和全部访问点。
   - 不增加兼容别名。

3. **补充 9 个中文 Doxygen 类型注释**
   - 注释说明用途和数据边界。
   - 不逐字段重复代码可以直接表达的信息。

4. **重构 `SchedulerQueues` 访问模型**
   - 建立最小查询和变更接口。
   - 迁移外部直接状态访问。
   - 将 10 个状态字段转为 private 并保留 `m_`。

5. **增强命名门禁**
   - 加入 10 个 record-like 类型或实现自动语义候选扫描。
   - 新增所有 public 非静态 `m_` 字段的全局 AST 分类门禁。

6. **执行格式、构建和回归闭环**
   - GCC 与 Clang 构建。
   - 全量 CTest。
   - 标准 libcurl 一致性测试。
   - 扩展能力 libcurl 一致性测试。
   - 旧版或能力缺失 libcurl 条件分支编译验证。
   - ABI 门禁。
   - 命名门禁。
   - 格式和 `git diff --check`。

## 十四、最终验收门槛

- [ ] `RequestOptionStorage` 声明和全部访问统一使用 `connectToList`。
- [ ] `tst_QCNetworkProxy.cpp` 不再引用不存在的 `m_errorCode`、`m_errorMessage`、`m_capabilityWarnings`。
- [ ] 10 个 record-like 类型的 30 个 public 字段全部使用无前缀小驼峰。
- [ ] 9 个缺失类型已经具有简洁、符合 Doxygen 风格的中文注释。
- [ ] `SchedulerQueues` 的 10 个状态字段全部转为 private，并继续使用 `m_`。
- [ ] 全局不存在未分类的 public 非静态 `m_` 字段。
- [ ] 命名门禁不再只依赖现有授权清单。
- [ ] GCC 与 Clang 全量构建通过。
- [ ] 全量 CTest 无失败、无未解释跳过。
- [ ] 标准和扩展 libcurl 一致性测试全部通过。
- [ ] libcurl 能力缺失和旧版本条件分支完成编译验证。
- [ ] ABI 门禁、格式检查和 `git diff --check` 全部通过。

## 十五、综合最终结论

初次审核关于“合法 PIMPL 和 record-like public 字段应使用无前缀小驼峰”的方向是准确的，但“所有 public `m_` 字段都应直接去前缀”的范围过宽。

逐代码复核后的最终分类是：

- **52 个 private 字段保留 `m_`。**
- **30 个 record-like public 字段去除 `m_`。**
- **10 个 `SchedulerQueues` public 状态字段不直接改名，而是封装为 private 后保留 `m_`。**
- **10 处测试 PIMPL 错误引用恢复为现有无前缀字段名。**

门禁通过仅证明现有授权清单内部一致，不能证明整个项目已经完成语义分类。提交当前仍包含真实编译阻断、配置敏感测试编译回归、行为类公开状态以及 Doxygen 注释缺口，因此尚不满足最终代码风格和全配置验收要求。

历史深度 QA 仅作为命名语义和门禁实践背景；本文中与目标提交有关的数量、位置和分类以 Git、源码及逐代码复核结果为准。
