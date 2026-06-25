# Vora 优化路线图

> 最后更新：2026-06-25 (Phase 1-2 全部完成；稳定性：技术债清零 + 崩溃审计全部修复)
> 基于对 Vora 语言定位、当前状态、行业标准的全面审查。

---

## 目录

0. [审查：当前路线图的问题](#零审查当前路线图的问题)
1. [Vora 的真正定位](#一vora-的真正定位)
2. [哪些特性不适合 Vora](#二哪些特性不适合-vora)
3. [Vora 真正需要的特性](#三vora-真正需要的特性)
4. [修订后的路线图](#四修订后的路线图)
5. [语言缺陷与限制](#五语言缺陷与限制)
6. [远期愿景：对标 Rust / Go / Python](#六远期愿景对标-rust--go--python-的长期路线图)

---

## 零、审查：当前路线图的问题

当前 `docs/00-roadmap.md` 存在几个严重问题，需要从根上纠正。

### 0.1 问题一：身份危机 — 试图成为"所有语言的克隆"

旧路线图的 "行业标准差距分析" 以 Rust、Go、Python、TypeScript 为标杆，逐项对标，试图把所有"现代语言标配"都塞进 Vora。这犯了一个根本性错误：**Vora 不是这些语言的替代品**。

Vora 的定位是：**JS-like syntax + Lua-level simplicity + Wren-style OOP 的嵌入式脚本语言**。它的竞争对手是 Lua、Wren、AngelScript，而不是 Python、Rust、TypeScript。

按 Rust/Go/Python 的标准来设计 Vora，就像按跑车标准来设计自行车——不是自行车不好，而是方向错了。

### 0.2 问题二：特性膨胀 — 列了一堆"nice to have"而非"must have"

旧路线图列出 20+ 个"缺失特性"，但没有区分：
- **Vora 真正需要的**：能让它成为更好的嵌入式脚本语言
- **锦上添花的**：有更好，没有不影响核心价值
- **根本不该有的**：违反 Vora 的设计哲学

### 0.3 问题三：忽略了 Vora 的核心场景 — 嵌入 C++ 应用

Vora 最大的差异化价值是**作为 C++ 应用的嵌入式脚本**。但旧路线图对此几乎只字未提——C++ 嵌入 API 被排到 Q1 2027（一年后），而一些不紧急的语言特性却排在前面。

### 0.4 问题四：性能优化的时机不对

NaN-boxing、Superinstruction、JIT 编译被放在路线图中，但当前 Vora 的瓶颈不是性能——是功能缺失（标准库、模块生态、实用性）。在功能不完整时做性能优化，就像在没建好房子时装修。

---

## 一、Vora 的真正定位

### 核心身份

> **Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

这句话不是口号，而是设计边界。每一个新特性都应该问：**它服务于"JS 语法 + Lua 简洁 + Wren OOP"这个定位吗？**

### 竞争格局

| 维度 | Lua 5.4 | Vora (当前) | Vora 的机会 |
|------|---------|-------------|-------------|
| 语法现代性 | 低（`end` 关键字、无类语法） | 高（JS 风格、大括号、`Obj`） | ✅ 语法是核心优势 |
| OOP | 无原生支持（metatable hack） | 原生类 + C3 多继承 + super | ✅ OOP 是核心优势 |
| 嵌入性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐（C++ API + GC + 零依赖） | C++ 嵌入 API 已完善 |
| 标准库 | 极小但够用 | 8 个模块（math/json/fs/os/datetime/array/string/regex） | ✅ 标准库已覆盖核心场景 |
| 异常处理 | 无（pcall 不是语法级） | try/catch/finally 完整 | ✅ 异常处理是核心优势 |
| 性能 | ⭐⭐⭐⭐⭐（寄存器 VM + 经典优化） | ⭐⭐⭐ | 中期优化 |

**Vora 的差异化武器**：
1. **现代语法**：比 Lua 的 `end/if/then` 语法现代得多，开发者上手零成本
2. **原生 OOP**：Lua 没有类，Wren 没有多继承，Vora 两者都有
3. **异常处理**：Lua 的 pcall 是函数式错误处理，Vora 的 try/catch/finally 是语法级的
4. **跨平台构建 + 原生打包**：同类项目中罕见的工程品质

### 目标用户

1. **C++ 应用开发者**：需要在 C++ 项目中嵌入脚本能力（游戏、GUI、工具）
2. **教学/原型**：学习语言实现、快速原型
3. **轻量脚本任务**：配置文件、数据处理、自动化

---

## 二、哪些特性不适合 Vora

以下是旧路线图中**不应该加入 Vora**的特性，以及原因：

### 2.1 🔴 明确排除（违反设计哲学）

| 特性 | 为什么不适合 |
|------|-------------|
| **可选静态类型（TS 模式）** | Vora 的定位是"动态类型"，渐进式类型会把语言分裂成两种范式。Python 的 typing 是 10 年后才加的，且依赖庞大的工具链。Vora 不需要这个负担。 |
| **方法重载** | Lua、JS、Python 都没有。同名方法最后定义覆盖是动态语言的自然行为。重载增加了调用歧义和实现复杂度，与"Lua-level simplicity"矛盾。 |
| **装饰器 `@`** | 元编程语法糖，需要元类/反射系统支撑。Vora 的 Obj 系统足够简单，装饰器会引入不必要的复杂度。 |
| **LINQ 风格查询** | C# 专属范式，依赖表达式树和类型推导。动态脚本语言中，列表推导式已经覆盖了这个需求。 |
| **Char 独立类型** | 单字符就是短字符串，不需要独立类型。增加 Char 类型会增加 Value 变体大小和比较复杂度。 |
| **错误传播 `?` 运算符** | Rust 的 `?` 基于 `Result<T, E>` 类型，Vora 没有这个类型系统。Vora 的 try/catch 已经是成熟的异常处理机制，不需要另一套。 |
| **Sealed class / Record** | Java/C# 专属，与动态语言的灵活性冲突。 |
| **抽象方法 / 抽象类** | 可以通过在方法体中 `throw "abstract"` 实现。语言层面不需要强制抽象。 |

### 2.2 🟡 不急于添加（锦上添花，非核心）

| 特性 | 说明 |
|------|------|
| **接口 / Trait** | Vora 已有 C3 多继承，可以通过 mixin 模式模拟。接口是好的，但不是现在。 |
| **Match 守卫 + 穷尽性检查** | match v1 已经够用。守卫条件和穷尽性是锦上添花。 |
| **参数解构 `func f({x,y})`** | 语法糖，不改变能力边界。 |
| **海象赋值 `:=`** | Python 风格，Vora 有 `while` + 内联赋值的替代方案。 |

---

## 三、Vora 真正需要的特性

以下是 Vora **真正应该优先实现**的特性，按优先级排序：

### 3.1 🔴 最高优先级 — 让 Vora 能做实际工作

| 特性 | 状态 | 为什么关键 | 工期 |
|------|------|-----------|------|
| **`std/fs` + `std/os` 标准库** | ✅ 已完成 | 无文件 I/O 无法做任何实际任务。这是从"玩具"到"工具"的质变。 | 1-2 周 |
| **`std/regex` 正则表达式** | ✅ 已完成 | 脚本语言的字符串处理核心能力。 | 1 周 |
| **空安全 `?.` + `??`** | ✅ 已完成 | 现代语言标配，消除 null 检查嵌套，代码量 -30%。JS 语法天然适配。 | 1 周 |
| **错误类型层级 + `catch (e if Type)`** | ❌ 已排除 | Vora 保持动态类型的简洁性，不引入错误类型层级。 | — |

### 3.2 🟠 高优先级 — 提升语言体验

| 特性 | 状态 | 为什么需要 | 工期 |
|------|------|-----------|------|
| **`std/datetime` + `std/array` + `std/string`** | ✅ 已完成 | 日期处理、数组工具、字符串增强——日常脚本必备。 | 1-2 周 |
| **`do-while` 循环** | ✅ 已完成 | 基础控制流缺口。先执行后判断的场景（如输入验证）需要它。 | 1 天 |
| **调用端展开 `...expr`** | ✅ 已完成 | 与 rest 参数对称，解锁函数式模式。成本极低。 | 1-2 天 |
| **列表/Dict 推导式** | ✅ 已完成 | 一行替代多层循环，数据处理核心体验。JS/Python 风格。 | 1 周 |
| **命名参数** | ✅ 已完成 | `func(name="Vora", age=18)` 自文档化，减少参数顺序错误。 | 2-3 天 |
| **访问控制 `private`** | ❌ 已排除 | `import`/`export` 已提供模块级封装，类级 `private` 非必需。 | — |

### 3.3 🟡 中优先级 — 完善 OOP + 性能

| 特性 | 为什么需要 | 工期 |
|------|-----------|------|
| **静态方法 / 类方法** | 全部 OOP 语言都有。工厂方法、工具方法需要它。 | 2-3 天 |
| **Tuple 元组** | 轻量不可变聚合，多返回值场景更优雅。 | 1 周 |
| **C++ 嵌入 API 完善** | 单头文件 `vora.hpp` + `vora_lib.lib`，零依赖嵌入。 | ✅ 已完成 |
| **NaN-boxing** | Value 从 16 字节压缩到 8 字节，性能提升 2-5×。 | 3-4 周 |

### 3.4 🟢 后续 — 生态和工具链

| 特性 | 说明 | 工期 |
|------|------|------|
| **异步/协程** | 基于现有 generator 扩展。 | 2-3 周 |
| **调试器 DAP** | 断点 + 单步 + 变量查看。 | 3 周 |
| **GC 分代回收** | 暂停时间降低 5-10×。 | 2-3 周 |
| **包管理器** | 社区生态基础。 | 4-6 周 |

---

## 四、修订后的路线图

### 总体原则

1. **嵌入优先**：C++ 嵌入 API 是核心路线，而非远期目标
2. **实用优先**：标准库 > 语法糖 > 性能优化
3. **身份优先**：每个特性都必须服务于"JS 语法 + Lua 简洁 + Wren OOP"
4. **不贪多**：每个季度只做 3-5 个核心特性，做到位

---

### Phase 1: 2026 Q3 (7-9月) — 标准库 + 核心语法补全 🔴 ✅ 已完成

> 目标：让 Vora 能做实际工作（文件操作、正则匹配、空安全）
> 进度：✅ 全部完成（std/fs, std/os, std/datetime, std/array, std/string, std/regex, ?. + ??, defer, do-while, ...expr, 常量池去重）

```
标准库（最高优先级）
├── ✅ std/fs  — readFile/writeFile/exists/listDir/mkdir/delete
├── ✅ std/os  — env/getcwd/exit/shell/args
├── ✅ std/datetime — now/nowMs/timestampToDate/formatDuration/sleep
├── ✅ std/array — sort/reverse/join/flatten/unique/chunk/fill/compact
├── ✅ std/string — repeat/padStart/padEnd/capitalize/titleCase/count/lines
└── ✅ std/regex — test/find/replace/split（基于 C++ std::regex，ECMAScript 语法）

核心语法
├── ✅ 空安全 ?. + ??                    ← 消除 null 检查嵌套
├── ✅ defer 延迟执行                    ← Go 风格资源释放（编译期实现）
├── ✅ do-while 循环                     ← 基础控制流补全
├── ✅ 调用端展开 ...expr                ← 与 rest 参数对称
└── ✅ 常量池去重优化（编译期）            ← 编译速度提升
```

**为什么标准库排第一**：没有 `std/fs` 和 `std/os`，Vora 无法读写文件、获取环境变量、执行系统命令。这限制了它作为脚本语言的基本能力。一个不能读文件的脚本语言，语法再好也没用。**（已补齐：fs/os/datetime/array/string/regex 共 6 个模块，全部完成。）**

---

### Phase 2: 2026 Q3-Q4 (提前完成) — 语言竞争力 ⭐ ✅ 已完成

> 目标：提升日常编码体验，让 Vora 写起来舒服
> 进度：✅ 全部完成（列表推导式、Dict 推导式、命名参数、类型注解、常量池 WIDE 指令、C++ 嵌入 API）

```
语法糖
├── ✅ 列表推导式 [x for x in arr if cond]
├── ✅ Dict 推导式 {k: v for k, v in pairs}
├── ✅ 命名参数 func(name="Vora", age=18)
├── ✅ 类型注解运行时转换 :float/:int/:bool/:str
└── 参数解构 func f({x, y})  ← 延后至 Phase 3

基础设施
├── ✅ C++ 嵌入 API（vora.hpp 单头文件 + embed 示例）
├── ✅ 常量池 16-bit WIDE 指令（突破 256 条目限制）
└── ✅ 常量池 O(1) double 去重

OOP 完善
├── 静态方法 / 类方法  ← 延后至 Phase 3
└── 对象 rest 解构 {x, ...rest}  ← 延后至 Phase 3
```

---

### 稳定性里程碑：2026-06-25 — 技术债清零 + 崩溃审计 ✅

> **P3 技术债**：20 项已修复（默认参数辅助方法、LSP 死代码清理、防御性检查、`VORA_VERSION` 连接、语法高亮 `as alias`）
>
> **崩溃终止 vs RuntimeError 审计**：18 项全部处理
> - P0 (5)：`assert()` → 异常/编译器错误、`std::get` → `holds_alternative` 守卫
> - P1 (8)：`catch(...)` 兜底、`peek()` 边界检查、OP 栈下溢守卫、`push()` 溢出标志
> - P2 (3)：AST Printer / Formatter / Compiler 递归深度限制 (MAX_DEPTH=10000)
>
> **CI**：改为 `workflow_dispatch` 手动触发

---

### Phase 3: 2027 Q1 (1-3月) — 嵌入增强 + OOP 完善 ⭐

> 目标：完善 OOP 能力，让 Vora 嵌入体验达到 Lua 级别

```
OOP 完善
├── 静态方法 / 类方法
├── 对象 rest 解构 {x, ...rest}
└── 参数解构 func f({x, y})

嵌入能力增强
├── ✅ C++ 嵌入 API 已完善（vora.hpp 单头文件 + embed 示例）
├── VM public API 扩展（registerNativeFunction、setGlobal/getGlobal）
├── 嵌入文档（嵌入指南 + 完整示例项目）
└── SDK 安装器（cmake --install 一键导出头文件 + .lib）

性能
├── NaN-boxing — Value 从 16 字节压缩到 8 字节
└── Superinstruction 合并 — 常见字节码序列融合
```

**为什么嵌入 API 排在这里**：Lua 的成功很大程度上因为 C API 极其简洁。Vora 的 C++ 嵌入 API 已有基础（`vora.hpp` + `vora_lib.lib`），但还需要完善 VM public API 和嵌入文档才能与 Lua 竞争嵌入式脚本市场。

---

### Phase 4: 2027 Q2+ — 生产级 + 生态建设

```
性能
├── GC 分代回收
├── Superinstruction 合并
├── 常量池共享 + 字节码内联
└── 性能基准套件 + 与 Lua/Python 对比

异步/并发
└── async/await + 事件循环（基于现有 generator 扩展）

工具
├── 调试器 DAP（断点 + 单步 + 变量查看）
├── 包管理器 vpm
└── 文档生成器

JIT
└── JIT 编译（研究阶段，仅在 NaN-boxing + superinstruction 之后）
```

---

## 五、语言缺陷与限制

> 本节记录 Vora 当前存在的语法歧义、运行时限制、设计缺憾。

### 5.1 语法缺陷

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **`return` 必须带值** | ✅ 已修复 | 现已支持 `return` / `return;` (void return) |
| **一元负号绑定强于下标** | ✅ 已修复 | `-a[i]` 正确解析为 `-(a[i])` |
| **控制流关键字后不能有分号** | ✅ 已修复 | `break;` / `continue;` / `return;` 现已支持 |
| **类定义关键字为 `Obj`（非直觉）** | 🟢 低 | `class` / `obj` (小写) 均为未定义变量。这是设计选择，不是缺陷。 |

### 5.2 类型与数据结构缺口

| 缺口 | 严重度 | 说明 |
|------|--------|------|
| **无 `Array.sort` / 内建排序** | ✅ 已修复 | `std/array.sort()` 已提供排序。 |
| **字符串拼接低效** | 🟡 中 | 每次 `+` 都重新分配。可引入 StringBuilder 减少 GC 压力。 |
| **`Value` 类型不可扩展** | 🟡 中 | `std::variant` 封闭了类型集合。用户无法注册新类型。但这是设计权衡——封闭 variant 更安全、更快。 |

### 5.3 标准库空白

| 模块 | 状态 | 说明 |
|------|------|------|
| `std/math` | ✅ 已完成 | abs/min/max/floor/ceil/sqrt/sin/cos/random |
| `std/json` | ✅ 已完成 | parse/stringify |
| `std/fs` | ✅ 已完成 | readFile/writeFile/exists/listDir/mkdir/delete |
| `std/os` | ✅ 已完成 | env/getcwd/exit/shell/args |
| `std/datetime` | ✅ 已完成 | now/nowMs/timestampToDate/formatDuration/sleep |
| `std/array` | ✅ 已完成 | sort/reverse/join/flatten/unique/chunk/fill/compact |
| `std/string` | ✅ 已完成 | repeat/padStart/padEnd/capitalize/titleCase/count/lines |
| `std/regex` | ✅ 已实现 | test/find/replace/split |

### 5.4 工具链缺失

| 缺口 | 状态 | 说明 |
|------|------|------|
| **LSP 服务器** | ✅ 已完成 | 诊断、补全、跳转、悬停、格式化 |
| **调试器 (DAP)** | 🔴 待实现 | 断点 + 单步执行 |
| **包管理器** | 🔴 待实现 | 安装/发布/版本化第三方库 |
| **Profiler** | 🔴 待实现 | 性能剖析器 |

### 5.5 互操作性

| 缺口 | 状态 | 说明 |
|------|------|------|
| **C++ 嵌入 API** | ✅ 已完成 | 单头文件 `vora.hpp` + `vora_lib.lib` |
| **无 FFI** | 🔴 不计划 | 不做 C ABI；C++ 原生函数可替代 |
| **无跨进程序列化** | 🔴 待实现 | 无法将 Value 导出为二进制/字节码 |

---

## 附录：旧路线图中被移除的特性及理由

| 被移除的特性 | 移除理由 |
|-------------|---------|
| 可选静态类型 (TS 模式) | Vora 定位为动态类型语言。渐进式类型需要类型推导引擎，与"Lua-level simplicity"矛盾。 |
| 方法重载 | 动态语言不需要。同名方法最后定义覆盖是自然行为。 |
| 装饰器 `@` | 元编程语法糖，需要元类/反射系统支撑。Vora 不需要这层复杂度。 |
| LINQ 风格查询 | C# 专属范式。列表推导式已覆盖同样需求。 |
| Char 独立类型 | 单字符就是短字符串。增加 Char 类型增加 Value 大小和比较复杂度。 |
| 错误传播 `?` 运算符 | 基于 Result 类型，Vora 没有也不需要这个类型系统。 |
| Sealed class / Record | Java/C# 专属，与动态语言灵活性冲突。 |
| 抽象方法 / 抽象类 | 可以通过 `throw "abstract"` 实现，不需要语言层面强制。 |
| Match 守卫 + 穷尽性 | match v1 已够用。守卫是锦上添花，穷尽性检查对动态语言过严。 |
| 海象赋值 `:=` | Python 风格，Vora 有替代方案。 |

> **设计原则**：Vora 是"实用中间地带"——JavaScript 式语法 + Lua 级简洁 + Wren 风格 OOP。新增特性应服务于该定位，避免成为 C++/Rust 的臃肿克隆。**宁可少做，不可做错。**

---

## 六、远期愿景：对标 Rust / Go / Python 的长期路线图

> 本节描述 Vora 在完成核心阶段（Phase 1-4）后，如何逐步扩展能力边界，最终在特定领域与 Rust、Go、Python 形成竞争。
>
> **核心原则**：不是成为它们的克隆，而是利用 Vora 的独特优势（可嵌入性、现代语法、极简设计）在它们的薄弱环节建立优势。

### 6.0 竞争策略总览

```
Vora 不是要成为"下一个 Python"或"下一个 Rust"。
Vora 要成为"唯一一门同时满足以下条件的语言"：

1. 语法现代、开发者友好（JS 风格，非 Lua 的 end/if/then）
2. 可嵌入任何 C++ 应用（Lua 级别的嵌入能力）
3. 原生 OOP + 异常处理（Lua 没有，Wren 不完整）
4. 性能可接受（不是最快，但足够快）
5. 跨平台、零依赖、可打包
```

### 6.1 对标 Rust — 工具链 + 可靠性

Rust 的核心卖点是**内存安全 + 零成本抽象 + 优秀工具链**。Vora 无法在系统编程层面与 Rust 竞争（也不应该），但可以在以下领域超越：

| Rust 的痛点 | Vora 的机会 | 所需特性 |
|-------------|-------------|---------|
| **编译慢**（大项目 10+ 分钟） | Vora 脚本即时执行，无编译等待 | 已具备 |
| **学习曲线陡峭**（所有权、生命周期） | Vora 语法零学习成本 | 已具备 |
| **嵌入性差**（不适合嵌入脚本） | Vora 天生为嵌入设计 | C++ 嵌入 API |
| **工具链分散**（cargo + rustfmt + clippy） | Vora 统一 CLI（vora run/fmt/check） | vora fmt 已有，扩展 check 命令 |
| **构建系统复杂** | Vora 零依赖，CMake 一键构建 | 已具备 |

**Vora vs Rust 的差异化定位**：

| 场景 | Rust | Vora | Vora 的优势 |
|------|------|------|-------------|
| 系统编程（OS、驱动） | ✅ 首选 | ❌ 不适用 | — |
| C++ 应用嵌入脚本 | ❌ 不适合 | ✅ 首选 | C++ 嵌入 API + GC + 零依赖 |
| CLI 工具 | ✅ 好但编译慢 | ✅ 即时执行 | 开发速度 |
| 构建系统插件 | ✅ 但学习成本高 | ✅ 语法简单 | 降低贡献门槛 |
| WebAssembly | ✅ 一流支持 | 🔜 长期目标 | WASM 编译后端 |

**长期路线（2028+）**：

```
Vora → Rust 竞争力路线
├── vora check — 静态分析命令（lint + 类型推导提示）
├── vora build — 编译为独立可执行文件（AOT 编译）
├── vora test — 内建测试框架（不依赖外部工具）
├── vora bench — 内建基准测试
├── WebAssembly 后端（编译 .va → .wasm）
└── 与 C++ 项目深度集成（CMake find_package + vora_add_script）
```

> **目标**：成为 C++ 项目的"官方脚本语言"——比 Lua 语法现代，比 Python 嵌入简单，比 Python 快。

---

### 6.2 对标 Go — 网络 + 并发 + 工程化

Go 的核心卖点是**并发原语 + 网络编程 + 极简工程**。Vora 可以在以下领域与 Go 竞争：

| Go 的痛点 | Vora 的机会 | 所需特性 |
|-----------|-------------|---------|
| **错误处理冗长**（if err != nil） | Vora 的 try/catch/finally 更简洁 | 已具备 |
| **无 OOP**（仅组合） | Vora 有原生类 + 多继承 | 已具备 |
| **无泛型**（1.18 后有但语法丑） | 动态类型天然不需要泛型 | 已具备 |
| **无三元运算符** | Vora 有 `?:` | 已具备 |
| **无模式匹配** | Vora 有 match | 已具备 |
| **无列表推导式** | Vora 已实现 | ✅ v0.26 |

**Vora vs Go 的差异化定位**：

| 场景 | Go | Vora | Vora 的优势 |
|------|-----|------|-------------|
| 微服务 / HTTP API | ✅ 首选 | 🔜 可竞争 | 更简洁的错误处理 |
| CLI 工具 | ✅ 好 | ✅ 好 | 更快的开发速度 |
| 并发编程 | ✅ goroutine（首选） | 🔜 async/await | 基于 generator 的协程 |
| 嵌入式脚本 | ❌ 不适合 | ✅ 首选 | C++ 嵌入 API + GC |
| DevOps 工具 | ✅ 首选 | 🔜 可竞争 | 脚本语言更灵活 |
| 网络爬虫 | ✅ 可以 | 🔜 可竞争 | 更简单的语法 |

**长期路线（2028+）**：

```
Vora → Go 竞争力路线
├── async/await + 事件循环（Phase 3 已规划）
├── std/http — HTTP 客户端 + 服务器
├── std/net — TCP/UDP socket
├── std/thread — 多线程 + channel
├── std/sync — 互斥锁 + 信号量
└── vora serve — 内建 HTTP 服务器模式
```

> **目标**：成为轻量网络服务的首选脚本语言——比 Go 更简洁，比 Python 更快，比 Node.js 更可嵌入。

---

### 6.3 对标 Python — 脚本 + 数据 + 生态

Python 的核心卖点是**庞大的生态 + 数据科学 + 极致的易用性**。Vora 无法复制 Python 的生态（50 万+ PyPI 包），但可以在以下领域建立优势：

| Python 的痛点 | Vora 的机会 | 所需特性 |
|--------------|-------------|---------|
| **GIL 限制并发** | Vora 可设计无 GIL 的并发模型 | async/await + 多线程 |
| **启动慢**（CPython 初始化） | Vora VM 启动更快 | 已具备（轻量 VM） |
| **性能差**（比 C 慢 100×） | Vora VM + NaN-boxing + JIT 可接近 Lua 性能 | NaN-boxing (Phase 2) |
| **依赖管理混乱**（pip/pipenv/poetry/uv） | Vora 统一包管理器 | vpm (Phase 4) |
| **C 扩展复杂**（CPython API 臃肿） | Vora C++ 嵌入 API 极简 | 已实现 |
| **动态类型无检查** | Vora 同样，但可提供可选 lint | vora check |

**Vora vs Python 的差异化定位**：

| 场景 | Python | Vora | Vora 的优势 |
|------|--------|------|-------------|
| 数据科学 / ML | ✅ 绝对统治 | ❌ 不适用 | — |
| Web 后端 (Django/Flask) | ✅ 成熟 | 🔜 可竞争 | 更简洁的语法 |
| 脚本 / 自动化 | ✅ 首选 | ✅ 可竞争 | 更快、更可嵌入 |
| 系统管理 | ✅ 首选 | ✅ 可竞争 | 更简洁的错误处理 |
| 嵌入式脚本 | ❌ C API 臃肿 | ✅ 首选 | C++ 嵌入 API 极简 |
| 教学语言 | ✅ 常用 | ✅ 可竞争 | 语法更现代 |
| CLI 工具 | ✅ 好 | ✅ 好 | 更快的执行速度 |
| 网络爬虫 | ✅ 首选 | 🔜 可竞争 | 更好的并发模型 |

**长期路线（2028+）**：

```
Vora → Python 竞争力路线
├── std/http — HTTP 客户端（requests 级别）
├── std/path — 路径操作（pathlib 级别）
├── std/process — 子进程管理
├── std/datetime — 日期时间（datetime 级别）
├── std/collections — 高级数据结构（deque、counter、defaultdict）
├── std/functools — 函数式工具（map/filter/reduce/partial）
├── vora test — 内建测试框架（pytest 级别的简洁 API）
├── vora script — 脚本打包为独立可执行文件
└── 第三方库生态（vpm 注册表）
```

> **目标**：成为"更好的 Python"——语法同样简洁，但更快、更可嵌入、更现代。

---

### 6.4 三阶段扩展路线图

#### Phase 5: 2027 Q3-Q4 — 通用语言能力 🌐

> 目标：从"嵌入式脚本语言"升级为"通用脚本语言"

```
网络与 I/O
├── std/http — HTTP 客户端 + 服务器
├── std/net — TCP/UDP socket
├── std/path — 路径操作（跨平台）
├── std/process — 子进程管理
└── std/datetime — 日期时间

数据处理
├── std/collections — deque / counter / defaultdict / ordered
├── std/functools — map / filter / reduce / partial / curry
├── std/sort — 高级排序（自定义比较、稳定排序）
└── std/string 扩展 — 编码/解码、模板、格式化

并发
├── std/thread — 多线程（基于 C++ std::thread）
├── std/sync — Mutex / Channel / WaitGroup
└── async/await 完善 — Promise / async for / async with
```

#### Phase 6: 2028 Q1-Q2 — 工程化 + 工具链 🔧

> 目标：建立完整的开发者工具链

```
工具链
├── vora check — 静态分析 + lint + 类型推导提示
├── vora test — 内建测试框架（describe/it/expect）
├── vora bench — 内建基准测试
├── vora script — 打包为独立可执行文件
└── vora doc — 从源码注释生成文档

包管理器 vpm
├── vpm init — 创建项目骨架
├── vpm install — 安装依赖
├── vpm publish — 发布到注册表
├── vpm search — 搜索包
└── vora.lock — 依赖锁定

性能
├── JIT 编译（Tiered: 解释器 → 基线 JIT → 优化 JIT）
├── Copy propagation + Dead code elimination
└── Inline caching（属性访问 + 方法调用）
```

#### Phase 7: 2028 Q3+ — 生态爆发 🚀

> 目标：建立社区和第三方库生态

```
生态
├── vora-lang.org — 官方网站 + 文档 + 教程
├── vpm 注册表 — 包发现 + 版本管理 + 依赖解析
├── VS Code 扩展增强 — 调试器集成 + 代码片段
├── JetBrains 插件 — IntelliJ / CLion 支持
├── Vim / Neovim 插件 — 语法高亮 + LSP
└── Emacs 模式

特定领域库
├── vora-web — HTTP 框架（类 Flask/Express）
├── vora-db — 数据库驱动（SQLite / PostgreSQL）
├── vora-test — 测试框架（类 pytest/Jest）
├── vora-arg — 命令行参数解析（类 argparse/click）
├── vora-log — 日志框架
└── vora-yaml — YAML 解析

互操作
├── WebAssembly 后端 — .va → .wasm
├── Python 扩展 — 在 Python 中嵌入 Vora
├── Rust 互操作 — 通过 C++ FFI 调用 Rust 库
└── Node.js 绑定 — 在 Node.js 中嵌入 Vora
```

---

### 6.5 差异化竞争优势矩阵

| 能力 | Vora vs Lua | Vora vs Go | Vora vs Python | Vora vs Rust |
|------|-------------|------------|----------------|--------------|
| 可嵌入性 | ≈ 持平 | **Vora 胜** | **Vora 胜** | **Vora 胜** |
| 语法现代性 | **Vora 胜** | **Vora 胜** | ≈ 持平 | **Vora 胜** |
| OOP 能力 | **Vora 胜** | **Vora 胜** | ≈ 持平 | **Vora 胜** |
| 异常处理 | **Vora 胜** | **Vora 胜** | ≈ 持平 | **Vora 胜** |
| 性能 | Lua 更快 | Go 更快 | **Vora 胜** | Rust 远胜 |
| 并发 | Go 更好 | Go 更好 | ≈ 持平 | Rust 更安全 |
| 生态 | Lua 更丰富 | Go 更丰富 | Python 远胜 | Rust 更丰富 |
| 学习曲线 | **Vora 胜** | **Vora 胜** | **Vora 胜** | **Vora 胜** |
| 工具链 | **Vora 胜** | Go 更好 | Python 更好 | Rust 更好 |
| 生产就绪度 | Lua 更成熟 | Go 更成熟 | Python 远胜 | Rust 更成熟 |

---

### 6.6 Vora 的终极愿景

```
短期（2026-2027）：最好的嵌入式脚本语言
  → 比 Lua 语法现代，比 Wren 功能完整
  → C++ 应用的首选脚本方案

中期（2027-2028）：最好的通用脚本语言
  → 比 Python 更快，比 Go 更简洁
  → 脚本、自动化、网络服务的首选

长期（2028+）：语言生态平台
  → 完整的包管理器 + 第三方库生态
  → WebAssembly + 多语言互操作
  → 开发者社区 + 教程 + 文档
```

> **一句话总结**：Vora 的路径不是"成为下一个 X"，而是"成为唯一同时满足：可嵌入 + 现代语法 + 高性能 + 完整生态"的语言。
