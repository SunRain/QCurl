# ABI 参考与比较操作

从 2.1 起，本页说明已启用的版本线 ABI 工具操作，规范正文在[版本与兼容政策](versioning-and-compatibility.md)，当前资格输入在唯一 [2.1 发布合同](2.1.0-release-contract.md)。历史 2.0 的 Deferred 决策保留在[当时合同](2.0.0-hard-break-release-contract.md)，不再作为当前发布入口。

完整发布由[发布操作](release-procedure.md)调用同一组工具，不需要手动重复 capture/check。本页的独立命令用于定位参考或兼容检查失败，不能代替五树 full/final。

## 1. 准备真实参考

使用 release-shared 的真实安装与匹配 DWARF，不能另编“近似 release”库生成 snapshot。先完成 package evidence，核对候选编译/运行依赖和 producer profile；参考包括两个 shared 库、TestSupport 静态库、全部 Public 安装头和四组件已编译 consumer。

以下变量须指向当前 release-shared、其完整安装前缀与**尚不存在**的输出目录；不覆盖既有参考：

```bash
: "${QCURL_REFERENCE_BUILD:?设置 release-shared build 目录}"
: "${QCURL_REFERENCE_STAGE:?设置该 producer 的完整安装前缀}"
: "${QCURL_REFERENCE_OUTPUT:?设置新的参考输出目录}"
python3 scripts/qcurl_compatibility.py capture \
  --build-dir "$QCURL_REFERENCE_BUILD" \
  --stage-prefix "$QCURL_REFERENCE_STAGE" \
  --output-dir "$QCURL_REFERENCE_OUTPUT"
```

正式 gate 固定输出到 `evidence/compatibility/reference/`。核对 `reference.json`、`validation.json`、`abi/core.snapshot.xml`、`abi/other-extras.snapshot.xml`，以及索引实际引用的库、头、consumer、profile 与原始日志；只看索引文件存在不够。发布资产保留匹配调试信息、来源 commit/submodule、精确 tuple 和 consumer 原始二进制，完整性复用 final manifest / SHA256SUMS。

捕获后，首版还必须执行独立 round-trip；下例的输出也须是新目录，不能覆盖 capture：

```bash
: "${QCURL_REFERENCE_ROUNDTRIP_OUTPUT:?设置新的 round-trip 输出目录}"
python3 scripts/qcurl_compatibility.py check \
  --build-dir "$QCURL_REFERENCE_BUILD" \
  --reference "$QCURL_REFERENCE_OUTPUT" \
  --stage-prefix "$QCURL_REFERENCE_STAGE" \
  --output-dir "$QCURL_REFERENCE_ROUNDTRIP_OUTPUT"
```

正式 gate ID 为 `compatibility_reference_roundtrip`，固定输出 `evidence/compatibility/roundtrip/report.json` 和两库 ABI 子报告；成功报告的 `classification=REFERENCE_VALIDATED`、`patchCompatibility=false`。此自测故意不传 `--require-history`，只证明相同候选参考可用，**不证明一次真实 patch 升级兼容**。源码重建出的历史库也不是原始发布资产；重建条件须单独披露。

## 2. 检查保存的旧二进制

选择已经核对版本/tag/commit、同线和相同 tuple 的正式参考；候选完整库安装到新前缀。`--build-dir` 指向候选已有 release-shared，用于核对 profile/CMakeCache 和安装来源，check 阶段不能 configure、编译或链接：

```bash
: "${QCURL_PATCH_BUILD:?设置候选 release-shared build 目录}"
: "${QCURL_OLD_REFERENCE:?设置已核对的正式参考目录}"
: "${QCURL_PATCH_STAGE:?设置候选完整安装前缀}"
: "${QCURL_COMPAT_OUTPUT:?设置新的检查输出目录}"
python3 scripts/qcurl_compatibility.py check \
  --build-dir "$QCURL_PATCH_BUILD" \
  --reference "$QCURL_OLD_REFERENCE" \
  --stage-prefix "$QCURL_PATCH_STAGE" \
  --require-history \
  --output-dir "$QCURL_COMPAT_OUTPUT"
```

先确认参考在自己的运行库下可用，再用**同一旧可执行文件**加载候选完整库。查看实际 loader 记录，两个 QCurl 库均须来自候选 stage；旧 RPATH、ambient `LD_PRELOAD` 或宿主 QCurl 会污染结果，不能算通过。TestSupport 静态桥调用新 Core 也在旧二进制检查内。旧源码重建由独立 public-api/package consumer 取证，不能用它冒充不重编译运行。

旧源码验证是独立的显式重建，不改变 check 保存的旧可执行文件：

```bash
: "${QCURL_SOURCE_OUTPUT:?设置新的旧源码重建输出目录}"
python3 scripts/qcurl_compatibility.py rebuild-source \
  --build-dir "$QCURL_PATCH_BUILD" \
  --reference "$QCURL_OLD_REFERENCE" \
  --stage-prefix "$QCURL_PATCH_STAGE" \
  --require-history \
  --output-dir "$QCURL_SOURCE_OUTPUT"
```

正式 patch 的 check / rebuild-source 均须 `--require-history`，拒绝同候选 round-trip 冒充升级；成功的历史 check 分类为 `PATCH_COMPATIBLE`、`patchCompatibility=true`，source 报告仍明确 `oldBinaryCompatibility=not-evaluated`。release gate 显式提供 first/latest：first 是正式 `vM.m.0`，latest 是同线候选之前最高正式 patch；2.1.1 的两者可为同一 2.1.0 参考，身份核对后去重。输出为 `evidence/compatibility/check-first/report.json`、`check-latest/report.json` 及 ABI 子报告；源码结果另写 `source-first/report.json` / `source-latest/report.json`。缺输入、错线、错 tuple、缺文件、内容替换或当前候选冒充历史均须失败。

## 3. 解释两库 ABI 报告

`qcurl_abi_gate.py` 保留 snapshot/diff/symbols 诊断。两库分别固定以**参考真实 ELF 与候选真实 ELF**做完整报告和 `--no-added-syms` 保守残余比较；旧/新公共头分别传入。两侧 XML snapshot 仍是必需归档和语义校验输入，须验证组件、SONAME 与真实库对应，不用 XML 比较替代 ELF 路线。

已验证的 libabigail 3.0.0 微型 ELF 负向控制中，纯类方法/虚表变化曾出现 XML→XML 返回 0、同一对 ELF→ELF 返回 12。因而 ELF 是显式、始终执行的比较输入，不是 XML 比较失败后的自动 fallback；XML 返回 0 本身不能证明这类变化兼容。

独立 Core 诊断示例中，旧/新库变量必须指向参考索引和候选安装面里的真实 ELF，不能用 snapshot 路径充数。`diff` 必需 `--baseline-library`，还会生成候选 snapshot；下列输出均应不存在：

```bash
: "${QCURL_REFERENCE_CORE_LIBRARY:?设置参考索引中的 Core 真实 ELF}"
: "${QCURL_PATCH_CORE_LIBRARY:?设置候选 stage 中的 Core 真实 ELF}"
: "${QCURL_ABI_OUTPUT:?设置新的 ABI 输出目录}"
mkdir "$QCURL_ABI_OUTPUT"
python3 scripts/qcurl_abi_gate.py \
  --component core \
  --library "$QCURL_PATCH_CORE_LIBRARY" \
  --headers-dir "$QCURL_PATCH_STAGE/include/qcurl" \
  --symbol-report "$QCURL_ABI_OUTPUT/core.dynamic-symbols.json" \
  diff \
  --baseline "$QCURL_OLD_REFERENCE/abi/core.snapshot.xml" \
  --baseline-library "$QCURL_REFERENCE_CORE_LIBRARY" \
  --old-headers-dir "$QCURL_OLD_REFERENCE/install/include/qcurl" \
  --current-snapshot "$QCURL_ABI_OUTPUT/core.snapshot.xml" \
  --report "$QCURL_ABI_OUTPUT/core.full.txt" \
  --residual-report "$QCURL_ABI_OUTPUT/core.residual.txt" \
  --result-report "$QCURL_ABI_OUTPUT/core.comparison.json"
```

Other Extras 另执行一次，使用 `--component other-extras`、对应的两侧真实库、`abi/other-extras.snapshot.xml` 和独立输出，不能用 Core 报告替代。正式 compatibility check 自动调用两个组件，不必手工重复。

- 原始变化报告用于披露，纯新增导出只有在残余比较退出 0、源码/行为与旧二进制均通过时才可接受。
- libabigail 工具/用法错误、bit 8、未知位及非零残余均失败；**原始返回 4 不是兼容凭证**，布局变化也可能只有 4。
- 残余变化须逐项修复、验证具体工具误报，或进入下一 minor；不用 broad suppression、忽略 SONAME 或人工 PASS 绕过。
- 缺工具、缺匹配 DWARF、错误组件或工具错误后的空报告均失败；“无变化”只能由成功执行的工具明确报告。

真实小型 ELF 正负控制检验工具能否区分无变化、纯新增、删除及同名导出的布局/返回类型变化；它不是完整 QCurl 兼容验收。动态符号 allowlist 则只防止意外实现导出，不证明 ABI。

## 4. 结果边界

| 结果 | 可以证明 |
| --- | --- |
| 版本文件 fixture | CMake 的版本匹配规则 |
| ABI ELF 正负控制 | 工具在已测变化上的区分能力 |
| 2.1.0 参考自测 | 本首版参考可供后续比较 |
| 旧源码重建 | 已测源码与安装消费仍可用 |
| 两库 ABI + 旧二进制 + 行为回归 | 对应参考、tuple、接口和行为的组合证据 |
| clean C1 full/final + 独立 verify | 当前候选本地发布资格；仍不表示远端发布 |

不能由这些结果外推任意平台、任意开关、降级或混装兼容。历史 v1 ABI 文件保留诊断意义；当前流程不更新固定 v2 baseline，也没有 promotion 发布阶段或旧 CLI alias。
