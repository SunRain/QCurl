# Qt6_CPP17_Coding_Style.md

## 指导原则

---
你=资深 Qt/KDE与现代 C++17 开发者，以下条款为强制最高优先级；任何冲突以序号小者为准。
所有代码须在现代 C++17 下编译（GCC≥11、Clang≥14、MSVC≥2019），同时通过 clang-format（标准配置：`Qt6_CPP17_CLANG-FORMAT`）与 clang-tidy（可选；示例见第 10 章），并保持项目构建/测试零警告（如适用）。详细的代码规范可以参考：
- https://wiki.qt.io/Qt_Coding_Style
- https://wiki.qt.io/Coding_Conventions
- https://community.kde.org/Policies/Frameworks_Coding_Style

---

## 0 总览
- 编译器：GCC ≥ 11 | Clang ≥ 14 | MSVC ≥ 2019
- 标准：C++17 (`set(CMAKE_CXX_STANDARD 17)`)
- 警告：`-Wall -Wextra -Wpedantic` 全开，**零警告提交**
- 格式化：项目根放置 `Qt6_CPP17_CLANG-FORMAT`（必要时可复制/链接为 `.clang-format` 供工具自动发现），提交前 `git clang-format --style=file:Qt6_CPP17_CLANG-FORMAT`
- 禁止：异常、RTTI、dynamic_cast、裸 new（`QObject` 派生为明确例外，见第 6 章）、单语句无 braces、64-bit enum
- Use templates wisely, not just because you can（明智地使用模板，不仅仅是因为你可以）
- Avoid C casts, prefer C++ casts (static_cast, const_cast, reinterpret_cast)
- Don't use dynamic_cast, use qobject_cast for QObjects or refactor your design, for example by introducing a type() method (see QListWidgetItem)
- Use the constructor to cast simple types: int(myFloat) instead of (int)myFloat

---

## 1 文件与编码
| 规则 | 正例 | 反例 |
|---|---|---|
| UTF-8 无 BOM | 保存为 UTF-8 | UTF-8-BOM |
| include 顺序 | clang-format 分组：① `"..."` ② `<Q...>/<Qt...>` ③ 其他 `<...>` | 顺序颠倒 |
| include 语法 | `#include <QString>` | `#include <QtCore/QString>` |
| guard 写法 | `#ifndef MYWIDGET_H ...` | `#pragma once`（仅工具可用） |

### 1.1 Include Guards
- If you would include it with a leading directory, use that as part of the include
- Put them below any license text

Example for kaboutdata.h:
```cpp
#ifndef KABOUTDATA_H
#define KABOUTDATA_H
```
Example for kio/job.h:
```cpp
#ifndef KIO_JOB_H
#define KIO_JOB_H
```
---

## 2 命名
| 类型 | 风格 | 正例 | 反例 |
|---|---|---|---|
| 类 | 大驼峰 | `class MainWindow` | `class main_window` |
| 函数/变量 | 小驼峰 | `void updateData()` | `void updatedata()` |
| 成员变量 | `m_` 前缀 | `int m_count` | `int count_` |
| 静态/全局 | `s_` 前缀 | `static QObject *s_instance` | `static QObject *instance` |
| 常量 | `k` 前缀 | `constexpr int kMaxDepth = 3` | `const int MAX_DEPTH = 3` |
| 枚举值 | 驼峰 + 尾逗号 | `enum class Direction { North, South, };` | `enum Direction { NORTH };` |
| 命名空间 | 全小写 | `namespace app::utils` | `namespace AppUtils` |

- Avoid short or meaningless names (e.g. "a", "rbarr", "nughdeget")
- Single character variable names are only okay for counters and temporaries, where the purpose of the variable is obvious
- Wait when declaring a variable until it is needed
- Variables and functions start with a lower-case letter. Each consecutive word in a variable's name starts with an upper-case letter

---

## 3 缩进与括号（KDE 风格）
| 规则 | 正例 | 反例 |
|---|---|---|
| 缩进 | 4 空格 | Tab |
| 单语句 if/for/while | 必须加 braces | `if (x) {\n    return;\n}` |
| 左 brace | 控制语句附着式；函数/类/struct 换行 | `if (x) {` ... |
| else 位置 | `} else {` | `}\nelse` |
| case 缩进 | case label 缩进 1 级 | `    case 0:\n        break;` |

- 对于指针或引用，类型和'*'或'&'之间始终使用单个空格，但'*'或'&'和变量名之间不加空格：
```cpp
char *x;
const QString &myString;
const char * const y = "hello";
```
- Surround binary operators with spaces
- No space after a cast (and avoid C-style casts)
```cpp
// Wrong
char* blockOfMemory = (char* ) malloc(data.size());

// Correct
char *blockOfMemory = reinterpret_cast<char *>(malloc(data.size()));
```
---

## 4 行长与换行
- 软限制 100 列；二元运算符放新行首（由 clang-format 决定）；构造函数初始化列表使用“行首逗号”（KDE/clang-format）
```cpp
// 正
if (longCondition1
    && longCondition2) {
}

// 误
if (longCondition1 &&
    longCondition2) {
}
```

构造函数初始化列表示例（行首逗号）：
```cpp
Foo::Foo(int a, int b)
    : m_a(a)
    , m_b(b)
{
}
```

---

## 5 可选的现代 C++17 最佳实践（已在 Qt6/KF6 使用）

> **说明**：以下特性为**可选推荐**，而非强制要求。
> - ✅ **鼓励使用**：在新代码中优先采用这些现代化写法
> - 🔄 **渐进迁移**：现有代码可保持不变，不强制重构
> - 🤔 **权衡选择**：根据团队熟悉度、性能需求、可读性综合判断

| 场景 | 推荐 | 传统写法（仍可接受） |
|---|---|---|
| 可选返回值 | `std::optional<QColor> tryColor()` | `bool getColor(QColor *out)` |
| variant 访问 | `std::visit([](auto& v){ ... }, var)` | 手写 switch(type) |
| 结构化绑定 | `auto [it, inserted] = map.insert({k, v});` | `QPair<It,bool> res = ...` |
| 编译期常量 | `constexpr int kSize = 256;` | `const int kSize = 256;` 或 `#define` |
| nodiscard | `[[nodiscard]] int calc() const;` | 无属性（编译器不强制检查） |
| maybe_unused | `[[maybe_unused]] auto idx = ...;` | `Q_UNUSED(idx);` |
| 原子操作 | `std::atomic<int> value; value.fetch_add(1)` | `QAtomicInt` 或互斥锁 |
| 二进制缓冲 | `QByteArrayView buf` | `(const char*, size_t)` |
| 路径计算 | `std::filesystem::path p = dir / "file.txt";` | `QDir::cleanPath(dir + "/file.txt")` |
| 计时 | `auto t0 = std::chrono::steady_clock::now();` | `QElapsedTimer` |
| 折叠表达式 | `(stream << ... << args);` | 手写循环拼接 |

**使用建议**：
- 新功能/新文件：优先使用现代写法
- 维护旧代码：保持风格一致，避免混用
- 团队协作：根据团队共识选择，统一标准
- 性能敏感：实测验证，`std::optional` 等零成本抽象通常无性能损失
- `[[nodiscard]]`：对“可能失败”的 API 属于强制项（见 §5.1）；其他场景可按团队选择

### C++20+ 可选（仅当启用 C++20 及以上时）

| 场景 | 推荐 | 说明 |
|---|---|---|
| 二进制缓冲 | `std::span<const std::byte> buf` | 需要 C++20 |
| 原子视图 | `std::atomic_ref<int>(val).fetch_add(1)` | 需要 C++20 |

## 5.1 错误处理与断言（无异常项目）

> 本规范禁止异常（`throw`/`try`/`catch`），因此必须有统一、可审计的“失败表达”策略，避免项目内混乱与吞错。

### 5.1.1 总则（强制）

- **禁止**：项目代码中使用 `throw`/`try`/`catch`；禁止以异常作为控制流。
- **必须**：任何“可能失败”的函数都要**显式表达失败**；禁止通过隐式默认值吞掉错误。
- **必须**：失败返回值必须带 `[[nodiscard]]`，防止调用方忽略失败（例如 `[[nodiscard]] bool ...`、`[[nodiscard]] std::optional<T> ...`）。
- **必须**：断言只用于“编程错误”（前置条件/不变量被破坏，理论上不应发生）；对用户输入、I/O、网络、插件加载等可恢复错误，必须走返回值错误路径。
- **应该**：错误信息的归属明确：底层库函数**不应**无条件 `qWarning()` 噪声刷屏；在“边界层”（UI/命令/服务入口）统一记录日志或转为用户可见信息。

### 5.1.2 失败表达选型（推荐路径）

> 目标：同一模块内尽量统一，评审时能快速判断“失败如何被处理/传播”。

**A. 不需要错误原因：`std::optional<T>`**
- **推荐**：`std::optional<T>` 表达“有/无结果”；无结果返回 `std::nullopt`。
- **必须**：在接口注释中写清 `nullopt` 的语义（未找到/不适用/解析失败等）。

**B. 需要错误原因（文本）：`bool + QString *error`**
- **推荐**：返回 `bool`，额外用 `QString *error`（允许为 `nullptr`）输出失败原因。
- **必须**：失败时若 `error != nullptr`，写入可诊断的原因；成功时不写或清空（团队统一即可）。

**C. 需要错误原因（结构化）：`bool + ErrorCode`**
- **推荐**：对可枚举的失败原因定义 `enum class ErrorCode`，并以 `ErrorCode *out` 返回（或作为成员 `lastError()`），避免大量字符串拼接。

### 5.1.3 第三方异常边界（强制）

- **必须**：若依赖的第三方库可能抛异常，必须在模块边界捕获并转换为本节的失败表达；异常不得跨越 Qt 事件循环/信号槽边界传播。

### 5.1.4 示例（Qt6 + C++17）

**❶ optional：解析失败返回空**
```cpp
#include <optional>
#include <QStringView>

[[nodiscard]] std::optional<int> parsePort(QStringView text)
{
    bool ok = false;
    const int port = text.toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        return std::nullopt;
    }
    return port;
}
```

**❷ bool + error：携带可诊断信息**
```cpp
#include <QFileInfo>
#include <QString>

[[nodiscard]] bool loadConfig(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path)) {
        if (error) {
            *error = QStringLiteral("Config file not found: %1").arg(path);
        }
        return false;
    }
    return true;
}
```

---

## 6 Qt 6 专属约定
| 规则 | 正例 | 反例 |
|---|---|---|
| Q_OBJECT | 每个 QObject 派生必须带 | 忘记导致 qobject_cast 失败 |
| 信号槽连接 | `connect(sender, &Sender::valueChanged, receiver, &Receiver::update);` | `SIGNAL/SLOT` 字符串 |
| 字符串字面量 | `QStringLiteral("hello")` 或 `u"hello"_qs` | `QString("hello")` |
| 线程耗时 | `QtConcurrent::run(&Worker::doWork)` | 手动 `new Thread` |
| QObject 派生语义 | 指针语义：仅用指针/引用传递与存储 | `QVector<Foo *> foos;` | `QVector<Foo> foos;` |
| 内存管理 | 父子树（优先）/ `deleteLater()` / RAII | 裸 `new` + 手动 `delete` |

- 对于智能指针，优先使用Qt自带的智能指针（`QScopedPointer`、`QSharedPointer`、`QWeakPointer`、`QPointer`）
- **QObject 生命周期（例外规则）**：
  - 默认：仍然**禁止裸 `new`**。
  - 例外：`QObject` 派生对象允许裸 `new`，但释放必须满足其一：
    - ✅ **父子对象树（优先）**：`new T(parent)`，由父对象析构自动释放
    - ✅ **事件循环安全析构**：无 parent 且涉及事件循环/异步/queued connection/跨线程场景时，用 `obj->deleteLater()`
  - ❌ 禁止：对 `QObject` 派生对象手动 `delete`
  - 非 `QObject`：使用 RAII 与智能指针（如 `std::unique_ptr`/`QScopedPointer`）管理资源
  - 谨慎：对 `QObject` 使用 `std::unique_ptr`/`std::shared_ptr`/`QScopedPointer` 容易与 `deleteLater()` 或线程/事件循环时序冲突；需要防悬挂引用时优先 `QPointer`

### 6.1 值语义/RAII 与 QObject（团队落地版）

#### 结论（先看这个）
- `QObject` 及其派生类**不应、也无法**“强制遵守值语义（可拷贝/可移动）”。它们是典型的 **identity type（身份对象）**：语义绑定到对象身份（地址）、对象树、元对象系统、信号槽连接与线程亲和性。
- RAII 原则对 `QObject` **依然适用**，但落地方式应是：**把非 Qt 资源用 RAII 封装在成员里**；`QObject` 自身释放遵循 **parent ownership / `deleteLater()`**。把整个 `QObject` 当成可拷贝/可移动的“值对象”是误区。
- 团队执行建议：把“值语义/RAII”规则拆成两套并明确适用范围：① 一般 C++ 类型（非 `QObject`）——优先值语义 + RAII；② `QObject` 派生类——指针语义 + Qt 生命周期模型。

#### 为什么 `QObject` 不适用值语义（按维度解释）
- **可拷贝/可移动语义**：`QObject` 禁用拷贝；“移动一个 QObject”在语义上会改变地址与身份，并破坏内部状态（连接、事件、属性等），Qt 也不提供可用的 move 语义。
- **对象身份（identity）**：`QObject` 常表示“某个具体对象”（`objectName`、dynamic properties、`eventFilter`、`findChild` 等）；等价关系不是“值相等”。
- **父子对象树（ownership tree）**：parent 负责析构子对象；拷贝/移动无法定义“子树如何复制/迁移”，且容易引入双重释放/悬挂引用；并且 parent/child 必须在同一线程。
- **元对象系统（meta-object）**：`Q_OBJECT`、反射、属性系统与动态属性都与对象实例绑定；拷贝/移动并不会“复制运行期元信息/注册状态”。
- **信号槽连接**：连接以 sender/receiver 的对象身份（地址）为核心；拷贝/移动无法正确“复制连接关系”，移动还会导致既有连接语义不成立。
- **线程亲和性**：`QObject` 有 thread affinity；对象应在所属线程中处理事件/调用多数成员；跨线程销毁必须走事件循环与队列连接。
- **事件循环/延迟删除**：`deleteLater()` 通过事件循环安全析构，避免待处理事件/queued slot 造成 UAF；这与“作用域结束立即析构”的传统 RAII 时序不同。

#### 规则条款（可直接执行的规范文本）

**A. 对一般 C++ 类型（非 `QObject`）适用的值语义/RAII**
- **必须**：资源由对象生命周期管理（RAII）；避免裸 `new`/`delete`（见本规范第 0/6 章）。
- **应该**：优先值语义（可拷贝或可移动）与按值容器；独占资源类型应“可移动但不可拷贝”；共享资源才考虑 `std::shared_ptr`/`QSharedPointer`。
- **禁止**：用裸指针表达所有权；API 通过“返回裸指针 + 口头约定”隐式转移所有权。

**B. `QObject` 派生类（特例：指针语义 + Qt 生命周期模型）**
- **必须**：`QObject` 派生类**禁止拷贝/移动**，并在类内显式声明以获得清晰的编译期诊断：推荐 `Q_DISABLE_COPY(Class)`，并显式 `Class(Class &&) = delete; Class &operator=(Class &&) = delete;`。
- **禁止**：按值传参/按值返回 `QObject` 派生类型；禁止将 `QObject` 派生类型按值存入 `std::vector`/`QVector`/`QList` 等需要移动/拷贝的容器。
- **必须**：需要 `moveToThread()` 的 `QObject` **禁止设置 parent**；禁止构造跨线程 parent/child 关系（parent/child 必须在同一线程）。
- **必须**：所有权二选一且可追溯：
  - **parent ownership**：`new T(parent)`，由 parent 析构释放；
  - **显式所有权**：明确唯一 owner（通常是某个 `QObject`/manager），释放用 `deleteLater()` 或在所属线程安全删除（见下方跨线程示例）。
- **禁止**：对 `QObject` 派生对象手动 `delete`（已在第 6 章声明）。
- **应该**：以下场景优先使用 `deleteLater()`：对象涉及事件循环/异步回调/queued connection/计时器/网络等；销毁请求来自非所属线程；或正在其 slot/event 处理过程中需要自杀式销毁。并确保对象所属线程有可处理 deferred delete 的事件循环。
- **应该**：当对象可能被异步销毁（parent 析构、`deleteLater`、跨线程）时，持有方用 `QPointer<T>` 等弱引用方式避免悬挂指针；跨异步边界避免缓存裸指针。
- **应该**：当子 `QObject` 生命周期与 owner 完全一致时，优先使用“直接成员 + parent”（如 `QTimer m_timer{this};`）而不是堆分配；这不是值语义，仍禁止 copy/move/按值容器。
- **应该**：创建型 API 用 `QObject *parent` 参数表达所有权；返回值通常用裸指针表达“非 owning”（由 parent/owner 管理），并在函数注释中写清生命周期。
- **应该**：默认约定：裸指针/引用参数与返回值表达“借用（non-owning）”；一旦发生所有权转移，必须通过设置 parent、明确命名（如 `takeOwnership...`）、或在注释中显式说明。
- **可选（需理由）**：使用 `std::unique_ptr`/`QScopedPointer` 拥有 `QObject` 仅在**不使用 `deleteLater()`、不跨线程、析构线程与对象 thread affinity 一致**时允许；若必须延迟删除，必须使用自定义 deleter 调用 `deleteLater()` 并确保事件循环存在。
- **可选（强约束）**：共享所有权通常不适用于 `QObject`；如确实需要，**禁止设置 parent**，并使用带自定义 deleter（`deleteLater`）的 `QSharedPointer`/`std::shared_ptr`，同时对外优先暴露 `QPointer`/`QWeakPointer`。

#### 6.1.1 QIODevice 特别说明（Qt I/O 设备对象）

> 适用范围：任何继承自 `QIODevice` 的类型（包含你们自定义派生类）。`QIODevice` 本身就是 `QObject` 派生，因此继续适用上面的 `QObject` 特例；本小节仅补充“为什么更要谨慎”与“工程落地规则”。

- **语义定位**：`QIODevice` 不是“数据容器”，而是**带状态与回调的 I/O 端点**（打开/关闭、缓冲区、异步通知信号等），强依赖对象身份与事件循环；因此**禁止值语义（copy/move/按值容器）**。
- **生命周期策略（比一般 QObject 更偏向 `deleteLater()`）**：
  - **应该**：只要设备参与异步通知（例如 `readyRead`/`bytesWritten`/错误信号、socket notifier、queued connection、定时器驱动读写），销毁优先 `deleteLater()`，避免待处理事件/queued slot 触发 UAF。
  - **应该**：在调度销毁前先 `close()`（如适用）以尽快停止 I/O 与回调；并避免在析构顺序不清晰时把 device 指针泄露到长生命周期对象。
- **栈对象边界**：
  - **可选**：`QFile` 等在“纯同步、指针不外泄、无异步连接”的函数内可用栈对象；一旦连接信号或把 `QIODevice*` 交给异步逻辑/跨作用域保存，应改为 parent/显式 owner + `deleteLater()`。
- **owning 智能指针（强化版）**：
  - **禁止**：除满足“纯同步场景”外，禁止使用 owning 智能指针（`std::unique_ptr`/`QScopedPointer`/`std::shared_ptr`/`QSharedPointer`）以**默认 deleter**管理 `QIODevice` 及派生类。
  - **纯同步场景（必须全部满足）**：
    1. 单线程同步使用，不 `moveToThread()`
    2. 不依赖 `readyRead`/`bytesWritten` 等异步信号/回调，不与长生命周期对象建立连接
    3. 不将 `QIODevice *`/引用外泄（不保存、不跨异步边界、不交给异步 API）
    4. 不调用 `deleteLater()`，并且期望离开作用域立即析构
  - **可选（受控例外）**：如必须用智能指针样式表达所有权且对象参与事件循环，允许使用“自定义 deleter 调 `deleteLater()`”的智能指针（例如 `QSharedPointer<T>(new T, &QObject::deleteLater)`），并**禁止设置 parent**，且必须确保对象所属线程事件循环存在。
- **线程与亲和性**：
  - **必须**：设备对象只在其 thread affinity 所属线程使用；跨线程只用信号/queued connection 交互。
  - **必须**：需要 `moveToThread()` 的设备对象**不得设置 parent**；销毁必须 queued 到其线程执行 `deleteLater()`。
- **API 所有权表达（建议统一成团队习惯）**：
  - **应该**：把“使用某个设备”作为依赖注入：形参用 `QIODevice *device`/`QIODevice &device` 表达借用；调用方负责确保生命周期覆盖使用期。
  - **必须**：如函数/对象需要“接管设备生命周期”，必须通过 `QObject *parent`（或明确 owner）表达所有权归属；owning 智能指针策略见上文“owning 智能指针（强化版）”。
- **常用 `QIODevice` 体系类（非穷尽）**：
  - QtCore：`QBuffer`、`QFile`、`QSaveFile`、`QTemporaryFile`、`QProcess`
  - QtNetwork：`QAbstractSocket`（`QTcpSocket`、`QUdpSocket`）、`QSslSocket`、`QLocalSocket`、`QNetworkReply`
  - QtSerialPort：`QSerialPort`
  - 备注：`QTextStream`/`QDataStream` 等**不是** `QIODevice` 子类，而是面向 `QIODevice` 的读写封装。

#### 典型示例（Qt6 + C++17）

**❶ 错误示例：把 `QObject` 当值类型 / 把 `deleteLater()` 与 owning 智能指针混用**
```cpp
#include <QObject>
#include <QVector>
#include <QWidget>
#include <memory>

class Bad : public QObject
{
    Q_OBJECT
public:
    explicit Bad(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
};

class BadOwner : public QObject
{
    Q_OBJECT
public:
    explicit BadOwner(QObject *parent = nullptr)
        : QObject(parent)
        , m_widget(std::make_unique<QWidget>())
    {
    }

    void scheduleDelete()
    {
        m_widget->deleteLater(); // ❌ deleteLater 可能先于 unique_ptr 析构发生 → 悬挂/二次 delete
    }

private:
    std::unique_ptr<QWidget> m_widget; // ❌ owning 智能指针与 Qt 异步销毁语义冲突
};

void badExamples()
{
    QVector<Bad> list; // ❌ QObject 派生不可拷贝/不可移动：容器按值存放不成立
}
```

**❷ 推荐示例：parent 机制 + RAII 成员协同（资源放在成员，QObject 用 Qt 生命周期）**
```cpp
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>

class Controller : public QObject
{
    Q_OBJECT
public:
    explicit Controller(QObject *parent = nullptr)
        : QObject(parent)
        , m_timer(this) // ✅ 子 QObject：同寿命，直接成员 + parent
        , m_backend(std::make_unique<Backend>()) // ✅ 非 QObject 资源：RAII
    {
        m_timer.setInterval(1000);
        connect(&m_timer, &QTimer::timeout, this, &Controller::poll);
        m_timer.start();
    }

    Q_DISABLE_COPY(Controller)
    Controller(Controller &&) = delete;
    Controller &operator=(Controller &&) = delete;

private slots:
    void poll();

private:
    struct Backend {
        void poll();
    };

    QTimer m_timer;
    std::unique_ptr<Backend> m_backend;
    QPointer<QObject> m_maybeGone; // ✅ 弱引用：防悬挂（例如由外部 owner/parent 管理的对象）
};
```

**❸ 跨线程示例：线程亲和性与销毁策略（禁止跨线程 delete，使用 queued + deleteLater）**
```cpp
#include <QObject>
#include <QThread>

class Worker : public QObject
{
    Q_OBJECT
public slots:
    void doWork();
signals:
    void finished();
};

void startWorker(QObject *owner)
{
    auto *thread = new QThread(owner); // ✅ thread 由 owner 管理
    auto *worker = new Worker();       // ✅ 无 parent，后续在其线程 deleteLater
    worker->moveToThread(thread);

    QObject::connect(thread, &QThread::started, worker, &Worker::doWork);
    QObject::connect(worker, &Worker::finished, thread, &QThread::quit);
    QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}
```

### 6.2 信号槽连接与线程语义（强制 + 推荐）

> 目标：避免 lambda 悬挂引用、跨线程 UI 访问、queued 参数类型不匹配等高频崩溃问题；并让评审可快速审计“slot 在哪个线程执行、连接生命周期如何结束”。

#### 6.2.1 connect 语法与生命周期（强制）

- **必须**：仅使用新式 connect（函数指针/成员函数指针/带 context 的 functor）；禁止 `SIGNAL()/SLOT()` 字符串。
- **必须**：使用 lambda/functor 连接时必须提供 **context**（通常为接收者/owner），禁止使用“无 context”的 functor connect 重载。
- **必须**：lambda/functor 不得捕获可能先于连接断开而析构的裸指针；若捕获 `this`，context 必须是 `this`（或更长生命周期 owner），并优先捕获 `QPointer<T>` 做弱引用保护。
- **推荐**：重复连接点（可能被调用多次）使用 `Qt::UniqueConnection` 防止重复连接；需要手动断开时保存 `QMetaObject::Connection` 并在合适时机 `disconnect()`。

示例（推荐/禁止）：
```cpp
// ✅ 推荐：带 context；this 析构后自动断开
connect(sender, &Sender::valueChanged, this, [this](int v) { onValue(v); });

// ❌ 禁止：无 context；若捕获 this，极易 UAF
connect(sender, &Sender::valueChanged, [this](int v) { onValue(v); });
```

#### 6.2.2 线程语义（强制）

- **必须**：不得在非 GUI 线程访问 GUI 对象；GUI 更新必须回到 GUI 线程（queued connection 或 `QMetaObject::invokeMethod`）。
- **必须**：当 sender/receiver 明确跨线程（或 receiver 可能 `moveToThread()`）时，连接必须显式指定 `Qt::QueuedConnection`。
- **禁止**：跨线程使用 `Qt::DirectConnection`（除非 slot 完全线程安全且不触及 GUI/Qt 对象线程亲和性，且在评审中说明理由）。
- **禁止（默认）**：`Qt::BlockingQueuedConnection`，除非有明确同步需求并证明不会死锁（需要评审说明）。

#### 6.2.3 queued 参数类型（强制）

- **必须**：queued connection 传递的自定义类型必须可被 Qt 元类型系统识别：至少 `Q_DECLARE_METATYPE(T)`，并保证在使用前完成注册（推荐在 `main()` 或模块初始化中 `qRegisterMetaType<T>()`）。

示例（自定义类型作为 queued 参数）：
```cpp
struct Payload { int id = 0; };
Q_DECLARE_METATYPE(Payload)

// 在程序启动或模块初始化处（一次即可）
// qRegisterMetaType<Payload>("Payload");
```

### 6.3 元对象系统与属性（Q_OBJECT / Q_PROPERTY / 元类型）（强制 + 推荐）

#### 6.3.1 宏与类型选择（强制）

- **必须**：任何 `QObject` 派生类必须包含 `Q_OBJECT`。
- **推荐**：仅需要反射（枚举/属性）但不需要 QObject 生命周期/信号槽的值类型，使用 `Q_GADGET`；需要在命名空间暴露枚举使用 `Q_NAMESPACE`（配合 `Q_ENUM_NS`/`Q_FLAG_NS`）。

#### 6.3.2 Q_PROPERTY（强制）

- **必须**：可变属性必须提供 `NOTIFY`；setter 必须在值未变化时早返回，避免无意义信号与绑定抖动。
- **必须**：属性的线程亲和性要明确：若属性可能被跨线程更新，必须通过 queued 方式切回对象所属线程再修改并 emit。

示例（推荐写法）：
```cpp
class Person : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
public:
    QString name() const { return m_name; }
    void setName(const QString &name)
    {
        if (m_name == name) {
            return;
        }
        m_name = name;
        emit nameChanged();
    }
signals:
    void nameChanged();
private:
    QString m_name;
};
```

#### 6.3.3 Q_ENUM / Q_FLAG（推荐）

- **推荐**：枚举使用 `enum class`，并用 `Q_ENUM`/`Q_ENUM_NS` 暴露到元对象系统，便于 QML/调试/反射。
- **推荐**：位标志使用 `QFlags` + `Q_FLAG`/`Q_FLAG_NS`，并通过 `Q_DECLARE_FLAGS`/`Q_DECLARE_OPERATORS_FOR_FLAGS` 统一运算符。

#### 6.3.4 元类型注册（强制）

- **必须**：任何用于 queued connection 参数、`QVariant` 存储、QML 边界（属性/信号参数/方法参数）的自定义类型必须注册为元类型（见 6.2.3）；跨模块时需确保注册发生在使用方可达路径上，避免“只在某个 .cpp 静态初始化注册导致偶发缺失”。

---

## 7 内存与单例
```cpp
// 正：函数静态
Thing& thing() {
    static Thing inst;
    return inst;
}

// 正：Q_GLOBAL_STATIC
Q_GLOBAL_STATIC(Thing, s_thing)

// 误：全局裸指针
static Thing* g_thing = new Thing;
```

---

## 8 lambda 与 auto
```cpp
// 正：多行格式
auto l = []() -> bool {
    doSomething();
    return true;
};

// 误：单行混多行
auto l = []() { doSomething();
    return true; };
```

---

## 9 项目模板结构
```
MyApp/
├── CMakeLists.txt
├── Qt6_CPP17_CLANG-FORMAT
├── .clang-tidy
├── src/
│   ├── main.cpp
│   ├── MainWindow.h
│   └── MainWindow.cpp
├── qml/              (可选)
├── resources/
│   └── resources.qrc
├── translations/
└── tests/
```

---

## 10 配置文件（直接复制到项目根）

### Qt6_CPP17_CLANG-FORMAT（通用 Qt/C++ 基线）

本仓库已提供完整配置，SSOT 为仓库根目录 `Qt6_CPP17_CLANG-FORMAT`（请以该文件为准，不再在本文档重复粘贴完整 YAML，避免漂移；如需 clang-format 默认自动发现，可复制/软链为 `.clang-format`）。

关键约定（摘要）：
- 4 空格缩进（`IndentWidth: 4`，`TabWidth: 4`，`UseTab: Never`）
- 指针风格：`Type *var`（`PointerAlignment: Right`）
- 行宽：`ColumnLimit: 100`
- include 分组：强制归组（`IncludeBlocks: Regroup`），并将 `<Q...>` 与 `<Qt.../...>` 统一归类为 Qt；块内按大小写敏感排序（`SortIncludes: CaseSensitive`）
- braces：控制语句附着式；函数/类/struct 换行（`BraceWrapping`）；不允许单行 blocks（`AllowShortBlocksOnASingleLine: Never`）
- switch/case：case labels 缩进（`IndentCaseLabels: true`，`IndentCaseBlocks: false`）
- 换行：二元运算符放新行首（`BreakBeforeBinaryOperators: All`）；构造函数初始化列表行首逗号（`BreakConstructorInitializers: BeforeComma`）
- 注释：不自动折行（`ReflowComments: false`），行尾注释对齐（`AlignTrailingComments`）

关键配置对照（换行/缩进/初始化列表/括号/空格/注释/排序）（以 `Qt6_CPP17_CLANG-FORMAT` 为准）：

| 键（Qt6_CPP17_CLANG-FORMAT） | 含义 | 示例 |
|---|---|---|
| `BraceWrapping.AfterControlStatement: Never` | `if/for/while` 的 `{` 不换行，附着在同一行 | `if (ok) {` |
| `BraceWrapping.AfterFunction: true` | 函数定义的 `{` 换行 | `void f()`<br>`{` |
| `BraceWrapping.AfterClass: true` / `AfterStruct: true` | `class/struct` 的 `{` 换行 | `class Foo`<br>`{` |
| `BraceWrapping.BeforeElse: false` / `BeforeCatch: false` | `else/catch` 与右花括号同一行 | `} else {` |
| `SpaceBeforeParens: ControlStatements` | 控制语句括号前有空格；函数调用/声明名后无空格 | `if (ok)` / `foo()` |
| `SpacesInAngles: Never` | 模板尖括号内不加空格 | `std::vector<int>` |
| `SpaceBeforeRangeBasedForLoopColon: true` | 范围 `for` 的 `:` 两侧留空格 | `for (auto v : xs)` |
| `SpacesInLineCommentPrefix.Minimum: 1` | `//` 后至少 1 个空格 | `// comment` |
| `ReflowComments: false` | 不自动重排/折行注释 | `// long long long ...`（不被自动换行） |
| `AlignTrailingComments.Kind: Always` + `SpacesBeforeTrailingComments: 1` | 行尾注释对齐，且注释前至少 1 个空格 | `int a = 1;   // a`<br>`int bb = 2;  // b` |
| `IncludeBlocks: Regroup` | `#include` 按类别归组并插入空行 | `"Foo.h"`<br>`<Q...>`<br>`<...>` |
| `IncludeCategories` | include 分类优先级：① `"..."` ② `<Qt|Q...>` ③ 其他 `<...>` | `#include "Foo.h"`<br>`#include <QString>`<br>`#include <vector>` |
| `SortIncludes: CaseSensitive` | 分组内按（大小写敏感）顺序排序 | `#include <QByteArray>` 在 `#include <QWidget>` 之前 |
| `SortUsingDeclarations: LexicographicNumeric` | `using` 按“字典序+数字序”排序 | `using Foo2 = ...;` 在 `using Foo10 = ...;` 之前 |
| `IndentWidth: 4` + `TabWidth: 4` + `UseTab: Never` | 缩进使用 4 空格，不使用 Tab | `if (ok) {`<br>`    foo();`<br>`}` |
| `ContinuationIndentWidth: 4` | 换行续行缩进 4 空格 | `foo(arg1,`<br>`    arg2);` |
| `ColumnLimit: 100` | 行宽软限制 100 列（超过时更倾向断行） | `auto s = QStringLiteral("...");`（超长时将换行） |
| `BreakBeforeBinaryOperators: All` | 换行时二元运算符放行首 | `if (a`<br>`    && b) {` |
| `BreakBeforeTernaryOperators: true` | 换行时三元运算符 `?`/`:` 放行首 | `auto x = cond`<br>`    ? a`<br>`    : b;` |
| `AllowShortBlocksOnASingleLine: Never` | 不允许 `{ ... }` 单行 block | `if (ok) {`<br>`    foo();`<br>`}` |
| `AllowShortIfStatementsOnASingleLine: Never` | 不允许 `if (x) y;` 单行 if | `if (ok) {`<br>`    foo();`<br>`}` |
| `AllowShortLoopsOnASingleLine: false` | 不允许 `for (...) x;` 单行 loop | `for (...) {`<br>`    work();`<br>`}` |
| `BreakConstructorInitializers: BeforeComma` + `PackConstructorInitializers: BinPack` | 构造函数初始化列表使用“行首逗号”风格 | `Foo::Foo()`<br>`    : m_a(a)`<br>`    , m_b(b)` |
| `ConstructorInitializerIndentWidth: 4` | 初始化列表相对函数签名缩进 4 空格 | `Foo::Foo()`<br>`    : m_a(a)` |
| `IndentCaseLabels: true` + `IndentCaseBlocks: false` | `switch` 内 case label 缩进；case 语句块按层级缩进 | `switch (x) {`<br>`    case 0:`<br>`        break;`<br>`}` |
| `AlwaysBreakTemplateDeclarations: Yes` | `template <...>` 声明与后续定义分行 | `template <typename T>`<br>`class Foo` |
| `BreakInheritanceList: BeforeColon` | 继承列表换行时 `:` 放到下一行（KDE 风格） | `class Foo`<br>`    : public Bar` |

### .clang-tidy（最小零警告集）

以下为可复制到业务仓库根目录的最小示例（按需调整）：
```
Checks: >
  -*,performance-*,readability-*,-readability-magic-numbers,modernize-*,
  -modernize-use-trailing-return-type,bugprone-*,cppcoreguidelines-*,
  -cppcoreguidelines-pro-bounds-pointer-arithmetic
WarningsAsErrors: ''
HeaderFilterRegex: '.*'
```

---

## 11 提交前自检清单（Copy & Paste）

```
- [ ] UTF-8 无 BOM
- [ ] include 顺序 & guard 正确
- [ ] clang-format --dry-run 无差异
- [ ] clang-tidy 零警告
- [ ] 无异常/RTTI/dynamic_cast
- [ ] 成员变量 m_xxx，静态 s_xxx
- [ ] 单语句 if/for/while 加 braces
- [ ] 枚举尾逗号
- [ ] QStringLiteral / u""_qs
- [ ] `QObject`：禁止 copy/move/按值容器；用父子树/`deleteLater()`，禁止手动 `delete`；非 `QObject` 用 `std::unique_ptr`/RAII
- [ ] 线程耗时任务用 QtConcurrent
- [ ] 无异常：不新增 `throw`/`try`/`catch`；可能失败的 API 明确失败表达，并用 `[[nodiscard]]` 防忽略
- [ ] 信号槽：lambda/functor connect 必须带 context；跨线程显式 `Qt::QueuedConnection`；queued 参数类型已做元类型注册
- [ ] 元对象：`Q_PROPERTY` 可变属性必须有 `NOTIFY`，setter 仅在变更时 emit；自定义类型用于 QVariant/QML 时已注册
```

---

**文档包版本**：v1.0.6
**最后更新**：2026-01-17

---
