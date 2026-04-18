# PIMPL 与 Shared-Data 规范草案

> 适用范围：QCurl 的 public / library-facing 头文件，以及会影响 install surface、ABI 和 public-api guardrail 的实现约定。

## 1. 目标

本规范用于替代 `QCPimpl.h` 的 helper-macro 约定，统一回到 Qt6 / KDE 公共库可直接审查的原生写法。

目标只有三个：

- 保持 public header 不泄漏 libcurl / private 实现细节
- 让值类型与运行时类分别使用清晰、稳定、可 grep 的命名模式
- 把 ABI 风险点收敛到少数明确规则，而不是依赖仓库自定义宏

## 2. 总规则

QCurl 统一使用两套模式，不能混用：

- 值类型：`ClassData` 模式
- QObject / manager / reply / logger / runtime service：`ClassPrivate` 模式

本文中的 `ClassData` / `ClassPrivate` 是占位写法。落到实际代码时，必须展开为 `FooData` / `FooPrivate`，不保留嵌套裸 `Data` / `Private`。

## 3. 值类型：`ClassData`

### 3.1 适用对象

以下类型应优先使用 `ClassData` + `QSharedDataPointer`：

- 配置对象
- 轻量值语义类型
- 需要 copy-on-write 的 public ABI 友好类型

典型场景：

- TLS / proxy / timeout / retry 等配置类
- 仅承载请求配置、协议参数、序列化状态的值对象

### 3.2 推荐写法

头文件：

```cpp
class FooData;

class QCURL_EXPORT Foo
{
public:
    Foo();
    Foo(const Foo &other);
    Foo(Foo &&other) noexcept;
    ~Foo();
    Foo &operator=(const Foo &other);
    Foo &operator=(Foo &&other) noexcept;

private:
    QSharedDataPointer<FooData> d;
};
```

源文件：

```cpp
class FooData : public QSharedData
{
public:
    QString name;
    int timeoutMs = 0;
};
```

### 3.3 强制规则

- 命名使用 `FooData`，不使用嵌套 `class Data`
- `QSharedDataPointer<FooData>` 只能承载值语义数据，不承载运行时句柄
- 析构、拷贝、移动、赋值在持有不完整类型时必须 out-of-line
- setter 参数遵循 Qt / KDE API 参数规范
- 非 `const` 写路径依赖 `QSharedDataPointer` 自带的 detach 语义，不手写无意义的 `d.detach()`

### 3.4 libcurl binding 边界

值类型不得在 public header 中暴露以下内容：

- `CURL *`
- `curl_*`
- `<curl/...>`
- `_p.h`

libcurl easy / multi / share handle 属于运行时实现细节，应放在 `.cpp`、private header 或 `FooPrivate` 中，而不是 `FooData` 中。

## 4. 运行时类：`ClassPrivate`

### 4.1 适用对象

以下类型应使用 `FooPrivate` + d-pointer：

- `QObject` 派生类
- manager / reply / logger / middleware / runtime service
- 持有线程、事件循环、socket、timer、curl handle 等运行时状态的类

### 4.2 推荐写法

头文件：

```cpp
class FooPrivate;

class QCURL_EXPORT Foo : public QObject
{
    Q_OBJECT

public:
    explicit Foo(QObject *parent = nullptr);
    ~Foo() override;

private:
    Q_DECLARE_PRIVATE(Foo)
    QScopedPointer<FooPrivate> d_ptr;
};
```

### 4.3 强制规则

- 命名使用 `FooPrivate`，不使用嵌套 `class Private`
- d-pointer 持有不完整类型时，析构必须 out-of-line
- public header 只保留表达 contract 所需的最小 include
- `FooPrivate` 可持有 libcurl handle、Qt runtime 对象、锁、队列、缓存和其他实现细节

## 5. 禁止写法

以下写法不再作为推荐实现：

```cpp
class Foo
{
private:
    class Data;
    QSharedDataPointer<Data> d;
};
```

```cpp
class Foo
{
private:
    class Private;
    QScopedPointer<Private> d_ptr;
};
```

```cpp
QCURL_DECLARE_DPTR(Foo)
QCURL_DECLARE_SHARED_DATA(Foo)
```

原因如下：

- 嵌套裸 `Data` / `Private` 在公共库代码里不够直观，grep 与批量审查成本更高
- helper macro 会把“规则”重新包装成仓库私有框架层
- 对 install surface 的长期维护，显式写法比宏壳更容易做 guardrail 和 code review

## 6. 审查清单

提交涉及 public / library-facing 头文件时，至少检查以下事项：

- 值类型是否使用 `FooData` 而不是裸 `Data`
- 运行时类是否使用 `FooPrivate` 而不是裸 `Private`
- 是否仍引入 `QCPimpl.h` 或 `QCURL_DECLARE_*`
- public header 是否泄漏 libcurl / `_p.h` / Qt private include
- 持有不完整类型时，special members 是否 out-of-line
- setter 参数是否符合 `Qt6_KDE_API_Parameter_Style.md`
- 值类型是否只承载配置数据，而不是运行时句柄

## 7. 渐进迁移策略

本规范采用“禁止回流 + 触点迁移”：

- 新增代码必须直接使用 `FooData` / `FooPrivate`
- 已存在的嵌套 `Data` / `Private` 作为迁移债务逐步消除
- 不为兼容旧风格新增 wrapper、helper macro 或过渡壳层

### 7.1 当前迁移状态（2026-04-16）

截至 2026-04-16，Core install surface 中的 public / library-facing 类型已完成显式
`FooData` / `FooPrivate` 命名迁移；当前没有新的裸 `Data` / `Private` 迁移债务。

本轮已完成的触点迁移：

- `QCNetworkSslConfig`：改为 `QCNetworkSslConfigData + QSharedDataPointer`
- `QCNetworkTimeoutConfig`：改为 `QCNetworkTimeoutConfigData + QSharedDataPointer`
- `QCNetworkRetryPolicy`：改为 `QCNetworkRetryPolicyData + QSharedDataPointer`
- `QCNetworkProxyConfig`：改为 `QCNetworkProxyConfigData + QSharedDataPointer`
- `QCNetworkProxyConfig::ProxyTlsConfig`：改为 `QCNetworkProxyTlsConfigData + QSharedDataPointer`
- `QCNetworkHttpAuthConfig`：改为 `QCNetworkHttpAuthConfigData + QSharedDataPointer`
- `QCNetworkRequestScheduler::Config`：改为 `QCNetworkRequestSchedulerConfigData + QSharedDataPointer`
- `QCNetworkRequestScheduler::Statistics`：改为 `QCNetworkRequestSchedulerStatisticsData + QSharedDataPointer`
- `QCNetworkRequestScheduler::LaneConfig`：改为 `QCNetworkRequestSchedulerLaneConfigData + QSharedDataPointer`
- `QCNetworkLogger`：改为 `NetworkLogEntryData + QSharedDataPointer` 的 accessor-only Core contract
- `QCNetworkDefaultLogger`：改为 `QCNetworkDefaultLoggerPrivate + QScopedPointer`
- `QCNetworkCancelToken`：改为 `QCNetworkCancelTokenPrivate + QScopedPointer`

Extras / internal 范围内，本轮 public/library-facing 审计未新增新的裸 `Private` 例外。

### 7.2 Consumer 迁移清单（accessor-only contract）

这一轮不只是内部命名收口，也同步把一批默认安装面的值类型收敛成 accessor-only
contract。下游如果还在用 public field / aggregate 风格，需要一并迁移。

受影响的类型：

- `QCNetworkSslConfig`
- `QCNetworkTimeoutConfig`
- `QCNetworkRetryPolicy`
- `QCNetworkProxyConfig`
- `QCNetworkProxyConfig::ProxyTlsConfig`
- `QCNetworkHttpAuthConfig`
- `QCNetworkRequestScheduler::Config`
- `QCNetworkRequestScheduler::Statistics`
- `QCNetworkRequestScheduler::LaneConfig`
- `NetworkLogEntry`

迁移示例：

```cpp
// before
QCNetworkRetryPolicy policy;
policy.maxRetries = 3;
policy.initialDelay = std::chrono::milliseconds(250);

QCNetworkSslConfig sslConfig;
sslConfig.verifyPeer = true;
sslConfig.verifyHost = true;

QCNetworkProxyConfig proxy;
proxy.type = QCNetworkProxyConfig::ProxyType::Https;
proxy.hostName = QStringLiteral("proxy.example.com");
proxy.port = 443;

QCNetworkProxyConfig::ProxyTlsConfig tls;
tls.verifyPeer = true;
proxy.setTlsConfig(std::nullopt);

QCNetworkRequestScheduler::LaneConfig lane;
lane.weight = 3;

// after
QCNetworkRetryPolicy policy;
policy.setMaxRetries(3);
policy.setInitialDelay(std::chrono::milliseconds(250));

QCNetworkSslConfig sslConfig;
sslConfig.setVerifyPeer(true);
sslConfig.setVerifyHost(true);

QCNetworkProxyConfig proxy;
proxy.setType(QCNetworkProxyConfig::ProxyType::Https);
proxy.setHostName(QStringLiteral("proxy.example.com"));
proxy.setPort(443);

QCNetworkProxyConfig::ProxyTlsConfig tls;
tls.setVerifyPeer(true);
proxy.clearTlsConfig();

QCNetworkRequestScheduler::LaneConfig lane;
lane.setWeight(3);
```

额外约束：

- 读取配置/统计值时，统一改为 `foo()` getter，不再直接读字段
- `QCNetworkProxyConfig::setTlsConfig()` 只接受 `ProxyTlsConfig`
- 需要清空 proxy TLS 配置时，使用 `clearTlsConfig()`；不要再传 `std::nullopt`
- `NetworkLogEntry` 只通过 `level()` / `category()` / `message()` / `timestampUtc()` 访问
  字段

## 8. 与 public-api guardrail 的关系

自动 blocker 只负责低误报规则：

- install headers 不得 include `QCPimpl.h`
- install headers 不得出现 `QCURL_DECLARE_DPTR(`
- install headers 不得出现 `QCURL_DECLARE_SHARED_DATA(`

以下内容保留在文档与 code review 层，不直接做脆弱 regex blocker：

- 是否使用 `FooData` / `FooPrivate` 命名
- 是否存在不值得自动化的局部例外
- 更细的风格一致性判断

## 9. 当前方案包口径

`qcpimpl-removal-guidelines-and-guardrails` 的当前统一口径如下：

- 值类型：`ClassData` 占位，实际代码展开为 `FooData`
- 运行时类：`ClassPrivate` 占位，实际代码展开为 `FooPrivate`
- 删除 `QCPimpl.h` 后，不再引入新的 helper macro / wrapper 层
