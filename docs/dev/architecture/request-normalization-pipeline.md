# 请求归一化管线（internal-only）

面向维护者说明当前 method/body 计划的职责。管线不是 public API，不安装或导出；它不是所有策略、libcurl options 和执行上下文的总编译器。

## 1. 数据路径与代码位置

```text
QCNetworkRequest + HttpMethod + RequestBody
  -> normalizeRequest(): NormalizedRequest
  -> compileRequest(): CurlPlan
  -> QCNetworkReply 执行器应用计划
```

- 声明与纯值描述：[QCRequestPipeline_p.h](../../../src/private/QCRequestPipeline_p.h)。
- 实现：[QCRequestPipeline.cpp](../../../src/private/QCRequestPipeline.cpp)。
- manager 入口：[QCNetworkAccessManagerSend.cpp](../../../src/QCNetworkAccessManagerSend.cpp)；reply 构造处生成计划：[QCNetworkReply.cpp](../../../src/QCNetworkReply.cpp)。
- 异步调度位于[私有 scheduler](../../../src/private/QCNetworkRequestScheduler.cpp)，不再引用旧的 public 源码位置。

## 2. 阶段职责

`RequestBody` 表达 Empty、InlineBytes、Device，携带 custom method、大小以及借用设备描述。构造设备描述时不跨线程读取设备；basePos/seek 能力在执行期通过 owner-thread 检查后采样。

`normalizeRequest()` 复制 request、method 和 body，形成值快照，不在这里运行 middleware、mock、cache 或 scheduler，也不集中产生所有 capability warnings。

`compileRequest()` 只决定 method flags、customRequest、transferMode、bodySize 与 hasRequestBody。HEAD/GET/POST 与 device PUT 的 flag 区分、custom method 原字节、内联/设备 body 选择由这一纯计划表达。

headers/slist、TLS/代理、重试、运行时 lease、callback 和具体 option 安装仍由 reply/Blocking 执行器及共用 helper 承担。Blocking Extras 使用独立同步 value-result 路径，**当前不调用这份异步 CurlPlan**；不把两执行器虚写为同一实现路径。

## 3. 维护边界

- 新增 method/body 形态在本管线维护；具体传输选项放在对应内部 adapter，不把同一映射复制到 scheduler。
- scheduler 管理已创建 reply 的 admission，不另编译一份请求计划；direct 与 scheduled 异步请求共用计划。
- NormalizedRequest/CurlPlan 不进入安装头、动态导出面或下游 API。
- `QCURL_ENABLE_TEST_HOOKS` 只在非安装 TestInternals companion 中暴露计划 digest，正式 runtime 不启用。

## 4. 验证

[RequestPipeline QtTest](../../../tests/qcurl/tst_QCNetworkRequestPipeline.cpp)验证计划字段和摘要；其范围是异步 direct/scheduler method/body 一致性，不是 Blocking 全路径或全部 libcurl options 的证明。跨执行器外部表现用[libcurl 一致性专题](../../../tests/libcurl_consistency/README.md)及对应 QtTest 检查，命令见[构建与测试](../build-and-test.md)。
