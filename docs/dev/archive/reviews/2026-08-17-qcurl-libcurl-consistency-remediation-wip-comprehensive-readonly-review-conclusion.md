# QCurl 未提交一致性整改代码综合只读审核结论

> 历史记录：以下审查/执行结论仅适用于正文注明的候选与时点；不是当前工程状态、发布资格或远端 CI 证明。原始结论保留，统一从[历史索引](../README.md)查阅。

> 审查日期：2026-08-17
>
> 源码基线：`master-tmp@3dfd2c6c1d53782ac5099f219f1cb8cd4ac1b75b`
>
> 受审整改候选 dirty-tree identity（报告生成时）：
> `ae8188f442dfc826b820975f64bc720f7d36c0bcd435ead31586f3302cd62a6d`
>
> 整改 authority：
> `docs/reviews/2026-08-15-qcurl-tests-libcurl-consistency-review-conclusion.md`
>
> 审查范围：当前未提交的测试、libcurl consistency gate、workflow、UCE 和相关证据代码
>
> 文档性质：当前 WIP 的只读关闭状态审查；不替换 2026-08-15 authority，不证明整改完成，
> 不构成发布签署、ABI promotion、commit、tag、push 或 release 授权

## 1. 最终结论

当前未提交实现是一次**方向正确、完成度较高，但仍未闭环的部分整改**。

必须同时保留以下两个判断：

1. **当前执行结果真实通过。** 最新 all+ext 报告执行 112 个测试，0 failed、0 errors、
   0 skipped、0 planner exclusions；现有 execution contract 没有报告违规。
2. **整改尚未达到“准确、无偏移、证据完整”。** R1 的静态执行 authority、R3 的
   UCE/RC 证据路径、R4 的部分 P2 合同以及 R5 默认 Core API 矩阵仍未关闭。

本轮裁决如下：

| 类别 | 数量 | 当前裁决 |
| --- | ---: | --- |
| P0 | 0 | 未发现当前产品行为错误或测试结果被直接伪造的证据 |
| 剩余 P1 authority ID | 2 | F1 gate 完整性、F6 CI/RC 执行链 |
| 剩余 P2 authority ID | 4 | F2、F3、F4、F8 |
| 已关闭 authority ID | 3 | F5a、F5b、F7 |
| R5 完成缺口 | 1 组 | 默认 Core API 双端矩阵；不计入 F1-F8 严重度数量 |
| 总体 | - | **CURRENT TESTS PASS；REMEDIATION PARTIAL；EVIDENCE INCOMPLETE** |

因此：

- 当前代码仍需修订；
- 不应把当前 WIP 描述为 R1-R5 全部完成；
- 不应把现有 green report 升级为 `CONSISTENCY EVIDENCE COMPLETE`；
- 2026-08-15 authority 仍然准确，不应为了匹配半完成实现而修改。

## 2. 计数口径

前序只读结论曾分别使用“问题组”和“authority ID”两种计数方式。本文件统一按
2026-08-15 authority 的 F1-F8 编号统计：

- F3 与 F4 在技术上可合并为一组元数据问题，但仍是两个 P2 authority ID；
- R5 是 authority 单独声明的完成缺口，不计入 P2 数量；
- 因此当前剩余口径是 P1=2、P2=4、R5=1 组；
- 若按六组技术问题合并展示，则是两个 P1 问题组、三个 P2 问题组和一个 R5 问题组。

该差异只影响展示计数，不改变任何缺口或整改要求。

## 3. Authority 关闭状态

| Authority | 严重度 | 当前状态 | 结论 |
| --- | --- | --- | --- |
| F1 gate 完整性 | P1 | 未关闭 | 仍存在覆盖缩水、未授权 exclusion 和同 run-id 旧工件窗口 |
| F2 comparator/schema | P2 | 部分关闭 | fallback 已删除，但 case schema 与 legacy producer 残留未关闭 |
| F3 minimal set | P2 | 部分关闭 | 已标准解析，但没有精确 collect 参数、suite 和 planner 归属 |
| F4 consistency map | P2 | 部分关闭 | 结构校验已改善，但双向 authority 与 promotion metadata 未闭环 |
| F5a infrastructure gate | P1 | 已关闭 | 独立 CTest 已注册、接入 workflow 并通过 |
| F5b orphan test | P2 | 已关闭 | `test_ext_api_reported_status.py` 已删除 |
| F6 CI/RC 执行链 | P1 | 未关闭 | checkout/config 已修，但 UCE run-scoped 报告登记仍断裂 |
| F7 fixture/evidence type | P2 | 已关闭 | 废弃 fixture 已删除，evidence type 已进入 report |
| F8 capability manifest | P2 | 部分关闭 | run/source/build identity 已绑定，但内嵌 manifest hash 无效 |

## 4. 剩余六组主要问题

### 4.1 P1：F1/R1 仍无法阻止执行集合缩水

#### 已完成

当前实现已经补充：

- run-scoped 执行目录；
- collect 后的精确参数化 nodeid；
- JUnit nodeid 与计划 nodeid 顺序核对；
- baseline/QCurl 工件按 case-key 配对；
- 零工件、单侧工件和未计划工件检查；
- contract、smoke、diagnostic 的 evidence type 统计。

这些改动解决了原 F1 的大部分基础问题。

#### 仍未关闭

`tests/libcurl_consistency/pytest_support/gate_execution.py:151-176` 仍从当前源码执行
`pytest --collect-only`，并把本次动态收集结果直接作为预期执行集合。

这意味着：

- 从源码删除参数化 case 后，“预期集合”会同步缩小；
- gate 没有独立于当前测试源码的完整静态 nodeid authority；
- 当前 112 个计划 nodeid 中，coverage map 的 38 个声明 case 只能匹配 52 个 nodeid；
- 剩余 60 个 nodeid 没有独立的静态 case 声明；
- 删除 TLS 的 6 个参数化 nodeid 中任意 5 个后，`validate_collected_cases()` 仍返回零错误。

`tests/libcurl_consistency/pytest_support/gate_manifest.py:227-243` 对 planner exclusion
只验证 planned 与 excluded 是否构成 candidate 分区，以及 exclusion reason 是否非空。它没有
校验文件是否得到 suite/capability policy 明确授权。实测任意候选文件使用任意非空理由即可得到
零 execution-contract 违规。

`tests/libcurl_consistency/pytest_support/gate_runtime.py:157-177` 允许显式复用
`--run-id`，既不拒绝既有 run 目录，也不清理或隔离旧工件。
`gate_manifest.py:95-110` 又只核对工件中的 run-id 和 nodeid，不核对生成时间、execution token
或 source/build identity。

需要精确限定该风险：

- 旧工件如果对应当前未计划 nodeid，会被判为 unplanned；
- 同 run-id、同 nodeid、同 case-key 的旧工件无法与本轮工件区分；
- 实测将成对工件 mtime 设为历史时间，只要 run-id/nodeid 相同，execution contract 仍返回零违规。

#### 必须修改

- 为每个 suite 建立独立于当前动态 collect 的完整预期 nodeid/参数 authority；
- 推荐将 `tests/libcurl_consistency/coverage-map.yaml` 明确升级为 consistency 唯一静态
  合同 authority，不新增平行清单；
- collect 结果必须与该 authority 双向完全一致；
- exclusion 必须绑定 suite、文件和 capability policy，不接受任意理由；
- 显式 run-id 已存在时必须直接失败；
- 工件必须绑定本轮不可复用 execution token，并纳入 source/build identity；
- 增加文件空化、参数丢失、任意 exclusion、同 run-id 旧工件和并发运行负向测试。

### 4.2 P1：F6/R3 的 UCE/RC 证据路径仍断裂

#### 已完成

相关 workflow 已基本补齐：

- `submodules: recursive`；
- `QCURL_BUILD_LIBCURL_CONSISTENCY=ON`；
- 独立 infrastructure CTest；
- 统一 `run_gate.py` 调用方向。

原 F6 的 checkout/configure 主体问题已经修复。

#### 当前断点

`scripts/uce_gate/execute.py:80-99` 向 consistency gate 传递：

`<uce-evidence>/libcurl_consistency/reports`

gate 实际写入：

- `<reports>/runs/<run-id>/gate_<suite>.json`；
- `<reports>/runs/<run-id>/junit_<suite>.xml`。

`scripts/uce_gate/execute.py:123-138` 随后仍在 UCE manifest 中登记旧的顶层路径：

- `libcurl_consistency/reports/gate_p0.json`；
- `libcurl_consistency/reports/junit_p0.xml`；
- `libcurl_consistency/reports/gate_p1.json`；
- `libcurl_consistency/reports/junit_p1.xml`。

实际复现结果：

- p0 consistency gate：10 项通过；
- p1 consistency gate：61 项通过；
- UCE 总结果：`fail`；
- policy violation：`artifact_required_missing`；
- 缺少上述四个正式报告工件。

此外，`scripts/uce_gate/contracts.py:27-43`、`:183-184` 和 `:219-220` 仍从 curl
testenv 的全局 artifacts 根目录收集 timeline、CTBP 和 HES 证据，没有绑定当前
consistency run。

因此：

- PR UCE 无法形成完整正式证据包；
- nightly/soak 可能读取其他运行留下的全局 artifacts；
- 单个 consistency gate 通过不能升级为 UCE/RC 通过。

#### 必须修改

- UCE 为每个 suite 生成并传入唯一、可预测的 run-id；
- manifest 登记 `reports/runs/<run-id>/...` 的实际路径；
- timeline、CTBP、HES 只消费本轮 run-scoped artifacts 根目录；
- 增加 UCE 路径合同单测，防止 gate 输出布局再次漂移；
- 四个 gate/JUnit 工件完整存在且通过后，UCE manifest 才能转为 PASS。

### 4.3 P2：F2 comparator/schema 只完成了一部分

#### 已完成

当前实现已经：

- 删除 comparator 从 legacy `payload.error` 回退生成新命名空间的逻辑；
- 支持向 comparator 传入 case-specific `required_fields`；
- 对字段缺失、类型错误和嵌套路径错误增加验证；
- 单侧缺少 optional 字段时报告差异。

`compare_optional_fields()` 允许真正 optional 字段双方同时缺失本身不是错误。
`validate_required_fields()` 已能让必需字段双方同时缺失时失败。

#### 真实残留

required-field authority 尚未精确到所有参数化 variant。当前 TLS 和 HTTPS proxy TLS 的
node contract 统一只要求：

- `request`；
- `response`。

实际强合同还需要：

- TLS/proxy 成功 case：`transport`；
- TLS/proxy 失败 case：`observed.error`、`derived.error`；
- raw header case：对应 raw lines/hash；
- 其他专题 case：各自的非默认强合同字段。

如果成功 case 两侧同时停止写入 `transport`，或失败 case 两侧同时停止写入错误命名空间，
宽松 optional 比较仍可能判绿。

`tests/libcurl_consistency/pytest_support/artifacts.py:14-48` 仍显式生成 legacy
`payload["error"]` 兼容视图。当前仓库没有实际消费者，继续保留与 hard-break、无兼容层准则冲突。

#### 必须修改

- required fields 按精确参数化 nodeid 或 artifact case-key 声明；
- 成功和失败 variant 使用不同 schema；
- 保留 optional helper 对真正 optional 字段的现有语义；
- 删除 producer 的 legacy `payload.error`；
- 增加 TLS/proxy 双侧同时缺少 transport/error 的负向测试。

### 4.4 P2：F3/F4 元数据合同仍未完全闭环

#### 已完成

当前实现已经：

- 使用 PyYAML 标准解析；
- 校验 map schema、pytest 路径、suite、evidence type；
- 校验 contract case ID、nodeid、required fields 和 artifact cases；
- 验证 map 声明 case 能在 collect 结果中找到；
- 把 planner 文件集合集中到 consistency coverage map；
- 将相关测试纳入独立 infrastructure CTest。

因此，不能再沿用“F4 完全没有结构校验”的旧说法。

#### F3 剩余问题

`validate_minimal_set()` 当前只验证：

- 31 个 case 数量；
- ID 唯一；
- source 类型；
- pytest 文件和函数存在；
- curl data path/test number 存在。

它没有验证：

- 精确参数选择能否被 pytest collect；
- suite 归属；
- planner 是否包含对应文件；
- capability exclusion 是否允许；
- nodeid 是否只匹配到函数名、却遗漏参数漂移。

因此 F3 仍是部分关闭。

#### F4 剩余问题

不能仅凭“112 个 nodeid 中有 60 个未在 case map 显式声明”直接认定 F4 错误，因为当前
`contracts` 是否全量需要先定义清楚。

如果按本结论建议，将该 map 升级为 R1 的唯一静态 nodeid authority，它就必须成为全量、
双向合同；否则 R1 需要另一份静态 authority，会形成不必要的平行真相源。

另一个确定缺口是：

- `gate_report.py:348-366` 已实现 `execution_contract` failure promotion；
- `coverage-map.yaml:273-282` 未声明该 code；
- `tests/test_coverage_maps.py:68-84` 构造期望集合时也没有触发该 code。

#### 必须修改

- minimal set 对全部 pytest nodeid 执行真实 collect 验证；
- 同时校验参数、suite、planner 和 capability exclusion；
- coverage map 明确并实现单一完整 authority；
- gate policy metadata 加入 `execution_contract`；
- 测试从真实 `policy_violations_from_report()` 完整场景派生所有 promotion code。

### 4.5 P2：F8 capability manifest 的持久化 hash 不自洽

#### 已完成

当前 capability manifest 已绑定：

- run-id；
- producer 文件；
- source HEAD；
- dirty-tree hash；
- QCurl、libcurl、probe、CMake cache 构建身份；
- generated epoch；
- manifest 内容 hash；
- freshness。

这些改动基本解决了原 F8 的来源不明问题。

#### 当前问题

`gate_runtime.py:297-305` 先验证 standalone manifest hash。HTTP/3 preflight 随后在
`gate_preflight.py:191-215` 修改 manifest 中的 tests policy。最后
`gate_execution.py:114-129` 将修改后的 manifest 嵌入 gate report，但保留修改前的
`content_sha256`。

当前实测：

- standalone manifest：hash 有效；
- gate report 内嵌 manifest：hash 无效；
- 保存值：`aeeb82bccb885e22193ce3af2ef6dc493184906764ca2978b4fef6bc659dfabc`；
- 重算值：`531ee250115826ebecc021c79f349f774cab4a7fea8a86acf56cbcb19457a165`。

#### 必须修改

最干净的路线是保持 capability manifest 不可变：

- HTTP/3 preflight 结果写入独立 execution-plan/report 字段；
- planner 使用“不可变 capability manifest + 本轮 preflight”计算派生计划；
- gate report 嵌入原始 manifest 及其有效 hash；
- 派生 execution plan 单独计算内容 hash。

这可以避免把探测能力、运行环境和规划决策混为一个可变对象。

### 4.6 R5：默认 Core API 双端矩阵仍不完整

该问题真实存在，但不计入 F1-F8 的 P2 数量。

#### 已完成

- `setIpResolve(Ipv4)` 已有 QCurl/libcurl 成对证据；
- `setIpResolve(Ipv6)` 已有 QCurl/libcurl 成对证据；
- mTLS 基础 certificate/key 成功及缺失失败已覆盖；
- origin TLS 1.2/1.3 cipher 成功路径已覆盖；
- HTTPS proxy CA、TLS 1.3 和 invalid cipher 路径已有初步覆盖。

#### 剩余缺口

**mTLS**

- 没有加密 client private key；
- 没有正确 key password 成功；
- 没有错误 key password 失败。

**Origin TLS policy**

- TLS minimum 只验证 TLS 1.3 成功；
- 没有 TLS 1.3 client 对 TLS 1.2-only server 的反向失败；
- 缺少能证明 minimum option 确实生效的负向边界。

**HTTPS proxy TLS**

- verify-peer 和 verify-host 始终同时为 true；
- 没有两者的独立变体；
- minimum 固定 TLS 1.3，传入的 TLS 1.2 cipher 没有参与实际协商；
- invalid cipher 只能证明配置值无效；
- 它不能证明 `QCUnsupportedSecurityOptionPolicy::Fail` 在能力缺失时的行为。

#### 必须修改

- 增加 encrypted key/password 正反向矩阵；
- 增加 TLS minimum 跨版本失败矩阵；
- 独立覆盖 proxy verify-peer、verify-host；
- 让 proxy TLS 1.2 server 实际协商 TLS 1.2 cipher；
- 使用确定性 capability 缺失场景验证 unsupported-security-option policy；
- 所有新增强合同继续生成成对、run-scoped、可重放工件。

## 5. 已确认关闭或显著改善的内容

- F5a：独立 `qcurl_libcurl_consistency_infrastructure` CTest 已注册并通过；
- F5a：相关 workflow 已显式调用 infrastructure target；
- F5b：orphan `test_ext_api_reported_status.py` 已删除；
- F7：废弃 pinned-key fixture 已删除；
- F7：contract/smoke/diagnostic 已进入 map 与报告统计；
- F6：主要 workflow 已补递归 submodule 和 consistency 构建选项；
- F8：source/build/producer/run identity 与基础 freshness 已加入；
- R5：IPv4/IPv6 `setIpResolve()` 已形成成对证据；
- F2：comparator legacy fallback 已删除。

## 6. 当前验证证据

| 验证 | 结果 |
| --- | --- |
| 最新 all+ext gate | 112 tests，0 failures、0 errors、0 skipped |
| planner exclusions | 0 |
| 当前 execution contract | 0 reported violations |
| 计划 nodeid | 110 contract、2 smoke |
| artifact cases | 125 contract、2 smoke |
| infrastructure CTest | 1/1 通过 |
| gate report identity | 与受审整改候选的 HEAD/dirty-tree identity 一致 |
| `git diff --check` | 通过 |
| 删除 5 个 TLS 参数化 nodeid | collection contract 未失败 |
| 任意非空 exclusion reason | execution contract 可接受 |
| 同 run-id、同 nodeid 历史工件 | execution contract 可接受 |
| UCE PR 证据包 | 失败，缺少四个正式报告工件 |
| 内嵌 capability manifest hash | 无效 |
| `execution_contract` promotion metadata | coverage map 未登记 |

本轮取证使用以下本地报告；它们绑定受审候选快照，但不是仓库内长期保存的发布资产：

- all+ext gate：
  `build/libcurl_consistency/reports/runs/20260817T045733Z-c4dccc0d38a4/gate_all.json`；
- UCE manifest：
  `build/evidence/uce/review-20260817-path-contract/manifest.json`。

当前 all+ext 的通过证明本轮执行成功，但不能反向证明上述结构性缺口已经关闭。

## 7. 唯一整改顺序

### 7.1 第一阶段：关闭 F1/R1

- 建立完整静态 nodeid authority；
- 双向核对 authority、collect、JUnit 和 artifacts；
- 禁止复用 run-id；
- exclusion 改为显式 policy allowlist；
- 增加参数丢失、旧工件和并发负向测试。

### 7.2 第二阶段：关闭 F6/R3

- UCE 绑定 consistency run-id；
- 登记真实 run-scoped 报告路径；
- timeline、CTBP、HES 只读取本轮 artifacts；
- 取得完整、可归档的 UCE PASS manifest。

### 7.3 第三阶段：完成 R4

- F2：精确 required schema，删除 legacy `payload.error`；
- F3：minimal set 精确 collect；
- F4：明确并实现单一完整 map authority；
- F8：保持 manifest 不可变，独立封签 execution plan。

### 7.4 第四阶段：完成 R5

- mTLS encrypted key/password；
- TLS minimum 反向失败；
- proxy verify、TLS 1.2 cipher、unsupported policy。

### 7.5 第五阶段：重新取得新鲜证据

- infrastructure CTest；
- all+ext gate；
- UCE PR/nightly 对应路径；
- 当前 HEAD/dirty-tree/build/run identity；
- 零 skip，所有 exclusion 均得到明确授权；
- 当前报告及所有嵌入对象 hash 自洽。

## 8. 完成判定

只有同时满足以下条件，才能声称本轮整改准确、无偏移：

- [ ] 删除任一 authority nodeid 或参数时 gate 必须失败；
- [ ] 任意未授权 exclusion 必须失败；
- [ ] 同 run-id 复用和旧工件注入必须失败；
- [ ] JUnit 与静态计划集合必须完全一致；
- [ ] 所有 paired contract 必须生成本轮成对工件；
- [ ] UCE manifest 必须引用真实 run-scoped 报告并整体通过；
- [ ] timeline、CTBP、HES 不再读取全局历史 artifacts；
- [ ] standalone 与嵌入 capability manifest hash 均有效；
- [ ] minimal set 的参数、suite、planner、exclusion 全部验真；
- [ ] F2 成功/失败 variant 使用精确必需字段；
- [ ] R5 的 mTLS、TLS minimum 和 HTTPS proxy policy 矩阵完整；
- [ ] 完整 all+ext、infrastructure 和正式 UCE 证据重新绑定当前代码。

## 9. 完成与非完成边界

本文完成的是：

- 当前未提交整改代码相对 2026-08-15 authority 的关闭状态复核；
- 前序两轮只读结论的计数和措辞统一；
- 剩余 P1、P2 与 R5 缺口的证据、影响和唯一整改顺序确认。

本文没有完成：

- F1、F2、F3、F4、F6、F8 的代码整改；
- R5 剩余默认 Core API 矩阵；
- 新鲜 UCE/RC 完整证据；
- ABI promotion、commit、tag、push 或 release。

最终状态是：

`CURRENT TESTS PASS；REMEDIATION PARTIAL；CONSISTENCY EVIDENCE INCOMPLETE`

在 F1/R1 和 F6/R3 两个 P1 关闭前，不得继续用新增业务 case 数量包装门禁完整性；后续也不得
新增平行 evidence authority、兼容 fallback 或绕过当前 gate 的替代入口。
