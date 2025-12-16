# AI 编码行为说明文档

> 本文档解释 AI 助手如何处理项目编码规范中的"强制规范"与"可选推荐"

---

## 📋 规范分类体系

本项目将编码规范分为三个层级：

### 1️⃣ **强制规范**（Mandatory）

**定义**：必须严格遵守的规则，违反将导致代码不被接受。

**AI 行为**：
- ✅ 生成的所有代码都必须符合这些规范
- ⚠️ 如果用户明确要求违反，AI 必须拒绝并提供符合规范的替代方案
- 🔍 代码审查时会严格检查这些项目

**包含内容**：
- 命名规范（`m_`、`s_`、`k` 前缀）
- 代码格式（4 空格缩进、单语句必须加括号）
- Qt 6 专属约定（新式信号槽、`QStringLiteral`、`Q_OBJECT` 宏）
- 禁止项（异常、RTTI、`dynamic_cast`、默认禁止裸 `new`/`delete`（`QObject` 派生允许裸 `new`，但必须父子树或 `deleteLater()`；禁止手动 `delete` `QObject`）、C 风格转换）

**示例**：
```cpp
// ❌ 错误：违反强制规范（单语句无括号）
if (condition)
    doSomething();

// ✅ 正确：符合强制规范
if (condition) {
    doSomething();
}
```

---

### 2️⃣ **可选推荐**（Optional Recommended）

**定义**：鼓励使用的现代化最佳实践，但不强制要求。

**AI 行为**：
- ✅ **新代码/新功能**：默认使用推荐写法
- 🔄 **维护旧代码**：保持原有风格，不强制重构
- 🤝 **用户明确偏好**：尊重用户选择，无需多次提醒
- 💡 **首次使用**：可以简要说明推荐写法的优点（仅一次）

**包含内容**（详见 `Qt6_CPP17_Coding_Style.md` 第 5 章）：
- `std::optional<T>` vs `bool func(T *out)`
- 结构化绑定 vs `QPair`
- `constexpr` vs `const`/`#define`
- `[[nodiscard]]`、`[[maybe_unused]]` 属性
- STL 容器（`std::span`、`std::variant` 等）vs Qt 容器

**示例**：
```cpp
// ✅ 推荐：现代 C++17 写法
std::optional<QColor> tryGetColor() {
    if (isValid) {
        return QColor(255, 0, 0);
    }
    return std::nullopt;
}

// ✅ 也可以接受：传统写法
bool getColor(QColor *outColor) {
    if (isValid) {
        *outColor = QColor(255, 0, 0);
        return true;
    }
    return false;
}
```

---

### 3️⃣ **项目特定约定**（Project-Specific）

**定义**：根据项目历史或团队习惯形成的惯例。

**AI 行为**：
- 🔍 **分析现有代码**：学习项目的实际编码风格
- 📊 **统计分析**：如果现有代码库中 90% 使用某种写法，优先沿用
- 🆕 **新模块**：可以适度引入现代化写法
- 📝 **文档更新**：建议用户将新约定补充到 `CLAUDE.md`

---

## 🤖 AI 编码决策树

当 AI 助手生成代码时，按以下流程决策：

```
开始生成代码
    ↓
是否违反 强制规范？
    ├─ 是 → ❌ 拒绝生成，提供符合规范的替代方案
    └─ 否 → 继续
         ↓
涉及对象生命周期管理？
         ├─ 否 → 继续
         └─ 是 → 先判断是否为 QObject 派生
                ├─ 是 → 优先 new T(parent)，必要时 deleteLater()，禁止手动 delete
                └─ 否 → 使用 RAII/智能指针，禁止裸 new/delete
                     ↓
是新代码还是维护旧代码？
    ├─ 新代码 → 优先使用 可选推荐 的现代写法
    └─ 旧代码 → 保持原有风格
          ↓
用户是否明确要求特定写法？
    ├─ 是 → 尊重用户选择（不违反强制规范即可）
    └─ 否 → 使用推荐写法
         ↓
生成代码完成
```

---

## 📊 实际场景示例

### 场景 1：用户请求生成新的工具函数

**用户请求**：
> "写一个函数，尝试解析配置文件中的颜色值，如果失败返回空"

**AI 决策过程**：
1. ✅ 这是新代码，优先使用现代写法
2. ✅ 使用 `std::optional<QColor>` 返回值
3. ✅ 函数名使用 `camelCase`，常量使用 `k` 前缀
4. ✅ 字符串字面量使用 `QStringLiteral`

**AI 生成的代码**：
```cpp
#include <QColor>
#include <QString>
#include <optional>

std::optional<QColor> parseColorFromConfig(const QString &key) {
    constexpr int kDefaultAlpha = 255;

    // 尝试从配置读取
    QString colorStr = readConfigValue(key);
    if (colorStr.isEmpty()) {
        return std::nullopt;
    }

    QColor color(colorStr);
    if (!color.isValid()) {
        return std::nullopt;
    }

    return color;
}
```

**AI 说明**：
> 我使用了 `std::optional<QColor>` 作为返回类型，这是 C++17 推荐的现代写法。如果您更倾向于传统的 `bool parseColor(const QString&, QColor*)` 风格，我可以调整。

---

### 场景 2：用户请求修改现有函数

**现有代码**：
```cpp
bool MainWindow::loadConfig(QString *errorMsg) {
    if (!m_configFile.exists()) {
        *errorMsg = QString("Config file not found");
        return false;
    }
    // ...
}
```

**用户请求**：
> "添加一个新的配置验证步骤"

**AI 决策过程**：
1. ✅ 这是维护旧代码，保持原有风格
2. ✅ 继续使用 `bool + 输出参数` 模式（不强制改为 `std::optional`）
3. ✅ 字符串构造使用 `QStringLiteral`（强制规范）
4. ✅ 成员变量使用 `m_` 前缀（强制规范）

**AI 生成的代码**：
```cpp
bool MainWindow::loadConfig(QString *errorMsg) {
    if (!m_configFile.exists()) {
        *errorMsg = QStringLiteral("Config file not found");
        return false;
    }

    // 新增：配置验证步骤
    if (!validateConfigSchema()) {
        *errorMsg = QStringLiteral("Config schema validation failed");
        return false;
    }

    // ...
}
```

**AI 行为**：
- ✅ 保持了原有的 `bool + 输出参数` 风格
- ✅ 修正了字符串字面量写法（从 `QString("...")` 改为 `QStringLiteral`）
- ✅ 不会主动建议重构为 `std::optional`

---

### 场景 3：用户明确要求传统写法

**用户请求**：
> "用传统的 QPair 写一个返回迭代器和插入状态的函数"

**AI 决策过程**：
1. ✅ 用户明确要求 `QPair`，尊重用户选择
2. ✅ 不提示使用结构化绑定
3. ✅ 仍然遵守强制规范（命名、格式等）

**AI 生成的代码**：
```cpp
QPair<QMap<QString, int>::iterator, bool> insertValue(
    QMap<QString, int> &map,
    const QString &key,
    int value)
{
    auto it = map.find(key);
    if (it != map.end()) {
        return qMakePair(it, false);
    }

    it = map.insert(key, value);
    return qMakePair(it, true);
}
```

---

### 场景 4：用户要求违反强制规范

**用户请求**：
> "用裸 new 创建一个 QWidget，然后我会在合适时机 delete 它"

**AI 决策过程**：
1. ❌ 违反强制规范（对 `QObject` 派生对象手动 `delete`）  
2. ✅ 允许裸 `new`，但必须用父子树或 `deleteLater()` 释放  

**AI 响应**：
> ⚠️ **无法按原需求生成代码**
>
> 根据项目编码规范：`QObject` 派生对象允许裸 `new`，但**禁止手动 `delete`**；应使用父子对象树或 `deleteLater()` 做事件循环安全析构。
>
> **推荐替代方案**：
>
> **方案 1：使用 Qt 父子树管理**（优先）
> ```cpp
> void createWidget(QWidget *parent) {
>     auto *widget = new QWidget(parent);
>     widget->setObjectName(QStringLiteral("myWidget"));
> }
> ```
>
> **方案 2：无 parent 且涉及异步/事件循环时，用 deleteLater()**
> ```cpp
> void createWidget() {
>     auto *widget = new QWidget();
>     widget->setObjectName(QStringLiteral("myWidget"));
> 
>     // 事件循环析构，避免手动 delete
>     widget->deleteLater();
> }
> ```
>
> 如果你确实需要“立即释放”，请说明为何不能走事件循环析构，我可以帮你调整为更安全的生命周期设计（例如明确 owner、改为非 QObject 资源走 RAII 等）。

---

## 🎯 关键原则总结

### AI 应该做的 ✅

1. **严格执行强制规范**：零容忍违规
2. **新代码使用现代写法**：默认采用"可选推荐"
3. **维护旧代码保持风格**：不强制重构
4. **尊重用户明确选择**：用户懂得权衡
5. **提供教育性说明**：首次使用新特性时简要解释（不啰嗦）

### AI 不应该做的 ❌

1. **不强制推荐项**：不反复提示使用 `std::optional`
2. **不混合风格**：在同一文件中混用传统和现代写法
3. **不假设用户知识盲区**：简洁说明，不过度教学
4. **不自作主张重构**：除非违反强制规范
5. **不忽略项目历史**：学习现有代码库的风格

---

## 📚 相关文档

- **完整编码规范**：[Qt6_CPP17_Coding_Style.md](./Qt6_CPP17_Coding_Style.md)
- **项目架构文档**：[CLAUDE.md](./CLAUDE.md)（含 AI 行为规范章节）
- **强制规范摘要**：`CLAUDE.md` 第 "AI 使用指引" 章节
- **可选推荐详情**：`Qt6_CPP17_Coding_Style.md` 第 5 章

---

**最后更新**：2025-10-28
**文档版本**：v1.0
