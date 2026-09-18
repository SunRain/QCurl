# 发布操作

维护者用本页准备候选、执行五树资格验收、打包并在获得授权后发布。唯一 release identity authority 是[2.0 正式发布合同](2.0.0-hard-break-release-contract.md)；本页只定义操作，不以历史报告、文档校验或局部 PASS 代替候选证明。命令默认从仓库根目录执行。

## 1. 前置条件与授权

- `2.0.0` 的正式 release notes 必须在形成 C1 前冻结于 CHANGELOG；在通过 L1-L3 并获得独立 P1 授权前，不得把候选描述为已发布。Core 的合同是 2.x 源码兼容、ABI 非稳定，每次更新都要求下游重编译和重新链接。
- 准备 Linux、CMake/Ninja、GCC/Clang、满足最低版本的 Qt/libcurl/zlib、Python/pytest、Doxygen，以及[本地测试服务](../build-and-test.md#2-qttest-与本地服务)。一致性与 HTTP/3 所需 curl testenv/QUIC 依赖见[专题说明](../../../tests/libcurl_consistency/README.md)。
- 正式候选不得裁剪可用的 WebSocket。其余组件归属、成熟度、安装面与[公共头边界](../architecture/public-header-boundary.md)一致，Test Support 不是生产 Runtime。
- 先完成源码、文档与机器输入，再验证候选。最终资格要求干净、完整的 C1 身份及子模块；dirty WIP 的 remediation 结果不能改名为 final。
- 本地 gate 不执行 commit/tag/push/Release，但身份采集会读取 Git。任何远端动作和 ABI promotion 都需要独立授权。

<a id="producer-trees"></a>
## 2. 五棵独立 producer 树

构建目录必须物理独立，不接受同一路径、符号链接复用或其他 producer 回退。新一轮完整验收使用为该候选准备的目录；不要覆盖需要保留的旧证据。

| tree ID | gate 参数 | 固定能力与职责 |
| --- | --- | --- |
| release-shared | `--release-shared-build-dir` | GCC、shared、BUILD_TESTING=OFF；安装/导出/consumer/生命周期/动态符号 |
| release-static | `--release-static-build-dir` | GCC、static、BUILD_TESTING=OFF；安装/导出/consumer/生命周期 |
| test-shared-gcc | `--test-shared-gcc-build-dir` | GCC、shared、BUILD_TESTING=ON；QtTest、public API、一致性 |
| test-shared-clang | `--test-shared-clang-build-dir` | Clang、shared、BUILD_TESTING=ON；交叉编译器 QtTest/public API |
| asan-ubsan-lsan | `--asan-ubsan-lsan-build-dir` | Clang、shared、BUILD_TESTING=ON；ASan/UBSan/LSan |

本次 2.0.0 正式发布不要求 TSan 树、插桩 Qt 或 TSan 报告。独立诊断入口及其真实失败语义保留在 [UCE 的检测 Qt 教程](../uce/README.md#71-检测专用-qt)，不进入本次 final 必需集合或通过数量；没有成功证据不得宣称候选通过 TSan，已确认产品缺陷仍按缺陷本身处理。本次政策不自动推广到后续版本。

以下五树均使用 Ninja；所有 shared 测试树必须明确 shared，static 只允许测试关闭：

```bash
git submodule update --init --recursive
cmake -S . -B build-release-shared -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=OFF -DQCURL_BUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake --build build-release-shared --parallel

cmake -S . -B build-release-static -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=OFF -DQCURL_BUILD_SHARED_LIBS=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake --build build-release-static --parallel

cmake -S . -B build-test-shared-gcc -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON
cmake --build build-test-shared-gcc --parallel

cmake -S . -B build-test-shared-clang -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON
cmake --build build-test-shared-clang --parallel

cmake -S . -B build-asan-ubsan-lsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON \
  -DQCURL_SANITIZER_PROFILE=asan-ubsan-lsan \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined,leak -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined,leak -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined,leak" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined,leak"
cmake --build build-asan-ubsan-lsan --parallel
```

`QCURL_SANITIZER_PROFILE` 是 gate 读取的 cache 能力记录，不能代替真实编译/链接插桩。普通/ASan 树不能加载检测 Qt。固定能力、真实路径和 compiler family 会在 full gate 开始前检查；runtime 检测环境另由 sanitizer runner 校准。ASan/UBSan/LSan、真实泄漏检测及普通线程/生命周期测试仍为必需项。

<a id="qualification"></a>
## 3. 选择 tier 与阶段

| tier | 当前实现的检查范围 |
| --- | --- |
| fast | release-shared 的 package contract 与 candidate 能力检查；**不执行完整安装 consumer** |
| strict | 加上 test-shared-gcc 的 public-api、严格 offline QtTest、examples/benchmarks build/smoke、deprecated curl API、label/skip 检查 |
| full | 五树完整 CTest、一致性 all+ext、shared/static package consumer/生命周期、动态符号、capability、ASan/UBSan/LSan、UCE nightly、Doxygen 和 metadata |

`remediation`（默认）是修复中本地验证，不能形成最终资格；`final` 必须显式指定并绑定干净 C1。`promotion` 及非 none ABI 模式留给[未来稳定 ABI 项目](stable-abi-contract-and-baseline.md)，不是 2.0 的必需阶段。

最终资格命令：

```bash
python3 scripts/run_release_gate.py --tier full --stage final --abi-mode none \
  --authority docs/dev/release/2.0.0-hard-break-release-contract.md \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --manifest build-release-shared/release/qa-manifest.json
```

修复阶段使用同一参数集合，将 stage 明确改为 remediation。full 必须且只能提供上述一份 authority；缺失、重复或额外文件都失败。`--abi-mode none` 不读写 v2 ABI baseline/diff，不运行 promotion。

在命令末尾增加 `--dry-run` 只打印步骤：当前实现仍检查 full 的 tree capability，但提前返回会跳过实际 authority 和证据执行，因此 dry-run 不能当作 authority、consumer 或 final 通过。参数核对时还必须调用实际 authority 校验或运行对应负向回归。

### 结果复核

完成后用同一集合验证 manifest：

```bash
python3 scripts/run_release_gate.py --tier full --stage final --abi-mode none \
  --authority docs/dev/release/2.0.0-hard-break-release-contract.md \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --manifest build-release-shared/release/qa-manifest.json --verify-manifest
python3 scripts/run_release_gate.py --scan-metadata
git diff --check
```

manifest 需要同一候选、工具链、五树能力、规范化命令、正式合同与 regular-file artifact digest；每个必需 gate ID 必须恰有一条真实通过结果，不按旧六树的总数验收。手写结果字段、历史 PASS、单个 sanitizer/control、普通 pytest 或文档检查均不能替代完整 final。政策变更前的 manifest 也不能作为当前证据。Core/Other Extras 动态符号 allowlist 防止私有符号泄漏，不证明二进制兼容。

full 内的 UCE 固定使用该 test tree 下的 release-gate run-id；若已有同名证据，应保留旧结果并使用新候选构建树，不覆盖或手改归档。更多归档与远端持久化边界见[UCE](../uce/README.md)。

<a id="package-evidence"></a>
## 4. OFF producer 的完整 package evidence

release-shared/static 均由现有脚本无过滤安装并构建四个逻辑组件的隔离 consumer，生成安装和生命周期报告；两棵 OFF 树不注册或运行 CTest。需要单独验证 static 安装改动时，完成第 2 节的 static 构建后执行：

```bash
python3 scripts/release_package_evidence.py \
  --build-dir build-release-static --linkage static \
  --contract tests/public_api/package_gate_manifest.json \
  --surface-manifest tests/public_api/surface_manifest.json \
  --install-report build-release-static/evidence/package/static-install-consumer.json \
  --lifecycle-report build-release-static/evidence/lifecycle/static.xml
```

shared 使用对应 shared tree、linkage 与报告文件名；full gate 自动执行两条路径，不必重复手工执行。Core-only stage 只用于证明非默认头未泄漏，不能替代完整包安装。static consumer 还覆盖 enum-only 元类型显式初始化；Core export 必需的 libcurl 依赖与 find_dependency 必须配套，不能额外泄漏 ZLIB。

WebSocket-capable libcurl 必须保持默认能力并提供 WebSocket/Pool 测试证据；force-disable 只作 negative variant。任何单组件/裁剪包 PASS 都不等于完整包或最终发布就绪。

## 5. 打包、说明与 checksum

本地 final 通过不创建远端发布。[CHANGELOG](../../../CHANGELOG.md)对应版本条目是 release notes 唯一来源，不再单独维护同版说明页。上传前冻结版本条目、确认 SECURITY 支持范围和已知限制。

在 release-shared 构建树生成指定格式，避免默认同时生成本机缺少打包工具的格式：

```bash
cpack --config build-release-shared/CPackConfig.cmake -G TGZ -B build-release-shared/packages
```

DEB/RPM 需各自工具，选择相应 generator 并确认实际产物。资产包括源码归档、实际生成的包、[Doxygen HTML](../api-docs.md)、gate 日志/manifest 与 SHA256SUMS。只对本次核对过的真实文件生成 checksum，不用包含不存在扩展名的通配命令：

```bash
cd build-release-shared/packages
sha256sum QCurl-2.0.0-Linux.tar.gz > SHA256SUMS
sha256sum --check SHA256SUMS
```

示例文件名按 CPack 实际输出替换，其他资产逐项加入同一清单；随后回到仓库根目录。checksum 证明下载完整性，不等于签名或 provenance；这些增强在启用前不得宣称已提供，安全要求见[供应链](supply-chain.md)。

## 6. CI 与远端发布

[release_delivery_http3_gate](../../../.github/workflows/release_delivery_http3_gate.yml)提供 Debian HTTP/3 gate、digest-pinned Arch snapshot 和 TGZ/DEB 候选 artifacts，不自动创建 GitHub Release。HTTP/3 required 依赖 QCURL_REQUIRE_HTTP3 与 QCURL_LC_EXT；Arch 还需 ARCH_REPO_SNAPSHOT_DATE。CI artifacts、完整本地 final 与远端发布仍是独立证据。

获得发布授权后按顺序：

1. 确认 release commit、final manifest、实际 CI run URL 与待上传 assets 一一对应。
2. 创建 annotated tag 并推送到核对过的远端；不能由 readiness 自动触发。
3. 从 CHANGELOG 对应版本条目生成 GitHub Release，正确标记是否最新稳定版。
4. 上传包、API 文档、证据、checksum；SBOM/签名/provenance 只在确已启用并生成后上传。
5. 重新下载核对 SHA256SUMS、tag/commit 和 asset 清单，用独立 consumer 验证消费。

安全修复先在私密 advisory 分支验证，公开利用细节不得早于可下载修复。发布后更新 README/CHANGELOG/SECURITY 的版本入口；需要撤回时明确标记、撤下问题资产并说明修复路线。

## 7. 归档与完成边界

保留 commit/tag、CI URL、完整命令与结果、manifest 的 abiMode=none、下游重编译声明、assets/sha256 和已知限制。SBOM、签名、provenance、advisory 自动联动仍是未启用的工程增强，不能当作本次已提供能力。

文档校验、remediation、本地 final、远端 CI、Git 提交和 Release 各自有边界；只报告实际完成的层级，不把历史 snapshot 当作当前资格。
