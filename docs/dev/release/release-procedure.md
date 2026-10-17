# 发布操作

维护者用本页准备候选、执行五树资格、打包并在授权后发布。唯一当前 release identity authority 为 [2.1 正式合同](2.1.0-release-contract.md)；长期承诺在[版本与兼容政策](versioning-and-compatibility.md)。本页是操作说明，不以文档校验、历史报告或局部 PASS 代替真实候选证据。命令从仓库根目录执行。

## 1. 前置条件与授权

- 当前候选 2.1.0 建立新线，使用 snapshot 捕获首版参考；同线后续 patch 使用 current 与 first/latest 参考。跨 minor 不保证兼容，不能保留“所有 2.x 兼容”或“所有更新必须重编译”的当前表述。
- 完成源码、文档、失败控制与 remediation 后，经授权形成 clean C1，冻结 CHANGELOG 对应条目、依赖和构建输入。最终资格要求 superproject 与递归子模块干净；dirty 结果不能改名为 final。
- 准备 Linux x86_64、GCC/libstdc++ C++17、Clang、CMake/Ninja、Python/pytest、Doxygen、libabigail、真实最低 Qt 6.10.3 / libcurl 8.14.1 独立前缀，以及[本地测试服务](../build-and-test.md#2-qttest-与本地服务)。HTTP/3/testenv 依赖见[一致性专题](../../../tests/libcurl_consistency/README.md)。
- 二进制 tuple 与完整默认功能遵循正式合同；不裁剪 WebSocket。Core、Other Extras 和 TestSupport 使用同一候选，static/Clang/sanitizer 的验证不代表任意二进制互换。
- 本地 gate 读取 Git 身份，不执行 commit/tag/push/Release。远端发布是独立动作；本地 READY 不构成授权。

<a id="producer-trees"></a>
## 2. 五棵独立 producer 树

每轮 fresh qualification 使用新的、物理独立的五树目录，保留旧证据。不要覆盖旧参考、复用同一路径/符号链接，或把 remediation 树改名当作 final。

| tree ID | gate 参数 | 能力与职责 |
| --- | --- | --- |
| release-shared | `--release-shared-build-dir` | GCC、Release shared、BUILD_TESTING=OFF；完整安装/consumer/生命周期/动态符号、参考与 ABI |
| release-static | `--release-static-build-dir` | GCC、Release static、BUILD_TESTING=OFF；完整安装/consumer/生命周期 |
| test-shared-gcc | `--test-shared-gcc-build-dir` | GCC、shared、BUILD_TESTING=ON；QtTest、public-api、一致性 |
| test-shared-clang | `--test-shared-clang-build-dir` | Clang、shared、BUILD_TESTING=ON；QtTest/public-api 交叉检查 |
| asan-ubsan-lsan | `--asan-ubsan-lsan-build-dir` | Clang、shared、BUILD_TESTING=ON；ASan/UBSan/LSan |

TSan 在 2.1 为独立非阻断诊断，不要求第六树或插桩 Qt。未运行记 NOT_RUN，缺环境或失败不计为 PASS；确认的产品缺陷仍须处理。独立入口见 [UCE 检测 Qt](../uce/README.md#71-检测专用-qt)。ASan/UBSan/LSan、真实泄漏检测、普通线程/生命周期验证仍为必需。

以下 Bash 命令不下载依赖。先把两个变量设为已核对的真实最低版安装前缀，再执行；不允许回落到 host 高版本。release-shared 使用 Release 加匹配 DWARF，参考必须对应实际交付库。

最低 libcurl 的实际构建与安装消费由两棵 release OFF 树验证。三棵 ON 树开启 `QCURL_BUILD_LIBCURL_CONSISTENCY` 后，使用固定子模块中的 bundled curl；`CURL_ROOT` 或 `CMAKE_PREFIX_PATH` 不会将其替换为最低版。逐树记录实际 provider，不切换子模块来冒充最低版命中；WebSocket、TLS session 等最低版运行回归可另用 system/shared ON 诊断树，不增加第六棵 final 必需 producer。

```bash
set -euo pipefail
: "${QT_MIN_PREFIX:?设置真实 Qt 6.10.3 安装前缀}"
: "${CURL_MIN_PREFIX:?设置真实 libcurl 8.14.1 安装前缀}"
B=build-v2.1-policy-final
test ! -e "$B"
common=(
  -G Ninja -DCMAKE_INSTALL_LIBDIR=lib
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF
  -DQCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=OFF
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
  "-DCMAKE_PREFIX_PATH=$QT_MIN_PREFIX;$CURL_MIN_PREFIX"
  "-DCURL_ROOT=$CURL_MIN_PREFIX"
)
cmake -S . -B "$B/release-shared" "${common[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g' \
  -DBUILD_TESTING=OFF -DQCURL_BUILD_SHARED_LIBS=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake -S . -B "$B/release-static" "${common[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
  -DBUILD_TESTING=OFF -DQCURL_BUILD_SHARED_LIBS=OFF \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake -S . -B "$B/test-shared-gcc" "${common[@]}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=g++ \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON
cmake -S . -B "$B/test-shared-clang" "${common[@]}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON
cmake -S . -B "$B/asan-ubsan-lsan" "${common[@]}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON \
  -DQCURL_SANITIZER_PROFILE=asan-ubsan-lsan \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined,leak -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined,leak -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined,leak' \
  '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined,leak'
for tree in release-shared release-static test-shared-gcc test-shared-clang asan-ubsan-lsan; do
  cmake --build "$B/$tree" --parallel
done
```

逐树核对 CMakeCache、生成头、实际链接及运行库路径、编译器/标准库 ABI 和 flags，不能仅凭 CMAKE_PREFIX_PATH 推断选择正确。GCC 正式 shared 的 `_GLIBCXX_USE_CXX11_ABI` 必须为 1；普通/ASan 树不能加载检测 Qt。sanitizer cache 标签只记录能力，不能代替真实插桩。

<a id="qualification"></a>
## 3. 选择 tier、阶段和 ABI 模式

| tier | 范围 |
| --- | --- |
| fast | release-shared package contract / candidate capability；不执行完整安装 consumer |
| strict | 增加 GCC public-api、严格 offline QtTest、examples/benchmarks build/smoke、deprecated curl API、label/skip 检查 |
| full | 五树完整 CTest、一致性 all+ext、shared/static 安装和四组件 consumer/生命周期、动态符号、capability、ASan/UBSan/LSan、UCE nightly、Doxygen、metadata 和必需兼容参考 |

`remediation` 是本地修复验证；`final` 必须显式指定且绑定 clean C1。首版 `M.m.0` 的 full/final 必须使用 `snapshot`；后续 patch 必须使用 `current`。`none` 只允许非 full/final 的局部诊断；没有 promotion 发布阶段。

本次 2.1.0 首版命令：

```bash
gate=(python3 scripts/run_release_gate.py
  --tier full --stage final --abi-mode snapshot
  --authority docs/dev/release/2.1.0-release-contract.md
  --release-shared-build-dir "$B/release-shared"
  --release-static-build-dir "$B/release-static"
  --test-shared-gcc-build-dir "$B/test-shared-gcc"
  --test-shared-clang-build-dir "$B/test-shared-clang"
  --asan-ubsan-lsan-build-dir "$B/asan-ubsan-lsan"
  --manifest "$B/release-shared/release/qa-manifest.json"
)
"${gate[@]}"
"${gate[@]}" --verify-manifest
python3 scripts/run_release_gate.py --scan-metadata
git diff --check
```

remediation 使用不同的新 `B`，并把 stage 改为 remediation；不能复用 final 输出。full 必须且只能提供一份上述 authority；缺失、重复、额外或旧 2.0 authority 均失败。`--dry-run` 只核对步骤选择，不证明实际 authority、参考、consumer 或 final 执行成功。

未来准备真实 patch 时，在相同五树参数集合中把 ABI 部分替换为：

```text
--abi-mode current
--abi-reference-first <已核对的本线首版正式参考目录>
--abi-reference-latest <已核对的最近正式patch参考目录>
```

相同参数也传给 `--verify-manifest`。first/latest 必须由真实同线正式 tag 和资产核对，不能任意挑一个较老 patch；两者相同则核对身份后去重。缺参考、错线/tuple、候选冒充历史或 patch 选择 snapshot 均阻断。没有真实 patch 与历史参考时，不声称这条产品升级链已经实测。

### 结果复核

要求当前候选 manifest `result=pass`，独立 verifier `valid=true`、`reasons=[]`，当前注册 required gate/artifact 齐全且真实通过；不按历史 CTest/pytest/gate 总数验收。核对 skip、disabled、空选集、退出码与原始失败，手写结果或删 required 列表不能过关。

候选、authority、命令、五树 capability、参考来源/tuple 和引用文件摘要都必须一致。源码、依赖或构建输入改变要求新候选及 fresh qualification。full 的 UCE 使用 test tree 中固定 run-id；同名证据已存在时保留，另选新树，不覆盖或手改归档。详见 [UCE](../uce/README.md)。

<a id="package-evidence"></a>
## 4. 安装、参考与旧 consumer

两棵 release OFF 树由脚本无过滤安装，运行四个逻辑组件的独立 consumer 并记录生命周期；OFF 树不运行 CTest。单独检查 static 安装改动时：

```bash
python3 scripts/release_package_evidence.py \
  --build-dir "$B/release-static" --linkage static \
  --contract tests/public_api/package_gate_manifest.json \
  --surface-manifest tests/public_api/surface_manifest.json \
  --install-report "$B/release-static/evidence/package/static-install-consumer.json" \
  --lifecycle-report "$B/release-static/evidence/lifecycle/static.xml"
```

shared 使用对应 producer、linkage 和报告名；full 自动执行，不必重复手工跑。consumer 使用 producer profile 的编译器、flags、Qt/CURL 前缀，运行环境按该 producer 单独绑定并核对实际 QCurl/Qt/libcurl 加载路径，不能回退到源码 include 或 host 安装。不要向整个 full gate 导出最低版 libcurl 的 `LD_LIBRARY_PATH`，以免覆盖 ON 树的 bundled provider。Core-only stage 只检验非默认头不可见，不能代替完整包；static consumer 还须覆盖 enum-only 元类型显式初始化。

首版由 `compatibility_reference_capture` 从 release-shared 捕获 `evidence/compatibility/reference/`，包含 `reference.json`、`validation.json`、两库 snapshot、完整 Public 头/真实库、TestSupport、tuple/profile、四组件源码和仅构建一次的可执行文件、原始日志。随后独立必需 gate `compatibility_reference_roundtrip` 输出 `evidence/compatibility/roundtrip/report.json` 及两库 ABI 子报告，分类为 `REFERENCE_VALIDATED`、`patchCompatibility=false`。这只证明参考可用，不能称为 patch 兼容 PASS。

后续 patch 的 `compatibility_first_check` / `compatibility_latest_check` 在 `check-first` / `check-latest` 目录生成 report 与 ABI 子报告，旧二进制运行阶段不 configure/build；独立 source 步骤在 `source-first` / `source-latest` 重建旧源码。两类历史步骤均传 `--require-history`，不能拿同候选自测替代升级。两库完整/残余 ABI 比较明确使用旧/新真实 ELF，不是 XML 自动 fallback；两侧 snapshot 继续作为必需归档与语义校验。旧源码、ABI、旧二进制 loader 路径和公开行为各自取证；动态符号 allowlist 不能替代兼容分析。独立诊断命令、真实 ELF 的必要性与返回码边界见[ABI 参考操作](stable-abi-contract-and-baseline.md)。

WebSocket force-disable 只作负向变体，单组件或裁剪包 PASS 不等于完整包就绪。

## 5. 打包、说明与 checksum

[CHANGELOG](../../../CHANGELOG.md)对应版本条目是 release notes 来源。final 通过后，核对 SECURITY 维护范围、已知限制及同一候选来源，再生成实际资产：

```bash
cpack --config "$B/release-shared/CPackConfig.cmake" -G TGZ -B "$B/release-shared/packages"
```

DEB/RPM 选择相应 generator 并准备工具。Arch 使用[本地完整包入口](../../user/archlinux-packaging.md)，在独立空 work/build/package 目录运行，不安装宿主；验最终归档、SONAME/链接和解包 consumer，不能只验 producer stage。源码归档包含子模块或精确可复验的 pin，不能依赖维护者本地目录。

资产清单至少列明源码、真实包、Doxygen HTML、compatibility-reference、gate 日志/manifest 与 SHA256SUMS。参考资产含匹配调试信息、头与旧程序；final 引用参考，资产清单再引用 final，避免摘要循环。只为已核对的实际文件计算 checksum：

```bash
cd "$B/release-shared/packages"
sha256sum QCurl-2.1.0-Linux.tar.gz > SHA256SUMS
sha256sum --check SHA256SUMS
```

以上文件名须按 CPack 真实输出核对，其他实际资产逐项加入同一清单；不对不存在扩展名写 checksum。重新压缩引起字节变化时复验包和 checksum；输入变化则重做候选资格。checksum 证明完整性，不代表已提供签名/SBOM/provenance，见[供应链](supply-chain.md)。

## 6. CI 与远端发布

[release_delivery_http3_gate](../../../.github/workflows/release_delivery_http3_gate.yml)生成 HTTP/3、Arch/TGZ/DEB 候选 artifacts，不自动创建 Release。HTTP/3 required 需 QCURL_REQUIRE_HTTP3 / QCURL_LC_EXT，Arch 需 ARCH_REPO_SNAPSHOT_DATE。CI、完整本地 final 和远端发布分别取证。

得到独立发布授权后：

1. 核对 release commit、final manifest、CI URL、正式版本 notes 与实际资产清单。
2. 在核对的 remote/branch 创建并推送 annotated tag。
3. 按 CHANGELOG 创建 Release，上传实际包、API 文档、参考材料、证据和 checksum。
4. 重新下载所有资产，核对 tag/commit、SHA256SUMS 和清单，再用独立 consumer 验证。

SBOM、签名、provenance 仅在确已启用并生成时上传或声明。安全修复先在私密 advisory 中验证，利用细节不得早于可下载修复公开。README/CHANGELOG/SECURITY 的已发布入口只随真实发布更新。

## 7. 归档与完成边界

保留候选/子模块、tuple、完整命令与原始日志、manifest 和独立 verify、参考目录、实际资产/sha256、CI URL 与限制。TSan 未运行写 NOT_RUN；首版参考自测不替代真实 patch 升级，局部 PASS 不替代完整资格。

本地 READY / 包就绪不表示 tag、push、Release、上传或下载回验完成。仅报告实际完成的层级，历史 snapshot 不作为当前资格。
