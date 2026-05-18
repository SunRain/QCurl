# Public API Layout Audit Checklist

本清单用于补充 `run_public_api_checks.py` 的自动规则，记录当前已知的 ABI/layout 暴露点。

## 使用方式

- 自动 gate：`qcurl_public_api_scan` 会扫描并比对 `public_api_layout_allowlist.txt`。
- 人工复核：每次相关重构完成后，先验证代码，再从 allowlist 移除已修复项。
- 回归策略：新增暴露点（不在 allowlist）直接失败；已修复但仍留在 allowlist 的 stale 条目同样直接失败，必须先清理再提交。
- `public_api_layout_allowlist.txt` 是临时基线，不是稳定 API 承诺。新增条目必须写明对应 public header 与迁移任务，不能作为长期放行手段。

## 当前重点分类

- 导出类型 public fields
- 导出类型 nested struct 布局外泄
- 导出函数签名引用 struct 类型（布局耦合）
- 不完整类型持有者 special members（out-of-line 规则）

## 追踪建议

建议把清理节奏与方案包任务对齐：

- `QCNetworkAccessManager` 的 legacy struct 暴露仍由 allowlist 临时兜底；后续若迁移为 ABI 友好的值类型，再移除对应条目。
- request / proxy / timeout / retry family 一旦完成值类型收口，必须同步删除对应 allowlist，不能让 stale baseline 长期保留。

## Scheduler Core contract（2.7）持续验证清单

- compile：`tests/public_api/consumer_smoke/main.cpp` 必须保留 `<QCNetworkRequestScheduler.h>` include，并覆盖
  - `manager.scheduler()`
  - `Config/LaneConfig` accessor API（`set*` + `*()`）
  - `QCNetworkSslConfig` / `QCNetworkProxyConfig::ProxyTlsConfig` / `QCNetworkTimeoutConfig` / `QCNetworkRetryPolicy` / `QCNetworkHttpAuthConfig` 的最小 consumer 调用路径
- gate：`run_public_api_checks.py consumer-smoke` 会先执行 fixture 审计（缺失 contract 覆盖或出现 direct field 用法直接失败），再执行 staged consumer 构建。
- owner-thread：Core consumer 只验证 owner-thread `scheduler()`；不再承诺跨线程 owner-thread 阻塞 getter。
- audit：若 scheduler contract 扩展（新增线程约束、accessor、signals 相关公开承诺），必须同步更新本清单与 consumer_smoke fixture，避免“能编过但未覆盖核心合同”的回归。
