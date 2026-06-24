# Release procedure

本文是 QCurl 维护者执行 GitHub Release 的单一流程入口。它不替代 release contract；`docs/arch/1.0-first-stable-release-contract.md` 定义发布承诺，本文定义执行步骤和证据归档。

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
- 上传 ABI diff report、Doxygen API docs artifact、release notes 和 gate manifest。
- 确认 release commit、tag、assets、checksum 与 CI run URL 一一对应。

以下是发布工程 follow-up，不得在未落地前写成已提供能力：

- SBOM。
- asset signing。
- provenance / SLSA 产物。
- GitHub Security Advisory 自动联动。

## 1. 前置确认

发布前确认：

- 当前版本号、`PROJECT_VERSION`、shared library `SOVERSION` 与 release contract 一致。
- `CHANGELOG.md` 已包含本次用户可见变更。
- `SECURITY.md` 的支持版本范围仍准确。
- WebSocket、Diagnostics、Other Extras 仍按 Preview / non-default surface 表述，除非已有独立稳定合同。
- 工作区无无关 dirty change；发布分支只包含本次 release 所需变更。

## 2. 本地/CI 验证

推荐使用 clean build 目录执行 release gate：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel

cmake -S . -B build-static -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_SHARED_LIBS=OFF \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake --build build-static --target QCurl qcurl_public_api_self_compile --parallel

python3 scripts/qcurl_abi_gate.py --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src baseline \
  --output abi/baseline/qcurl-core-v1.abi.xml
python3 scripts/qcurl_abi_gate.py --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src diff \
  --baseline abi/baseline/qcurl-core-v1.abi.xml \
  --report build/abi/qcurl-core-v1.abidiff.txt \
  --current-snapshot build/abi/qcurl-core-v1.current.abi.xml

python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static
python3 scripts/run_release_gate.py --scan-metadata --build-dir build
git diff --check
```

Fresh release 口径下，`QCurl 1.0.0` 是首个 Stable ABI baseline。ABI 证据以当前
`libQCurl.so.1.0.0` 生成的 `abi/baseline/qcurl-core-v1.abi.xml` 和
`build/abi/qcurl-core-v1.abidiff.txt` clean diff 为准。若任一 gate 失败，不得 tag 或创建
GitHub Release。缺少 `abidw` / `abidiff`、HTTP/3 环境、httpbin 等前置条件时，应先补齐环境或把失败记录为 release blocker。

## 3. 打包与 release assets

CPack 产物来自 release build 目录：

```bash
cmake --build build --target package
```

建议随 GitHub Release 上传：

- source archive（GitHub tag 自动生成，必要时补充维护者生成的 source package）。
- CPack TGZ / DEB / RPM。
- ABI diff report：`build/abi/qcurl-core-v1.abidiff.txt`。
- Doxygen HTML artifact（见 `docs/dev/api-docs.md`）。
- release gate logs / manifest。
- checksums：`SHA256SUMS`。
- SBOM / provenance / signature（仅在对应流程已启用时，见 `docs/dev/supply-chain.md`）。

生成 checksum 示例：

```bash
sha256sum build/*.tar.gz build/*.deb build/*.rpm build/abi/qcurl-core-v1.abidiff.txt > SHA256SUMS
```

## 4. Tag 与 GitHub Release

Tag 和 GitHub Release 是远程发布动作，不能由本地 readiness PASS 自动代替。

推荐顺序：

1. 确认 release commit。
2. 创建带注释 tag，例如 `v1.0.0`。
3. 推送 tag。
4. 在 GitHub Release 中使用 `docs/arch/1.0.0-release-notes.md` 和 `CHANGELOG.md` 生成 release notes。
5. 上传 assets、checksums、ABI report、SBOM/provenance/signature。
6. 标记是否为 latest stable release。

示例命令只供维护者人工执行：

```bash
git tag -a v1.0.0 -m "QCurl 1.0.0"
git push public v1.0.0
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
- ABI baseline / diff 位置。
- assets 清单和 sha256。
- 已知限制和 follow-up。

长期公共文档不应把某次本地 PASS 当作永久当前状态；具体证据应绑定 tag、commit 或 CI artifact。
