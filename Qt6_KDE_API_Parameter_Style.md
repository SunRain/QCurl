# Qt6 / KDE 风格共享库 Public API 参数规范（中文）

English | 简体中文 | 原文

> 说明：本文档为当前 Qt6 / KDE API 参数规范的中文整理版（用于阅读与分发）。若与规范基线存在差异，以规范基线为准。

适用范围：对外导出的 Qt6/KF6 共享库（public headers / exported symbols），以及包含 QML 绑定（`Q_PROPERTY` / `Q_INVOKABLE` / signals/slots）的接口层。  
目标：对外接口清晰可维护；内部实现高性能且简洁；允许在演进中逐步引入 view（borrow）能力，同时避免公共头文件的重载陷阱。

版本/前置条件（建议在项目内明确基线）：
- 语言：C++17（本文提到的 `std::span` 仅适用于 C++20；C++17 项目应避免在 Public API 暴露 `std::span`，见 1.1）
- Qt/KF：Qt 6 / KF 6（不同 minor 版本对 view 类型支持不完全一致；在引入 `QAnyStringView/QUtf8StringView/...` 前先确认项目基线可用）

---

## 0. 术语与总原则

### 术语
- **Public API**：安装到下游、会被链接到的头文件与导出符号。
- **Internal API**：`.cpp` / private header / 未导出的实现层。
- **Borrow（只读/解析）**：只在本次调用内使用参数；不保存、不异步使用。
- **Owning（持有/存储）**：会把入参保存为成员/缓存，或跨调用/异步使用。
- **View（借用视图）**：`QStringView` / `QAnyStringView` / `QByteArrayView` 等，不拥有数据。
- **Qt Meta-Object 边界**：任何经 `moc` 暴露并可能走 `QMetaObject` 调用链的接口（`signals` / `slots` / `Q_INVOKABLE` / `Q_PROPERTY` notifier 等）。

### 总原则（必须）
- 先决定语义：**Borrow** 还是 **Owning**；不要让“参数类型”替你做语义选择。
- Public API 禁止跨调用持有 **Borrow 入参** 的生命周期（view、借用指针、借用引用；成员/缓存/队列/lambda 捕获后延迟使用等都算持有）。
- “要存/要异步/要跨线程”一律按 **Owning** 处理（立即 owning 化）。
- Qt Meta-Object 边界默认按 Owning 处理：避免在信号槽/`Q_INVOKABLE`/QML 调用链里传播非 owning 的 view。

> 注：并非“禁止所有指针/引用参数”。例如 `QObject*` 观察者指针、父子关系等属于另一类约定（所有权/线程亲和性/何时失效），需要单独写清；但同样不应把“借用语义”的入参跨调用保存。

---

## 1) View（借用视图）：只读 / 解析入口（推荐按值传递）

### 1.1 选型：用什么 view（Public C++ API）

| 场景 | 推荐参数类型 | 说明 |
|---|---|---|
| 文本入参（通用、来源多、编码不固定） | `QAnyStringView text` | 适合作为“公共入口参数”。可从多种字符串来源构造，必要时转换为 `QString`。 |
| 文本入参（明确 UTF-16 / 需要 `QChar` 语义） | `QStringView text` | 适合做“算法/解析”入口（例如大量 `QStringView` 操作链）。 |
| 文本入参（明确 UTF-8，且希望 API 层锁定编码） | `QUtf8StringView text` | 只在协议/格式明确 UTF-8 时使用。 |
| 文本入参（明确 Latin1） | `QLatin1StringView text` | 只在协议/历史原因明确 Latin1 时使用。 |
| 二进制/字节入参 | `QByteArrayView bytes` | 替代 `(const char *, size_t)`；仅 Borrow 场景适用。 |
| 非 Qt 容器切片 | （避免）如必须：项目统一的 span 类型（C++17）/ `std::span<const T> items`（C++20） | 仅用于非常稳定且简单的切片入参；避免在公共头文件里引入大规模模板体系。 |

> 注：在 Qt 6 中，`QLatin1String` 与 `QLatin1StringView` 是同一类型；旧名称保留主要出于兼容性考虑。新代码优先使用 `QLatin1StringView` 这一名称。

**规则（必须）**
- View 参数按值传递：`foo(QAnyStringView s)`，不要写 `const QAnyStringView&`。
- View 只用于 Borrow 语义；任何可能延长生命周期的场景都必须 owning 化。

---

### 1.2 `QAnyStringView` vs `QStringView`：如何选择（面向公共 API 的建议）

**推荐（Public API）**
- 边界入口（接收“用户输入/配置/协议字段/命令行/日志关键字”等）：优先 `QAnyStringView`。
  - 目的：让下游不必为了调用 API 先构造 `QString`。
  - 代价：如果内部需要大量 UTF-16 语义操作，通常会在实现里做一次 owning 化或转换。

**推荐（Internal/算法层）**
- 算法/解析函数（大量 substring/trim/scan/toInt 等，且你希望统一为 UTF-16 语义）：优先 `QStringView`。
  - 目的：统一语义与实现；避免在算法层同时处理多编码分支。

**规则（必须）**
- 不要为了“多支持几种输入”在同一语义上同时提供 `QStringView` 与 `QAnyStringView` 两个重载（见第 5 章重载控制）。

---

### 1.3 生命周期规则：什么能做，什么绝对不能做

#### 允许（Borrow 场景）
- 在函数体内做只读判断/解析/比较：`startsWith`、`contains`、`toInt`、`trimmed` 等。
- 在函数体内临时转换（仅当确实需要 owning 结果）：`text.toString()`、`text.toUtf8()` 等。

#### 禁止（必须）
- 禁止把 view 存入成员或缓存：
  - `m_nameView = name;`（任何 `QStringView/QAnyStringView/QByteArrayView` 都不允许）
- 禁止捕获 view 并延迟使用：
  - `QTimer::singleShot(..., [name]{ ... })`（name 是 view）
- 禁止把 view 通过 queued connection / 跨线程传递后再使用。
- 禁止返回指向临时对象的 view：
  - `return QStringView(QString("tmp"));`

#### 典型正确写法（Owning 化后再保存/异步）
```cpp
void Foo::setName(QAnyStringView name)
{
    d->name = name.toString();
}

void Foo::postLogMessage(QAnyStringView msg)
{
    const QString owned = msg.toString();
    QMetaObject::invokeMethod(this, [owned] { qInfo() << owned; }, Qt::QueuedConnection);
}
```

---

### 1.4 Qt/QML 边界：不要把 view 当成“可反射 API 类型”

**规则（必须）**
- 以下接口不得使用 view 类型作为参数/返回值：
  - `signals` / `slots`
  - `Q_INVOKABLE`
  - `Q_PROPERTY` 的 READ/WRITE/NOTIFY 相关函数/信号

**原因（面向工程现实）**
- QML/`QMetaObject` 调用链要求参数类型可稳定拷贝/转换；view 不拥有数据，复制 view 并不会复制底层字符/字节。
- 一旦进入 queued 调用链（跨线程/跨事件循环），view 指向的数据极容易在投递后失效，变成悬空引用。
- 即使某些 view 类型可以被注册为 metatype，也不等于它们在“可延迟调用”的语义上安全。

**推荐模式（两层 API）**
- 对 QML 暴露 owning 签名（通常是 `QString` / `QUrl` / `QVariant` 等）。
- 对 C++ 提供 view/borrow 的高性能入口（非 invokable/非 slot），由 owning 层转发。
  - 对 `QObject` 类型，建议 C++ 的 view 入口使用不同方法名，避免在现代 `connect(..., &Type::method)` 语法下因重载导致二义性（见下方示例）。

```cpp
class Foo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
public:
    QString name() const;

public slots:
    void setName(const QString &name);        // QML/Meta-Object 边界：owning

public:
    void setNameView(QAnyStringView name);    // 纯 C++ 入口：Borrow（view，仅 C++）

signals:
    void nameChanged(const QString &name);    // 信号也必须 owning
};
```

如果你坚持使用同名重载（例如 `void setName(const QString&)` + `void setName(QAnyStringView)`），在现代 connect 语法下需要显式消歧：

```cpp
connect(sender, &Sender::nameChanged,
        foo, qOverload<const QString &>(&Foo::setName));
```

---

### 1.5 性能与代码简洁：避免“view 入口 + 反复 owning 化”

**规则（推荐）**
- 若函数本质是解析/比较：尽量在 view 上完成逻辑，避免过早 `toString()`。
- 若函数最终必须产生 owning 结果：
  - 只做一次 owning 化；不要多次 `toString()`/`toUtf8()`。
  - owning 化后立即把后续逻辑建立在 owning 对象上（减少重复转换）。

**典型反例**
```cpp
// 反例：重复 owning 化
bool parse(QAnyStringView text)
{
    return text.toString().trimmed() == text.toString().toLower();
}
```

**典型正例**
```cpp
bool parse(QAnyStringView text)
{
    const QString owned = text.toString();
    const QString trimmed = owned.trimmed();
    return trimmed == owned.toLower();
}
```

---

### 1.6 与 `std::string_view` / `std::u16string_view` 的关系（公共 API 建议）

**规则（推荐）**
- 在 Qt/KDE 风格的公共 API 中，优先使用 Qt 的 view（`QAnyStringView/QStringView/QByteArrayView`）。
  - 原因：Qt view 在类型层面表达了更明确的字符语义（例如 UTF-16、Latin1、UTF-8 或二进制字节），且与 Qt 生态函数更自然对接。
- `std::*_view` 更适合作为 internal 或跨库“纯 STL 接口”场景。
  - 如果项目必须对外暴露 `std::string_view`：需要在文档中明确编码假设（UTF-8/Latin1/不保证）与生命周期契约，并提供等价的 Qt 入口或转换策略。

---

### 1.7 返回值规范（Public API）

**默认规则（推荐）**
- Public API 默认返回 owning 类型（如 `QString`/`QByteArray`）。
  - 对 `QString` 来说，返回值在很多情况下也是隐式共享的，代价通常可接受。

**仅在满足以下条件时才考虑返回 view（谨慎）**
- 你能为返回值提供清晰且可被下游安全遵循的生命周期契约（例如：返回的 view 指向对象内部稳定存储，且在对象存活且未被修改前有效）。
- 你愿意在文档中明确写出失效条件，并接受 review 中对其严格审查。

---

### 1.8 `QByteArrayView`：协议/序列化场景的落地模式（Public API）

**规则（推荐）**
- Borrow-only：按值传递 `foo(QByteArrayView bytes)`，在函数体内完成解析/比较/切片即可。
- 要存 / 要异步 / 要跨线程：尽早 owning 化，并只转换一次（例如 `const QByteArray owned = bytes.toByteArray();`）。
- 二进制数据不要使用“仅指针”构造：`QByteArrayView(ptr)` 会扫描到第一个 `Byte(0)` 来推导长度；二进制必须使用 `(ptr, len)` 或从容器构造。

**示例**
```cpp
bool isValidPacket(QByteArrayView packet)
{
    if (!packet.startsWith("PKT/"))
        return false;
    return packet.contains('\n');
}

void Foo::enqueuePacket(QByteArrayView packet)
{
    const QByteArray owned = packet.toByteArray();
    m_queue.enqueue(owned);
}
```

---

## 2) Owning：要存 / 要变成成员（以 `QString` owning 为主，可加 view 便捷入口）

### 推荐做法（Public C++ API）
- 对“要存为成员/缓存”的 Qt 隐式共享值类型（`QString/QByteArray/QImage/...`）：
  - 提供 owning 入口：`const QString&`（或对应类型）
  - 可选提供 view 入口：`QAnyStringView`（便捷、减少调用方临时构造）

校准：大多数 public setter 仅提供 `const QString&` 入口就已经合理。是否额外提供独立的 view 便捷入口，取决于该 API 是否确实需要统一接收 `QString` 之外的多种字符串来源，以及你是否愿意为此承担一个额外 public overload 的长期维护成本。

说明：`QString` 的 implicit sharing 使“从 `QString` 拷贝进成员”通常是浅拷贝共享；而 view 入口在存储时往往需要构造新的 `QString`，因此保留 `QString` 入口有现实收益（同时也更贴近 Qt/KDE 既有风格）。

### 规则（必须）
- Owning 场景：成员类型必须是 owning（如 `QString`），不得存 view。
- 若同时提供 owning + view 两个入口：
  - view 重载必须立即 owning 化，再落到同一个实现点（避免重复逻辑）。
- 需要改动/规范化内容时，改的是库内部持有的副本，不要试图通过非常量引用修改调用方对象。

> 提示：若该类是 `QObject` 且经常作为 `connect(..., &Type::setName)` 的目标，建议把 view 入口改用不同名字（例如 `setNameView(QAnyStringView)`），避免同名重载导致函数指针二义性；否则连接处需要 `qOverload`/`static_cast` 消歧（见 1.4、5.6）。

### 2.1 可选：Public C++ API 直接使用 owning sink（按值 + move）

在以下场景可以考虑把 `QString` 按值作为 canonical 入口，以减少重载并天然支持 move（尤其是调用方传临时值时）：
- 这是纯 C++ Public API（非 `slots`/`Q_INVOKABLE`/QML 边界）。
- 你希望用“单入口”降低公共头文件重载复杂度。

> 校准说明：这种写法可作为 pure C++ Public API 的可选方案，但不宜概括为 Qt6 / KDE Frameworks 6 public setter 的主流默认样式。对需要持有字符串的 public API，`const QString&` 仍是更稳妥、也更常见的默认起点。

```cpp
class Foo {
public:
    void setName(QString name);            // canonical sink（pure C++）
    void setNameView(QAnyStringView name); // 可选：便捷入口（view -> owning）
};

void Foo::setName(QString name)
{
    d->name = std::move(name);
}

void Foo::setNameView(QAnyStringView name)
{
    setName(name.toString());
}
```

### 示例（推荐模板）
```cpp
class Foo {
public:
    void setName(const QString &name);  // owning-friendly / shared-data friendly
    void setNameView(QAnyStringView name);  // convenience（view -> owning）
private:
    // d-pointer / private members...
};
```

```cpp
void Foo::setName(const QString &name)
{
    d->name = name; // 利用 implicit sharing（通常为浅拷贝共享）
}

void Foo::setNameView(QAnyStringView name)
{
    setName(name.toString()); // 需要存，就立刻 owning 化
}
```

---

## 3) Internal：实现层推荐用 sink（按值 + move）以保证简洁/性能

sink 不应被理解为“仅限 Internal”；在合适的 pure C++ Public API 中，它也可以成立。更稳妥的理解是：对外（public）默认仍以 `const QString&` 作为主流 owning 风格，必要时再配合独立的 view 便捷入口；对内（internal）则最适合统一到单一 sink 实现。

### 推荐模板
```cpp
static void setNameImpl(FooPrivate *d, QString name)
{
    d->name = std::move(name);
}

void Foo::setName(const QString &name)   { setNameImpl(d, name); }
void Foo::setNameView(QAnyStringView name)   { setNameImpl(d, name.toString()); }
```

优点：
- 逻辑单点：减少重复与分支。
- 调用方传临时 `QString` 时，内部能自然 move。
- 对外 API 不必为了 move 再扩一堆 `T&&` 重载。

---

## 4) 生命周期 / 异步 / 线程（强制落 Owning）

出现任一情况，一律按 Owning 处理（立即转换为 owning 类型）：
- 参数会被保存为成员/缓存
- 参数会跨事件循环使用（queued signal、`QMetaObject::invokeMethod(Qt::QueuedConnection)`、任务队列等）
- 参数会跨线程使用
- 参数会被 capture 到延迟执行的闭包中

---

## 5) 重载与二义性控制（公共头文件必须克制）

公共头文件里，重载越多：
- 下游越容易遇到“重载决议意外变化”（特别是在引入 view 之后）；
- 兼容性风险越高（新增一个更匹配的 overload 就可能改变既有调用点的选择结果）；
- QML/Meta-Object 边界更难保持行为一致；
- 长期维护成本显著上升。

因此：重载不是性能优化手段，而是 API 设计成本。

---

### 5.1 设计原则：把重载当作“能力集”，而不是“类型全集”

**规则（必须）**
- 每个重载必须带来清晰且独立的能力（语义差异、编码差异、生命周期差异），而不是“为了多接收几种类型”。

**规则（推荐）**
- 为每一组同名函数定义一个“规范入口”（canonical overload），并让其他入口转发到它。
- 同一语义尽量维持最小重载集合（见 5.2、5.3）。

---

### 5.2 文本参数：推荐的最小重载集合

#### Borrow-only（只读/解析）
- 推荐：只提供一个 view 入口（通常是 `QAnyStringView`；或你明确锁定为 `QStringView/QUtf8StringView/QLatin1StringView`）。
- 不推荐：同一语义同时提供多个 view 重载（会制造下游选择困惑与未来演进负担）。

```cpp
// 推荐：单入口
bool isValidId(QAnyStringView id);

// 不推荐：同一语义多 view 重载
bool isValidId(QStringView id);
bool isValidId(QAnyStringView id);
```

#### Owning（要存/要异步）
- 推荐：`const QString&` 作为 owning 入口；可选再加一个 `QAnyStringView` 便捷入口。
- 避免：再额外加 `const char*` / `QStringView` / `QLatin1StringView` / `QUtf8StringView` 的同语义重载。

---

### 5.3 编码与 `const char*`：公共 API 不要“猜编码”

**规则（推荐）**
- 公共 API 尽量不要提供 `const char*` 入口：编码语义不清晰，且很容易与 `QString`/view 产生重载竞争。
- 若你必须支持字节/字符指针输入，使用显式编码的 API（用名字和参数类型一起表达编码）：
  - `fromUtf8(QByteArrayView utf8)` / `setNameUtf8(QUtf8StringView utf8)`
  - `fromLatin1(QLatin1StringView latin1)`

```cpp
void setNameUtf8(QUtf8StringView name);
void setNameLatin1(QLatin1StringView name);
```

---

### 5.4 默认参数与重载：避免“仅靠默认参数区分语义”

**规则（推荐）**
- 不要同时依赖“默认参数 + 重载”来表达语义差异；这会显著增加调用点读代码的歧义，并放大未来演进风险。

建议二选一：
- 方案 A：用一个函数 + 明确默认参数（语义保持单一）。
- 方案 B：用不同函数名表达不同语义（特别是跨 QML 边界时）。

---

### 5.5 构造函数重载：优先工厂函数减少二义性

构造函数天然更容易触发隐式转换与二义性。

**规则（推荐）**
- 面向 public 的构造函数尽量少重载；复杂输入（尤其不同编码/不同来源）优先用命名工厂函数：
  - `Foo Foo::fromUtf8(QUtf8StringView)`
  - `Foo Foo::fromString(QString)`
- 构造函数如果接受单参数，通常应该 `explicit`。

---

### 5.6 QML/Meta-Object 可见方法：避免重载（强烈推荐）

**规则（推荐）**
- 对 QML 暴露（`Q_INVOKABLE` / `slots`）的方法：
  - 尽量不重载（同名多签名）
  - 采用不同方法名表达不同语义/编码：`setName()` / `setNameUtf8()`
- 如果确实需要 C++ 的重载便利：
  - 只把 owning 版本标记为 `Q_INVOKABLE`/slot；其他重载保持纯 C++ 方法（不进入 meta-object），避免 QML 侧看到多重载。
  - 注意：即便只有一个重载进入 meta-object，同名的纯 C++ 重载仍会让 `&Type::method` 在现代 `connect()` 语法下变成二义性；更推荐不同方法名（例如 `setNameView()`），或在连接处显式消歧：

```cpp
connect(sender, &Sender::nameChanged,
        foo, qOverload<const QString &>(&Foo::setName));
```

---

### 5.7 新增重载前的“下游调用面”检查（建议写成编译测试）

新增/调整重载（尤其是从 `const QString&` 迁移到加入 view）前，至少检查这些典型调用是否：
- 能编译（无二义性）
- 选择了你期望的 overload（行为不变或可接受）

建议覆盖调用样例：
- `QString s; foo(s);`
- `foo(QStringLiteral("abc"));`
- `foo(u"abc"_qs);`（若项目使用 `_qs`）
- `foo(QStringView{u"abc"});`
- `foo(QLatin1StringView{"abc"});`
- `foo(QUtf8StringView{u8"abc"});`

最实用的落地方式：在 `tests/` 加一个只负责“包含头文件 + 编译调用”的编译期测试 target（不跑也行，至少在 CI 编译）。

---

## 6) 演进与兼容性（非强制；由 review/重构决策）

- 建议：若项目对外承诺长期稳定（特别是 ABI），尽量不修改已发布 public 函数签名（参数类型、cv 限定、默认参数等），优先通过新增重载/新增函数演进。
- 允许：在 review/重构 或版本演进中可以主动修改 public API（包括改签名）。但应把它当作一次“兼容性决策”，并在评审记录中明确：
  - 为什么要破坏兼容（收益点是什么）
  - 影响范围（下游调用、二进制兼容、源码兼容、QML 兼容）
  - 迁移方案（替代接口/示例/过渡期策略；必要时主版本/SONAME 策略）

---

## 7) Code Review Checklist（落地检查）

- 该参数语义是 Borrow 还是 Owning？是否与实现一致？
- Borrow 入口是否用 view 且按值传递？是否意外写成 `const& view`？
- 是否存在 view/引用被保存或延迟使用的风险（成员/缓存/队列/闭包捕获/queued connection）？
- Qt/QML 边界是否只使用 owning 类型（`QString`/`QByteArray`/`QVariant` 等），避免 view？
- Owning 场景是否提供了 `QString`（或对应隐式共享类型）的入口以利用 implicit sharing？view 入口是否仅作为便捷入口并立即 owning 化？
- public header 的重载集合是否可能引发二义性或隐式转换陷阱？是否对典型调用面做过编译检查？
- `QObject` 接口在现代 `connect()` 语法下是否会因重载导致二义性？是否通过改名或 `qOverload` 明确选择？
- 若函数本身只是纯解析/比较且项目风格允许，是否适合标注 `noexcept`？（增强项，非本文主规则）
- 若 public API 返回 view，是否已在签名/文档中明确生命周期与失效条件；必要时是否考虑 `[[nodiscard]]`？（增强项，按项目约定）
