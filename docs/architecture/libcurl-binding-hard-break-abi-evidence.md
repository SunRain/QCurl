# libcurl binding hard-break ABI evidence

本文记录 `docs/architecture/libcurl-binding-contract.md` 本轮 hard-breaking 授权下的 ABI 证据边界。

## 授权前提

本轮修订允许 hard-breaking，因此 ABI 变化不作为放行阻断项。ABI 审查目标改为确认：

- baseline 刷新是本轮授权 hard break 的一部分。
- 当前库与刷新后的 baseline 一致。
- 旧 baseline 到当前库的差异没有被误判为“兼容性必须保持”的失败。

## 影响范围

本轮 hard-break 证据只覆盖 libcurl binding hardening 相关改动：

- `QCCurlHandleManager` 集中 `curl_easy_init()` 和 `CURLOPT_NOSIGNAL=1L`。
- Blocking Extras、cookie helper 和 Core async 继承 common easy defaults。
- diagnostics HTTP probe 收敛到 QCurl Core async 路线，不再使用 `QNetworkAccessManager`。
- signed URL redaction 命中 marker 后脱敏全部 query value。

## 证据

当前工作树中的 `scripts/qcurl_abi_gate.py diff` 先生成 current ABI XML，再比较 refreshed baseline XML 与 current XML。该路径避免 XML ↔ ELF 直接比较带来的 Qt template / weak-symbol 噪声，并用于确认“刷新后的 baseline 与当前库一致”。

已验证命令：

```bash
python3 scripts/qcurl_abi_gate.py --library build/Qt_6_11_1_system_cmake-Debug/src/libQCurl.so.1.0.0 --headers-dir src diff --baseline abi/baseline/qcurl-core-v1.abi.xml --report build/Qt_6_11_1_system_cmake-Debug/abi/qcurl-core-v1.abidiff.txt --current-snapshot build/Qt_6_11_1_system_cmake-Debug/abi/qcurl-core-v1.current.abi.xml
```

验证结论：

- current snapshot 已写入 `build/Qt_6_11_1_system_cmake-Debug/abi/qcurl-core-v1.current.abi.xml`。
- refreshed baseline 对 current snapshot 的 ABI diff 通过。

## 旧 baseline 差异说明

用 `HEAD` 版本 baseline 对当前库直接运行 `abidiff` 会报告大量新增函数和未被 debug info 引用的新增符号。该结果在 hard-breaking 授权下不作为阻断项；它的意义是证明 baseline 刷新确实代表一次 hard-break，而不是无差异兼容更新。

review 时不得把“当前 baseline 已通过”单独解释为“旧 ABI 兼容”。正确表述是：

> 旧 ABI 兼容性未保持；当前 hard-breaking 授权允许 baseline 刷新，且刷新后的 baseline 已与当前构建产物一致。
