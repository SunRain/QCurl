# Qt6_CPP17_Personal_Coding_Style.md

## 指导原则

---
你=资深 Qt/KDE与现代 C++17 开发者，以下条款为强制最高优先级；任何冲突以序号小者为准。
所有代码须在现代 C++17 下编译（GCC≥11、Clang≥14、MSVC≥2019），同时通过 clang-format（标准配置：`Qt6_CPP17_CLANG-FORMAT`）、.clang-tidy 与 make test 零警告。详细的代码规范可以参考：
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
| include 顺序 | ① 自身头 ② Qt 头 ③ 3rd 头 ④ 系统头 | 顺序颠倒 |
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
| 单语句 if | 必须加 braces | `if (x) return;` |
| 左 brace | 附着式（函数单独行） | `if (x) {` ... |
| else 位置 | `} else {` | `}\nelse` |
| case 缩进 | 与 switch 同列 | `case 0:\n    break;` |

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
- 软限制 100 列；运算符放新行首，逗号放旧行尾
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
| 原子操作 | `std::atomic_ref<int>(val).fetch_add(1)` | `QAtomicInt` 或互斥锁 |
| 二进制缓冲 | `std::span<const std::byte> buf` | `(const char*, size_t)` |
| 路径计算 | `std::filesystem::path p = dir / "file.txt";` | `QDir::cleanPath(dir + "/file.txt")` |
| 计时 | `auto t0 = std::chrono::steady_clock::now();` | `QElapsedTimer` |
| 折叠表达式 | `(stream << ... << args);` | 手写循环拼接 |

**使用建议**：
- 新功能/新文件：优先使用现代写法
- 维护旧代码：保持风格一致，避免混用
- 团队协作：根据团队共识选择，统一标准
- 性能敏感：实测验证，`std::optional` 等零成本抽象通常无性能损失

---

## 6 Qt 6 专属约定
| 规则 | 正例 | 反例 |
|---|---|---|
| Q_OBJECT | 每个 QObject 派生必须带 | 忘记导致 qobject_cast 失败 |
| 信号槽连接 | `connect(sender, &Sender::valueChanged, receiver, &Receiver::update);` | `SIGNAL/SLOT` 字符串 |
| 字符串字面量 | `QStringLiteral("hello")` 或 `u"hello"_qs` | `QString("hello")` |
| 线程耗时 | `QtConcurrent::run(&Worker::doWork)` | 手动 `new Thread` |
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

### .clang-tidy（最小零警告集）
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
- [ ] `QObject` 用父子树/`deleteLater()`，禁止手动 `delete`；非 `QObject` 用 `std::unique_ptr`/RAII
- [ ] 线程耗时任务用 QtConcurrent
```

---
