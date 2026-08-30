# Release procedure

本文是 QCurl 维护者执行后续 GitHub Release 的单一流程入口。它不替代 release contract；`docs/arch/2.0.0-hard-break-release-contract.md` 定义当前候选承诺，本文定义执行步骤和证据归档。

## 0. 当前 workflow 覆盖边界

`.github/workflows/release_delivery_http3_gate.yml` 是 release candidate 证据入口，不是完整发布流水线。正式发布仍以本文的人工步骤为单一真源。

当前 workflow 已覆盖：

- Debian 12 release gate：构建带 HTTP/3 能力的 vendored curl，运行 `tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`，并上传 gate reports。
- Arch snapshot release gate：要求 digest-pinned Arch 镜像和 `ARCH_REPO_SNAPSHOT_DATE`，用于补充发行版快照证据。
- CPack 候选包：在 workflow 中生成 TGZ / DEB 候选产物并作为 CI artifact 上传。
- HTTP/3 required 语义：通过 `QCURL_REQUIRE_HTTP3=1` 与 `QCURL_LC_EXT=1` 固化候选证据前置。

正式 GitHub Release 前仍是 release blocker 的未覆盖项：

- 创建 annotated tag、推送 tag、创建 GitHub Release。
- 生成并上传 `SHA256SUMS`，并核对所有 release assets。
- 上传 Doxygen API docs artifact、release notes 和 gate manifest。
- 确认 release commit、tag、assets、checksum 与 CI run URL 一一对应。

以下是发布工程 follow-up，不得在未落地前写成已提供能力：

- SBOM。
- asset signing。
- provenance / SLSA 产物。
- GitHub Security Advisory 自动联动。

## 1. 前置确认

发布前确认：

- 当前版本号、`PROJECT_VERSION`、shared library `SOVERSION` 与 release contract 一致。
- 发布说明明确 2.x Core 源码兼容、ABI 非稳定，并要求下游在每次 QCurl 更新后重编译和重新链接。
- `CHANGELOG.md` 已包含本次用户可见变更。
- `SECURITY.md` 的支持版本范围仍准确。
- 发布面固定为 Core、Blocking Extras、Other Extras、Test Support 四个逻辑消费组件和三个物理库；Preview 是成熟度，Internal 是可见性，不得写成第五模块。
- Diagnostics 与 WebSocket 仍标注为 Other Extras 内的 Preview API；Middleware Extras 标注为 Other Extras 内的 Stable API。
- Test Support 只作为开发静态库交付，不得写成生产 Runtime。
- 工作区无无关 dirty change；发布分支只包含本次 release 所需变更。

## 2. 本地/CI 验证

完整 gate 只接受六棵彼此独立、从空目录配置的物理树。每个参数都必须显式提供，
不得把其他树作为 producer fallback：

| tree ID | 参数 | 固定能力与职责 |
| --- | --- | --- |
| `release-shared` | `--release-shared-build-dir` | `BUILD_TESTING=OFF`、shared；安装、导出、consumer、生命周期和动态符号导出面 |
| `release-static` | `--release-static-build-dir` | `BUILD_TESTING=OFF`、static；安装、导出、consumer 和生命周期 |
| `test-shared-gcc` | `--test-shared-gcc-build-dir` | `BUILD_TESTING=ON`、GCC；QtTest、public API、libcurl consistency |
| `test-shared-clang` | `--test-shared-clang-build-dir` | `BUILD_TESTING=ON`、Clang；交叉编译器 QtTest 和 public API |
| `asan-ubsan-lsan` | `--asan-ubsan-lsan-build-dir` | `BUILD_TESTING=ON`、Clang、ASan/UBSan/LSan |
| `tsan` | `--tsan-build-dir` | `BUILD_TESTING=ON`、Clang、TSan |

示例配置和构建命令如下：

```bash
cmake -S . -B build-release-shared -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DQCURL_BUILD_SHARED_LIBS=ON
cmake --build build-release-shared --parallel

cmake -S . -B build-release-static -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF \
  -DQCURL_BUILD_SHARED_LIBS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake --build build-release-static \
  --target QCurl QCurlOtherExtras QCurlTestSupport --parallel

cmake -S . -B build-test-shared-gcc -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON -DCMAKE_CXX_COMPILER=g++
cmake --build build-test-shared-gcc --parallel

cmake -S . -B build-test-shared-clang -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build-test-shared-clang --parallel

cmake -S . -B build-asan-ubsan-lsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined,leak"
cmake --build build-asan-ubsan-lsan --parallel

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread"
cmake --build build-tsan --parallel

python3 scripts/run_release_gate.py --tier full --abi-mode none \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --tsan-build-dir build-tsan
python3 scripts/run_release_gate.py --scan-metadata \
  --release-shared-build-dir build-release-shared
git diff --check
```

`release-shared` 和 `release-static` 的 package evidence 由
`scripts/release_package_evidence.py` 直接执行无过滤完整安装、四个独立 consumer 和生命周期
报告；这两棵 `BUILD_TESTING=OFF` 树不注册 CTest。测试和 sanitizer 证据只消费表中指定的
ON tree。候选 manifest 必须绑定同一 Linux、同一完整 commit、同一 toolchain、clean
worktree、tree capability、规范化 command 和 regular-file digest；手工编辑结果字段不能
授权发布。

`--abi-mode none` 是 2.0 的默认和正式模式。它不生成 ABI baseline、snapshot 或 diff，缺少
`abi/baseline/qcurl-core-v2.abi.xml` 也不构成 release blocker。Core allowlist 同时覆盖 Core
与嵌入其中的 Blocking Extras 公共符号，Other Extras 保留独立 allowlist；这些检查只防止
private symbol 泄漏，不证明二进制兼容。Test Support 是静态开发库，不存在对应动态符号
门禁。`current`、`promotion-candidate` 和 `qcurl_abi_gate.py promote` 仅为
`docs/roadmap/stable-abi-contract-and-baseline.md` 保留，不能用于扩大或缩小 2.0 发布声明。

## 3. 打包与 release assets

CPack 产物来自 release build 目录：

```bash
cmake --build build --target package
```

建议随 GitHub Release 上传：

- source archive（GitHub tag 自动生成，必要时补充维护者生成的 source package）。
- CPack TGZ / DEB / RPM。
- Doxygen HTML artifact（见 `docs/dev/api-docs.md`）。
- release gate logs / manifest。
- checksums：`SHA256SUMS`。
- SBOM / provenance / signature（仅在对应流程已启用时，见 `docs/dev/supply-chain.md`）。

生成 checksum 示例：

```bash
sha256sum build/*.tar.gz build/*.deb build/*.rpm > SHA256SUMS
```

## 4. Tag 与 GitHub Release

Tag 和 GitHub Release 是远程发布动作，不能由本地 readiness PASS 自动代替。

推荐顺序：

1. 确认 release commit。
2. 创建带注释 tag，例如 `v2.0.0`。
3. 推送 tag。
4. 在 GitHub Release 中使用 `docs/arch/2.0.0-release-notes.md` 和 `CHANGELOG.md` 生成 release notes。
5. 上传 assets、checksums、SBOM/provenance/signature。
6. 标记是否为 latest stable release。

示例命令只供维护者人工执行：

```bash
git tag -a v2.0.0 -m "QCurl 2.0.0"
git push public v2.0.0
```

## 5. 安全发布与 advisory

涉及安全修复时：

- 使用 GitHub Security Advisory 私密协作。
- 先在私密分支验证修复和回归测试。
- release notes 中避免提前公开可直接利用的细节。
- Advisory 公开时间应与修复 release 可下载时间一致或之后。

## 6. 发布后核验

发布后至少检查：

- GitHub Release 页面显示正确 tag、release notes 和 assets。
- `SHA256SUMS` 与上传资产匹配。
- 独立 consumer 可以通过 release asset 或 tag 构建并 `find_package(QCurl CONFIG REQUIRED)`。
- `SECURITY.md`、`README.md`、`CHANGELOG.md` 指向当前 release 线。
- 如需撤回，先标记 release 为 pre-release 或撤下资产，再发布说明和修复版本。

## 7. 证据归档

每次 release 应保留：

- commit SHA / tag。
- CI run URL。
- gate 命令摘要和结果。
- manifest 中的 `abiMode=none` 与下游重编译声明。
- assets 清单和 sha256。
- 已知限制和 follow-up。

长期公共文档不应把某次本地 PASS 当作永久当前状态；具体证据应绑定 tag、commit 或 CI artifact。
