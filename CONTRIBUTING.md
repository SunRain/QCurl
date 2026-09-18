# 贡献指南

贡献者从独立分支提交可复验的代码、测试或文档变更。新能力和影响公共合同的变更先说明目标与边界；缺陷修复、性能改善和测试补齐应附可重现的依据。

## 开始之前

- 运行前提与命令：[构建与测试](docs/dev/build-and-test.md)。
- 代码规范：[Qt6/C++17](Qt6_CPP17_Coding_Style/cn/Qt6_CPP17_Coding_Style.md)；公开与非显然合同注释遵循[中文注释规范](Qt6_CPP17_Coding_Style/cn/CPP_Code_Comment_Guidelines.md)。
- 遵守[行为准则](CODE_OF_CONDUCT.md)。不要提交真实 token、密码、私有地址或带敏感参数的日志。

## 贡献流程

1. Fork 或使用有权限的仓库，从当前开发分支建立职责明确的分支。
2. 完成改动，并更新直接受影响的测试、调用方和文档；行为变化不能只改说明。
3. 按下表选择验证，保留实际命令、原始结果和未验证边界。
4. 提交并发起 PR，写清动机、变更、风险、验证结果及 breaking changes。

## 按改动选择验证

| 改动 | 必需关注 |
| --- | --- |
| docs-only | diff、链接/章节和当前路径；影响候选说明时检查 metadata/hard-break guard；变更安装或 C++ 示例时实际 configure、build、运行文档派生 consumer |
| 普通代码 | 构建、相关 QtTest 与离线严格检查；覆盖本次行为的失败和边界路径 |
| public header / install / export / pkg-config | [安装面检查](docs/dev/build-and-test.md#public-api)，shared 测试树与适用的 static/OFF package evidence，不能给 static/OFF 树分配 CTest |
| 一致性合同 | [libcurl consistency](docs/dev/build-and-test.md#libcurl-consistency) 的对应专题和负向判据 |
| 发布范围、producer 或 release evidence | [发布操作](docs/dev/release/release-procedure.md)的对应检查；最终资格使用 full/final 与 `abi-mode none`，不要求 2.0 ABI baseline |

检查选择应与影响相称；docs-only 不自动触发五树完整发布验收。任何定向 PASS 都不能代替未运行的产品测试或远端 CI。

## 文档与 PR 要求

- 当前主题保持一个正文，入口只分流；文档结构见[总目录](docs/README.md)。
- 变更公开行为、组件边界、机器输入或命令时，同步真实消费者；历史报告不改写成当前结论。
- PR 至少说明为什么改、改了什么、跑了哪些检查及结果、未覆盖的风险。提供截图或日志时先脱敏。
- 本地 `.helloagents/` 不是 CI 或发布输入，也不是贡献的必备文件；正式合同与机器字典留在版本控制中。
- 安全漏洞使用[安全披露流程](SECURITY.md)，不要在公开 Issue 中先披露可利用细节。
