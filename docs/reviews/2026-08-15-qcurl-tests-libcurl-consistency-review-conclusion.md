# QCurl 测试与 libcurl 外部可观测一致性审查结论

> 审查日期：2026-08-15
>
> 源码基线：`master-tmp@3dfd2c6c1d53782ac5099f219f1cb8cd4ac1b75b`
>
> 审查范围：`tests/`，重点为 `tests/libcurl_consistency/`
>
> 审查视角：Qt6/C++ 测试、libcurl/C 测试、Qt6 网络编程、Qt6 绑定库和
> Qt6/KDE 应用库工程实践
>
> 文档性质：只读测试审查 authority；不是测试整改完成证明、发布签署、ABI
> promotion 或产品源码缺陷证明

## 1. 最终结论

QCurl 当前测试实现质量较高，`tests/libcurl_consistency/` 已覆盖较广的 HTTP、TLS、
代理、流控和 WebSocket 场景。审查阶段执行的 `all + ext` 一致性 gate 通过
`101/101`，且没有 pytest skip、planner exclusion、schema violation 或脱敏违规。

但是，当前测试体系仍不能作为企业级“覆盖完整且不会假绿”的 QCurl/libcurl
外部可观测一致性证明。主要原因不是当前 101 个测试失败，而是门禁没有持续证明：

- 计划 nodeid、参数化 case、JUnit testcase 与 baseline/QCurl 工件一一对应；
- 每个强合同 case 都在本轮隔离目录中生成完整、成对且字段齐全的工件；
- gate 基础设施单测进入独立、受支持且持续执行的正式入口；
- 所有 CI/RC 证据入口都能取得 submodule、构建目标并执行统一 gate；
- 默认 Core consumer contract 的 TLS、IP resolve 和 HTTPS proxy API 都有成对证据。

本轮裁决如下：

| 严重度 | 数量 | 裁决 |
| --- | ---: | --- |
| P0 | 0 | 未发现当前执行结果被直接伪造或强合同已确定失效的证据 |
| P1 | 3 | F1 gate 完整性、F5a 基础设施正式门禁、F6 CI/RC 执行链 |
| P2 | 6 | F2、F3、F4、F5b、F7、F8 |
| 撤销 | 3 | env smoke 退出码误判、顶层代表 map 全量化误判、Advanced API 默认合同归类误判 |
| 总体 | - | **CURRENT TESTS PASS；CONSISTENCY EVIDENCE INCOMPLETE；必须整改** |

必须区分两个结论：

1. **当前运行是真实通过。** JUnit 中 43 个计划文件均至少产生一个 testcase。
2. **门禁证明力不足。** 当前实现没有强制未来运行继续满足相同覆盖和证据完整性。

本表替换此前“P1=6、P2=1”的旧裁决。后续整改只能以本版 F1-F8 为准。

## 2. 审查范围与判定口径

### 2.1 审查对象

本轮覆盖以下测试层：

- QtTest 和 CTest 注册、标签及 skip 处理；
- public API 安装、consumer 和 hard-break 合同测试；
- 普通 Python 静态合同测试；
- `tests/libcurl_consistency/run_gate.py` 及其 planner、preflight、report、artifact、
  comparator 和 capability manifest 链路；
- `coverage-map.yaml`、`minimal_set.yaml` 与相关一致性自检；
- GitHub Actions 中调用一致性门禁的 workflow；
- QCurl 默认 Core consumer contract 的 TLS、IP resolve 和 HTTPS proxy 覆盖。

### 2.2 严重度口径

- **P1**：当前正式证据入口存在结构性假阳性窗口，或 CI/RC 无法可靠形成证据。
- **P2**：缺口真实，但当前强路径有前置断言保护、元数据不参与正式门禁，或问题主要影响
  可维护性、证据来源与后续扩展。
- **撤销**：复核后确认原论据与当前代码、测试结果或已声明合同不一致。

宽松 comparator、stale metadata 或单侧 smoke 不会仅凭名称自动成为 P1。必须证明它能让
当前正式强合同假绿，或直接破坏当前发布证据入口。

## 3. Finding 总表

| ID | 严重度 | 问题 | 当前影响 | 唯一整改方向 |
| --- | --- | --- | --- | --- |
| F1 | P1 | gate 不核对精确 nodeid、参数数量、JUnit 与成对工件，也缺少本轮隔离标识 | 覆盖或工件缩水后仍可能全绿 | 建立 run-scoped execution manifest，做计划、执行和工件双向核对 |
| F2 | P2 | comparator 允许双方同时缺少 optional 字段，并保留 legacy error fallback | 新增或弱断言 case 可能掩盖 producer 退化；当前强路径有前置断言 | 按 case/evidence type 声明必需字段并删除 legacy fallback |
| F3 | P2 | minimal set 有 4 个 stale nodeid，自检只检查少数字符串 | 维护元数据漂移，但不影响当前正式 planner | 标准解析全部条目并在独立 infrastructure gate 中 collect 验真 |
| F4 | P2 | libcurl consistency coverage map 的解析和校验忽略 case、路径、artifact 与 evidence type | map 无法证明自身条目与 planner/collection 一致 | 使用标准 YAML 解析并核对 map、planner 和收集结果 |
| F5a | P1 | comparator、gate、minimal-set 基础设施单测未进入正式门禁 | 基础设施回归不能持续阻断 CI | 建立不依赖 testenv 的独立 infrastructure gate 并接入 CI/RC |
| F5b | P2 | `test_ext_api_reported_status.py` 未进入入口且只有 QCurl 单侧证据 | 测试归属不清，不能计入 paired consistency | 完成合同归属裁决后删除，或并入现有单侧 binding 测试 |
| F6 | P1 | 部分 workflow 缺一致性构建选项或递归 submodule checkout | CI 在形成一致性证据前失败 | 统一 checkout、configure 和 `run_gate.py` 入口 |
| F7 | P2 | pinned-key fixture 与实现漂移，map/report 不能核验 evidence type | 维护者可能误读 fixture 来源或证据强度 | 删除废弃 fixture，并让 map/report 显式表达 smoke/paired contract |
| F8 | P2 | capability manifest 路径、producer、build identity、hash 和 freshness 未绑定 | 证据来源不清，非 `--build` 调用可读取不可核验的既有 manifest | 生成 run-scoped manifest，并校验 producer 与构建身份 |

## 4. F1：gate 可在覆盖或工件缩水时假绿

### 4.1 执行集合未精确核对

`policy_violations_from_report()` 只检查：

- JUnit 是否可解析；
- 全局 tests 是否大于零；
- pytest skip 是否为零；
- schema、redaction 和 required HTTP/3 是否出现显式 violation。

它没有比较：

- `pytest_files` 与 JUnit 实际 classname；
- 预期 nodeid 与实际 testcase；
- 参数化 case 数量；
- 每个计划文件的最小收集数量；
- planner exclusion 是否得到 suite policy 明确授权。

因此，只要仍有一个 pytest testcase 通过，其他文件被清空或参数化 case 丢失后，全局
`tests > 0` 仍可满足门禁。当前运行中 43 个计划文件均产生 testcase，只能证明该次运行
没有整文件消失，不能证明未来 gate 会阻断缩水。

### 4.2 工件扫描没有本轮身份

`postflight_artifacts_schema_check()` 固定递归扫描 curl testenv 的全局 artifacts 目录，
通过文件 `mtime` 与 gate 开始时间筛选 `baseline.json` 和 `qcurl.json`。该实现：

- artifacts 目录不存在或本轮零工件时返回零扫描、零违规；
- 不按 case-key 验证 baseline/QCurl 配对；
- 不核对 JUnit testcase 与工件；
- 不知道 case 属于 paired contract、smoke 还是 diagnostic；
- 没有 run-id、producer identity 或并发运行隔离；
- 可能读取临近运行在同一全局目录生成的工件。

最小复现中，JUnit tests 为 1、工件扫描为 0 时，
`policy_violations_from_report()` 仍返回空列表。这是当前明确的 P1 假阳性窗口。

### 4.3 关闭条件

F1 只有同时满足以下条件才能关闭：

- gate 在执行前持有 suite 对应的精确 nodeid、参数和 evidence type；
- 每次运行使用唯一 run-id 和隔离工件目录；
- JUnit 实际执行集合与计划集合完全一致；
- paired contract 必须为每个 case-key 生成本轮 baseline/QCurl 成对工件；
- smoke 和 diagnostic 必须显式声明，不能隐式免除配对；
- 零工件、孤立工件、重复工件和未声明 exclusion 均成为 gate failure；
- 单测覆盖零工件、单侧工件、文件空化、参数丢失、stale manifest 和并发运行。

## 5. F2：comparator/schema 宽松属于 P2

`_cmp_optional_fields()` 在字段双方同时缺失时不报告差异。
`_extract_error_namespaces()` 还会从 legacy `payload.error` 回退生成
`observed.error` 和 `derived.error`。这两项都不符合当前项目的 hard-break 方针。

问题真实，但不能继续定为 P1：

- raw request header 用例对两侧 `headers_raw_lines`、长度和摘要做前置强断言；
- raw response header 用例对两侧重复头、大小写和关键行做前置强断言；
- timeout、cancel 等错误专题直接断言两侧 `observed.error` 或 `derived.error`；
- 当前强路径不会仅依赖宽松 comparator 判绿。

因此，F2 的风险是新 case、弱断言 case 或未来 producer 退化时缺少统一 schema 保护，严重度为
P2。关闭条件如下：

- comparator 接收当前 case 的必需字段集合；
- paired contract 的必需字段双方同时缺失时失败；
- smoke/diagnostic 的免除规则由 evidence type 明确声明；
- 删除 legacy `payload.error` fallback；
- 增加双侧缺失、legacy-only、字段类型错误和嵌套路径缺失单测。

## 6. F3：minimal set 元数据漂移属于 P2

`tests/libcurl_consistency/minimal_set.yaml` 包含 31 个 case，其中 18 个使用 pytest
nodeid，13 个引用 curl data test。当前自检只验证 6 个 ID 字符串和少数 planner 文件。

以 `pytest --collect-only` 核对后，确认以下 4 个 nodeid 已过时：

| minimal set 位置 | 过时引用 | 当前实际收集入口 |
| --- | --- | --- |
| `minimal_set.yaml:344` | `test_p1_socks5h_success` | `test_p1_socks_success_http_1_1[socks5h-...]` |
| `minimal_set.yaml:435` | `test_p1_request_headers_raw_http_1_1` | `test_p1_request_raw_headers_http_1_1` |
| `minimal_set.yaml:661` | `test_p1_redirect_302_303_308_method_body_http_1_1` | `test_p1_redirect_302_303_308_method_body[...]` |
| `minimal_set.yaml:726` | `test_p2_range_boundaries_http_1_1` | `test_p2_range_boundaries[...]` |

文件头仍只描述 `p0|p1|all`，H3 备注仍要求报告 skip；当前 planner 已采用 exclusion，
正式 gate 又将 pytest skip 视为失败，说明文档语义也已漂移。

但是，`minimal_set.yaml` 不被 `run_gate.py` planner、release workflow 或 UCE 消费。
它是维护元数据，不是当前发布证据 authority，因此 F3 为 P2。

关闭 F3 时，应标准解析全部 31 个 case，并在独立 infrastructure gate 中核对 path、nodeid、
参数选择、suite、planner 归属和 capability exclusion。不得把它描述为当前 release planner。

## 7. F4：仅 libcurl consistency map 存在 P2 缺口

`tests/test_coverage_maps.py` 对
`tests/libcurl_consistency/coverage-map.yaml` 使用手写行解析器，只读取：

- contract group 名称；
- gate policy；
- failure promotion code。

解析器忽略每个 contract 下的 `cases`、pytest 文件、artifact 字段和 evidence type。对应测试
只要求存在六个 contract group，因此 case 删除、路径错误或必需字段漂移不会失败。

F4 只覆盖该弱解析和弱校验问题，严重度为 P2。关闭条件是使用标准 YAML 解析，并双向核对：

- map 中声明的 case、pytest 路径和 evidence type；
- paired contract 要求的 artifact 字段；
- planner 实际文件与 pytest collect 结果；
- failure promotion code 与 gate 实现。

顶层 `tests/coverage-map.yaml` 不属于 F4 缺陷。它在 `tests/README.md` 中明确定位为按
QCurl surface 记录“代表 gate”的索引。应维持该代表性索引合同，不将其扩张为全量
inventory，也不把代表性定位表述为假阳性。

## 8. F5：基础设施门禁与 orphan test 必须拆分

### 8.1 F5a：基础设施单测未进入正式门禁，P1

以下基础设施测试不在 CTest、`run_gate.py`、release workflow 或 UCE 中：

- `test_compare_unit.py`；
- `test_run_gate_unit.py`；
- `test_minimal_set_consistency.py`。

三者使用独立入口执行为 `16 passed`。该结果只能证明测试当前可运行，不能证明 CI 会持续
执行。默认从 `tests/libcurl_consistency/` 启动 pytest 时，还会先加载目录级
`conftest.py` 和 curl testenv，导致本应独立的基础设施单测受到集成环境耦合。

这三类测试直接保护 comparator、planner 和 gate policy。它们缺席正式门禁会让基础设施回归
绕过 CI，因此 F5a 为 P1。

唯一整改路线是建立不加载目录级 testenv 的独立 infrastructure test target，再由 CTest 和
所有正式证据 workflow 统一调用。不得继续依赖维护者手工执行。

### 8.2 F5b：orphan reported-status test，P2

`test_ext_api_reported_status.py` 有两个参数化 case，独立执行为 `2 passed`，但不在 ext
planner 文件列表中。该测试只核对 QCurl `reported_meta.json` 与服务端观测，不包含
libcurl baseline，因此不能注册为 paired consistency contract。

整改时先核对其 reported metadata bridge 是否具有现有测试未覆盖的独立合同价值：

- 没有独立价值时删除；
- 有独立价值时并入现有单侧 binding 测试，并明确标记 evidence type；
- 不新建另一条 paired consistency 路线，也不计入 paired contract 数量。

该问题影响测试归属和维护性，不构成当前发布证据假绿，严重度为 P2。

### 8.3 撤销 env smoke 退出码误判

在当前 pytest/toolchain 下，强制缺失 testenv 时，裸
`test_env_smoke.py` 负向验证退出码为 1。原退出码判定与当前验证结果不一致，必须撤销。

正式门禁仍必须通过受支持 wrapper 显式执行 skip=fail 和 no-tests=fail。该要求用于稳定合同，
不能继续建立在 pytest 当前退出码实现细节上。

## 9. F6：部分 CI/RC workflow 不能可靠形成证据

顶层 `QCURL_BUILD_LIBCURL_CONSISTENCY` 默认值为 OFF。一致性 QtTest、baseline、
`qcurl_lc_deps` 和 `qcurl_nghttpx_h3` 目标只在该选项为 ON 时创建。

以下 workflow 配置 QCurl 时没有传入该选项，却随后构建或运行一致性目标：

- `.github/workflows/libcurl_consistency_ext_gate.yml`；
- `.github/workflows/release_delivery_http3_gate.yml` 的 Debian job；
- 同一 release workflow 的 Arch job。

这些 workflow 的 checkout 也没有设置 `submodules: recursive`。`curl/` 是 Git
submodule，选项为 ON 时若缺少 `curl/CMakeLists.txt`，顶层 CMake 会直接失败。

`basic_no_problem_gate.yml` 已传入一致性选项，但同样没有递归 checkout submodule。
`pr_fast_gate.yml`、`uce_nightly.yml` 和 `uce_soak.yml` 是当前正确参照。

这些问题通常让 job 在一致性测试前失败，不会把产品错误悄悄变绿；但这些 workflow 是当前
有效 CI/RC 证据入口，无法执行就无法形成发布证据，因此 F6 维持 P1。

关闭条件：所有调用一致性 gate 的 workflow 统一使用递归 submodule checkout、
`QCURL_BUILD_LIBCURL_CONSISTENCY=ON`、锁定依赖安装和同一个 `run_gate.py` 入口。

## 10. F7：fixture 与 evidence type 漂移

`tests/libcurl_consistency/README.md` 声称 pinned-public-key 用例只消费固定 fixture：

`tests/libcurl_consistency/testdata/pinned_public_key_sha256.txt`

实际测试从当前 curl testenv 证书动态提取公钥并计算 `sha256//...` pin。固定 fixture 在
仓库中没有实际引用，属于废弃资产。

`test_ext_tls_policy_and_cache.py` 文件自身已经明确声明为 smoke。它只执行 QCurl 侧，
验证请求成功及 HSTS/Alt-Svc 文件存在且非空，没有 libcurl baseline。问题不在测试冒充
paired contract，而在 coverage map 和 gate report 不能表达、核验其 evidence type。

F7 为 P2。关闭条件：

- 删除无引用 pinned-key fixture，并同步 README；
- coverage map 为每个 case 声明 evidence type；
- gate report 分别统计 paired contract、smoke 和 diagnostic；
- smoke 不得计入 paired contract 数量，也不得隐式免除 paired case 的工件要求。

## 11. F8：capability manifest 来源与新鲜度不可核验

`run_gate.py --reports-dir` 只控制 JUnit 和 gate JSON。capability manifest 仍固定读取
`<qcurl_build>/libcurl_consistency/reports/capabilities.json`，不会跟随自定义报告目录。

当调用不带 `--build` 且 manifest 已存在时，gate 直接加载 JSON。加载过程不验证：

- producer 可执行文件及其 hash；
- QCurl、libcurl 和 capability probe 的 build identity；
- 源码 HEAD 与 dirty-tree identity；
- 生成时间与本次 gate 开始时间；
- manifest 内容 hash 或 run-id。

现存 `gate_all.json` 内嵌 manifest 的 `generatedAt` 为
`2026-08-14T15:47:30Z`，standalone `capabilities.json` 为
`2026-08-15T13:35:44Z`。这证明持久化证据来源不够清晰，但不能证明正式 gate 使用了
陈旧 manifest：审查阶段正式调用带有 `--build`，会先重新生成 manifest。因此 F8 为 P2，
不是新的 P1 假绿证据。

关闭条件：

- manifest 写入本轮 run-scoped 报告目录；
- report 记录 manifest 内容 hash；
- manifest 记录 producer、QCurl 和 libcurl 构建身份；
- gate 核对 run-id、生成时间和构建身份；
- 不带 `--build` 时，身份或 freshness 不匹配必须失败。

## 12. 外部可观测一致性覆盖判断

### 12.1 已覆盖能力

当前 suite 已覆盖：

- HTTP/1.1、HTTP/2、HTTP/3；
- GET、HEAD、POST、PUT、PATCH 和自定义方法；
- redirect、HTTP auth、Cookie、proxy 和 SOCKS；
- raw request/response headers、multipart、binary POST 和 chunked upload；
- timeout、cancel、pause/resume 和 backpressure；
- TLS verify 与 pinned public key；
- Range/resume、share handle 和 connection limits；
- WebSocket frame 与连接行为。

这些覆盖具有真实价值，不应因门禁缺陷而否定。

### 12.2 默认 Core consumer contract 仍缺少的成对证据

以下默认公开 API 没有本地确定性的 QCurl/libcurl 双端外部一致性矩阵：

1. mTLS：client certificate、client key 和 key password；
2. origin TLS minimum version、TLS 1.2 cipher list 和 TLS 1.3 ciphers；
3. `setIpResolve()` 的 IPv4/IPv6 选择行为；
4. HTTPS proxy TLS 的 verify peer、verify host、CA、minimum TLS version、
   TLS 1.2/1.3 cipher policy 和 unsupported-security-option policy。

相关默认合同位于 `src/QCNetworkSslConfig.h`、`src/QCNetworkRequest.h` 和
`src/QCNetworkProxyConfig.h`。普通 QtTest 对 setter、值对象或 option mapping 的覆盖，
不能替代真实服务端观测与 libcurl baseline。

Happy Eyeballs、network interface、local-port range、resolve/connect-to、DNS servers 和
DoH 当前受 `QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API` 宏控制。只有 internal/test
targets 定义该宏，因此它们不属于当前默认 Core consumer contract。该结论描述当前安装合同，
不把这些能力永久限定为测试专用 API。

默认公开 API 的成对证据缺口保留在本节，不强行计入 F1-F8 数量。

## 13. 唯一整改顺序

后续不得先堆叠更多业务 case，再延后修门禁。执行顺序固定如下：

### R1：关闭 F1 gate 完整性

- 建立 run-scoped collect/execute manifest；
- 校验 nodeid、参数化 case、JUnit 与工件集合；
- 强制 paired contract 工件按 case-key 配对；
- 零工件、孤立工件和未授权 exclusion 失败。

首个证明点：删除任一预期 nodeid、同时删除双方 raw field、仅生成单侧 artifact，或注入上一轮
工件时，gate 必须稳定失败。

### R2：关闭 F5a 基础设施正式门禁

- 建立不加载 testenv 的独立 infrastructure target；
- 纳入 comparator、report、planner 和 minimal-set 单测；
- 由 CTest、CI 和 RC 统一调用；
- `16 passed` 必须从手工证据变成持续门禁。

### R3：关闭 F6 CI/RC 执行链

- 递归 checkout submodule；
- 显式启用一致性构建；
- 删除 workflow 的重复配置分叉；
- 使用统一 gate 命令和报告归档。

### R4：处理全部 P2

- F2：收紧 comparator/schema，删除 legacy fallback；
- F3/F4：标准解析并核对维护元数据；
- F5b：完成 reported-status 合同归属裁决；
- F7：删除 stale fixture，补齐 evidence type；
- F8：绑定 run-scoped capability manifest 与构建身份。

### R5：补齐默认公开 API 双端矩阵

按以下顺序增加 paired contract：

1. mTLS；
2. origin TLS version/cipher policy；
3. IPv4/IPv6 与 `setIpResolve()`；
4. HTTPS proxy TLS policy。

止损规则：

- F1、F5a、F6 未关闭前，不得用新增业务 case 数量包装门禁完整性；
- 新增 case 不能形成稳定、离线、可重放的服务端观测和成对工件时，只能归入 smoke 或
  diagnostic；
- 任一整改引入兼容分支、重复入口或平行 evidence authority 时，停止该路线并收敛到唯一入口。

## 14. 审查验证证据

以下结果是审查阶段已执行的技术证据，不是 F1-F8 已整改的证明：

| 验证 | 结果 |
| --- | --- |
| `run_gate.py --suite all --with-ext --build` | `101 passed`，0 failed，0 skipped，0 exclusions |
| 计划文件与 JUnit classname 核对 | 43 个计划文件均产生 testcase |
| artifact schema postflight | 229 个本轮工件通过 |
| redaction postflight | 246 个文件通过，无敏感头明文违规 |
| HTTP/3 preflight | server 与 bundled curl 均可用 |
| infrastructure tests 独立入口 | `16 passed` |
| coverage-map tests | `6 passed` |
| `test_env_smoke.py` 正向验证 | `1 passed` |
| 缺失 testenv 负向验证 | 退出码 1 |
| 4 个 stale nodeid 对应业务文件 | `11 tests collected` |
| CTest 注册项统计 | 96 |
| 未注册 `test_ext_api_reported_status.py` | `2 passed` |
| 普通 Python tests | `260 passed` |
| strict offline CTest | `47/47`，零 skip |
| public API CTest | `27/27`，零 skip |
| `git diff --check` | 通过 |

本轮没有重新执行显式 opt-in 的公网 diagnostics 或 external large-file smoke。历史
`96/96` full CTest 不是本次静态审查的新鲜执行证据；96 仅表示当前 CTest 注册项数量。

`qa-review.json`、`closeout.json`、STATE、临时配置修改和代理交付门禁属于会话运行态，
不进入本长期技术 authority。

## 15. 完成与非完成边界

本文完成的是测试体系审查、Finding 分级校正和唯一整改裁决。本文没有完成：

- F1-F8 的代码、测试或 workflow 修复；
- 新增默认公开 API 一致性测试；
- release gate、ABI gate、sanitizer 或发布验证；
- commit、tag、push 或发布。

只有 R1-R5 全部实施并重新取得绑定当前 HEAD、dirty-tree identity、run-id 和构建身份的新鲜
gate 证据后，才能把总体状态从
`CONSISTENCY EVIDENCE INCOMPLETE` 改为 `CONSISTENCY EVIDENCE COMPLETE`。
