# QCurl 测试套件

本目录维护测试代码、测试资产与证据映射。长期运行命令统一维护在 `docs/dev/build-and-test.md`；本文只做目录导航、证据类型和关键入口说明。

## 目录结构

- `tests/qcurl/`：QCurl QtTest、httpbin 启停脚本、WebSocket evidence server、TLS fixtures 和本地测试数据。
- `tests/libcurl_consistency/`：QCurl 与 libcurl 的外部可观测一致性测试，入口是 `run_gate.py`。
- `tests/public_api/`：public/install surface guardrails，包括逐头 self-compile、surface manifest、staging install、export contract 和 isolated consumer smoke。
- `tests/coverage-map.yaml`：按 QCurl surface 记录代表 gate、证据类型和测试入口。

## 证据类型

- `contract`：强合同。失败表示被覆盖的产品或安装面合同不成立。
- `regression`：历史缺陷或边界回归。失败应阻断对应 gate。
- `smoke`：粗粒度健康检查。它不能替代强合同证据。
- `diagnostic`：定位辅助。默认不作为发布合同证明。

维护者需要判断“某个 API 或行为由哪些测试证明”时，先查 `tests/coverage-map.yaml`，再进入对应 CTest、pytest 或 public API gate。

## LABELS 与 skip 口径

本仓库的门禁原则是 **未执行 = 无证据 = 必须失败**。除显式 opt-in 的 `external_*` 集合外，QtTest 目标一旦被选中执行，就不应通过 `QSKIP` 逃避证据。

关键分层：

| 标签 / 集合 | 作用 | 口径 |
| --- | --- | --- |
| `offline` | 默认离线 gate | skip=fail |
| `env` / `httpbin` | 依赖本地 httpbin 的集成证据 | suite preflight 先检查环境，缺前置即失败 |
| `local_port` / `node` / `websocket` | 本地端口、Node、WebSocket evidence server | suite preflight fail-closed |
| `capability` / `http2` | 能力前置用例 | 选中后必须真实执行 |
| `external_network` / `external_heavy` | 公网探测或大体量外部 smoke | 显式 opt-in，允许 QSKIP |

静态守卫：

- `scripts/check_qcurl_label_matrix.py` 校验 preflight 与 LABELS 的一致性。
- `scripts/check_skip_contract.py` 防止非 `external_*` 目标关闭 skip=fail。
- `tests/test_coverage_maps.py` 校验 coverage map 与 CMake / libcurl consistency gate policy 对齐。

## Public API 安装面门禁

Public API gate 覆盖：

- 每个安装头作为第一个 include 单独编译。
- public header 规则扫描。
- staging install 头集合、导出合同和 isolated consumer smoke。
- Blocking Extras / Test Support / Other Extras 的 opt-in install / consumer smoke 与 default Core 负向 consumer。

机器可读 public surface 真源是 `tests/public_api/surface_manifest.json`。当改动 public headers、`QCURL_INSTALL_HEADERS` 或 install/export 合同时，运行命令以 `docs/dev/build-and-test.md` 的 Public API 章节为准。

## libcurl_consistency

`tests/libcurl_consistency/run_gate.py` 是一致性测试唯一受支持的取证入口。它会统一处理 capabilities、pytest 规划、schema 校验、redaction 和 gate report。裸 `pytest` 只能用于本地诊断，不能单独作为通过证据。

专题状态看板已内部化到 `docs/internal/maintainer-backlog/libcurl-consistency.md`，不再作为公开测试入口。

## 证据工件

常见工件类型：

- CTest / QtTest 终端输出。
- `build/libcurl_consistency/reports/gate_<suite>.json`。
- `build/libcurl_consistency/reports/junit_<suite>.xml`。
- `build/libcurl_consistency/reports/capabilities.json`。
- `curl/tests/http/gen/artifacts/<suite>/<case>/...`。
- `build/evidence/basic-no-problem/<run-id>/manifest.json` 与对应 tarball。

## 命令 SSOT

构建、httpbin、QtTest、public API、libcurl consistency、release gate 和“基本无问题”验收命令统一见：

- `docs/dev/build-and-test.md`

测试门禁可证明边界和非证明边界见：

- `docs/test_gate.md`
