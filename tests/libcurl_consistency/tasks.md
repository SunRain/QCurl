# libcurl_consistency 状态看板

本文只保留当前专题状态、边界与维护规则；逐次执行日志与回归记录统一看 `reports/` 和 artifacts。

## 1. 当前状态

- 基础 gate（baseline runner / QCurl runner / compare / schema）已落地
- `p0` / `p1` / `p2` / `ext` 分层已落地
- HTTP/3、WebSocket、pause/resume、backpressure、multipart 等专题均已有专门用例
- 当前文档的职责是“告诉维护者哪里有 contract、哪里有证据”，不是保存施工流水账

## 2. 主题状态

| 主题 | 代表 ID | 状态 | 说明 |
|------|---------|------|------|
| artifacts / compare / gate 基础设施 | `LC-0 ~ LC-16` | 已完成 | 基础 runner、schema、对比器与 gate 已稳定存在 |
| pause/resume 一致性 | `LC-15` | 已完成 | 弱判据 + 强判据均已接入；详情见 `LC-15_handoff.md` |
| 响应头原始字节 / unfold / 重复头 | `LC-26`、`LC-52` | 已完成 | 以 `rawHeaderData()` 路径和专题用例为准 |
| 空 body 与 `readAll()` 终态语义 | `LC-27` | 已完成 | 终态空 body 应表现为空字节，而不是额外文档约定 |
| 超时语义 | `LC-28` | 已完成 | connect/total/low-speed 以专题用例为准 |
| 取消语义 | `LC-29` | 已完成 | 取消后的终态与事件约束已固化 |
| 进度摘要 | `LC-30` | 已完成 | 对齐稳定摘要，不比较原始事件频率 |
| 连接复用/多路复用可观测性 | `LC-31` | 已完成 | 默认看统计与集合等价，不拿完成顺序当默认契约 |
| 错误路径 | `LC-32` | 已完成 | 连接拒绝、407、malformat 等已覆盖 |
| HTTP 方法面 | `LC-33`、`LC-36 ~ LC-40` | 已完成 | HEAD / PATCH / DELETE / redirect / expect-100 等已覆盖 |
| WS 扩展场景 | `LC-34` | 已完成 | 基础 WS 与扩展场景分层管理 |
| multipart 语义一致性 | `LC-35` | 已完成 | 比较 parts 语义，不比较 boundary |
| backpressure 最小合同 | `LC-55` | 已完成 | 以边沿与 body 字节为主断言 |

## 3. 默认 gate 不承诺的内容

以下内容即使被观察到，也不自动进入默认 gate：

- libcurl 内部状态机细节
- 动态头与时间快照
- multipart boundary 或其他实现生成物
- 仅在某个版本输出的诊断文本
- 未明确定义为产品契约的完成顺序/调度顺序

## 4. 何时新开 LC 任务

出现以下任一情况时，应新增或重开 LC 任务：

1. 新增 public API，且其外部可观测结果需要与 libcurl 对齐
2. 现有 contract 发生变化，compare/schema 无法继续稳定表达
3. 某类差异反复出现在 reports 中，且现有专题无法归因
4. 文档里只能靠“口头解释”而不能靠专题测试证明

## 5. 变更原则

- 优先修改测试和 compare 规则，再更新文档
- README 记录稳定 contract
- handoff 记录专题决策
- 本文件只维护专题级状态，不保留逐次执行日志

## 6. 相关入口

- `tests/libcurl_consistency/README.md`
- `tests/libcurl_consistency/LC-15_handoff.md`
- `tests/libcurl_consistency/run_gate.py`
- `tests/libcurl_consistency/pytest_support/compare.py`
- `build/libcurl_consistency/reports/`
