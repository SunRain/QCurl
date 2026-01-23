# 支持（Support）

本文件用于说明如何获取帮助以及如何高质量地反馈问题。

## 1. 获取帮助

优先通过仓库的 Issue / Discussions（如启用）进行讨论与提问。

## 2. 报告 Bug（建议模板）

提交 Issue 前建议先确认：

- 已阅读 `docs/README.md` 与相关模块文档
- 能用最小示例复现（尽量去除业务代码）
- 已在本地运行基础门禁：`ctest --test-dir build --output-on-failure`

Issue 内容建议包含：

- 复现步骤（尽量最小化）
- 期望行为 vs 实际行为
- 环境信息（Linux 发行版、Qt 版本、libcurl 版本、编译器版本）
- 日志/错误堆栈（注意脱敏）

## 3. 功能建议

欢迎 Feature Request。建议说明：

- 使用场景与价值
- 可能的 API 形态（可选）
- 是否能接受破坏性变更（如涉及）

## 4. 安全问题

安全漏洞请不要公开披露，按 `SECURITY.md` 的流程报告。
