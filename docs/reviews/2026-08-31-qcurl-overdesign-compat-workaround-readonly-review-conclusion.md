# QCurl 过度设计 / 兼容层 / workaround 综合只读审查结论

> 审查日期：2026-08-31
>
> 源码基线：`master-tmp@49c227610b24a92f0bb351bd552521b0b1459d14`
>
> 工作树状态：除本报告及其 `docs/README.md` 索引条目外，未修改任何内容
> （`git status --short` 仅含本报告自身与该索引行；`git diff --check` 通过）
>
> 发布 authority：`docs/arch/2.0.0-hard-break-release-contract.md`
>
> 方案包 authority：`.helloagents/plans/202608051224_qcurl_v2_comprehensive_review_remediation/`
>
> 审查范围：`src/`、`tests/`、`scripts/`、`docs/` 全量；目标是识别可删除的过度设计、
> 历史兼容层、workaround/fallback、重复门禁与失效扩展点
>
> 文档性质：只读审查结论与重构建议；本轮**未修改**任何源码、测试、构建配置或
> Git 状态（除本报告及其 `docs/README.md` 索引条目外），不构成发布签署、ABI promotion、
> commit、tag、push 或 release 授权

<!-- markdownlint-disable MD013 -->

## 1. 最终结论

本轮审查的核心判断是：**QCurl 当前残留的兼容层数量不大，但其中三处属于"让门禁在应该
变红时变绿"的失效证据机制，危害显著高于普通冗余代码。**

必须同时保留以下两个判断：

1. **绝大多数复杂性有对价。** `QCurlRuntime` 生命周期、libcurl share poison/quarantine、
   六树 provenance、run-scoped consistency evidence 均为已裁决的可靠性合同，不能仅因
   "复杂"而删除（详见第 8 节）。
2. **已被授权删除的兼容层从未真正执行删除。** `docs/arch/2.0.0-hard-break-release-contract.md:70`
   明确授权移除 public API 与 ABI 符号，`:103`/`:109` 确立非稳定 ABI 且默认
   `--abi-mode none`。因此本报告列出的兼容桩**没有任何剩余的合同理由**，它们是
   "已授权但未执行的清理"，而非"需要谨慎评估的破坏性变更"。

本轮裁决如下：

| 类别 | 数量 | 当前裁决 |
| --- | ---: | --- |
| P0 | 0 | 未发现当前产品运行时行为错误 |
| P1 | 2 | 失效 public 兼容桩；CTBP 接受 legacy-only error 导致假绿 |
| P2 | 3 | 静态 public-api 步骤零证据；缓存 QMap 兼容重载；consistency conftest 环境发现缺陷与 H3 静默降级 |
| P3 | 6 | 见第 7 节，均可延后 |
| 明确不删除 | 5 组 | 见第 8 节 |
| 总体 | - | **CLEANUP AUTHORIZED；THREE FALSE-GREEN MECHANISMS PRESENT；NO PRODUCT-BEHAVIOUR DEFECT FOUND** |

因此：

- 三处"假绿"（P1-2、P2-1，以及 P2-3 子项 B 的 H3 静默降级）应优先修复，**先恢复门禁
  约束力，再改被门禁覆盖的代码**。三者危害不同量级：P1-2 与 P2-1 无任何缓解，P2-3 子项 B
  有五层已实现的 fail-closed 缓解（见 5.3 节），故排在第 4 序；
- P1-1 与 P2-2 的删除风险极低（零调用面 + 合同授权），可一次做完；
- P3 中 `basic-no-problem` 存在有效的文档化约束，**当前不可下线**。

## 2. 审查基线与方法

### 2.1 基线确认

| 项 | 值 |
| --- | --- |
| 分支 | `master-tmp` |
| HEAD | `49c227610b24a92f0bb351bd552521b0b1459d14` |
| 工作树 | 除本报告及其 `docs/README.md` 索引条目外无改动（受审对象未被触碰） |
| 项目版本 | `project(QCurl VERSION 2.0.0)`（`CMakeLists.txt:2`），即开发候选版本 |
| 已发布版本 | `v1.0.0`（合同 `:5`）；与 `CMakeLists.txt` 的 `2.0.0` **不矛盾** |
| 知识图谱 | 7,529 nodes / 70,334 edges / 627 files，与 HEAD 一致 |

### 2.2 方法与取证纪律

本轮按以下顺序取证，并对结论强度做了明确区分：

1. **先用 `code-review-graph` 做导航**：确认图谱与 HEAD 一致，取 29 个社区与高耦合边界
   （`uce-utc` ↔ `uce-gate-gate`：74；`src-qcnetwork` ↔ `private-reply`：67；
   `infrastructure-identity` ↔ `libcurl-consistency-id`：41）以划定审查范围。
2. **图谱结果不单独作为结论依据**。图谱对 Python 动态调用存在名称解析误报，历史上已出现
   误判 dead code 的情况；所有"零调用面"结论均由源码检索独立复核。
3. **对可执行判断补最小探针**：CTBP 假绿与 release gate 重复步骤均以实际运行验证，
   不停留在读码推断。
4. **历史 PASS 不移植到当前 HEAD**：执行台账固定在旧提交 `15f4d2e`，本轮只将其作为
   *决策来源*，不作为当前基线的绿灯证据。

### 2.3 一处取证纠正

审查过程中 `rawHeaderList` 的检索输出出现标识符被改写的情况（可由残留的 mangled 符号
`...13nEv` 的长度前缀 `13` 反推原名为 13 字符）。该批输出已作废，相关结论改用
逐文件精确复核重做——这直接影响了 P3-1 的正确性判断（见 7.1 节的双类同名 API 区分）。

## 3. 已参考的历史依据

**发布状态与 C1 张力（阅读本节前须知）**：合同 `:5-20` 明确最新已发布为 `v1.0.0`、当前候选
为 `v2.0.0`、状态 `REMEDIATION_REQUIRED / NOT_READY_FOR_RELEASE`，且 `C1` 为唯一 final
candidate；而方案包 `plan.md:142` 记录 T12-T13（创建 C1/C2）**已取消**。即：合同要求 C1，
方案包取消 C1。该张力不影响本报告的清理授权（第 11 节已声明不构成发布签署），但读者需知悉
当前不存在满足合同的 final candidate。

附带澄清：`CMakeLists.txt:2` 的 `2.0.0` 与 `docs/README.md` 的 `1.0.0` **不矛盾** —— 前者是
开发候选版本，后者是已发布版本；本报告选用 2.0.0 合同作为 v1→v2 变更授权依据是正确的。


| 来源 | 用途与结论 |
| --- | --- |
| `.helloagents/plans/202608051224_qcurl_v2_comprehensive_review_remediation/` | 当前活跃方案包。T1–T11 已完成，T12–T13（C1/C2、baseline promotion、发布）已明确取消（`plan.md:142`）。发布状态保持 `NOT READY FOR RELEASE`（合同 `:5-20` 定义为 `REMEDIATION_REQUIRED / NOT_READY_FOR_RELEASE`；C1 为唯一 final candidate，但已被取消）。方案包 authority 路径仍被 `scripts/release_identity.py:26` 固定引用，属 **live authority**，不可误判为历史依赖 |
| `docs/arch/2.0.0-hard-break-release-contract.md` | 本轮最关键依据。`:70` 授权移除 public API/ABI 符号；`:103` 确立 source-compatible / rebuild-required / 非稳定 ABI；`:109` 默认 `--abi-mode none`；`:135` 明确 supersede 方案包中未完成的 ABI promotion 阶段；`:168-170` 明确 v2 ABI 基线、兼容性 diff、baseline promotion **均非发布要求** |
| `.helloagents/archive/2026-05/202605151522_qt6_libcurl_kde_api_hard_break_cleanup/decisions.md` | hard-break 决策，允许删除旧 API 形态 |
| `.helloagents/archive/2026-06/202606182013_qcurl_libcurl_binding_hardening/decisions.md` | binding 加固决策，确认 ordered raw headers 为唯一事实来源 |
| `.helloagents/archive/2026-08/202608171721_qcurl_libcurl_consistency_followup_remediation/` | consistency 整改，要求删除 legacy `payload.error` producer |
| `.helloagents/archive/_index.md` | 8 月已有运行时、Qt/C++17、libcurl consistency、发布 evidence 多轮整改记录 |
| `.helloagents/sessions/master-tmp/host-b99d2570/artifacts/qcurl-v2-execution-ledger.json` | 执行台账，**固定在旧提交 `15f4d2e`**；仅作决策来源，不作当前 HEAD 证据 |
| `docs/uce/README.md:111` | 明确约束：UCE runner 未稳定接管归档与 fail-closed 语义前，不下线 `basic-no-problem` |

### 3.1 合同授权的原文依据

`docs/arch/2.0.0-hard-break-release-contract.md:70`：

> The v1 to v2 transition may remove public APIs and ABI symbols named by the active plan.
> It does not relax memory safety, thread ownership, protocol semantics, error propagation,
> local-fixture requirements, or release evidence.

这一条同时给出了授权与边界：**可以删 API 与 ABI 符号，但不得放松内存安全、线程所有权、
协议语义、错误传播、本地夹具要求与发布证据**。本报告的全部建议都落在该边界内——第 8 节
的"不应删除"清单正是这条边界的直接推论。

注意 `:70` 要求移除项须"named by the active plan"。因此下述 P1-1、P2-2、P3-1 的删除
需在活跃方案包中登记；这是流程步骤，不构成技术阻塞。

## 4. P1 findings

### 4.1 P1-1 失效的 public 兼容桩：`QCNetworkMultipartBody::device()`

**定位**

| 位置 | 内容 |
| --- | --- |
| `src/QCNetworkMultipartBody.h:80` | 注释：`/// 兼容查询入口；流式描述在 takeDevice() 内直接创建并转移 wrapper，因此返回 nullptr。` |
| `src/QCNetworkMultipartBody.h:81` | `[[nodiscard]] QIODevice *device() const noexcept;` |
| `src/QCNetworkMultipartBody.cpp:186-189` | 函数体只有 `return nullptr;` |

**事实**

- 注释自述为"兼容查询入口"，实现恒返回 `nullptr`，无任何条件分支。
- 全仓 `src/`、`tests/`、`examples/` 中对 `.device()` / `->device()` 的调用面为**零**
  （检索已排除 `takeDevice` 与 `fromSingleFileDevice`）。
- 公共契约守卫**未登记** `device()`：`tests/public_api/consumer_contract_validators.py:322`
  断言的是 `singleFileMultipart->takeDevice(&app, &multipartError)`，
  `tests/public_api/hard_break_guards.py:42` 断言的是 `fromSingleFileDevice`。
- 该符号仅出现在 v1 基线 `abi/baseline/qcurl-core-v1.abi.xml:1649`
  （`_ZNK5QCurl22QCNetworkMultipartBody6deviceEv`，`visibility='protected-visibility'`），
  而该基线在 `--abi-mode none` 默认下不参与门禁。

**影响**

向使用者承诺了一个恒为 `nullptr` 的查询入口。任何按 Qt 直觉写
`if (body.device()) { ... }` 的调用方都会静默走空分支，而不会收到编译期错误。这比
"无用代码"更坏：它把**编译期可捕获的误用降级为运行期静默错误**。

**唯一重构路线**

删除声明与实现。不加 `[[deprecated]]`、不保留转发实现——`takeDevice()` 的语义
（owner-thread 校验 + 所有权转移）与 `device()` 的 const 查询语义不可互相表达，保留任何
形式的桩都会重新引入歧义。按合同 `:70` 在活跃方案包中登记本次移除。

**首个证明点**

删除后 `cmake --build build --parallel` 通过，且 `ctest -L '^public-api$'` 全绿——
证明既无编译期依赖，也无契约面依赖。

**证伪条件**

若在 `examples/`、`docs/` 代码样例或任何外部 consumer fixture 中发现对 `device()` 的
真实读取，则改为先在合同中登记为显式移除项、再执行删除。

**裁剪 / 止损**

只动上述两处。**不要**顺手调整 `takeDevice()` 的线程与所有权契约，也不要改动
`fromSingleFileDevice` 的签名——两者都在 hard-break guard 的断言范围内。

### 4.2 P1-2 CTBP 校验接受 legacy-only error，产生假绿

**定位**

| 位置 | 内容 |
| --- | --- |
| `tests/uce/ctbp/validate.py:77` | `"observed_error": observed.get("error") ...` —— 收集后**无任何读取点**（死字段） |
| `tests/uce/ctbp/validate.py:79` | `"legacy_error": payload.get("error") ...` —— 收集顶层 `payload.error` |
| `tests/uce/ctbp/validate.py:104` | `def _error_kind(entry: dict[str, Any]) -> str:` |
| `tests/uce/ctbp/validate.py:109` | `return str(derived.get("kind") or legacy.get("kind") or "")` —— 回退到 legacy |

**事实**

- `:77` 收集的 `observed_error` 当前**全仓零读取点**（唯一引用即 `:77` 自身的收集代码；
  `tests/libcurl_consistency/pytest_support/compare_shared.py:100,102` 是另一模块的同名
  局部变量，在该模块内自洽使用，与本项无关）。
- `_error_kind()` 当前判定链只有 `derived → legacy`，**未包含 `observed`**。
- **最小负向探针已实际执行**：构造四个 runner/kind 完整工件、只保留顶层
  `error.kind=tls`（不写 `observed.error` 与 `derived.error`），校验结果为
  `policy_violations=[]`、`violations=[]`、`entry_count=4` —— **完全通过**。
- 历史 consistency 整改（`.helloagents/archive/2026-08/202608171721_.../`）要求删除
  legacy `payload.error` producer。

**影响**

本轮最严重的一项。已被判定应下线的 producer 形态仍能让门禁全绿，等于门禁**对该迁移
失去了约束力**：若某个 producer 回退到旧写法，CTBP 不会报警，而迁移完成度将无法通过
门禁度量。这是典型的假绿——不是缺少检查，而是检查主动接受了应被拒绝的输入。

**唯一重构路线**

`_error_kind()` 应改为接受 `observed.error` 与 `derived.error`；当前该修复是**新增**
`observed` 为判定来源（`:77` 收集后未被任何处消费），而非收紧现有路径。同时移除 `:79` 的
`legacy_error` 与 `:77` 的 `observed_error` 两个收集字段（前者应拒绝、后者当前为死字段），
避免留下"能读但不判"的中间态（保留字段会让下一个维护者以为它仍有语义）。TLS failure 要求
`observed` 与 `derived` 两个命名空间同时存在。补一条 legacy-only 负测，断言其**必须**
产生 violation。

**首个证明点**

新增的 legacy-only 负测在改动前为 PASS（正是它暴露了假绿）、改动后转为被断言捕获的
预期失败；同时 `pytest -q tests/test_uce_ctbp.py` 维持 3 passed。

**证伪条件**

若仍存在在编 producer 只写顶层 `error`，则**先迁 producer、再收紧 validator**，顺序
不可颠倒——否则门禁会以正确理由变红并阻塞在编工作。

**裁剪 / 止损**

不改 `policy_violations` 字典口径。`docs/uce/schema/manifest@v1.md:130` 明确要求
`basic-no-problem`、`libcurl_consistency` 与后续 UCE provider 共用一套 code 语义，
本项不得引入第四套。

## 5. P2 findings

### 5.1 P2-1 `static_public_api` 未验证静态树，提供零静态证据

**定位**

| 位置 | 步骤名 | 目标树 |
| --- | --- | --- |
| `scripts/release_gate_steps.py:41` | `shared_public_api` | `args.test_shared_gcc_build_dir`，tree_id `test-shared-gcc` |
| `scripts/release_gate_steps.py:120` | `static_public_api` | `args.test_shared_gcc_build_dir`，tree_id `test-shared-gcc` |

**事实**

- 两步命令逐字相同：
  `[args.ctest, "--test-dir", str(test_gcc), "-L", "^public-api$", "--output-on-failure"]`。
- 两处 `test_gcc` 均取自 `getattr(args, "test_shared_gcc_build_dir", None)`，tree_id 同为
  `test-shared-gcc`。已用 `build_steps()` 探针确认两条命令完全一致。
- `full_ctest` 还会第三次覆盖同一 CTest 集合。

**影响**

**比"重复执行"严重得多**：名为 `static_public_api` 的步骤根本没有触及静态树，却在发布
证据中登记为"静态 public API 已验证"。实际结果是**静态链接下的 public API 无人验证**，
而发布 manifest 会显示该项通过。这是第二处假绿，且直接影响发布结论的可信度。

**唯一重构路线**

删除 `static_public_api`。strict 层由 `shared_public_api` 保留唯一一次 public-api 执行；
full 层由 `full_ctest` 覆盖；静态链接的证明只保留 install/consumer/lifecycle 的
package evidence（这些确实读取 `release_static_build_dir`）。

若确实需要静态树的 public-api 验证，那是**新增能力**：应显式指向静态构建目录并新增对应
测试目标，不能靠沿用共享树 + 改步骤名来充数。

**首个证明点**

`pytest -q tests/test_release_gate_unit.py` 维持 54 passed，且 `build_steps()` 输出中
匹配 `^public-api$` 的命令数由 2 降为 1、静态 artifact 集合不变。

**证伪条件**

若发布合同明确要求"静态树必须独立跑一次 public-api"，则本项不是删除，而是把
`--test-dir` 修正为静态测试树——但那需要静态树中实际存在 `public-api` 标签的测试，
需先确认该前提。

**裁剪 / 止损**

不动 `package_evidence_step` 的 artifact id 与 linkage 标记；发布 manifest 的字段口径
保持不变，避免 `abiMode` / artifact 集合校验连带失效。

### 5.2 P2-2 私有缓存入口保留丢失语义的 QMap 兼容重载

**定位**

| 位置 | 内容 |
| --- | --- |
| `src/private/QCNetworkCacheIntegration_p.h:33` | 注释：`/// 兼容旧调用方；当 rawResponseHeaders 非空时仍以原始头为准。` |
| `src/private/QCNetworkCacheIntegration_p.h:34-39` | `QMap<QByteArray, QByteArray>` 重载声明 |
| `src/private/QCNetworkCacheIntegration.cpp:329-350` | 重载实现，含降级分支 |

**事实**

- `buildCacheMetadata` 全仓仅 **3 个调用点**，且全部传 ordered raw headers：
  - `QCNetworkReplyCache.cpp:84` 传 `reply->finalHeaderList`（canonical）
  - `QCNetworkReplyCache.cpp:115` 传 `mergedRawHeaders`（canonical）
  - `QCNetworkReplyCache.cpp:198` 传 `mergedRawHeaders`（canonical）
- **QMap 重载调用面为零**。
- `:336` 在 `rawResponseHeaders` 非空时转发到 canonical 重载（即正常路径下该重载只是一层
  转发）。
- `rawResponseHeaders` 为空时走降级分支：
  - `:342` `metadata.setHeaders(responseHeaders)` —— `QMap` 折叠重复 header；
  - `:347` `metadata.setCorrectedInitialAgeSeconds(std::numeric_limits<qint64>::max())`；
  - `:348` `metadata.setExpirationDate(responseTime)`。
- `:352` 的 `responseHeadersAreCacheable()` 已把 ordered raw headers 定为 cache admission
  的唯一输入事实（`_p.h:41` 注释亦如此声明）。

**影响**

降级分支产出的是"重复头被折叠 + 立即过期"的缓存条目——即一个语义已损坏的条目。它作为
private 入口始终可达：任何新调用方漏传 raw headers 就会**静默得到无效缓存条目，而不是
编译错误**。

更强的判断是：合同 `:72` 用完成时态断言该形态**已不存在**：

> The three lossy `QMap<QByteArray, QByteArray>` policy overloads and their conversion
> helper are **absent**.

实测三个 lossy overload 中已删两个，**残留一个**（`_p.h:34-39`）。因此这不是"实现与合同
要求冲突"，而是**合同的这条事实性陈述当前为假** —— 合同声称的清理只完成了三分之二。
修复本项同时也修复了该合同陈述的准确性。

但实测 QMap 重载调用面为**零**，删除风险从"极低"降为 trivially safe，与 P1-1 同级。

**唯一重构路线**

删除该 private 重载及其降级分支，只保留 ordered raw headers 的 canonical 签名。

**首个证明点**

删除后编译通过（即证明无残留调用方），且 `ctest -R QCNetworkCacheIntegration` 全绿。

**证伪条件**

若存在只能拿到 `QMap` 的真实调用路径，则在**调用点**做一次显式
`QMap` → ordered pairs 转换并明确记录"重复头已丢失"，不在缓存层内部隐藏该降级。

**裁剪 / 止损**

**不要误删** public metadata 的 `headers()` / `setHeaders()` —— 那是观察与存储接口，
不是兼容层。同样保留 `QCNetworkDiskCache::removeLegacyEntries()`（见 8.3 节）。

### 5.3 P2-3 consistency conftest 的环境发现缺陷与 H3 静默降级

> **本项包含两个失败模式相反的子机制**，定位表按子项分组。子项 A 是**响亮失败但暴露方式
> 粗劣**（不计入假绿）；子项 B 是**真正的静默降级**（第三处假绿）。

**定位（子项 A：环境发现与错误暴露）**

| 位置 | 内容 |
| --- | --- |
| `tests/libcurl_consistency/conftest.py:29` | `_DEFAULT_CURL_BUILD_DIR = _REPO_ROOT / "build" / "curl"` |
| `tests/libcurl_consistency/conftest.py:33` | `os.chdir(str(_TESTENV_IMPORT_CWD))` —— import 阶段切换 cwd |
| `tests/libcurl_consistency/conftest.py:40-41` | `os.environ.setdefault("CURL_BUILD_DIR", ...)` / `setdefault("CURL", ...)` |
| `tests/libcurl_consistency/conftest.py:50` → `:101` | `except Exception as exc:` 宽泛捕获后 `pytest.skip(allow_module_level=True)`；**实测为 config-parse 期 traceback + exit 1**，非静默跳过 |
| `tests/libcurl_consistency/conftest.py:149` | `for build_dir in sorted(_REPO_ROOT.glob("build*")):` —— 扫描任意构建目录猜 QtTest |

**定位（子项 B：H3 能力静默降级）**

| 位置 | 内容 |
| --- | --- |
| `tests/libcurl_consistency/conftest.py:182-187` | H3 能力探针 `except Exception` 降级为 `nghttpx_with_h3 = False` |
| `tests/libcurl_consistency/conftest.py:342-349` | 同上，Nghttpx 版本探针的 H3 降级 |

**事实**

- `tests/libcurl_consistency/pytest_support/gate_runtime.py:234` 的 `gate_environment()`
  已**显式注入全部所需变量**：`QCURL_QTTEST`、`CURL_BUILD_DIR`、`CURL`、`CURLINFO`、
  `QCURL_LC_CAPABILITY_MANIFEST`（以及 run-scoped 的 `QCURL_LC_RUN_ID`、
  `QCURL_LC_EXECUTION_TOKEN`、`QCURL_LC_ARTIFACTS_DIR`）。
- 因此 conftest 的默认值、`build*` 扫描与 import 期 `chdir` 对 gate 路径**全部是死重**，
  只服务于手工临时调用。
- 本轮验证中 `pytest tests/libcurl_consistency/test_compare_unit.py` 因缺少
  `build/curl/src/curl` 在 **config-parse 阶段**抛 traceback 并以 exit 1 结束 ——
  **该组当前未通过，不能计入绿灯**。

**影响**

该项实为两个失败模式相反的机制的复合：

**影响 #1：环境发现缺陷与错误暴露方式粗劣（非假绿）**

1. **import 期 `chdir` 约束源自上游**。`curl/tests/http/testenv/env.py:56` 直接取 import 期
   `os.getcwd()` 推导 `TOP_PATH`；`:65` 在模块加载期执行 `init_config_from(CONFIG_PATH)`；
   `:387` 在类体求值时实例化 `EnvConfig()` 并执行 `curlinfo`（`:162`）。因此必须在 import
   前设置正确 cwd —— **这是上游硬约束，非本仓自引入副作用**。
2. **`glob("build*")` 会在多构建树环境中绑定到错误的二进制而不报错**，使测试对象变得
   不确定。
3. **`:50` 的 `pytest.skip(allow_module_level=True)` 位于 conftest 而非测试模块**，pytest
   在 config 解析期将其视为错误。实测输出 `Traceback ... Skipped: testenv unavailable` 后
   以 **exit 1 结束**（§10 已如实记录），而非静默跳过。该处**意图**是静默跳过，**实际**是
   响亮失败（只是暴露方式粗劣、阶段过早）。因此本项不计入假绿。

**影响 #2：H3 能力静默降级（真正的第三处假绿）**

`:182-187` 与 `:342-349` 的 H3 探针 `except Exception → nghttpx_with_h3 = False` 形成完整
静默降级链路：

- `curl/tests/http/testenv/env.py:407` `have_h3_server()` 返回 `nghttpx_with_h3`
- `:557` `have_h3()` = curl h3 ∧ server h3
- `test_ext_suite.py:124-126` 静默只跑 h2；`:127-130` h3-only 用例 `pytest.skip` → **绿**
- `test_p0_consistency.py:137`、`test_p1_postfields_binary.py:45`、`test_env_smoke.py:12`
  三处 `if env.have_h3():` 条件纳入，能力缺失时静默少跑协议
- `gate_execution.py:83-84` `QCURL_REQUIRE_HTTP3` 默认未设 → **默认即静默降级模式**

**缓解层**（降低危害量级，但不消除假绿）：

- `capability_manifest.py:170-177` `guard_planned_test()` — manifest 标 disabled 而测试仍跑
  → `pytest.fail`
- `test_ext_http3_success_h3.py:36-37` — `have_h3()` 为假时 `pytest.fail`，**完全 fail-closed，
  不受静默降级影响**
- `gate_preflight.py:168` — `require_http3_enabled` 下缺 H3 server 产出 `missing_h3_server`
  violation（红）
- `gate_preflight.py:182-187` — 非 require 模式下 planner 以**显式 override + 记录 reason**
  排除 H3 用例，并产出 warning
- `test_p0_consistency.py:128-132` — `QCURL_REQUIRE_HTTP3=1` 提供 opt-in fail-closed

因此准确表述为：**默认模式下 H3 覆盖面会静默收窄（`test_ext_suite.py` 与三处条件纳入点），
但 gate 层有显式排除记录，且存在 opt-in 的 fail-closed 模式；H3 success 主用例本身已
fail-closed。** 这与 P1-2（CTBP 无任何缓解、legacy-only 直接全绿）、P2-1（静态步骤零证据、
无任何补偿）**不在同一量级**。

**唯一重构路线**

本项的目标是**收敛参数来源与错误暴露方式**，不是"消除自引入副作用"（副作用来自上游约束）。

针对影响 #1（子项 A：环境发现与错误暴露）：

- conftest 改为从环境读取必需变量，缺失即 `pytest.exit` / `fail`，**不扫描、不
  `setdefault`**；
- import 期 `chdir` 保留（上游 `env.py:56` import 期 `os.getcwd()` 硬约束，fixture 在
  import 之后运行，无法承载该约束），但改为**必需环境变量驱动 + 缺失即 fail**，取消
  `:40-41` 的 `setdefault` 与 `:149` 的 `glob("build*")` 扫描；
- 异常捕获缩小到 `ImportError` 与明确的 `EnvConfig` 错误类型，其余异常向上抛出；
- `:50` 的失败暴露方式从 config-parse 期 traceback 改为**收集期或用例期的明确失败信息**，
  使错误在正确阶段可读地呈现。

针对影响 #2（子项 B：H3 静默降级）：

- 保留五层已实现的 fail-closed 缓解，无需改动；
- opt-in `QCURL_REQUIRE_HTTP3=1` 模式已可用，文档化该模式为 H3 强制验证入口。

**首个证明点**

不带环境变量直接运行该目录，得到明确的"缺少 `CURL_BUILD_DIR`"失败信息（而非 collection
崩溃或静默跳过）；经 `run_libcurl_consistency_gate` 正常路径运行，结果与整改前一致。

**证伪条件**

若存在被文档化的手工调试入口依赖这些默认值，则保留一个显式的
`--lc-curl-build-dir` 命令行选项承载该需求，而不是恢复隐式扫描。

**裁剪 / 止损**

不改 H3/QUIC 能力探针的判定语义，也不改 capability manifest 的字段。本项只收敛
"参数从哪里来"与"错误如何暴露"两件事。

## 6. P3 findings 概览

| ID | 项 | 定位 | 裁决 |
| --- | --- | --- | --- |
| P3-1 | `QCBlockingNetworkResult::rawHeaderList()` 兼容别名 | `src/QCBlockingNetworkResult.h:72` | 迁移 4 处调用后删除 |
| P3-2 | `QCNetworkReplyTransferState::headerMap` 兼容索引 | `src/private/QCNetworkReplyTransferState_p.h:44` | 让 parser 直接消费 `finalHeaderList` |
| P3-3 | `run_uce_gate.py` 测试用再导出 | `scripts/run_uce_gate.py:22-33` | 保留 CLI，删再导出 |
| P3-4 | `qcurl_pimpl_scaffold.py` 孤儿脚本 | `scripts/qcurl_pimpl_scaffold.py` | 删除或补 owner/入口/测试 |
| P3-5 | UCE tar 内外 manifest 不一致 | `scripts/uce_gate/evidence.py:157-168` | 改为包内清单 + 包外 digest |
| P3-6 | `basic-no-problem` 门禁 | `scripts/run_basic_no_problem_gate.py` | **当前不可下线** |

## 7. P3 findings 详情

### 7.1 P3-1 `QCBlockingNetworkResult::rawHeaderList()`

> **重要前置区分：`rawHeaderList` 是两个不同类上的同名方法，只有其中一个是兼容层。**

| 类 | 位置 | 返回 | 语义 | 裁决 |
| --- | --- | --- | --- | --- |
| `QCNetworkRequest` | `src/QCNetworkRequest.h:79` | `QList<QByteArray>` | header **名**列表，与 `setRawHeader()` 配对 | **Qt 惯用 API，绝不可删** |
| `QCBlockingNetworkResult` | `src/QCBlockingNetworkResult.h:72` | `HeaderList`（键值对） | 注释自述 `headers()` 的兼容别名 | 可删 |

`QCNetworkRequest::rawHeaderList()` 有 **13 处**生产与测试调用，**与本项无关**：

| 位置 | 类别 |
| --- | --- |
| `src/QCNetworkAccessManagerSend.cpp:18` | 生产 |
| `src/private/QCRequestPipeline.cpp:168` | 生产 |
| `src/private/QCNetworkRetryDecision.cpp:15` | 生产 |
| `src/private/QCNetworkCacheIntegration.cpp:29`、`:279` | 生产 |
| `src/private/QCNetworkReplyCurlMethodOptions.cpp:208` | 生产 |
| `src/private/QCNetworkReplyMockExecution.cpp:46`、`:62` | 生产 |
| `src/private/QCBlockingCurlRequestSetup.cpp:118` | 生产 |
| `src/private/QCBlockingCurlRequestSetupExtensions.cpp:78` | 生产 |
| `tests/qcurl/tst_QCNetworkRequest.cpp:121` | 测试 |
| `tests/qcurl/tst_QCNetworkCacheIntegration.cpp:43` | 测试 |
| `tests/qcurl/tst_QCNetworkMiddlewareIntegration.cpp:205` | 测试 |

上表 13 处**全部**是 `QCNetworkRequest`（不可删的 Qt 惯用 API）。其中三处测试调用与本项
别名的调用面（下表 4 处）**在文本形态上无法区分** —— 朴素文本检索必然同时命中两类。
若按简单文本检索一并删除，将破坏请求头处理主链路。

**兼容别名的实际调用面**（仅 4 处真实调用）：

| 位置 | 形态 |
| --- | --- |
| `tests/qcurl/tst_QCBlockingNetworkClient.cpp:381` | `result.rawHeaderList()` |
| `tests/libcurl_consistency/tst_LibcurlConsistency.cpp:3216` | `result.rawHeaderList()` |
| `tests/public_api/consumer_blocking_extras_smoke/main.cpp:44-45` | `success.rawHeaderList()` ×2 |
| `tests/public_api/blocking_extras_contracts.py:34` | 契约清单登记 `"success.rawHeaderList()"` |

另有 `tst_QCBlockingNetworkClient.cpp:159,367` 两处测试方法名引用。

**路线**：`src/QCBlockingNetworkResult.h:70` 已将 `headers()` 定为 canonical。迁移上述 4 处
调用与契约清单条目，删除别名声明（`:72`）与实现（`.cpp:122`）。因 ABI 非稳定且
`--abi-mode none` 默认，无需处理 v1 基线中的对应符号。

**首个证明点**：`ctest -L '^public-api$'` 与 `ctest -R QCBlockingNetworkClient` 全绿。

**证伪条件**：若 `blocking_extras_contracts.py` 的契约被外部 consumer 直接引用为稳定清单，
则需先在合同中登记清单变更。

**止损**：本项**必须**与 `QCNetworkRequest::rawHeaderList()` 严格隔离，改动前先确认每个
调用点的接收者类型。

### 7.2 P3-2 `QCNetworkReplyTransferState::headerMap`

**定位**

| 位置 | 内容 |
| --- | --- |
| `src/private/QCNetworkReplyTransferState_p.h:44` | `QMap<QString, QString> headerMap;` |
| `src/QCNetworkReply_p.h:110` | `QMap<QString, QString> &headerMap;` —— 注释：`///< 最终响应头 block 的兼容索引` |
| `src/QCNetworkReply.cpp:35` | `, headerMap(transferState->headerMap)` —— 引用绑定 |
| `src/private/QCNetworkReplyResponse.cpp:38,63` | `clear()` 与 `insert()` 填充 |
| `src/private/QCNetworkReplyRuntime.cpp:115` | `retryAfter = parseReplyRetryAfterDelay(d->headerMap);` |
| `src/private/QCNetworkReplyRuntime.cpp:151` | `d->headerMap.clear();` |
| `src/QCNetworkReply_p.h:262` | `parseHeaders()` 文档注释：`将原始响应头数据（headerData）解析为键值对（headerMap）。` —— 删除成员时需同步更新 |

**事实**：注释自述为"兼容索引"。唯一实质消费者是 `:115` 的 Retry-After 解析；其余均为
生命周期维护（clear/insert/绑定）。`QMap<QString, QString>` 会折叠重复 header 并做
`QByteArray` → `QString` 转换，对 Retry-After 这一单值头无影响，但维持了一份与
`finalHeaderList` 并行的冗余状态。

**路线**：让 `parseReplyRetryAfterDelay()` 直接消费 `finalHeaderList`，随后删除
`headerMap` 成员、引用与全部填充点。**保留 `finalHeaderMap`** —— 它服务公开的
raw-header 查询，不是本项目标。

**首个证明点**：`ctest -R QCNetworkReply` 全绿，且 Retry-After 相关测试维持通过。

**证伪条件**：若存在依赖 `QString` 键大小写折叠语义的 Retry-After 用例，需先确认
`finalHeaderList` 的查找是否大小写不敏感，再迁移。

**止损**：不改 Retry-After 的解析语义与重试决策口径。

### 7.3 P3-3 `run_uce_gate.py` 的测试用再导出

**定位**：`scripts/run_uce_gate.py:22-33`，形如
`from scripts.uce_gate.contracts import run_ctbp_contract as _run_ctbp_contract`、
`... import run_hes_contract as _run_hes_contract`、
`... import run_timeline_contract as _run_timeline_contract`、
`from scripts.uce_gate.dci_contract import run_dci_seed_suite as _run_dci_seed_suite`、
`from scripts.uce_gate.execute import register_netproof_contract as _register_netproof_contract` 等。

**事实**：这些 `as _xxx` 别名以下划线前缀声明"私有"，实际用途是让测试从 CLI 模块导入
实现函数。真实实现位于 `scripts/uce_gate/` 各模块，CLI 只是转发层。

**影响**：CLI 模块承担了 re-export hub 的隐式职责（违反单一职责），且测试通过 CLI 间接
依赖实现模块，掩盖了真实的模块依赖关系。

**路线**：保留 `run_uce_gate.py` 的 CLI 入口与参数解析；删除仅为测试导入而存在的再导出，
让 `tests/test_run_uce_gate.py` 直接 import `scripts.uce_gate.*` 的真实模块。

**首个证明点**：`pytest -q tests/test_run_uce_gate.py` 维持 11 passed。

**证伪条件**：若某个再导出确实被 CLI 自身的运行路径使用（而非仅测试），则该项保留为普通
import，不加下划线别名。

**止损**：不改 CLI 的参数、退出码与 manifest 写入行为。

### 7.4 P3-4 `qcurl_pimpl_scaffold.py` 孤儿脚本

**定位**：`scripts/qcurl_pimpl_scaffold.py`（6,800 字节，最后修改 2026-04-08）。

**事实**：全仓引用面为**零** —— `CMakeLists.txt`、`cmake/`、`docs/`、`scripts/`、
`tests/`、`.github/` 中均无任何引用。无 workflow、无文档入口、无测试。

**影响**：属无主脚本。风险不在于占用空间，而在于它看似是受支持的开发工具：后续维护者
可能运行它并生成不符合当前 pimpl 约定的代码。

**路线**：二选一，不留中间态 ——

1. 删除；或
2. 补正式 owner、文档入口与最小测试，纳入维护范围。

考虑到它已 5 个月未被引用且当前 pimpl 约定已稳定，建议选 1。

**首个证明点**：删除后 `python3 scripts/run_release_gate.py --tier full` 的步骤集合不变
（证明它未被任何门禁路径引用）。

**证伪条件**：若存在未被检索覆盖的外部使用（如维护者本地脚本、CI 之外的手工流程），
则转为方案 2。

**止损**：不连带改动 `scripts/` 下其他脚本的导入结构。

### 7.5 P3-5 UCE tar 内外 manifest 不一致

**定位**：`scripts/uce_gate/evidence.py:157` 先执行 `tar_gz_dir(layout.evidence_dir, layout.tar_path)`，
`:162-168` 才 `add_artifact(manifest, artifact_id="archive_bundle", path=str(layout.tar_path), ...)`。

**事实**：打包发生在 `archive_bundle` 条目写入 manifest **之前**，因此磁盘上的 manifest
含 `archive_bundle`，而 tar **内部**的 manifest 副本不含该条目。已探针确认此差异存在。

**影响**：同一 run 存在两份内容不同的 manifest。若某个 verifier 以包内 manifest 为准做
artifact 完整性校验，会得到与包外不一致的结论。

**当前定级理由**：**尚未证明有 verifier 因此失败**，故定为 P3 而非 P2。

**路线**：tar 无法自引用（写入 digest 会改变 digest），因此正确形态是**包内清单 + 包外
digest/envelope**：包内 manifest 只登记包内 artifact；`archive_bundle` 的路径与 digest
写入包外的 envelope。不要尝试让包内 manifest 描述包自身。

**首个证明点**：`pytest -q tests/test_uce_manifest.py` 维持 2 passed，且新增断言确认包内
manifest 的 artifact 集合与解包后实际文件集合一致。

**证伪条件**：若 `docs/uce/schema/manifest@v1.md` 明确要求 manifest 必须自描述 archive，
则需先修订 schema 合同。

**止损**：不改 `manifest@v1` 的既有字段名与 `policy_violations` 口径。

### 7.6 P3-6 `basic-no-problem` 门禁：当前不可下线

**定位**：`scripts/run_basic_no_problem_gate.py`（`:221` 声明 `"gate_id": "basic-no-problem"`）；
文档面见 `docs/dev/build-and-test.md:413-430`、`tests/README.md:67`、
`docs/uce/schema/manifest@v1.md:20,130`。

**事实**：`docs/uce/README.md:111` 明确规定：

> 在 UCE runner 未稳定接管归档与 fail-closed 语义前，不下线 `basic-no-problem`。

**裁决**：这是**有效的文档化约束**，属第 3.1 节合同边界中"不得放松发布证据"的范畴。
表面上 `basic-no-problem` 与 UCE 存在职责重叠（两套归档门禁），但重叠本身不足以构成删除
理由。

**路线（需按序执行，不可跳步）**：

1. 产出覆盖矩阵，逐项证明 UCE runner 对 `basic-no-problem` 的归档与 fail-closed 语义
   具有等价或更强覆盖；
2. 依据该矩阵修订 `docs/uce/README.md:111` 的约束；
3. 迁移文档引用（`docs/dev/build-and-test.md`、`tests/README.md`）；
4. 最后下线门禁与脚本。

**首个证明点**：覆盖矩阵中每一条 `basic-no-problem` 的 fail-closed 分支都能指向 UCE 的
对应断言，且无一条只有"人工确认"作为对价。

**证伪条件**：若矩阵显示 UCE 缺失任一 fail-closed 分支，则本项**无限期保留**，直到该分支
被 UCE 覆盖。

**止损**：在第 1 步完成前，不得修改 `run_basic_no_problem_gate.py`、其证据路径或任何相关
文档中的门禁描述。

## 8. 明确不应删除（避免误判为过度设计）

以下均为已裁决的可靠性合同，复杂性有明确对价。它们直接对应第 3.1 节合同边界中"不得放松
内存安全、线程所有权、协议语义、错误传播、本地夹具要求与发布证据"的条款。

### 8.1 `QCurlRuntime` 进程级生命周期

进程级 lease / drain / poison 机制，以及 libcurl share 初始化失败后的 quarantine。
表面上"一个 HTTP 库不需要进程级单例状态"，但 libcurl 的 share handle 与全局 init 语义
决定了这一层不可省略；poison/quarantine 是对 share 失败后不可恢复状态的正确处理，
而非 workaround。

### 8.2 传输层与协议适配

- multipart 的 thread-affinity 校验（`takeDevice()` 的 owner-thread 检查）；
- WebSocket polling；
- HTTP/3 capability adaptation；
- `PreferNetwork` 的 cache fallback。

四项均为协议或线程语义的必要实现。特别注意 `PreferNetwork` 的 fallback 是
**缓存策略语义的一部分**（网络优先、失败回退缓存），与 5.2 节被判定应删的
"QMap 降级分支"性质完全不同：前者是被规定的行为，后者是丢失语义的兼容路径。

### 8.3 `QCNetworkDiskCache::removeLegacyEntries()`

**这是最容易被误判的一项。** 它清理的是**不受支持**的 `.data/.meta` 残留状态，
即主动删除无法解析的旧文件以避免缓存目录被污染。它**不是**兼容读取，**不是**迁移路径，
不会让旧格式继续可用。删除它会导致旧目录中的无效文件永久滞留。

### 8.4 `release_identity.AUTHORITY_RELATIVE_PATHS`

`scripts/release_identity.py:26` 指向 `docs/arch/2.0.0-hard-break-release-contract.md`。
当前合同明确将其列为 active authority，属 live 引用而非历史兼容层。同理，活跃方案包的
dated 路径（`.helloagents/plans/202608051224_.../`）虽然形如历史目录，但仍是被固定引用的
发布权威，不可按"日期命名即历史"的直觉清理。

### 8.5 六树 provenance 与 run-scoped consistency evidence

六树构建 provenance 与 run-scoped 证据身份是发布证据链的组成部分。其复杂度来自
"必须能证明每份证据出自哪棵树、哪次运行"这一硬要求，删除会直接削弱发布结论的可核验性。

## 9. 建议执行顺序

| 序 | 项 | 理由 |
| --- | --- | --- |
| 1 | **P1-2**（CTBP 假绿） | 门禁失效会掩盖后续所有改动的回归，必须先恢复约束力 |
| 2 | **P2-1**（静态假绿） | 同类问题：先让证据链诚实，再改被证据覆盖的代码 |
| 3 | **P1-1 + P2-2** | 零调用面、合同已授权，风险最低，可合并为一次改动 |
| 4 | **P2-3** | 需连带修复该组当前的 config-parse 期失败，工作量最大，独立推进；因子项 B 已有五层 fail-closed 缓解，危害量级低于第 1、2 序，故不上提 |
| 5 | **P3-1 ~ P3-5** | 按 7.1–7.5 逐项，注意 P3-1 的同名 API 隔离 |
| 6 | **P3-6** | 必须等覆盖矩阵，不与其他项并行 |

**排序原则**：前两项都是"让门禁在应该变红时变绿"的机制。在门禁本身失效的前提下修改被
它覆盖的代码，无法获得可信的回归信号——因此先修门禁，再动代码。

## 10. 本轮验证记录

以下为本轮实际执行的验证，含一项失败，不做美化：

| 命令 | 结果 |
| --- | --- |
| `pytest -q tests/test_uce_ctbp.py` | 3 passed |
| `pytest -q tests/test_release_gate_unit.py` | 54 passed |
| `pytest -q tests/test_run_uce_gate.py` | 11 passed |
| infrastructure 三组 | 12 passed / 13 passed / 3 passed |
| `pytest -q tests/test_uce_manifest.py` | 2 passed |
| `pytest tests/libcurl_consistency/test_compare_unit.py` | **FAIL** —— 因缺少 `build/curl/src/curl`，`conftest.py:101` 的 `pytest.skip(allow_module_level=True)` 在 **config-parse 阶段**（`_prepareconfig`）被判为错误，输出完整 traceback 并以 exit 1 结束。**不可视为当前 consistency 通过**。<br>**失败归因**：该退出码源于本地环境未构建 curl 子树（`build/curl/` 缺失），**不是代码回归**；但 P2-3 子项 A 的定级依据正是"环境缺失被暴露为 config-parse 期 traceback"这一暴露方式缺陷本身，与归因无关 |
| CTBP legacy-only 负向探针 | 确认假绿：`policy_violations=[]`、`violations=[]`、`entry_count=4` |
| `build_steps()` 步骤探针 | 确认 `shared_public_api` 与 `static_public_api` 命令逐字相同 |

## 11. 边界声明

本文件是只读审查结论与重构建议，**不是**整改完成证明。具体而言：

- 除本报告及其 `docs/README.md` 索引条目外，**未修改**任何源码、测试、文档、构建配置或
  Git 状态；两处改动均为本报告自身的发布产物，受审对象未被触碰。注意合同 `:182` 规定
  `READY_FOR_RELEASE` 要求 worktree 与 submodule clean —— 本报告不主张满足该条件；
- 未执行 commit、tag、push、发布或 ABI promotion；
- 不替换 `docs/arch/2.0.0-hard-break-release-contract.md` 的发布权威地位；
- 不构成发布签署授权；
- 第 10 节中 `libcurl_consistency` 一组当前未通过，任何"当前全绿"的表述均不成立；
- 执行台账 `qcurl-v2-execution-ledger.json` 固定在旧提交 `15f4d2e`，其历史 PASS 不适用于
  本文件的基线 `49c2276`。

后续若实施本报告建议，应在活跃方案包中登记被移除的 public API（合同 `:70` 要求），并按
第 9 节顺序推进。
