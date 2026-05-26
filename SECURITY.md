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
| `1.x` | 支持 | 当前 stable line，优先接收安全修复。 |
| `main` / `master` | 支持 | 开发分支，用于接收和验证修复。 |
| pre-1.0 / RC / 旧 hard-break 历史 | 不支持 | 仅作为内部历史归档，不承诺安全修复。 |
| WebSocket / Diagnostics / Other Extras Preview | best effort | 可随包发布，但不属于默认 Core Stable 支持面。 |

## 公开披露

安全修复合并、版本发布和 advisory 公开应按 `docs/dev/release-procedure.md` 执行。未经维护者确认前，不应在公开 issue、commit message 或 release notes 中暴露可直接利用的细节。
