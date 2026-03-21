# LC-15 pause/resume handoff

本文只保留 LC-15 的当前合同、关键决策与证据入口，避免后续维护再次回到长篇施工日志模式。

## 1. 当前合同

### 1.1 LC-15a：弱判据

- 比较 `pause -> resume -> finished` 的事件存在性与顺序
- 比较最终文件 `len/hash`
- 不把 pause window 内的原始 data/progress 事件计数当作稳定契约

### 1.2 LC-15b：强判据

- 使用 repo 内可控 baseline 与结构化事件边界
- 核心合同：
  - `PauseEffective -> ResumeReq` 期间，交付/写盘累计增量必须为 `0`
  - 最终文件字节必须一致

## 2. 关键决策

### 2.1 不以 `cli_hx_download -P` 的 stderr 打点定义 pause window

原因：

- `PAUSE/RESUMED` 属于文本打点，不是协议级边界
- 该窗口内仍可能观察到 baseline 侧 `RECV` 增长
- 直接拿它做强断言会把口径噪声误判成实现错误

### 2.2 强判据必须使用结构化事件边界

因此 LC-15b 选择：

- 自带 baseline
- 明确 `PauseEffective`
- 明确 `ResumeReq`
- 直接比较“窗口内累计增量是否为 0”

## 3. 证据入口

- baseline：
  - `tests/libcurl_consistency/pause_resume_baseline_client.cpp`
- pytest：
  - `tests/libcurl_consistency/test_p2_pause_resume.py`
  - `tests/libcurl_consistency/test_p2_pause_resume_strict.py`
- compare：
  - `tests/libcurl_consistency/pytest_support/compare.py`
- QCurl 执行器：
  - `tests/libcurl_consistency/tst_LibcurlConsistency.cpp`

## 4. 何时重开 LC-15

只有出现以下情况才应重开：

1. `pauseTransport()` / `resumeTransport()` 的外部语义改变
2. `PauseEffective` 的定义不再能稳定描述实现
3. compare schema 无法表达 pause/resume 的可观测边界
4. gate 报告显示 pause/resume 相关失败已无法通过现有证据定位

## 5. 关联文档

- `tests/libcurl_consistency/README.md`
- `tests/libcurl_consistency/tasks.md`
- `docs/arch/transport-pause-resume.md`
