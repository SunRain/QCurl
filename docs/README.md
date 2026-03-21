# QCurl 文档

> 本目录用于收敛仓库内的技术文档，并按读者角色分层组织。

维护约定：

- 入口页只保留导航与稳定 contract。
- 单次执行日志、环境快照、性能数字和评审记录不应长期堆在索引页里。
- 文档与代码冲突时，以代码和相关测试为准。

## 快速入口

- 使用者（集成与用法）: `docs/user/README.md`
- 贡献者（构建、测试、贡献流程）: `docs/dev/README.md`
- 维护者（架构与设计索引）: `docs/arch/README.md`
- 参考资料（性能/基准/协议等）: `docs/reference/README.md`

## 目录结构（信息架构）

- `docs/user/`: 面向库使用者（快速开始、配置、常见问题）
- `docs/dev/`: 面向贡献者（构建、测试门禁、贡献流程、API 文档生成）
- `docs/arch/`: 面向维护者（架构说明、迁移/交付设计文档索引）
- `docs/reference/`: 面向查阅（benchmark 报告、性能回归指南、协议/实现细节索引）

## 其他入口

- `README.md`: 项目总览与最小示例
- `SYSTEM_DOCUMENTATION.md`: 详细系统文档（偏“全量说明/实现细节”）
- `helloagents/`: 工程知识库（维护者 SSOT，与代码保持一致）
