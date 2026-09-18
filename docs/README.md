# QCurl 文档

本文档对应当前 `2.0.0` 开发候选；`1.0.0` 是已发布历史。Core 在 2.x 内保证源码兼容，ABI 非稳定，每次更新都需要下游重新编译和链接。候选文档不是发布通过证明。

## 用户

| 任务 | 正文 |
| --- | --- |
| 安装并跑通独立 Core 程序 | [快速开始](user/quickstart.md) |
| 请求、HTTP 版本、代理、TLS、重试、缓存和上传 | [常见配置](user/configuration.md) |
| lane、优先级、reservation、取消与通知 | [Lane scheduler](user/lane-scheduler.md) |
| 传输暂停、下载背压与上传源暂停 | [流控](user/flow-control.md) |
| 升级已发布的 1.0 API/包 | [迁移到 2.0](user/migration-2.0.md) |

用户可见变化见 [CHANGELOG](../CHANGELOG.md)，完整程序见[示例集合](../examples/README.md)。API 细节以安装头的注释为准，浏览文档的生成方式见下方维护者入口。

## 开发者 / 维护者

### 开发与验证

- [贡献流程](../CONTRIBUTING.md)：提交与验证范围。
- [构建与测试](dev/build-and-test.md)：日常构建、严格 QtTest、一致性专题与结果解释。
- [公共头与安装边界](dev/architecture/public-header-boundary.md)：install/export、隔离 consumer 与四类公共合同检查。
- [PIMPL 与 shared-data 规范](dev/pimpl-and-shared-data-style.md)：实现布局和 special members。
- [API 文档生成](dev/api-docs.md)：manifest-driven Doxygen 与产物核对。
- [性能回归](dev/performance.md)：方法、命令、阈值与证据局限。

### 架构

- [架构概览](dev/architecture/overview.md)：模块边界和请求路径。
- [libcurl binding](dev/architecture/libcurl-binding-contract.md)：owner/lifetime、驱动、协议与诊断约束。
- [请求归一化管线](dev/architecture/request-normalization-pipeline.md)。
- [传输 pause/resume 实现](dev/architecture/transport-pause-resume.md)。
- [业务移植尽调案例](dev/architecture/porting-due-diligence.md)：适配分层，不是上手或发布入口。

### 发布与证据

- [正式 2.0 发布合同](dev/release/2.0.0-hard-break-release-contract.md)：唯一 release identity authority。
- [发布操作](dev/release/release-procedure.md)：五树、阶段、候选验证、打包与远端动作。
- [UCE 使用与证据](dev/uce/README.md)：启动、tier、schema 入口与归档判据。
- [供应链安全](dev/release/supply-chain.md)。
- [未来稳定 ABI 项目](dev/release/stable-abi-contract-and-baseline.md)：Deferred，不是 2.0 发布阻断项。
- [历史索引](dev/archive/README.md)：已发布 1.0、pre-1.0、dated review 与旧任务/ABI 证据；仅作追溯。
