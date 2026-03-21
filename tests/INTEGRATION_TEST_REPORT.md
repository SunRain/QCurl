# `tst_Integration` 历史说明

本文件只保留 `tst_Integration` 的长期定位与取证入口，不再维护某次手工回归的日期、通过率、提交号、修复阶段或版本计划。

## 当前用途

- 说明 `tst_Integration` 覆盖的行为主题
- 给出当前可复现的证据入口
- 标注该测试文件的依赖与非目标

## 非用途

- 不作为当前“测试是否全部通过”的事实来源
- 不记录单次运行结果、历史修复过程、覆盖率估算或里程碑规划
- 不替代 `tests/README.md`、`docs/dev/build-and-test.md` 或自动化 evidence artifacts

## 当前取证入口

- QtTest / ctest：`python3 scripts/ctest_strict.py --build-dir build`
- 需要 env/httpbin 的集合：`python3 scripts/ctest_strict.py --build-dir build --label-regex env`
- “基本无问题”归档 runner：`python3 scripts/run_basic_no_problem_gate.py --build-dir build --run-id "<run-id>"`
- 一致性 gate：`python3 tests/libcurl_consistency/run_gate.py --suite <p0|p1|all> --build`

可复核工件以自动化产物为准，例如：

- `build/evidence/basic-no-problem/<run-id>/manifest.json`
- `build/evidence/basic-no-problem/<run-id>.tar.gz`
- `build/libcurl_consistency/reports/gate_<suite>.json`
- `build/libcurl_consistency/reports/junit_<suite>.xml`

## `tst_Integration` 覆盖主题

- 真实 HTTP 方法路径：GET、POST、PUT、DELETE、PATCH、HEAD
- Cookie 与 Header 语义：设置、发送、持久化、回显验证
- 超时与重定向：连接超时、总超时、自动跟随与最大重定向次数
- TLS/HTTPS 路径：请求成功/失败语义与本地证据用例
- 大文件与并发：下载、进度、并行请求、顺序请求
- 错误路径：无效主机、连接拒绝、HTTP 错误码

## 依赖与边界

- `tst_Integration` 依赖本地 httpbin；base URL 通过 `QCURL_HTTPBIN_URL` 注入
- 若缺少 httpbin 或相关端点不可用，在证据门禁口径下属于“无证据”，不应把 `QSKIP` 误读为通过
- 外部 HTTPS + 大体量传输已迁移到 `tst_LargeFileDownload`（`LABELS=external_heavy`），不再以本文件作为事实来源
- 具体运行命令、httpbin 启停与证据门禁口径以 `tests/README.md` 和 `docs/dev/build-and-test.md` 为准

## 维护规则

- 当 `tst_Integration` 的覆盖主题或依赖边界发生变化时，更新本文件的“覆盖主题 / 依赖与边界”
- 若新增需要长期保留的证据入口，补充到“当前取证入口”
- 不在本文件追加某次运行日志、通过率、手工结论或版本路线图
