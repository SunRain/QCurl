# Third-party notices

本文记录 QCurl 仓库中随源码或发布包可能出现的第三方材料。根目录 `LICENSE` 只描述 QCurl 自身授权，不自动覆盖第三方目录。

## QCurl

| 项 | 内容 |
| --- | --- |
| License | MIT |
| 文件 | `LICENSE` |
| 说明 | 适用于 QCurl 自身源码与文档，除非文件或目录另有说明。 |

## curl / libcurl submodule

| 项 | 内容 |
| --- | --- |
| 路径 | `curl/` |
| License | curl license |
| 文件 | `curl/COPYING` |
| 用途 | 可选 bundled curl / libcurl consistency gate。 |
| 分发说明 | 若 release package 或源码包包含 `curl/`，必须保留其 license 与 copyright notice。 |

## legendary-python reference material

| 项 | 内容 |
| --- | --- |
| 路径 | `legendary-python/` |
| License | GPL-3.0 |
| 文件 | `legendary-python/LICENSE` |
| 用途 | 历史/参考材料。不得在未完成许可复核前混入 QCurl MIT runtime/library 分发面。 |
| 分发说明 | 若公开 source archive 包含该目录，release notes 或 notices 应明确其独立许可证。 |

## Test and tooling dependencies

| 路径 | 说明 |
| --- | --- |
| `tests/libcurl_consistency/requirements.lock.txt` | Python test tooling lock file；仅用于测试/门禁。 |
| `tests/qcurl/package-lock.json` | Node-based local test support lock file；仅用于测试/门禁。 |
| `docs/local-mail-server/docker-compose.yml` | 本地邮件测试/开发 compose 示例；镜像治理见 `docs/dev/supply-chain.md`。 |

## SPDX / REUSE policy

后续若要提升许可可审计性，建议渐进执行：

1. 新增源码文件使用 `SPDX-License-Identifier: MIT`。
2. 第三方目录保留原始许可证文件，不改写上游版权声明。
3. 发布包生成前检查 source archive 是否包含 GPL、curl license 或其他第三方许可证材料。
4. 若引入新第三方代码，必须在合并前更新本文。
