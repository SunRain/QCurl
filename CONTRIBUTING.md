# 贡献指南（CONTRIBUTING）

感谢你愿意为 QCurl 贡献代码或文档。本指南面向贡献者，给出最小可复现的贡献路径。

## 1. 开始之前

- 请先阅读：`docs/dev/build-and-test.md`
- 代码风格：`Qt6_CPP17_Coding_Style.md`
- 注释规范（含 Doxygen 约定）：`CPP_Code_Comment_Guidelines.md`
- 行为准则：`CODE_OF_CONDUCT.md`

## 2. 提交内容类型

- 修复 Bug / 性能优化
- 新功能（建议先开 Issue 讨论）
- 文档补充与修订
- 测试补齐（强烈欢迎）

## 3. 本地构建与测试（最小门禁）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

> 如你的改动涉及一致性 gate，请参考 `docs/dev/build-and-test.md` 的 `libcurl_consistency` 章节。

## 4. PR 要求（建议）

PR 描述请包含：

- 背景/动机（为什么要改）
- 改动点（做了什么）
- 验证方式（跑了哪些命令/用例）
- 风险与兼容性（是否影响 API/行为/性能）

## 5. 文档与安全

- 禁止在代码/测试/文档中提交真实 token、密码、内部域名
- 安全问题上报流程见：`SECURITY.md`
