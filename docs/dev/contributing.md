# 贡献指南（开发侧补充）

> 本文件是对根目录 `CONTRIBUTING.md` 的补充，聚焦“仓库内开发与维护约定”。

## 1. 贡献流程（建议）

1. Fork 仓库并创建分支（按功能命名）
2. 本地构建与测试（见 `docs/dev/build-and-test.md`）
3. 提交变更并发起 PR
4. PR 描述中包含：动机、改动点、验证方式、风险

## 2. 代码与风格

- 遵循：`Qt6_CPP17_Coding_Style.md`
- 注释（含 Doxygen 风格）：`CPP_Code_Comment_Guidelines.md`

## 3. 测试要求

- 变更需补齐/调整对应测试（Qt Test / 集成测试）
- 提交前至少确保离线门禁通过（见 `docs/dev/build-and-test.md`）

## 4. 文档与知识库同步

当变更涉及行为契约、模块边界或新增能力时：

- 更新 `docs/` 中对应分层文档（使用者/贡献者/维护者/参考）
- 若涉及工程 SSOT，同步更新 `helloagents/modules/` 与 `helloagents/modules/project.md`

## 5. 安全与隐私

- 禁止在示例/测试/文档中提交真实 token、密码、内部域名
- 安全问题上报流程见 `SECURITY.md`
