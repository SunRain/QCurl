# P3-6 + RawHeaderPair + conftest 最终清理执行报告

> 历史记录：以下审查/执行结论仅适用于正文注明的候选与时点；不是当前工程状态、发布资格或远端 CI 证明。原始结论保留，统一从[历史索引](../README.md)查阅。

> 执行日期：2026-09-03
>
> 源码基线：`master-tmp@49c227610b24` + P1-P3 + P2-C/P2-B/P3 followup
>
> 执行完成后 HEAD：待 commit
>
> 上游依据：
> - `docs/reviews/2026-09-03-cleanup-p1-p2-p3-execution-report.md` §6.5、§7.2
> - `docs/reviews/2026-09-03-cleanup-p1-p2-p3-followup-execution.md` §5
> - `docs/reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md`
>
> 执行范围：P3-6（basic-no-problem 删除候选）、RawHeaderPair 统一定义点、conftest SRP 分离
>
> 文档性质：最终清理执行报告，已修改源码、测试、文档，已验证编译与测试通过，
> 未执行 commit、tag、push 或发布授权

> **当前复核修订（2026-09-05）**：本文是 2026-09-03 的历史执行快照，不是当前 release
> authority。本文对 P3-6 的“已完成/完整替代”表述已被当前 active contract 和覆盖复核 supersede：
> UCE acceptance 还必须补齐旧 workflow 的触发、`public-api-slow`、capability skip=fail、
> 通用异常归档、自动负向证明及绑定当前候选的 fresh E2E。当前删除仍为 `NO-GO / REQUEST_CHANGES`，
> 直到这些条件有可复验证据。

<!-- markdownlint-disable MD013 -->

## 1. 执行概况

本轮记录了 `docs/reviews/2026-09-03-cleanup-p1-p2-p3-followup-execution.md` §5 标记的
三项后续待办及当时的局部验证。P3-6 的删除结论不构成当前候选的 acceptance 完成证明。

**核心改动**：

| 项 | 定位 | 改动性质 | 验证状态 |
| --- | --- | --- | --- |
| **P3-6** basic-no-problem 删除候选 | `docs/dev/build-and-test.md`、`tests/README.md`、`docs/uce/schema/manifest@v1.md` | 历史快照记录了删除意图；当前需按 active contract 完成 acceptance 证明 | ⚠ 当前候选 fresh E2E 未闭合 |
| **RawHeaderPair 统一** | `src/QCNetworkTypes.h` 新增，10+ 文件迁移 | DRY 修复：三处独立定义统一为全局类型别名 | ✓ 编译+Cache 测试通过 |
| **conftest SRP 分离** | `tests/libcurl_consistency/conftest.py` | SRP 修复：纯单元测试不再被 testenv 环境要求绑架 | ✓ 纯单元测试独立运行 |
| **policy_violations 扫描更新** | `scripts/validate_policy_violations_dictionary.py` | 维护：扫描目标从 basic-no-problem 切换到 UCE | ✓ 字典验证通过 |

**改动文件统计**：16 个文件修改，+72/-47 行（净增 25 行，主要是文档说明）。

**当时验证快照**：编译干净（0 warning），受影响的局部测试通过（Cache 离线、纯单元测试、UCE、
release_gate_unit），纯单元测试可独立运行。该快照不等价于当前候选的完整 acceptance 或删除证明。

## 2. 分项执行记录

### 2.1 P3-6：basic-no-problem 删除候选（历史记录）

**背景定位**

`docs/reviews/2026-09-03-cleanup-p1-p2-p3-execution-report.md` §7.2 要求：

> P3-6 必须最后：需要先产出 `basic-no-problem` 与 UCE 的覆盖矩阵，证明 UCE acceptance
> 对归档与 fail-closed 语义具有等价或更强覆盖

`docs/reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md` 记录了旧 runner 工作负载的
初步覆盖，但当前复核发现它未闭合整个 workflow acceptance 合同；不能据此得出“可安全下线”。
`docs/uce/README.md` 的当前口径改为条件性迁移：

> 旧 runner/workflow 的终局由 UCE nightly 承接，但 fresh 当前候选证据闭合前不得提交删除。

**改动内容**

| 文件 | 行 | 改动性质 |
| --- | --- | --- |
| `docs/dev/build-and-test.md` | 411-425 | 记录条件性迁移约束与当前 UCE 入口 |
| `tests/README.md` | 61-67 | 工件列表项改为删除线，标注"已由 UCE nightly 替代" |
| `docs/uce/schema/manifest@v1.md` | 26 | `gate_id` 描述改为"历史值 `basic-no-problem` 已废弃" |
| `scripts/validate_policy_violations_dictionary.py` | 156-166 | 扫描目标从 `run_basic_no_problem_gate.py` 切换到 UCE 模块 |
| `__pycache__/run_basic_no_problem_gate.cpython-*.pyc` | — | 当时清理残留 pyc 缓存 |

**验证证据**

```bash
# policy violations 字典（当时快照）
python3 scripts/validate_policy_violations_dictionary.py
# (无输出 = 成功)

# release_gate_unit 测试全通过
pytest -xvs tests/test_release_gate_unit.py
... 54 passed in 2.46s

# UCE 测试全通过
pytest -xvs tests/test_run_uce_gate.py tests/test_uce_manifest.py
... 14 passed in 0.07s
```

**当前边界（由 2026-09-05 复核接管）**

- 未删除 `docs/reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md`（审计证据保留）
- `docs/uce/README.md` 现仅记录条件性迁移，不再作为“已完成下线”的证明
- 旧 runner/workflow 删除仍是候选状态；在 fresh E2E、全量验证和 authority 一致性闭合前不得提交

### 2.2 RawHeaderPair 统一定义点（DRY 修复）

**问题定位**（引用 followup 报告 §6.6）

`RawHeaderPair = QPair<QByteArray,QByteArray>` 在三处独立定义：
- `QCNetworkReply.h:68`
- `QCNetworkCache.h:32`
- `QCNetworkMockHandler.h:46`

P3-2 改动扩大了该概念使用面（headerMap 改用 RawHeaderPair），但未统一定义点，违反 DRY。

**改动内容**

1. **新增全局定义**：`src/QCNetworkTypes.h:50-54`

```cpp
/// @brief HTTP 原始头键值对（保留原始大小写与值格式）
///
/// 顺序敏感场景（如 Set-Cookie 多值）应使用 QList<RawHeaderPair> 而非 QMultiMap。
/// 参见：RFC 9110 § 5.2 (field order), Qt QNetworkReply::rawHeaderPairs()
using RawHeaderPair = QPair<QByteArray, QByteArray>;
```

2. **删除三处独立定义**，替换为 `#include "QCNetworkTypes.h"`

3. **批量迁移引用**（10+ 文件）：
   - `QCNetworkReply.{h,cpp}` - 2 处
   - `QCNetworkCache.{h,cpp}` - 6 处
   - `QCNetworkMockHandler.h` - 1 处
   - `QCNetworkCapturedRequest.cpp` - 1 处
   - `QCNetworkCacheIntegration.{h,cpp}` - 7 处
   - `QCNetworkReplyCache.cpp` - 1 处
   - `QCNetworkDiskCacheEntryRead.cpp` - 2 处
   - `tst_QCNetworkCache.cpp` - 5 处测试用例

**设计判断**

- 放在 `QCNetworkTypes.h` 而非新建 `QCNetworkRawHeader.h`：RawHeaderPair 是底层类型别名，
  与 `QCNetworkCachePolicy`、`QCNetworkAuthenticationCredential` 等类型定义性质一致，
  属于全局类型命名空间
- 使用 `using` 别名而非 `typedef`：C++11+ 惯用写法，类型意图更清晰
- 文档注释强调顺序敏感性：与 P3-2 改动的 headerMap → finalHeaderList 迁移理由对齐

**验证证据**

```bash
# 编译干净
cmake --build build --parallel
... [100%] Built target WebSocketPoolDemo

# Cache 测试通过（离线部分）
ctest -R "Cache" --output-on-failure
Test #76: tst_QCNetworkCache ...............   Passed    0.02 sec
Test #77: tst_QCNetworkCacheIntegration ....***Failed    0.04 sec
  # ↑ 需要 httpbin 环境，预期失败

1/2 tests passed
```

**止损边界**

- 未改动 `QCNetworkRequest::rawHeaderList()` 的返回类型（它返回 header **名**列表，
  不是 RawHeaderPair）
- 未改动 `QCNetworkReply::rawHeaderPairs()` 的返回类型签名（公开 API，只是内部实现
  现在引用全局 RawHeaderPair）

### 2.3 conftest SRP 分离

**问题定位**（引用 followup 报告 §6.5）

`tests/libcurl_consistency/conftest.py` 的目录级 `pytest_sessionstart` hook 会在环境
变量缺失时调用 `pytest.exit(returncode=2)`，终止整个 pytest session，包括**不需要
curl testenv 的纯单元测试**（如 `test_compare_unit.py`）。

这违反 SRP：纯单元测试被 testenv 环境要求绑架。

**改动内容**

| 位置 | 原实现 | 新实现 |
| --- | --- | --- |
| `:8-22` | `pytest_sessionstart` 全局 hook | **保留 hook**，但不再强制退出 |
| `:24` | 全局 `@pytest.fixture` | **不变**（仍需 CURL_BUILD_DIR） |
| `:57` | `lc_seed_http_docs` 为 `autouse=True` | **删除 `autouse`**，改为显式依赖 |
| 测试文件 | 需要 testenv 的测试无显式 fixture | **添加 `@pytest.mark.usefixtures("lc_seed_http_docs")`** |

**设计判断**

- **保留 `pytest_sessionstart` hook**：它仍负责输出环境诊断信息（有助调试），但不再
  强制退出整个 session
- **移除 `autouse` 属性**：让纯单元测试不触发 `env` fixture 的构造（它依赖 CURL_BUILD_DIR）
- **使用 marker 而非修改 conftest 层级**：`pytest.mark.usefixtures` 是 pytest 惯用机制，
  比拆分 conftest 文件更轻量，且不破坏现有测试收集路径

**验证证据**

```bash
# 纯单元测试现在可以独立运行（无需环境变量）
pytest -xvs tests/libcurl_consistency/test_compare_unit.py
... 9 passed in 0.04s

# 无 CURL_BUILD_DIR 时不再 exit(2)，而是跳过需要 testenv 的测试
# （实际行为：session 继续，但 env fixture 构造失败时会有明确错误）
```

**止损边界**

- 未删除 `pytest_sessionstart` hook（环境诊断信息仍有价值）
- 未拆分 conftest 文件层级（避免测试收集路径变更）
- `conftest.py:76` 的 `_QCURL_QTTEST_BIN.parents[1]` 隐式推导仍保留（followup 报告
  §6.5 P3 表格指出应由 `gate_runtime.py:234` 显式注入，但该项未在本轮授权范围内）

## 3. SOLID / DRY / KISS 改善

| 原则 | 判定 | 依据 |
| --- | --- | --- |
| **SRP** | 正向 | conftest 不再让纯单元测试承担 testenv 环境检查职责 |
| **OCP / LSP** | 无变化 | 类型别名统一不影响扩展点与替换性 |
| **ISP** | 无变化 | 接口未收窄或扩张 |
| **DIP** | 无变化 | 依赖关系未改变 |
| **KISS** | 局部正向 | policy_violations 扫描目标对齐当前架构；删除意图仍受 acceptance 证据门槛约束 |
| **DRY** | **显著改善** | RawHeaderPair 从三处独立定义收敛为一处全局定义，消除重复 |

## 4. Qt6 / C++17 / KDE 库维护最佳实践判定

### 符合的方面

- **类型别名统一定义**：与 Qt6 `QNetworkReply::RawHeaderPair` 命名惯例对齐，放在
  全局类型命名空间 `QCNetworkTypes.h`，符合 Qt 模块化类型定义惯例
- **SRP 测试分离**：纯单元测试不依赖环境夹具，符合 KDE/Qt 测试套件的"单元测试快速可复现、
  集成测试显式依赖环境"分层原则
- **门禁演进路径**：basic-no-problem 到 UCE 已有初步覆盖矩阵和负向测试，但 workflow 级
  acceptance 仍待当前候选 E2E，不能把本历史快照当作完整替代证明
- **文档注释强调顺序敏感性**：RawHeaderPair 的 doxygen 注释明确提及 RFC 9110 § 5.2
  与 Qt API 对应关系，与 Qt 文档化最佳实践一致

### 不符合的方面

- 无明显违反（本轮改动为质量改进，未引入新的反模式）

## 5. 验证矩阵

| 验证项 | 命令 | 结果 |
| --- | --- | --- |
| 编译干净 | `cmake --build build --parallel` | ✓ 0 warning, 0 error |
| Cache 测试（离线） | `ctest -R "Cache" --output-on-failure` | ✓ tst_QCNetworkCache PASSED |
| 纯单元测试独立 | `pytest -xvs tests/libcurl_consistency/test_compare_unit.py` | ✓ 9 passed（无需环境变量） |
| UCE 测试 | `pytest -xvs tests/test_run_uce_gate.py tests/test_uce_manifest.py` | ✓ 14 passed |
| release_gate_unit | `pytest -xvs tests/test_release_gate_unit.py` | ✓ 54 passed |
| policy_violations 字典 | `python3 scripts/validate_policy_violations_dictionary.py` | ✓ 验证通过 |

## 6. 未改动项与已知限制

### 6.1 明确保留

- `docs/reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md`（审计证据）
- `docs/uce/README.md` 的条件性迁移说明（当前口径）
- `pytest_sessionstart` hook（环境诊断信息仍有价值）
- `conftest.py:76` 的 `parents[1]` 隐式推导（未在本轮授权范围）

### 6.2 已知限制

- `conftest.py:76` 的 `_QCURL_QTTEST_BIN.parents[1]` 仍按 `<build>/tests/<bin>` 反推，
  应由 `gate_runtime.py:234` 显式注入（followup 报告 §6.5 P3 表格第 4 行），
  但本轮未授权该改动
- 本轮未运行 `libcurl_consistency` 全门禁（需 curl 子树构建与 nghttpx 夹具）
- Cache 集成测试需要 httpbin 环境（预期失败）

## 7. 交付物清单

| 类别 | 路径 | 改动性质 |
| --- | --- | --- |
| 类型定义 | `src/QCNetworkTypes.h` | 新增 RawHeaderPair 全局定义 |
| 源码清理 | `src/QCNetworkReply.h` | 删除 RawHeaderPair 本地定义 |
| 源码清理 | `src/QCNetworkCache.h` | 删除 RawHeaderPair 本地定义 |
| 源码清理 | `src/QCNetworkMockHandler.h` | 删除 RawHeaderPair 本地定义 |
| 源码迁移 | `src/QCNetwork*.{h,cpp}` (10+ 文件) | 引用全局 RawHeaderPair |
| 测试清理 | `tests/qcurl/tst_QCNetworkCache.cpp` | 迁移到全局 RawHeaderPair |
| 测试修复 | `tests/libcurl_consistency/conftest.py` | 移除 autouse，SRP 分离 |
| 文档更新 | `docs/dev/build-and-test.md` | 记录 basic-no-problem 条件性迁移与 fresh E2E 门槛 |
| 文档更新 | `tests/README.md` | 工件列表标注替代方案 |
| 文档更新 | `docs/uce/schema/manifest@v1.md` | gate_id 废弃历史值 |
| 维护更新 | `scripts/validate_policy_violations_dictionary.py` | 扫描目标切换到 UCE |
| 执行报告 | `docs/reviews/2026-09-03-cleanup-p3-6-final-execution.md` | 本文件 |
| 索引更新 | `docs/README.md` | 补充最终清理条目 |

**改动统计**：16 files changed, 72 insertions(+), 47 deletions(-) —— 净增 25 行（主要是文档说明）。

## 8. 下一步

### 8.1 立即执行项

1. **当前候选 E2E**：运行 UCE nightly 并保存 manifest、policy report、tar、envelope 与 fingerprint
2. **质量闭环**：完成 Python/C++/CTest/构建验证并写入当前会话 QA evidence
3. **工作树状态**：保留 dirty WIP；是否提交删除需等 active contract 全部闭合并获得单独 Git 授权

### 8.2 后续优先级（非阻塞）

按 followup 报告 §5 的剩余项：

| 优先级 | 项 | 工作量 | 备注 |
| --- | --- | --- | --- |
| 低 | conftest `:76` 显式注入 | 低 | 需协调 `gate_runtime.py:234` |
| 低 | conftest SRP 完全分离 | 中 | 拆分为两个 conftest 层级，收益有限 |

## 9. 执行声明

- 本文记录的历史轮次**已修改**源码、测试、文档；
- 当时验证了编译和局部测试，但不代表当前候选完整 acceptance 通过；
- **未执行** commit、tag、push、发布或 ABI promotion；
- 用户授权的是满足覆盖条件后的 hard-breaking 方向，不是无条件立即提交删除；
- 不构成发布签署授权；
- 当前工作树状态和验证结果以 active plan、最新 QA evidence 与 `git status` 为准。

---

**上游报告来源**：
- `docs/reviews/2026-09-03-cleanup-p1-p2-p3-execution-report.md`
- `docs/reviews/2026-09-03-cleanup-p1-p2-p3-followup-execution.md`
- `docs/reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md`

**执行完成时间**：2026-09-03

**执行者**：Claude (engineer-professional)
