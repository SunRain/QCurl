# Archived libcurl binding ABI comparison evidence

> Archived material. This document records a pre-1.0 ABI comparison used during
> libcurl binding hardening. It is not a current `QCurl 1.0.0` release gate,
> release note, or user-facing compatibility promise.

本文记录 `docs/architecture/libcurl-binding-contract.md` 早期修订阶段的 ABI 对比口径。当前
fresh release 结论以 `docs/arch/1.0-first-stable-release-contract.md` 为准：`QCurl 1.0.0`
是首个 Stable ABI baseline，release gate 使用当前 baseline → 当前库的 clean diff。

## 授权前提

本轮修订允许 hard-breaking，因此旧 baseline 到当前库的 ABI 差异不作为 release 阻断项。ABI 审查目标拆成两轨：

- **hard-break 差异轨**：旧 baseline → 当前库。用于证明本轮确实发生 ABI hard-break，并保存可审计差异。
- **release 阻断轨**：刷新后 baseline → 当前库。用于证明当前 release baseline 与当前构建产物一致；该轨仍 fail-closed。

缺少 `abidw` / `abidiff`、共享库、头目录、调试信息，或 ABI 工具返回使用错误 / 运行错误时，两轨都不得静默放行。

## 影响范围

本轮 hard-break 证据覆盖 libcurl binding hardening 相关改动：

- `QCCurlHandleManager` 集中 `curl_easy_init()` 和 `CURLOPT_NOSIGNAL=1L`。
- Blocking Extras、cookie helper 和 Core async 继承 common easy defaults。
- diagnostics HTTP probe 收敛到 QCurl Core async 路线，不再使用 `QNetworkAccessManager`。
- signed URL redaction 命中 marker 后脱敏全部 query value。
- 默认请求和 `ProxyType::None` 明确禁用 libcurl 环境代理继承，避免 `HTTP_PROXY` / `HTTPS_PROXY` 等环境变量改变“无代理”语义。

## hard-break 差异轨

旧 baseline 先保存为本地证据：

```bash
python3 -c 'from pathlib import Path; src=Path("abi/baseline/qcurl-core-v1.abi.xml"); dst=Path("build/abi/qcurl-core-v1.previous.abi.xml"); dst.parent.mkdir(parents=True, exist_ok=True); dst.write_bytes(src.read_bytes())'
```

然后用旧 baseline 生成当前库差异报告：

```bash
python3 scripts/qcurl_abi_gate.py \
  --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src \
  hardbreak-report \
  --baseline build/abi/qcurl-core-v1.previous.abi.xml \
  --report build/abi/qcurl-core-v1.hardbreak-from-previous.abidiff.txt \
  --current-snapshot build/abi/qcurl-core-v1.hardbreak-current.abi.xml
```

本轮证据：

- hard-break report：`build/abi/qcurl-core-v1.hardbreak-from-previous.abidiff.txt`
- hard-break current snapshot：`build/abi/qcurl-core-v1.hardbreak-current.abi.xml`
- `abidiff` returncode：`12`，即 ABI change + incompatible ABI change。
- 摘要：`Functions changes summary: 4638 Removed (1948 filtered out), 0 Changed, 0 Added functions`
- 摘要：`Function symbols changes summary: 1010 Removed, 0 Added function symbols not referenced by debug info`
- 摘要：`Variable symbols changes summary: 5 Removed, 0 Added variable symbols not referenced by debug info`

该报告证明旧 ABI 兼容性未保持。它不是 release 阻断轨的替代品，也不能被解释为“ABI gate 失败可忽略”。

## release 阻断轨

刷新 release baseline：

```bash
python3 scripts/qcurl_abi_gate.py \
  --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src \
  baseline \
  --output abi/baseline/qcurl-core-v1.abi.xml
```

再用刷新后的 baseline 比较当前库：

```bash
python3 scripts/qcurl_abi_gate.py \
  --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src \
  diff \
  --baseline abi/baseline/qcurl-core-v1.abi.xml \
  --report build/abi/qcurl-core-v1.abidiff.txt \
  --current-snapshot build/abi/qcurl-core-v1.current.abi.xml
```

本轮证据：

- refreshed baseline：`abi/baseline/qcurl-core-v1.abi.xml`
- current snapshot：`build/abi/qcurl-core-v1.current.abi.xml`
- clean diff report：`build/abi/qcurl-core-v1.abidiff.txt`
- 结论：刷新后 baseline 对当前 snapshot 的 ABI diff 通过。

## 解释边界

当前报告里大量 Qt template / weak-symbol / debug-info 相关项应作为 ABI 工具噪声与构建 profile 差异风险说明处理，不能直接宣称为全部 public API 语义变化。release 结论应使用以下口径：

> 旧 ABI 兼容性未保持；当前 hard-breaking 授权允许 baseline 刷新。旧 baseline → 当前库的 hard-break 差异报告已归档，刷新后 baseline → 当前库的 release 阻断 ABI diff 已通过。
