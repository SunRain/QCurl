# Request Normalization Pipeline（internal-only）

> 本文面向维护者，说明 QCurl 当前 internal request pipeline 的职责边界。该流水线不属于 public API，不承诺对下游稳定。

## 1. 目标

将所有发送路径统一收敛到同一条 internal-only 流水线：

```text
QCNetworkRequest + method/body/context
  -> NormalizedRequest
  -> CurlPlan
  -> QCNetworkReply / scheduler / sync path 执行
```

核心目的：

- 消除 `send*` / scheduler / sync 之间的重复映射；
- 让默认值派生、warnings、libcurl option 编译有单一事实来源（SSOT）；
- 为测试提供稳定的 plan digest 白盒断言。

## 2. 代码位置

- 声明：`src/private/QCRequestPipeline_p.h`
- 实现：`src/private/QCRequestPipeline.cpp`
- 调用入口：
  - `src/QCNetworkAccessManager.cpp`
  - `src/QCNetworkReply.cpp`
  - `src/QCNetworkRequestScheduler.cpp`

## 3. 阶段职责

### normalize()

输入：

- `QCNetworkRequest`
- HTTP method
- request body / upload source
- manager 上下文（logger / middleware / mock / cache / scheduler 相关上下文）

输出：

- `NormalizedRequest`

职责：

- 固化请求语义与默认值
- 收敛 body 形态（inline bytes / upload device / file path）
- 统一 follow redirect / timeout / TLS / proxy / auth / retry / cache / priority 等配置
- 在单一位置产生 capability warnings

### compile()

输入：

- `NormalizedRequest`

输出：

- `CurlPlan`

职责：

- 编译 headers / slist / option 值
- 统一 libcurl option 设置前的准备
- 保持敏感信息处理与 callback 绑定口径一致

## 4. 边界约束

- `NormalizedRequest` / `CurlPlan` 仅供库内实现使用，不安装、不导出。
- public headers 不得暴露这些 internal 类型。
- scheduler 路径不得旁路编译：所有异步 `send*()` 与 scheduler 出队都必须复用同一 plan。
- 新增配置项时，优先补到 normalize/compile，而不是在 reply/manager/scheduler 各写一份映射逻辑。

## 5. 验证口径

- Qt Test：`tests/qcurl/tst_QCNetworkRequestPipeline.cpp`
- test hooks：仅 `QCURL_ENABLE_TEST_HOOKS` 构建可见 plan digest
- 回归目标：
  - 不同入口在相同输入下生成相同 digest
  - scheduler / direct send / sync send 无旁路差异

