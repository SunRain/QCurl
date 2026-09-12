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

Scheduler 的正式命令入口位于 manager，返回 `[[nodiscard]] SchedulerCommandResult`：

- `deferScheduledRequest()` / `undeferScheduledRequest()` 只在 Pending / Deferred 间转换；
  `setScheduledRequestPriority()` 只改变 Pending；`cancelScheduledRequest()` 解除跟踪并安排取消。
- Applied 表示本次同步提交，不表示传输完成或重入后的最终状态；NoChange 不产生变化通知。
- 错误线程先拒绝；随后校验参数、manager 绑定、已跟踪对象亲和性和状态。其他 manager 与
  未跟踪请求统一 NotTracked，拒绝没有请求、队列、统计或通知副作用。
- `cancelLaneRequests()` 保留结构化计数结果，已注册空 lane 取消成功且数量为 0。
- 入队、优先级变化、启动前、启动提交、取消及 Pending 清空通知互相独立；reply 是借用，
  lane/origin/priority 是值快照。同步取消与 queued 观察的边界见 [用户合同](../user/lane-scheduler.md)。

## 验证

执行以下命令校验安装面覆盖、合同引用和 allowlist 一致性：

```bash
python3 tests/public_api/run_public_api_checks.py public-contract-inventory \
  --inventory tests/public_api/public_contract_inventory.json \
  --surface-manifest tests/public_api/surface_manifest.json \
  --source-root src
```
