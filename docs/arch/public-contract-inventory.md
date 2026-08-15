# Public Contract Inventory

## 目的

本清单冻结当前全部安装头的四类公共合同：可失败 API、错误生命周期、QObject 裸指针借用
以及 owning 智能指针 ABI。范围同时覆盖 Core、Blocking Extras、Other Extras 和 Test Support，
不以默认 Core 安装面代替完整 public surface。

机器可读真源是
[`tests/public_api/public_contract_inventory.json`](../../tests/public_api/public_contract_inventory.json)。
`surface_manifest.json` 中每个非 Internal 头都必须在四个审查轴恰好分类一次；分类为
`contracts` 时必须引用详细合同。每个详细合同固定当前签名、目标签名、状态与生命周期、线程
约束、唯一处置方式和执行阶段。

## T6 Hard-Break Allowlist

T6 只能修改机器清单 `hardBreakAllowlist` 中列出的合同：

- Request redirect/transfer configuration 的可失败事务 setter。
- RetryPolicy validated factory 与事务 setter。
- Scheduler 公共命令的结构化接受结果。
- ConnectionPoolManager 配置结果和语义不实的 idle-close API。
- DefaultLogger 文件输出与写入结果。
- WebSocket command acceptance 结果。
- manager/reply 的 opaque logger handle 和 snapshot retention。

不得借 T6 扩大到其他兼容性清理。错误生命周期和 QObject borrow 合同统一在 T7 补齐；
Scheduler/MultiManager wrong-thread fallback 的实现删除在 T8 完成。

Scheduler 命令结果已统一为 `[[nodiscard]] QCNetworkRequestScheduler::CommandResult`：

- `scheduleReply()`、`deferPendingRequest()`、`undeferRequest()`、`cancelRequest()`、
  `cancelAllRequests()`、`cancelLaneRequests()` 与 `changePriority()` 只报告同步接受状态。
- `Applied` 不表示 reply 已完成；执行、取消完成和最终错误仍以 reply 状态及 scheduler 信号为准。
- `NoChange` 表示命令合法但无需改变状态；其他拒绝结果保证不改变 reply、队列或统计。
- wrong-thread 调用同步返回 `WrongThread`，不再排队，也不捕获或读取 reply `QPointer`。
- `cancelLaneRequests()` 通过可选输出参数返回已提交取消数量；失败和 `NoChange` 均写入 `0`。

## 验证

执行以下命令校验安装面覆盖、合同引用和 allowlist 一致性：

```bash
python3 tests/public_api/run_public_api_checks.py public-contract-inventory \
  --inventory tests/public_api/public_contract_inventory.json \
  --surface-manifest tests/public_api/surface_manifest.json \
  --source-root src
```
