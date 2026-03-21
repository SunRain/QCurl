# QCurl 对 `legendary-python` 移植的技术尽调

本文保留“是否值得以 QCurl 作为移植网络底座”的稳定结论、缺口与边界。历史回归数字、时间戳、报价快照和一次性执行记录不在这里维护。

## 1. 结论摘要

把 `legendary-python` 迁移到 Qt/C++ 时，QCurl 可以覆盖大部分“通用网络基础设施”：

- HTTP/HTTPS 请求发送
- 请求级 header / auth / timeout / proxy 配置
- cookie jar 与会话桥接
- 连接复用与调度
- 流式下载/上传、取消、重试策略基础
- 日志与基础诊断

但 QCurl 不应吞掉 `legendary` 的业务语义。以下能力更适合放在上层实现：

- OAuth / 设备码 / Web 登录流程
- token 刷新与持久化
- Epic API 领域对象与错误映射
- manifest / chunk / 校验 / 安装流程
- 业务级下载编排与恢复策略

## 2. 适配边界

### 2.1 适合放在 QCurl 的部分

- 通用 HTTP 客户端能力
- 连接池、调度器、传输级控制
- cookie 与 request/response 级配置
- 可复用的错误与日志基础设施

### 2.2 不适合放在 QCurl 的部分

- 针对 Epic 端点的 API 语义
- token 生命周期决策
- 游戏安装与分块校验策略
- 任何只服务于单一业务的请求包装层

## 3. 当前能力判断

### 3.1 已具备的基础能力

- 通用 HTTP 请求与版本选择
- HTTP auth / header 注入
- cookie 导入导出与持久化
- 连接复用、调度与并发管理
- WebSocket 能力（对该移植非阻塞，但可作为附加能力）

### 3.2 仍需结合业务层设计的能力

- OAuth / WebView 登录桥接
- GraphQL / REST 客户端封装
- 下载编排、清单解析、断点与校验
- 业务级重试、限流和错误分类

## 4. 建议的移植分层

### 4.1 底层：QCurl

职责：

- 提供稳定网络原语
- 保持协议与传输层语义清晰
- 输出可诊断的错误与日志

### 4.2 中间层：移植项目网络适配

职责：

- EGS / LGD 等客户端封装
- token、cookie、XSRF 等会话策略
- 将业务错误映射到 UI 或 CLI 可消费的领域错误

### 4.3 上层：下载与安装域逻辑

职责：

- manifest / chunk 管理
- 校验与重试策略
- 安装目录、文件落盘、用户交互

## 5. 进入实施前应确认的事项

1. 目标平台矩阵与 Qt 版本
2. 是否需要 Qt WebEngine 或其他 Web 登录方案
3. token / cookie 的持久化介质
4. 下载目录、校验策略与恢复口径
5. 是否把 HTTP/3 视为可选能力还是交付门槛

这些问题不需要在 QCurl 内部解决，但必须在移植项目里先定边界。

## 6. 风险判断

### 6.1 低风险

- 把 QCurl 用作通用 HTTP 底座
- 用 QCurl 承接通用认证头、cookie、代理和 timeout

### 6.2 中风险

- Web 登录与 Qt cookie/profile 的双向同步
- 业务级重试与限流是否需要比通用策略更细的规则

### 6.3 高风险

- 试图把 Epic 专属业务语义直接写进 QCurl
- 把下载校验、安装流程和网络层强耦合

## 7. 推荐实施顺序

### P0

- 先确认业务分层
- 基于 QCurl 跑通登录、基本 API 调用与一次完整下载路径

### P1

- 固化 cookie / token 持久化策略
- 建立离线可回归的关键门禁

### P2

- 再讨论 HTTP/3、额外诊断与交付级优化

## 8. 证据入口

- `src/`
- `tests/qcurl/`
- `tests/libcurl_consistency/`
- `docs/dev/build-and-test.md`

如果需要更细的职责矩阵或 WBS，应把它们维护在专门的设计模块中，而不是在本文持续累积一次性评估细节。
