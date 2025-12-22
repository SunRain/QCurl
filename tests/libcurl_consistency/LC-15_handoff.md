# LC-15 pause/resume 一致性：现状与 LC-15b（强判据）落地备忘

本文用于记录 `tests/libcurl_consistency` 在 **LC-15（pause/resume 过程一致性）** 上的当前进展、关键证据与后续可执行落地方向，便于后续迭代时避免重复试错。

## 当前进度与关键决策

- 已落地并合入 **LC-15a（pause/resume 弱判据门禁）**：目标是“门禁稳定 + 数据一致”，不追求 pause window 内“零事件”。
- 关键决策：**不比较 `cli_hx_download -P` 的 pause window 内数据事件计数**（如 `paused_data_events`）。
  - 原因：其 stderr 打点顺序不足以稳定定义 pause window，存在“`[t-0] PAUSE` 与 `[t-0] RESUMED` 之间仍出现 `RECV ... total=...` 增长”的可观测现象，导致 baseline 侧先失败，属于 **口径/边界问题** 而非实现必然错误。
- 回归验证（历史结果）：提交后曾通过 `python "tests/libcurl_consistency/run_gate.py" --suite all --build`（输出 `35 passed`；该命令在部分环境下需要更高权限/依赖完整构建与 curl testenv）。

## 已合入变更（用于追溯）

### 提交 `e39be74`：`test(libcurl_consistency): add pause/resume weak gate`

- 新增用例：`tests/libcurl_consistency/test_p2_pause_resume.py`
  - 弱判据：事件存在性/顺序 + 最终文件 `hash/len` 一致（不再要求 pause window “零 data/progress 事件”）。
- 对比器收敛：`tests/libcurl_consistency/pytest_support/compare.py`
  - `pause_resume` 仅比较 `pause_offset/pause_count/resume_count/event_seq`，不比较 `paused_data_events`。
- Gate 接入：`tests/libcurl_consistency/run_gate.py`
  - `--suite all` 纳入 `test_p2_pause_resume.py`。
- QCurl 侧执行器：`tests/tst_LibcurlConsistency.cpp`
  - 增加 `p2_pause_resume` 分支，产出 `pause_resume.json`。

### 提交 `c35fb2a`：`docs(libcurl_consistency): clarify LC-15a vs LC-15b scope`

- `tests/libcurl_consistency/tasks.md`
  - 将 LC-15 标为“部分完成”，明确 LC-15a 已落地、LC-15b 待补齐；补充 LC-15a/LC-15b 分段说明与前置条件。
- `tests/libcurl_consistency/README.md`
  - 覆盖矩阵中“HTTP 回调/信号序列”的缺口改指向 LC-15b，并记录 `cli_hx_download -P` 打点不等于 pause window 的已知限制。

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

## 仍未完成：LC-15b（强判据：可控 baseline + 结构化事件边界）

当前唯一明确缺口：**LC-15b**（强判据）尚未实现。`tests/libcurl_consistency/tasks.md` 已将其标记为“未开始”，且 `tests/libcurl_consistency/README.md` 的覆盖矩阵仍将“HTTP 回调/信号序列”缺口指向 LC-15b。

LC-15b 的目标不是复刻 `cli_hx_download -P` 的 stderr 打点，而是建立：

1) **可控 baseline**（repo 内自带、版本固定、输入固定、环境固定）  
2) **结构化事件边界**（统一事件模型，明确窗口与时间基准）  
3) **可执行强判据**（pause window 内“应用层交付增量/落盘增量”为 0，且最终文件一致）

## 建议的后续实施路径（最小改动、可闭环）

1. 设计并固化结构化事件模型（JSON）：`Start/FirstByte/PauseReq/PausedEffective/ResumeReq/FirstByteAfterResume/Terminal`，字段至少包含：
   - `seq`（单调递增）、`t_us`（统一时间基准的微秒时间戳）、`type`
   - `bytes_delivered`（以“应用层交付/写盘累计”为口径）
   - `trace_id/req_id/config`（用于关联 baseline/QCurl 与服务端观测）
2. 新增 repo 内可控 baseline 可执行（类比现有 `qcurl_lc_http_baseline`）：`qcurl_lc_pause_resume_baseline`
   - 直接用 libcurl easy+multi（或按需）实现 pause/resume，支持 `pause_offset` + `resume_delay_ms`，输出 `events.json` 与 `download_0.data`。
3. QCurl 侧新增 `p2_pause_resume_strict`：
   - 在 `tests/tst_LibcurlConsistency.cpp` 基于 `readyRead/stateChanged/finished`（以及写盘累计）记录同构事件与 `bytes_delivered`。
4. 新增 pytest 用例 `test_p2_pause_resume_strict.py`：
   - 读取 baseline/QCurl 的 `events.json`，强断言 pause window 内 `bytes_delivered_delta == 0`，并对齐终态文件 `sha256/len`。
5. 初期将 LC-15b 放入非默认门禁（例如 ext/strict suite），连续运行 ≥10 次稳定后再评估纳入 `--suite all`。
6. 如需更严格对齐 callback 边界（不引入对外 API），可按 `docs/TRANSPORT_PAUSE_RESUME_PLAN.md` 引入“测试专用 hook/埋点”，将 pause 的“生效点”绑定到明确的可观测事件边界。

