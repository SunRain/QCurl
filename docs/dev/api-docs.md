# API 文档生成（Doxygen）

本项目使用 Doxygen 生成可查阅的 API 参考文档。生成产物不直接入库，可作为 CI artifact、GitHub Pages 内容或 release asset 发布。

API 文档输入必须由 `tests/public_api/surface_manifest.json` 派生，不能 broad 扫描 `src/`。这样可以保证发布 API 文档只覆盖当前 public install surface，不把 `_p.h`、`src/private/` 或 curl plumbing 误放进用户-facing API browser。

非目标说明：

- 本页只说明派生文档的生成与发布方式，不定义 API 行为、参数边界或默认值。
- 行为合同以 public headers 中的 Doxygen 注释为准；生成结果只是这些注释的浏览视图。

## 1. 安装 Doxygen

请通过发行版包管理器安装 `doxygen`，可选安装 `graphviz` 生成更丰富的图表。

## 2. 本地生成

在仓库根目录先生成 manifest-driven Doxygen 输入片段：

```bash
python3 scripts/generate_doxygen_input_from_surface_manifest.py \
  --manifest tests/public_api/surface_manifest.json \
  --output build/doxygen/qcurl_api_input.doxy \
  --check
```

随后执行：

```bash
doxygen Doxyfile
```

输出目录默认在：

```text
build/doxygen/index.html
```

## 3. 文档范围与约定

- `tests/public_api/surface_manifest.json` 是 API 文档输入的唯一机器可读真源。
- `tests/public_api/metatype_inventory.json` 记录公共 Qt 元类型的 canonical name、兼容理由、
  注册源码与 staged consumer；`qcurl_public_api_metatype_consumer_smoke` 同时验证 typed queued
  投递、动态名称解析和 `QCurl::initialize()` 早于首次连接。
- 生成脚本只选择 `visibility == Public` 且具有公开安装面的 headers；Internal 条目不得进入输入。
- `_p.h`、`src/private/` 和未安装 internal helper 不得进入 release API 文档输入。
- 非 Core 组件可以生成文档，但必须标注组件；Preview API 还必须标注成熟度，不能写入 Core 源码兼容范围。
- 当前 Core API 文档必须反映结构化 `QCNetworkCacheRequestKey`、`cachePartitionKey`、统一 retry method gate、`QCNetworkLoggerHandle` opaque ownership 与 reply snapshot；private transfer record 和 global-init state 不进入安装面文档。
- 注释风格参考：`CPP_Code_Comment_Guidelines.md`。
- 新增、移动或删除公共头文件时，先同步 `tests/public_api/surface_manifest.json` 与 public API gate，再重新生成 Doxygen 输入片段。
- 当前仓库未配置 Doxyqml，且没有 QML public API；Doxyqml 在本 profile 中不适用，不能把
  普通 Doxygen 生成成功记录为 Doxyqml 通过。

`Doxyfile` 通过 `@INCLUDE = build/doxygen/qcurl_api_input.doxy` 读取脚本生成的 `INPUT`，因此直接运行 `doxygen Doxyfile` 前必须先执行生成脚本。

## 4. 发布形态

正式 GitHub Release 建议附带 API 文档产物：

- CI artifact：上传 `build/doxygen/` 压缩包，便于 release 证据追溯。
- GitHub Pages：仅在维护者明确启用 pages 后发布稳定文档入口。
- Release asset：将 Doxygen HTML 压缩包与 release assets 一起上传。

建议命名：

```text
qcurl-<version>-api-docs.tar.gz
```

## 5. Release gate 建议

发布前至少确认：

- manifest-driven 输入脚本能通过 `--check`，且输出只包含 public install surface headers。
- `doxygen Doxyfile` 能完成生成。
- 生成日志无阻塞级别的文档错误。
- public headers 的新增 API 带有能解释行为、线程归属、错误边界和稳定性级别的注释。
- 非 Core 组件和 Preview API 在注释中不被写入 Core 源码兼容范围。

当前 `Doxyfile` 仍允许部分未文档化符号存在；是否把 `WARN_IF_UNDOCUMENTED` 升级为 release blocker，应在独立文档质量方案中处理，避免一次性阻断已有历史 API。
