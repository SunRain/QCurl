# 安全策略（SECURITY）

## 报告安全漏洞

请**不要**在公开 Issue、Pull Request 或 Discussions 中披露安全漏洞细节。

本项目使用 GitHub Private Vulnerability Reporting / GitHub Security Advisories 作为私密披露渠道。请在仓库页面使用 **Security → Report a vulnerability** 提交报告。

请在报告中提供：

- 影响版本/提交（如可用）
- 复现步骤与 PoC（如可公开的最小化示例）
- 影响范围、攻击前提和风险评估
- 你是否已经在公开渠道披露过相关信息

维护者会优先在私密 advisory 中确认影响范围、修复分支和披露节奏。若 GitHub 私密披露不可用，请先通过 `SUPPORT.md` 中的普通支持渠道请求建立私密联系，不要在公开 Issue 中贴出漏洞细节。

## 支持版本范围

| 版本线 | 支持状态 | 说明 |
| --- | --- | --- |
| `master` 当前开发线（2.1 候选） | 维护 | 用于接收和验证安全修复；候选尚不等于已发布版本。 |
| 历史版本与版本线（含 2.0、1.x） | 按 advisory 确认 | 报告时请给出确切版本；本策略不承诺所有旧线长期维护或自动回补。 |
| pre-1.0 / RC 历史 | 不支持 | 仅作归档，不承诺安全修复。 |

维护范围覆盖实际受影响的公共组件；Preview 表示成熟度，不构成忽略漏洞或破坏同线 patch 公共兼容的豁免。版本兼容与安全维护周期是不同承诺，具体规则见[版本与兼容政策](docs/dev/release/versioning-and-compatibility.md)。

## 公开披露

安全修复合并、版本发布和 advisory 公开应按 `docs/dev/release/release-procedure.md` 执行。未经维护者确认前，不应在公开 issue、commit message 或 release notes 中暴露可直接利用的细节。
