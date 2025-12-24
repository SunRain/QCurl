# LC-15 pause/resume 一致性：落地复盘（LC-15a/LC-15b 均已完成）

本文用于记录 `tests/libcurl_consistency` 在 **LC-15（pause/resume 过程一致性）** 上的关键决策与落地路径，便于后续迭代时避免重复试错。

## 当前进度与关键决策

- **LC-15a（pause/resume 弱判据门禁）** 与 **LC-15b（强判据/语义合同测试）** 均已落地：目标是“门禁稳定 + 数据一致”，并在需要时提供可执行的强判据约束。
- 关键决策：**不比较 `cli_hx_download -P` 的 pause window 内数据事件计数**（如 `paused_data_events`）。
  - 原因：其 stderr 打点顺序不足以稳定定义 pause window，存在“`[t-0] PAUSE` 与 `[t-0] RESUMED` 之间仍出现 `RECV ... total=...` 增长”的可观测现象，导致 baseline 侧先失败，属于 **口径/边界问题** 而非实现必然错误。
- 回归验证：`python "tests/libcurl_consistency/run_gate.py" --suite all --build`（该命令在部分环境下需要更高权限/依赖完整构建与 curl testenv）。

## 已合入变更（用于追溯）

### LC-15a（弱判据门禁）

- 新增用例：`tests/libcurl_consistency/test_p2_pause_resume.py`
  - 弱判据：事件存在性/顺序 + 最终文件 `hash/len` 一致（不再要求 pause window “零 data/progress 事件”）。
- 对比器收敛：`tests/libcurl_consistency/pytest_support/compare.py`
  - `pause_resume` 仅比较 `pause_offset/pause_count/resume_count/event_seq`，不比较 `paused_data_events`。
- Gate 接入：`tests/libcurl_consistency/run_gate.py`
  - `--suite all` 纳入 `test_p2_pause_resume.py`。
- QCurl 侧执行器：`tests/tst_LibcurlConsistency.cpp`
  - 增加 `p2_pause_resume` 分支，产出 `pause_resume.json`。

### LC-15b（强判据/语义合同测试）

- baseline（可控、结构化事件边界）：
  - `tests/libcurl_consistency/pause_resume_baseline_client.cpp`
- 用例（强判据断言）：
  - `tests/libcurl_consistency/test_p2_pause_resume_strict.py`
- Gate 接入：
  - `tests/libcurl_consistency/run_gate.py`（`--suite all` 已纳入 strict 用例）
- 文档口径：
  - `tests/libcurl_consistency/tasks.md`（LC-15b 已完成）
  - `tests/libcurl_consistency/README.md`（覆盖矩阵已覆盖）

## 关键证据与定位路径（可复现）

### 1) baseline pause window 的“可观测噪声”

当运行 `test_p2_pause_resume_h2` 生成 artifacts 后，可在如下目录检查 baseline/QCurl 的对比差异（注意：该目录由 curl testenv 生成）：

- `curl/tests/http/gen/artifacts/p2_pause_resume/p2_pause_resume_h2/baseline.json`
  - 历史观测：`pause_resume.paused_data_events = 5`
- `curl/tests/http/gen/artifacts/p2_pause_resume/p2_pause_resume_h2/qcurl.json`
  - 历史观测：`pause_resume.paused_data_events = 0`

并且在同一目录下的 baseline stderr/日志中，`[t-0] PAUSE` 与 `[t-0] RESUMED` 之间可能出现多条 `RECV ... total=...` 增长。

### 2) `cli_hx_download -P` 的 pause/resume 触发方式（理解边界）

`cli_hx_download -P` 的 pause 由 write callback 返回 `CURL_WRITEFUNC_PAUSE` 触发；`RESUMED` 日志在调用 `curl_easy_pause(..., CURLPAUSE_CONT)` 之后打印：

- `curl/tests/libtest/cli_hx_download.c`

因此仅依赖 stderr 打点将 pause window 当作“无数据/无进度事件窗口”并不稳健。

### 3) 设计解释参考

说明“仅依赖 `curl_easy_pause` 难以对齐 `-P` 的过程一致性”的背景材料：

- `docs/TRANSPORT_PAUSE_RESUME_PLAN.md`

## 备注：LC-15b 的目标与边界

LC-15b 的目标不是复刻 `cli_hx_download -P` 的 stderr 打点，而是建立：

1) **可控 baseline**（repo 内自带、版本固定、输入固定、环境固定）  
2) **结构化事件边界**（统一事件模型，明确窗口与时间基准）  
3) **可执行强判据**（PauseEffective→ResumeReq 期间交付/写盘增量为 0，且最终文件一致）
