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

共同请求配置由独立纯值模块 [QCNetworkRequestPolicy](../../../src/private/QCNetworkRequestPolicy_p.h) 解释，覆盖协议、redirect、header/auth、proxy、range、限速、verify/CA、connect/total timeout 及 HTTP version intent。`compileRequestPolicy()` 在准备后的快照上确定生效性与首错，成功才产生可绑定结果；`decideHttpVersionPolicy()` 只接收调用方在原 HTTP 检查点采样的运行时事实，不采样环境或持有 probe。

async 的 `QCReplyCurlConfiguration` 在现有 holder 内同时持有 method/body CurlPlan 和共同策略结果；Blocking 的本地 context 只消费共同策略，**不调用异步 CurlPlan**。两个执行器保留各自 setopt 顺序、线程、错误呈现和 native backing。策略不拥有整个请求、body、执行对象或通用 CURLOPT 指令列表。headers/slist、TLS 高级配置、callback、重试和 runtime lease 仍由原模块/adapter 应用。

middleware、认证头与缓存准备先完成，纯值校验随后作用于实际快照；cache/mock 命中不能绕过拒绝。纯值失败在任何共同 option 之前返回，native 首错在各执行器原插点停止。默认值、生效条件、诊断脱敏与原生重定向保护边界见 [binding 合同](libcurl-binding-contract.md#共同请求策略) 和[用户配置](../../user/configuration.md)。

## 3. 维护边界

- 新增 method/body 形态在本管线维护；具体传输选项放在对应内部 adapter，不把同一映射复制到 scheduler。
- scheduler 管理已创建 reply 的 admission，不另编译一份请求计划；direct 与 scheduled 异步请求共用计划。
- NormalizedRequest/CurlPlan 不进入安装头、动态导出面或下游 API。
- `QCURL_ENABLE_TEST_HOOKS` 只在非安装 TestInternals companion 中暴露计划 digest，正式 runtime 不启用。

## 4. 验证

[RequestPipeline QtTest](../../../tests/qcurl/tst_QCNetworkRequestPipeline.cpp)验证计划字段和摘要；其范围是异步 direct/scheduler method/body 一致性。[StringContracts](../../../tests/qcurl/tst_QCStringContracts.cpp) 通过生产策略 seam 验证首错、生效性和原生数值边界；BlockingRequestConfig、Proxy、CompletionContract 及 HttpSemantics 使用真实线路、故障注入和生产工厂时序验证两个 adapter。跨执行器外部表现另见[libcurl 一致性专题](../../../tests/libcurl_consistency/README.md)，命令见[构建与测试](../build-and-test.md)。
