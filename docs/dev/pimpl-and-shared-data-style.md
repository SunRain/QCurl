# PIMPL 与 Shared-Data 规范

适用于 QCurl 的 public / library-facing 类型，统一值数据与运行时资源的布局、命名及不完整类型处理。2.0 ABI 非稳定；PIMPL 不代替下游重编译，也不是二进制兼容证明。

## 1. 选择类型模式

| 类型职责 | 布局 | 数据边界 |
| --- | --- | --- |
| 配置、协议参数、序列化状态等轻量值类型 | `FooData` + `QSharedDataPointer<FooData>` | 只承载值语义数据，可 copy-on-write，不持有运行时句柄 |
| QObject、manager、reply、logger、middleware、runtime service | `FooPrivate` + d-pointer | 持有线程、事件循环、socket、timer、libcurl handle、锁、队列、缓存等运行时状态 |

`Foo` 表示实际类名，不能保留嵌套裸 `Data` / `Private`。不引入 `QCPimpl.h`、`QCURL_DECLARE_DPTR`、`QCURL_DECLARE_SHARED_DATA` 等自定义 helper 宏或兼容 wrapper；显式类型名便于审查，不为无关私有实现机械增加 ABI 包装。

## 2. 值类型的正向模式

以下仅展示布局；构造、析构、拷贝、移动及赋值的定义放在能看到完整 `FooData` 的源文件中。

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

源文件中的值数据：

```cpp
class FooData : public QSharedData
{
public:
    QString name;
    int timeoutMs = 0;
};
```

非 `const` 写路径使用 `QSharedDataPointer` 自带的 detach 语义，不手写无意义的 `d.detach()`；setter 参数遵循[Qt/KDE 参数规范](../../Qt6_CPP17_Coding_Style/cn/Qt6_KDE_API_Parameter_Style.md)。

## 3. 运行时类的正向模式

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

运行时资源放入 `FooPrivate` 或其他私有实现，不放入 `FooData`。d-pointer 持有不完整类型时，析构必须在源文件 out-of-line 定义。

## 4. 公共头与升级边界

两种模式都只在公共头保留表达合同所需的最小 include；libcurl easy/multi/share handle 属于源文件或私有实现，不能泄漏到安装面。禁止泄漏项和完整安装头边界只在[公共头与安装边界](architecture/public-header-boundary.md)维护。

Retry/TLS/Proxy/Timeout accessor 在 v1.0.0 已存在，不把旧 public-field 教程重新列为 2.0 升级要求。真实调用方变化见[迁移指南](../user/migration-2.0.md)，本页只定义现行实现规范。

## 5. 验证与人工评审分工

public-api guardrail 对安装头自动阻止 `QCPimpl.h`、`QCURL_DECLARE_DPTR(` 和 `QCURL_DECLARE_SHARED_DATA(`；命名是否贴合类型职责、值数据与运行时资源是否分离、special members 的完整类型条件，以及 setter 参数和局部风格例外仍需人工评审。不能把这些语义要求写成已由 regex 全部保证。

检查命令见[构建与测试](build-and-test.md)，安装 consumer 与 public header 验证入口见[公共头边界](architecture/public-header-boundary.md)。
