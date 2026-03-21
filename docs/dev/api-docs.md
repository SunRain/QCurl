# API 文档生成（Doxygen）

本项目使用 Doxygen 生成本地可查阅的 API 参考文档，生成产物不入库。

非目标说明：

- 本页只说明派生文档的生成方式，不定义 API 行为、参数边界或默认值。
- 行为合同以 public headers 中的 Doxygen 注释为准；生成结果只是这些注释的浏览视图。

## 1. 安装 Doxygen

请通过发行版包管理器安装 `doxygen`（可选安装 `graphviz` 以生成更丰富的图表）。

## 2. 生成文档

在仓库根目录执行：

```bash
doxygen Doxyfile
```

输出目录默认在：

```
build/doxygen/index.html
```

## 3. 文档范围与约定

- 扫描范围默认是 `src/`（排除 `src/private/`）
- 注释风格建议参考：`CPP_Code_Comment_Guidelines.md`
- 如新增/移动公共头文件，请同步更新 `Doxyfile` 的 `INPUT` / `FILE_PATTERNS` / `EXCLUDE_PATTERNS`
- 如需核对当前扫描边界，可直接查看 `Doxyfile` 与 `src/*.h` 中的 public API 注释
