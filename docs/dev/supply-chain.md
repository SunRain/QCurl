# Supply-chain and CI security

本文记录 QCurl 的 GitHub Actions、依赖更新、release asset 和本地测试镜像的供应链安全约定。

## GitHub Actions permissions

Workflow 默认应使用最小权限。普通 build/test workflow 建议：

```yaml
permissions:
  contents: read
```

需要上传 security events、签名、发布 release 或写入 packages 时，应在对应 job 中单独声明更高权限，并说明原因。不要依赖仓库默认 `GITHUB_TOKEN` 权限。

## Actions pinning

当前 workflow 可使用官方 action 的版本 tag，例如 `actions/checkout@v4`、`actions/upload-artifact@v4`。高风险 release workflow 建议升级为 pin 到 commit SHA，并通过 Dependabot 定期更新。

最低要求：

- 不使用未维护 action。
- 不从不可信 fork 执行带写权限 token 的脚本。
- release workflow 的第三方 action 变更必须经过人工 review。

## Dependabot

`.github/dependabot.yml` 覆盖：

- GitHub Actions。
- `tests/qcurl` 的 npm 测试依赖。
- `tests/libcurl_consistency` 的 Python 测试依赖。

curl submodule、系统包和 Qt/libcurl 版本仍由维护者按 release gate 需求人工升级。

## Docker and local services

`docs/local-mail-server/docker-compose.yml` 仅用于本地开发/测试。`latest` 镜像不应作为 release gate 的可信输入；如某个 gate 需要容器镜像，必须在 workflow 或锁文件中使用明确 tag，关键发布路径建议使用 digest。

## Release assets

正式 GitHub Release 建议提供：

- CPack packages：TGZ / DEB / RPM。
- `SHA256SUMS`。
- ABI diff report。
- Doxygen API docs artifact。
- release gate logs / manifests。
- SBOM / provenance / signatures（如果维护者启用对应工具）。

当前 release workflow 只产出候选 packages 与 gate reports；它不会创建 GitHub Release，也不会自动生成或上传 checksum、SBOM、signature 或 provenance。正式发布前，checksum、asset 清单和 release notes 仍是 release blocker；SBOM、signature 和 provenance 在未启用前只能作为 roadmap / follow-up 描述。

在未启用签名和 provenance 前，release notes 应明确可信边界：用户至少应校验 `SHA256SUMS`，并优先从 GitHub Release 官方 assets 下载。

## SBOM / provenance / signing roadmap

当前建议路线：

1. 先为 release assets 生成 `SHA256SUMS`。
2. 引入 SBOM 工具，覆盖源码包和 CPack 产物。
3. 对 release assets 引入签名。
4. 对 release workflow 引入 provenance 产物。

这些动作属于发布工程增强；未落地前不得在 README 或 release notes 中宣称已提供签名或 provenance。
