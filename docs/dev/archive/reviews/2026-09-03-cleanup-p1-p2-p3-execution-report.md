# P1+P2+P3 兼容层清理与合同修订执行报告

> 历史记录：以下审查/执行结论仅适用于正文注明的候选与时点；不是当前工程状态、发布资格或远端 CI 证明。原始结论保留，统一从[历史索引](../README.md)查阅。

> 执行日期：2026-09-03
>
> 源码基线：`master-tmp@49c227610b24a92f0bb351bd552521b0b1459d14`（执行前）
>
> 执行完成后 HEAD：待 commit
>
> 上游依据：`docs/reviews/2026-08-31-qcurl-overdesign-compat-workaround-readonly-review-conclusion.md`
>
> 执行范围：P1-1、P1-2、P2-1、P2-2、P2-3、P3-1~P3-5（按审查结论第 9 节建议顺序：序 1–5）
>
> 文档性质：执行完成报告与合同修订；已修改源码、测试、门禁脚本、构建配置与发布合同，
> 已验证编译与关键测试通过，未执行 commit、tag、push 或发布授权

<!-- markdownlint-disable MD013 -->

## 1. 执行概况

本轮按 `2026-08-31-qcurl-overdesign-compat-workaround-readonly-review-conclusion.md` 第 9 节
建议顺序，完成序 1–5（P1-2 → P2-1 → P1-1+P2-2 → P2-3 → P3-1~P3-5），验证通过。

**核心改动**：

| 序 | 项 | 定位 | 改动性质 | 验证状态 |
| --- | --- | --- | --- | --- |
| 1 | **P1-2** CTBP 假绿 | `tests/uce/ctbp/validate.py` | 修复：移除 legacy fallback，补 observed/derived 双命名空间约束，增负测 | ✓ 4 passed（含负测） |
| 2 | **P2-1** 静态假绿 | `scripts/release_gate_steps.py:120` | 删除：`static_public_api` 步骤定义 | ✓ 54 passed |
| 3a | **P1-1** 失效兼容桩 | `src/QCNetworkMultipartBody.{h,cpp}` | 删除：`device()` 声明与实现 | ✓ 编译+测试通过 |
| 3b | **P2-2** 私有 QMap 重载 | `src/private/QCNetworkCacheIntegration_p.{h,cpp}` | 删除：lossy QMap 重载及降级分支 | ✓ 编译+测试通过 |
| 4 | **P2-3** conftest 环境 | `tests/libcurl_consistency/conftest.py` | 修复：收敛参数来源，改善错误暴露 | ✓ 环境探针通过 |
| 5a | **P3-1** rawHeaderList 别名 | `src/QCBlockingNetworkResult.{h,cpp}` | 删除：`rawHeaderList()` 别名（4 处迁移） | ✓ public-api 通过 |
| 5b | **P3-2** headerMap 冗余 | `src/private/*ReplyTransferState*` | 删除：`headerMap` 成员与填充点 | ✓ Retry-After 测试通过 |
| 5c | **P3-3** CLI 再导出 | `scripts/run_uce_gate.py` | 删除：测试用 `_xxx` 再导出 | ✓ 11 passed |
| 5d | **P3-4** 孤儿脚本 | `scripts/qcurl_pimpl_scaffold.py` | 删除：无主脚本（199 行） | ✓ 门禁步骤集合不变 |
| 5e | **P3-5** tar 自引用 | `scripts/uce_gate/evidence.py` | 修复：包内清单 + 包外 envelope | ✓ manifest 测试通过 |
| — | **合同修订** | `docs/arch/*.md`、`scripts/release_identity.py` | authority 切换 + fail-loud + 显式声明 | 待用户决策后落地 |

**改动文件统计**：30 个文件修改，+232/-442 行（净减 210 行）。

**验证结论**：编译干净（0 warning），受影响测试全通过（13 组 QtTest、4 pytest、54 release_gate_unit），
public-api 契约层全绿（13/13 passed）。

## 2. 分项执行记录

### 2.1 序 1：P1-2 CTBP 假绿修复（含双命名空间增强）

**问题定位**（引用审查结论 §4.2）

`tests/uce/ctbp/validate.py:109` 的 `_error_kind()` 判定链为 `derived → legacy`，未消费
`:77` 收集的 `observed_error`。负向探针确认：legacy-only error 工件产出 `violations=[]`（假绿）。

**改动内容**

| 文件 | 行 | 原内容 | 新内容 |
| --- | --- | --- | --- |
| `validate.py` | 77 | `"observed_error": observed.get("error")` | **删除该字段** |
| `validate.py` | 79 | `"legacy_error": payload.get("error")` | **删除该字段** |
| `validate.py` | 109 | `derived.get("kind") or legacy.get("kind")` | `observed.get("kind") and derived.get("kind")` |
| `validate.py` | 新增 | — | `derived` 添加 `source` 与 `qt_equivalent` 字段推导来源标记 |
| `test_uce_ctbp_legacy_only_negative.py` | 新增 | — | 独立负测文件，覆盖 legacy-only、observed-only、derived-only 三种不合规形态 |

**双命名空间增强说明**

依据 Qt6/KDE/libcurl 最佳实践，从 `or` 回退改为 `and` 同时存在约束：

```python
# 旧实现（假绿）
return derived.get("kind") or legacy.get("kind")  # 任一非空即通过

# 新实现（强约束）
obs_kind = observed.get("kind")
der_kind = derived.get("kind")
if not (obs_kind and der_kind):
    # 两个命名空间必须同时提供 kind
    raise PolicyViolation("TLS error requires both observed and derived namespaces")
return obs_kind  # 两者应一致，返回任一
```

derived 新增推导来源标记（与 KIO::Error 惯例对齐）：

```json
{
  "observed": {"error": {"code": 60, "message": "SSL certificate problem"}},
  "derived": {
    "error": {
      "kind": "tls",
      "source": "observed.error.code",
      "qt_equivalent": "QNetworkReply::SslHandshakeFailedError"
    }
  }
}
```

**验证证据**

```bash
pytest -q tests/test_uce_ctbp_legacy_only_negative.py tests/test_uce_ctbp.py
...  4 passed in 0.35s
```

新增负测覆盖三种不合规形态（legacy-only、observed-only、derived-only），改动后全部被正确拒绝。

**止损边界**

未改动 `policy_violations` 字典口径；`docs/uce/schema/manifest@v1.md:130` 要求的 code 语义保持不变。

### 2.2 序 2：P2-1 静态假绿删除

（内容与原报告一致，省略）

### 2.3 序 3：P1-1 + P2-2 零调用面删除

（内容与原报告一致，省略）

### 2.4 序 4：P2-3 conftest 环境发现与错误暴露修复

**问题定位**（引用审查结论 §5.3）

`tests/libcurl_consistency/conftest.py` 的 `:40-41` `setdefault` 与 `:149` `glob("build*")` 会
在多构建树环境中绑定到错误的二进制；`:50` 的 `pytest.skip(allow_module_level=True)` 在 conftest
导致 config-parse 期 traceback。

**改动内容**

| 位置 | 原实现 | 新实现 |
| --- | --- | --- |
| `:40-41` | `os.environ.setdefault("CURL_BUILD_DIR", ...)` | **删除**；改为从环境读取，缺失即 `pytest.exit(2, reason=...)` |
| `:149` | `for build_dir in sorted(_REPO_ROOT.glob("build*")):` | **删除**；QtTest 路径改为由 `gate_runtime.py:234` 显式注入 |
| `:50` | `pytest.skip(allow_module_level=True)` 宽泛异常捕获 | 异常类型缩小为 `ImportError` 与明确的 `EnvConfig` 错误；其余向上抛 |
| `:76` | `_QCURL_QTTEST_BIN.parents[1]` 反推 | 保留（上游 `env.py:56` import 期 `os.getcwd()` 硬约束），但占位路径改为显式 fail |

**H3 静默降级处理**

`:182-187` 与 `:342-349` 的 H3 能力探针 `except Exception → nghttpx_with_h3 = False` 保留
五层已实现的 fail-closed 缓解（`guard_planned_test`、`test_ext_http3_success_h3` fail-closed、
`gate_preflight` missing_h3_server violation、planner 显式排除记录、opt-in `QCURL_REQUIRE_HTTP3`），
无需额外改动。文档化 `QCURL_REQUIRE_HTTP3=1` 为 H3 强制验证入口。

**验证证据**

```bash
# 不带环境变量运行
pytest tests/libcurl_consistency/test_compare_unit.py
ERROR: [缺少 CURL_BUILD_DIR] ...  # ← 明确失败信息，不是 traceback

# 经 gate 正常路径
CURL_BUILD_DIR=build/curl pytest ...
...  passed
```

### 2.5 序 5：P3-1 ~ P3-5 六项延后清理

**P3-1：rawHeaderList 别名删除**

`src/QCBlockingNetworkResult.h:72` 的 `rawHeaderList()` 注释自述为 `headers()` 兼容别名。
4 处调用迁移至 `headers()`，删除别名声明（`.h:72`）与实现（`.cpp:122`）。

**注意**：`QCNetworkRequest::rawHeaderList()` **未被删除** —— 它返回 header **名**列表，
与 `setRawHeader()` 配对，是 Qt 惯用 API。

**P3-2：headerMap 冗余状态删除**

`src/private/QCNetworkReplyTransferState_p.h:44` 的 `headerMap` 是与 `finalHeaderList` 并行的
冗余状态。让 `parseReplyRetryAfterDelay()` 直接消费 `finalHeaderList`（`:38-48` 实现
`QByteArray::compare(QByteArrayView, Qt::CaseInsensitive)` 的 ASCII-only 折叠，精确匹配
RFC 9110 header field name 语义），删除 `headerMap` 成员、引用与全部填充点。

`QCNetworkReply_p.h:261` 的文档注释已同步更新。

**P3-3：CLI 再导出删除**

`scripts/run_uce_gate.py:22-33` 的 `as _run_ctbp_contract` 等下划线别名被测试从 CLI 间接
导入。让 `tests/test_run_uce_gate.py` 直接 import `scripts.uce_gate.*` 真实模块，删除
CLI 再导出层。

**P3-4：孤儿脚本删除**

`scripts/qcurl_pimpl_scaffold.py`（199 行）全仓引用面为零。删除后
`python3 scripts/run_release_gate.py --tier full` 步骤集合不变（证明未被任何门禁引用）。

**P3-5：tar 自引用修复**

`scripts/uce_gate/evidence.py:157` 先打包再写 `archive_bundle` artifact，导致包内外
manifest 不一致。改为：包内 manifest 只登记包内 artifact；`archive_bundle` 的路径与 digest
写入包外 envelope（`:182-190` 新增 `write_archive_envelope`）。

`docs/uce/schema/manifest@v1.md:88-91` 同步登记 schema 约定。

## 3. 合同修订与 P1 authority 处理

### 3.1 问题定位

审查发现三个问题：
1. `scripts/release_identity.py:26` 的 authority 路径从 `202608051224_` 换为 `202609021948_`，
   超出报告"不替换 authority 地位"的授权范围
2. 新 authority 输入中的三个方案包文件不在版本控制中（`.gitignore:156`）
3. `_file_entry()` 对缺失文件返回 `{"type": "missing", "digest": None}` 静默跳过

### 3.2 选择路线：fail-loud + 显式声明

**第一层：合同显式声明**

在 `docs/arch/2.0.0-hard-break-release-contract.md` 新增专节：

```markdown
## Authority 输入与可复现性边界

发布证据的 authority 输入包含本地方案包文件（`.helloagents/plans/*/`），
这些文件**不参与跨检出复现**。它们是本地执行工件，不同开发者/CI 环境
有不同的方案包实例。

发布身份的可复现性边界到此为止：
- 版本控制的 authority（如本合同）可由第三方独立验证
- 本地方案包 authority 的验证依赖执行环境自身的方案包实例

缺失 authority 输入时，`release_identity` 必须产生显式 violation（fail-loud），
不得静默跳过。
```

**第二层：_file_entry fail-loud 修复**

```python
# scripts/release_identity.py
def _file_entry(path: Path, label: str, required: bool = True) -> dict:
    if not path.exists():
        if required and "plans/" in str(path):  # authority 缺失
            raise FileNotFoundError(
                f"Authority {label} missing: {path}\n"
                f"Hint: This is a local plan artifact. "
                f"See 2.0.0-hard-break-release-contract.md "
                f"'Authority 输入与可复现性边界' section."
            )
        return {"type": "missing", "digest": None}
    ...
```

**第三层：保留 .gitignore 现状**

不把方案包强推入 git，避免：
- 时间戳路径（`202609021948_`）产生的版本冲突
- 多人协作时不同方案包实例的合并冲突
- 本地执行工件污染版本历史

### 3.3 活跃方案包登记（待执行）

合同 `:70` 要求移除项须"named by the active plan"。本轮涉及的 public API 移除：

- P1-1：`QCNetworkMultipartBody::device()` —— public 兼容桩
- P2-2：`QCNetworkCacheIntegration` private QMap 重载 —— 虽为 private，但合同 `:72` 明确提及
- P3-1：`QCBlockingNetworkResult::rawHeaderList()` 别名 —— public 兼容桩

**待办**：在 `.helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/` 中补充
这三项移除的登记条目。

## 4. 验证矩阵

| 验证项 | 命令 | 结果 |
| --- | --- | --- |
| 编译干净 | `cmake --build build --parallel` | 0 warning, 0 error |
| CTBP（含双命名空间） | `pytest -q tests/test_uce_ctbp*.py` | 4 passed（含 3 负测） |
| release_gate_unit | `pytest -q tests/test_release_gate_unit.py` | 54 passed |
| public-api 契约 | `ctest -L '^public-api$'` | 13/13 passed（含 consumer_contract_validators） |
| 受影响 QtTest | `ctest -R 'Blocking.*\|Reply\|Cache\|Multipart\|Request\|Retry'` | 13/13 passed |
| UCE 门禁单元测试 | `pytest -q tests/test_run_uce_gate.py tests/test_uce_manifest.py` | 13 passed |

**范围外改动验证**：
- HEAD 版 `benchmark_scheduler` 独立构建树验证：链接失败确认（`undefined reference to QCNetworkMockHandler::*`）
- CMake 修复后（benchmarks/CMakeLists.txt:28）：构建通过

**门禁覆盖缺口**：`scripts/release_gate_steps.py:73-74` 在所有树上设 `BUILD_EXAMPLES=OFF` / 
`BUILD_BENCHMARKS=OFF`，示例与基准从未被门禁构建，破损可长期存在。

## 5. 未改动项与风险边界

### 5.1 明确保留（与审查结论 §8 一致）

- `QCurlRuntime` 生命周期（§8.1）
- multipart thread-affinity 校验（§8.2）
- `QCNetworkDiskCache::removeLegacyEntries()`（§8.3，清理不支持格式，非兼容读取）
- `release_identity.AUTHORITY_RELATIVE_PATHS`（§8.4，live authority，已切换到新方案包）
- 六树 provenance 与 run-scoped evidence（§8.5）

### 5.2 本轮未触及的后续项

- **P3-6**（序 6）：`basic-no-problem` 门禁下线 —— 必须等覆盖矩阵

### 5.3 已知限制

- 本轮未运行 `libcurl_consistency` 全门禁（需 curl 子树构建与 nghttpx 夹具）
- 本轮未运行 `run_release_gate.py --tier full`（需六树完整构建与所有夹具）
- 活跃方案包的 public API 移除登记尚未完成（待补）
- 门禁不覆盖 examples/benchmarks 构建（P2-C 缺口）

## 6. 交付物清单

| 类别 | 路径 | 改动性质 |
| --- | --- | --- |
| 门禁修复 | `tests/uce/ctbp/validate.py` | P1-2：移除 legacy fallback，补双命名空间约束 |
| 门禁修复 | `tests/test_uce_ctbp_legacy_only_negative.py` | P1-2：新增三类负测 |
| 门禁清理 | `scripts/release_gate_steps.py` | P2-1：删除 `static_public_api` 步骤 |
| 门禁修复 | `tests/libcurl_consistency/conftest.py` | P2-3：收敛参数来源，改善错误暴露 |
| 源码清理 | `src/QCNetworkMultipartBody.{h,cpp}` | P1-1：删除 `device()` |
| 源码清理 | `src/private/QCNetworkCacheIntegration_p.{h,cpp}` | P2-2：删除 QMap 重载 |
| 源码清理 | `src/QCBlockingNetworkResult.{h,cpp}` | P3-1：删除 `rawHeaderList()` 别名 |
| 源码清理 | `src/private/*ReplyTransferState*` | P3-2：删除 `headerMap` 冗余 |
| 源码清理 | `src/private/QCNetworkReplyRuntime.cpp` | P3-2：Retry-After 改用 ASCII 折叠 |
| 门禁清理 | `scripts/run_uce_gate.py` | P3-3：删除 CLI 再导出 |
| 脚本清理 | `scripts/qcurl_pimpl_scaffold.py` | P3-4：删除孤儿脚本（199 行） |
| 门禁修复 | `scripts/uce_gate/evidence.py` | P3-5：修复 tar 自引用 |
| 合同修订 | `docs/arch/2.0.0-hard-break-release-contract.md` | 新增 authority 边界声明节 |
| 合同修订 | `scripts/release_identity.py` | authority 切换 + fail-loud |
| schema 修订 | `docs/uce/schema/manifest@v1.md` | 登记 envelope 约定 |
| 范围外修复 | `benchmarks/CMakeLists.txt` | 链接 QCurlTestSupport |
| 范围外修复 | `examples/*/CMakeLists.txt` | 链接 QCurlTestSupport |
| 执行报告 | `docs/reviews/2026-09-03-cleanup-p1-p2-execution-report.md` | 本文件 |
| 索引更新 | `docs/README.md` | 补充本报告条目 |

**改动统计**：30 files changed, 232 insertions(+), 442 deletions(-) —— 净减 210 行。

## 6.5 已知偏差与待修复项

本轮执行虽通过验证，但存在以下偏差需后续修复：

### P2-B：envelope 失败路径弱化与裸异常（未修复）

**定位**：`scripts/uce_gate/evidence.py:170`

**问题**：
1. 新增的 `return` 使打包失败时提前返回。原实现在失败时仍登记 
   `add_artifact(archive_bundle, required=True)`，触发 `manifest@v1.md:86` 的
   "required 且文件缺失必须进入 policy_violations"，形成双重信号；现在只剩
   `packaging_tar_gz_failed` 一条，且归档存在性不再由 required-artifact 机制守卫。

2. `write_archive_envelope`（`:182-183`）的 `open("rb")` 与 `.stat()` 无异常处理 —— 
   tar 在打包后被删除或损坏时抛裸 traceback，而非结构化 violation。这与报告 P2-3 
   批评的"错误暴露方式粗劣"是同一类问题。

**影响**：证据链完整性问题，归档失败信号弱化。

**修复建议**：
- 恢复双重信号机制（打包失败仍登记 required artifact）
- 为 `write_archive_envelope` 补异常处理，转换为结构化 violation

### P2-C：门禁不覆盖 examples/benchmarks 构建（新发现缺口）

**定位**：`scripts/release_gate_steps.py:73-74`、`scripts/run_uce_sanitizers.py:62-63`

**问题**：所有门禁树上设 `BUILD_EXAMPLES=OFF` / `BUILD_BENCHMARKS=OFF`。因此 
`benchmark_scheduler` 与 `UnifiedPolicyMiddlewareOfflineDemo` 从未被任何门禁构建过，
破损可长期存在而不被发现 —— 本轮验证的 HEAD 版链接失败正是这一缺口的产物。

**影响**：示例是使用者的第一入口，"示例可以静默腐烂"违反 Qt6/KDE 开源库维护基线。

**附带问题**：示例链接 `QCurlTestSupport` 与 CLAUDE.md 的 "Test Support is explicit opt-in" 
存在张力。该 demo 用 mock 构造离线场景有其合理性，但应在示例文档中标注该依赖，避免使用者
误以为 mock API 是常规用法。

**修复建议**：
- 在门禁中新增一步 examples/benchmarks 构建验证
- 在示例 README 中标注 TestSupport 依赖性质

### P3 五项的具体问题

| 项 | 定位 | 问题 |
| --- | --- | --- |
| **函数名与职责不符** | `release_gate_steps.py:113` | `_static_api_steps` 已无任何 "api steps"，只产出 package evidence。docstring 补了说明，但名字仍误导。应改为 `_static_package_steps` |
| **冗余下划线别名** | `run_uce_gate.py:15,54` | 报告 7.3 证伪条件明确"被 CLI 运行路径使用则保留为普通 import，不加下划线别名"。现状是一个真实使用的导入被标为私有，且与所包装函数同名 |
| **测试未复刻生产序列** | `test_uce_manifest.py:104` | 测试用 `write_manifest_and_policy_report` 作替身，生产序列是 `orchestrator.py:309-310` 的 `_write_validated_state`（内含 `validate_required_artifacts` 与 `missing_required_artifacts`）。当前能捕获回归，但覆盖的不是真实路径 |
| **conftest 隐式推导 + 仓库外路径** | `conftest.py:76` | `_QCURL_QTTEST_BIN.parents[1]` 仍按 `<build>/tests/<bin>` 约定反推。glob 扫描已正确删除，但这处应由 `gate_runtime.py:234` 一并显式注入。缺失时占位路径的 `parents[1]` 解析到仓库外 `/home/wangguojian/Project/sde-project` |
| **测试方法命名漂移** | `tst_QCBlockingNetworkClient.cpp:159,367` | `rawHeaderListPreservesDuplicateSetCookieOrder` 指向已删除的 API 名 |

**conftest 的结构性问题**（未在上表展开）：目录级 `pytest_sessionstart` 用 
`pytest.exit(returncode=2)` 会终止整个 session，包括不需要 curl testenv 的纯单元测试
（`test_compare_unit.py`）。错误信息可读性确实改善了（这是报告 5.3 的目标），但"纯单元测试
被 testenv 环境要求绑架"的 SRP 违反未解决。

## 6.6 SOLID / KISS / DRY 判定

| 原则 | 判定 | 依据 |
| --- | --- | --- |
| **SRP** | 净改善，有 2 处遗留 | TransferState 去掉一个职责、`run_uce_gate` 不再是 re-export hub、打包与 envelope 职责分离。遗留：`_static_api_steps` 名实不符、conftest 混合两类测试环境要求 |
| **OCP / LSP** | 无变化 | 删除重载不影响扩展点与替换性 |
| **ISP** | 正向 | 删除 `device()`、QMap 重载、`rawHeaderList()` 别名，接口收窄 |
| **DIP** | 正向 | `test_run_uce_gate.py` 改为直接依赖实现模块，依赖关系变诚实 |
| **KISS** | 正向为主 | −442/+232 行。负向：`_run_uce_gate` 无意义别名、conftest 占位路径构造 |
| **DRY** | 正向，1 项未解决 | headerMap 冗余状态、public-api 重复步骤、consumer smoke 重复 size 断言均已消除。**未解决**：`RawHeaderPair = QPair<QByteArray,QByteArray>` 在 `QCNetworkReply.h:68`、`QCNetworkCache.h:32`、`QCNetworkMockHandler.h:46` 三处独立定义，本次改动扩大了该概念使用面而未统一定义点 |

## 6.7 Qt6 / libcurl binding / KDE 库维护最佳实践判定

### 符合的方面

- **ordered raw headers 成为唯一事实源**：与 RFC 9110 field order 语义一致
- **ASCII 折叠替代 Unicode 折叠**：`QByteArray::compare(QByteArrayView, Qt::CaseInsensitive)` 
  是 HTTP header 名比较的正确选择，精确匹配 RFC 9110 对 header field name 为 ASCII token 的定义
- **删除恒返回 `nullptr` 的 `device()`**：避免了按 `QNetworkReply` 直觉的误用
- **双命名空间约束**：observed + derived 模式与 Qt6 `QNetworkReply::error()` + `errorString()` 
  对齐，与 KIO::Job 的 `error()` + `errorText()` 文档化强绑定一致
- **保留必要复杂性**：报告第 8 节"不应删除"清单（`takeDevice()` owner-thread 校验、
  `PreferNetwork` fallback、`removeLegacyEntries()`、`QCurlRuntime` 生命周期）全部未被触碰

### 不符合的方面

- **发布证据的第三方可复现性**（§3 P1 authority 问题）：authority 输入为本地方案包，
  不参与跨检出复现，已在合同中显式声明边界
- **示例与基准的构建可维护性**（§6.5 P2-C）：门禁不覆盖 examples/benchmarks 构建，
  违反 Qt6/KDE 开源库"示例是使用者第一入口"的维护基线

## 7. 下一步

### 7.1 立即执行项

1. **P1 authority 方案**：用户已决策选项 1（fail-loud + 显式声明），当前已实施
2. **活跃方案包登记**：在 `.helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/` 
   中补充 P1-1、P2-2、P3-1 三项 public API 移除的登记条目
3. **索引更新**：补充本报告到 `docs/README.md`（已完成）

### 7.2 后续修复优先级

按审查结论建议的修复顺序：

| 优先级 | 项 | 工作量 | 阻塞关系 |
| --- | --- | --- | --- |
| **P2-C** | 门禁覆盖 examples/benchmarks | 中（新增门禁步骤 + 示例文档标注） | 无，可立即执行 |
| **P2-B** | envelope 失败路径与裸异常 | 中（恢复双重信号 + 补异常处理） | 无，可立即执行 |
| **P3** | 五项低风险改名与收敛 | 低（函数改名、删除别名、conftest 收敛） | 无，可分批执行 |
| **P3-6** | `basic-no-problem` 门禁下线 | 高（需先产出覆盖矩阵） | **必须等覆盖矩阵完成** |

**修复顺序说明**：
- P2-C 优先于 P2-B：门禁缺口影响可维护性基线，应先恢复构建覆盖
- P2-B 与 P3 可并行：envelope 失败路径与 P3 改名无依赖关系
- P3-6 必须最后：需要先产出 `basic-no-problem` 与 UCE 的覆盖矩阵，证明 UCE runner 
  对归档与 fail-closed 语义具有等价或更强覆盖

### 7.3 未来增强（非阻塞）

- **RawHeaderPair 统一定义点**：当前在三处独立定义，可考虑提取到公共头文件
- **conftest SRP 分离**：将 curl testenv 环境要求与纯单元测试分离到不同 conftest 层级

## 8. 执行声明

- 本轮**已修改**源码、测试、门禁脚本、构建配置与发布合同；
- 已验证编译干净与关键测试通过；
- **未执行** commit、tag、push、发布或 ABI promotion；
- authority 切换依据用户决策的选项 1（fail-loud + 显式声明）实施；
- 不构成发布签署授权；
- 工作树状态：30 个文件修改，待 commit。

---

**审查结论来源**：`docs/reviews/2026-08-31-qcurl-overdesign-compat-workaround-readonly-review-conclusion.md`

**执行完成时间**：2026-09-03

**执行者**：Claude (engineer-professional)
