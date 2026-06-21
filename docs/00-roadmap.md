# Vora 优化路线图

> 最后更新：2026-06-21 (行业标准差距分析 + README 全面审查)
> 基于 `docs/13-标准与反思.md` 对 Vora 的全面审查。

---

## 目录

0. [Vora vs 行业标准 — 差距分析](#零vora-vs-行业标准--差距分析-基于-docs13)
1. [性能优化](#一性能优化)
2. [语言特性](#二语言特性)
3. [工具链](#三工具链)
4. [质量工程](#四质量工程)
5. [时间线建议](#五时间线建议)
6. [语言缺陷与限制](#六语言缺陷与限制)

---

## 零、Vora vs 行业标准 — 差距分析 (基于 docs/13)

> 参考 `docs/13-标准与反思.md`，逐项审查 Vora 在"现代高级语言标配"和"各语言独有优雅特性"两个维度上的完成度。

### 0.1 基础流程控制

| 特性 | 状态 | 说明 |
|------|------|------|
| `if` / `else if` / `else` | ✅ | |
| `while` 循环 | ✅ | |
| `do`-`while` 循环 | ❌ | **基础循环形态缺失** — C/Java/JS/Go/Rust 标配 |
| C-style `for` | ✅ | v0.19 |
| `for`-`in` | ✅ | 数组/字符串/range/Dict/Set/Map/对象 |
| `break` / `continue` | ✅ | |
| `match` 模式匹配 | ⚠️ | v0.24 完成 v1：字面量/通配符/范围/多模式 |
| `match` 守卫条件 `if` | ❌ | 延后至 v1.1+，Rust/Python 标配 |
| `match` 穷尽性检查 | ❌ | 编译器不强制兜底分支 |
| `&&` / `\|\|` / `!` | ✅ | + `and`/`or` 别名 |
| `?:` 三元表达式 | ✅ | |

### 0.2 数据与类型

| 特性 | 状态 | 说明 |
|------|------|------|
| Number（int64/float64） | ✅ | |
| Boolean | ✅ | |
| String | ✅ | 不可变字符串 + `${}` 插值 |
| 独立 Char 类型 | ❌ | **单字符用 String 代替**，无 `char` 字面量 |
| Array | ✅ | |
| Dict（纯字符串 key） | ✅ | `{key: value}` 字面量 |
| Set | ✅ | v0.23 |
| Map（任意类型 key） | ✅ | v0.23 |
| Tuple（元组） | ❌ | **无轻量不可变聚合类型** — 用 Array 代替，失去不可变性 |
| 动态类型 | ✅ | 纯动态，运行期推导 |
| 可选类型标注 | ❌ | 无类型标注 → 参数类型错误只在运行时暴露 |
| 可选静态类型（TS 模式） | ❌ | 无法渐进式加类型 |
| `let` 可变 / `const` 不可变 | ✅ | `const` 编译期保护 |

### 0.3 函数体系

| 特性 | 状态 | 说明 |
|------|------|------|
| 普通函数 | ✅ | `func name(params) { body }` |
| 匿名函数 / Lambda | ✅ | v0.19 |
| 闭包（共享 Upvalue） | ✅ | v0.19 |
| 默认参数 | ✅ | v0.15 |
| 剩余参数 `...name` | ✅ | v0.22 |
| 命名参数 | ❌ | **函数调用不支持 `func(name=value)`** — Python/TS 标配 |
| 调用端展开 `...expr` | ❌ | `func(...array)` — 已在 2.2 规划 |
| 函数一等公民 | ✅ | 可赋值、传参、返回 |
| 尾调用优化 (TCO) | ✅ | v0.20 |

### 0.4 面向抽象（OOP）

| 特性 | 状态 | 说明 |
|------|------|------|
| 类 / 对象 | ✅ | `Obj Name(params) { ... }` |
| 单继承 | ✅ | `Obj Dog : Animal (params) { ... }` |
| 多继承 (C3 线性化) | ✅ | v0.15 |
| `super` 关键字 | ✅ | v0.15 |
| 访问控制（`public`/`private`） | ❌ | **所有成员默认公开** — OOP 基础安全机制 |
| 接口 / Trait | ❌ | **无行为契约定义机制** — Rust Trait/Go 隐式接口/Java Interface |
| 抽象方法 / 抽象类 | ❌ | 无强制子类实现 |
| 方法重载 | ❌ | 同名方法以最后定义覆盖 |
| 静态方法 / 类方法 | ❌ | 无 `static func` |
| `this` 在闭包中绑定 | ⚠️ | 需手动 `let self = this` |

### 0.5 错误处理

| 特性 | 状态 | 说明 |
|------|------|------|
| `try` / `catch` / `finally` | ✅ | 三种组合均支持 |
| 错误类型层级 | ❌ | **无 Error 基类** — catch 无法按类型过滤 |
| 按类型 `catch (e if Type)` | ❌ | 现有 catch 捕获所有异常 |
| 空安全操作符 `?.` | ❌ | **缺失** — TS/JS/C#/Swift/Kotlin 现代标配 |
| 空值合并 `??` | ❌ | **缺失** — `x ?? "default"` vs `x ? x : "default"` |
| 错误传播运算符 | ❌ | Rust `?` / Go `if err != nil` 风格 |

### 0.6 内存与资源管理

| 特性 | 状态 | 说明 |
|------|------|------|
| GC (Mark-Sweep) | ✅ | v0.18 |
| 资源自动释放 | ❌ | **无 `defer` / `with` / `using`** — Go/Python/C# 标配 |
| RAII | ❌ | C++ 风格，Vora 非底层语言不适用 |
| 析构函数 / finalizer | ❌ | 对象无销毁钩子 |

### 0.7 现代语法糖

| 特性 | 状态 | 说明 |
|------|------|------|
| 解构赋值（数组） | ✅ | v0.22 |
| 解构赋值（对象） | ⚠️ | 部分实现，对象 rest `{x, ...rest}` 待完善 |
| 参数解构 `func f({x,y})` | ❌ | 函数参数直接解构 |
| 展开运算符（定义端） | ✅ | `...rest` 剩余参数 |
| 展开运算符（调用端） | ❌ | `func(...arr)` 已在 2.2 规划 |
| 字符串插值 `${}` | ✅ | |
| 列表推导式 | ❌ | `[x for x in arr if cond]` — 已在 2.12 规划 |
| Dict 推导式 | ❌ | `{k: v for k, v in pairs}` — 已在 2.12 规划 |
| 链式调用 / 流式遍历 | ❌ | 无 `.filter().map()` 等集合链式算子 |
| 生成器 / `yield` | ✅ | v0.21 |
| 迭代器协议 | ✅ | `iter()` / `next()` / `StopIteration` |
| 海象赋值 `:=` | ❌ | Python 风格循环内赋值 |
| 装饰器 `@` | ❌ | Python 风格 AOP |
| 箭头函数自动绑定 this | ❌ | Vora 无 this 绑定问题，但闭包需手动 `let self` |

### 0.8 模块与工程化

| 特性 | 状态 | 说明 |
|------|------|------|
| `import` / `export` | ✅ | v0.21 |
| 命名导入 `import {a} from "m"` | ✅ | |
| 相对导入 `"./path"` | ✅ | |
| 标准库 `std/` | ⚠️ | math ✅, json ✅, **fs/os/regex 待实现** |
| 包管理器 | ❌ | 已在 3.3 规划 |
| C ABI / FFI | ❌ | 已在 2.10 规划 |

### 0.9 差距汇总 — 按严重度排序

#### 🔴 严重缺失（现代语言标配，Vora 完全没有）

| # | 缺失特性 | 对标语言 | 收益 |
|---|---------|---------|------|
| 1 | **空安全 `?.` + `??`** | TS/JS/C#/Swift/Kotlin | 消除 `== null` 嵌套检查，代码量 -30% |
| 2 | **资源自动释放 `defer`** | Go/Python(with)/C#(using) | 告别 finally 样板代码 |
| 3 | **访问控制 `private`** | 全部 OOP 语言 | 封装是 OOP 基础，无它对象只是命名空间 |
| 4 | **错误类型层级** | Java/Python/JS | catch 无法按类型过滤 = 错误处理盲抓 |
| 5 | **`do`-`while` 循环** | C/Java/JS/Go/Rust | 基础流程控制缺口 |

#### 🟡 中度缺失（竞争力提升关键）

| # | 缺失特性 | 对标语言 | 收益 |
|---|---------|---------|------|
| 6 | **Match 守卫 + 穷尽性检查** | Rust/Python/Scala | match 从"好用"到"可靠"的关键一步 |
| 7 | **推导式 (list/dict)** | Python/C#/Scala | 一行替代多层循环，数据处理核心体验 |
| 8 | **调用端展开 `...expr`** | JS/TS/Python | 与 rest 对称，解锁函数式模式 |
| 9 | **接口 / Trait** | Rust/Go/Java | 无继承多态，行为抽象 |
| 10 | **命名参数** | Python/TS/C# | `func(name="Vora", age=18)` 自文档化 |
| 11 | **标准库 `std/fs` + `std/os`** | 全部语言 | 无文件 I/O 无法做实际任务 |
| 12 | **元组 Tuple** | Python/Rust/TS/C# | 轻量不可变聚合，多返回值 |
| 13 | **Char 类型** | 全部语言 | 单字符与字符串语义分离 |

#### 🟢 轻度缺失（长期生态 / 加分项）

| # | 缺失特性 | 对标语言 | 收益 |
|---|---------|---------|------|
| 14 | 正则 `std/regex` | 全部语言 | 已在 2.3 规划 |
| 15 | C ABI / FFI | Lua/Python | 已在 2.10 规划 |
| 16 | 异步/协程 | JS/Python/Go | 已在 2.6 规划 |
| 17 | 调试器 DAP | 全部语言 | 已在 3.2 规划 |
| 18 | 包管理器 | npm/cargo/pip | 已在 3.3 规划 |
| 19 | 可选类型标注 | TS/Python(typing) | 渐进式类型安全 |
| 20 | 静态方法 / 类方法 | 全部 OOP 语言 | |

### 0.10 路线图更新 — 优先级重排

根据 docs/13 的行业标准审查，**Q3 2026 重点从"性能优化"转向"补齐语言标配缺口"**：

**新时间线（与旧版对比）：**

```
2026 Q3 (7-9月) — 补齐现代语言标配 🔴
├── 空安全 ?. + ??                    ← NEW 最高优先级
├── defer 资源释放                     ← NEW
├── 调用端展开 ...expr                 ← 移到 Q3 (极低成本)
├── 错误类型层级 + catch 按类型过滤    ← 从 Q3 保留
├── do-while 循环                     ← NEW (极低成本)
├── std/fs + std/os                   ← 从 Q3 保留
└── Match 守卫条件 + 穷尽性检查        ← NEW (提升 match 完成度)

2026 Q4 (10-12月) — 语言竞争力 ⭐
├── 列表/Dict 推导式                   ← 从 Q4 保留
├── 访问控制 private/public            ← NEW
├── 命名参数                          ← NEW
├── 参数解构 func f({x,y})            ← 从 Q4 保留
├── NaN-boxing                        ← 从 Q4 保留 (性能)
├── Superinstruction 合并              ← 从 Q4 保留 (性能)
└── std/regex                         ← 从 Q4 保留

2027 Q1 (1-3月) — 生产级能力 ⭐⭐
├── 接口 / Trait                       ← NEW (行为抽象基础)
├── 异步/协程                          ← 从 Q1 保留
├── GC 分代回收                        ← 从 Q1 保留
├── C ABI + FFI                        ← 从 Q1 保留
├── 调试器 DAP                         ← 从 Q1 保留
├── 元组 Tuple                         ← NEW
└── Char 独立类型                      ← NEW

2027 Q2+ — 生态建设
├── 常量池共享 + 内联                  ← 保留
├── 可选类型标注 (TS 模式)             ← NEW
├── 静态方法 / 类方法                  ← NEW
├── JIT 编译（研究阶段）               ← 保留
├── 包管理器                           ← 保留
└── 文档生成器                         ← 保留
```

> **设计原则**：Vora 定位"实用中间地带"——JavaScript 式语法 + Lua 级简洁 + Wren 风格 OOP。新增特性应服务于该定位，避免成为 C++/Rust 的臃肿克隆。

---

---

## 一、性能优化

### 1.1 NaN-boxing — Value 8 字节终极瘦身 ⭐⭐⭐

| 维度 | 当前 | 目标 |
|------|------|------|
| `sizeof(Value)` | 16 字节（GcString 迁移后） | **8 字节** |
| VM 栈每帧 256 locals | 4 KB | **2 KB** |
| L1 cache line (64B) Value 数 | 4 | **8** |
| 类型检查 | `std::holds_alternative` (跳转表) | 位运算 (2-3 指令) |

**方案**：IEEE 754 NaN-boxing — 所有非 NaN 的 64-bit 模式为 double，NaN 空间编码 null/bool/int/pointer。

**收益**：数值密集场景 2-5× 加速，cache 友好度翻倍。

**风险**：
- ~271 处 variant 访问需替换为位运算
- int64 → int51（溢出自动转 double）
- 依赖 GcObject 8 字节对齐（已满足）

**预估工期**：3-4 周

---

### 1.2 Superinstruction 合并 ⭐⭐

将高频连续 opcode 对合并为单条 superinstruction，减少 dispatch 开销：

| 合并前 | 合并后 | 出现频率 |
|--------|--------|---------|
| `OP_GET_LOCAL` + `OP_CALL` | `OP_CALL_LOCAL` | 极高（方法调用） |
| `OP_GET_GLOBAL` + `OP_CALL` | `OP_CALL_GLOBAL` | 高（函数调用） |
| `OP_GET_LOCAL` + `OP_GET_PROPERTY` | `OP_GET_LOCAL_PROP` | 高（链式访问） |

**收益**：解释循环 10-20% 加速。

**风险**：opcode 表膨胀；编译器需识别合并模式。

**预估工期**：1 周

---

### 1.3 GC 分代回收 ⭐⭐

| 维度 | 当前 | 目标 |
|------|------|------|
| 算法 | Mark-Sweep (全堆扫描) | Generational (minor GC 只扫新生代) |
| 触发频率 | 每 4MB 分配后全量 | Minor: 频繁轻量 / Major: 低频全量 |
| 写屏障 | 无 | 需 card-marking 或 remembered set |

**收益**：GC 暂停时间降低 5-10×，适合交互式场景。

**风险**：实现复杂度中等；需引入写屏障。

**预估工期**：2-3 周

---

### 1.4 常量池共享 + 字节码内联 ⭐

- 当前每个 Chunk 独立常量池 → 多个 Chunk 可共享字面量
- 小函数自动内联（消除 call/return 开销）

**收益**：内存 -15%，函数调用密集场景 15-25% 加速。

**预估工期**：2 周

---

### 1.5 JIT 编译（远期） ⭐

**方案**：Tiered compilation — 解释器 → 基线 JIT（模板化）→ 优化 JIT（方法级）。

参考：LuaJIT trace-based、Wren 方法级 JIT。

**收益**：数值密集循环 10-50× 加速。

**风险**：工程量大（>3 个月），需处理 deoptimization、GC 交互。

**建议**：NaN-boxing + superinstruction 之后再做，届时基础架构已优化到位。

---

## 二、语言特性

### 2.1 剩余参数 (Rest Parameters) ✅ 已完成 (v0.22)

```vora
func foo(a, b, ...rest) { return rest }
foo(1, 2, 3, 4)  // rest = [3, 4]
```

- `...name` 必须是最后一个参数
- 每个函数最多一个 rest
- rest 不能有默认值
- 适用于 func、lambda、Obj 构造函数和方法

**实现**：无新 opcode — rest 数组在 `callVoraFunction` / bound method / `runConstructor` 三个调用路径中直接创建。

---

### 2.2 调用端展开 (Spread at Call Site) ⭐⭐

```vora
func add(a, b, c) { return a + b + c }
let nums = [1, 2, 3]
add(...nums)  // 等价 add(1, 2, 3)
```

- `...expr` 在调用参数列表中展开数组为多个参数
- 可与普通参数混用：`add(0, ...nums, 4)`
- 仅需修改 `CallExpr` 参数解析 + 编译器 `visitCallExpr`

**收益**：与 2.1 对称，解锁函数式编程模式。

**预估工期**：1-2 天

---

### 2.3 标准库 ⭐⭐⭐

当前 `std/` 目录状态：

| 模块 | 状态 | 内容 |
|------|------|------|
| `std/math` | ✅ 已完成 | abs/min/max/floor/ceil/sqrt/sin/cos/random |
| `std/json` | ✅ 已完成 | parse/stringify |
| `std/fs` | 🔴 待实现 | readFile/writeFile/exists/listDir |
| `std/os` | 🔴 待实现 | env/getcwd/exit/shell |
| `std/regex` | 🔴 待实现 | match/replace（集成 PCRE2 或 RE2） |

**实现方式**：NativeFunction 注册，`import "std/fs"` 语法糖。

**预估工期**：1-2 周（fs + os）；regex 额外 1 周

---

### 2.4 数据结构：Set + Map ✅ 已完成 (v0.23)

Set（`std::unordered_set<Value>`）和 Map（`std::unordered_map<Value, Value>`），支持任意类型 key 的 O(1) 操作。

```vora
// Set — 去重、成员检测 O(1)
let s = Set([1, 2, 3])
s.has(2)       // true
s.add(4)

// Map — 任意类型 key + 索引语法
let m = Map()
m["key"] = 42
m["key"]       // 42
m.set(123, "value")
m.keys()       // 所有 key 的 Array

// 支持 + 运算：Set+Set 并集，Map+Map 合并
// 支持 for-in 遍历：Set 返回元素，Map 返回 key
```

**实现**：
- `ValueHash` + `ValueEqual` functor — 内容哈希（容器递归）+ 指针身份（函数/对象）
- `Set` / `Map` 结构体为 `GcObject`，GC 自动追踪 key 和 value 引用
- `Set()` 构造函数接受 Array/String/Set/Dict 作为可迭代参数
- 方法通过 `OP_GET_PROPERTY` 分发（`add`/`has`/`remove`/`clear`/`values`/`set`/`get`/`keys`）
- Map 支持 `m[key]` / `m[key] = val` 索引语法（`OP_INDEX`/`OP_SET_INDEX`）
- 迭代通过 `iter()`/`next()` 内置函数 + `Iterator::valueKeys` 快照

**预估工期**：1 周 ✅ 已完成

---

### 2.5 模块/导入系统 ✅ 已完成 (v0.21)

```vora
import "math"                        // 导入全部
import { sin, cos } from "math"     // 命名导入
```

- 解析 import → 定位文件 → 编译为独立 Chunk → 作为模块对象暴露
- 循环依赖检测
- 与标准库目录 `std/` 集成

---

### 2.6 异步/协程 ⭐⭐⭐

当前 Vora 纯同步。generator（`yield`）已实现暂停/恢复，可作为协程基础：

```vora
// 基于 generator 的 async/await
func fetchData(url) {
    let resp = await http.get(url)  // 不阻塞 VM，挂起等待
    return resp.body
}

// 并发执行
let results = await Promise.all([
    fetchData("/api/users"),
    fetchData("/api/posts"),
])
```

**方案**：
1. 基于现有 generator 实现协程（generator 已支持 yield/暂停/恢复）
2. 添加事件循环（timer、I/O 就绪回调）
3. `async func` 语法糖 → 自动返回 `Promise`
4. `await` 表达式 → 挂起当前协程直到 Promise resolve

**收益**：服务端/网络编程核心竞争力。

**预估工期**：2-3 周

---

### 2.7 模式匹配 ✅ 已完成 (v0.24) — v1

```vora
let x = match n {
    1 => "one",
    2 | 3 => "small",
    1..=5 => "low",
    _ => "default"
}
```

**v1 已实现：**
- 字面量模式（数字、字符串、布尔、null）
- 通配符 `_`
- 多模式 `|`（解析器支持，编译器通过多模式 OR 组合实现）
- 范围模式 `..=`（含等）和 `..`（不含等）
- 表达式体和块体 `{ stmts; lastExpr }`
- match 可作为表达式或语句使用

**实现细节：**
- 脱糖为 if-else 链 + 跳转回填，无新 opcode 需求
- 使用编译器内部全局临时变量存储 scrutinee（静态计数器保证跨嵌套编译器唯一性）
- 块体返回最后一条表达式语句的值（自动跳过 OP_POP）

**延后 (v1.1+)：** 解构模式、守卫条件 (`if cond`)、穷尽性检查

**预估工期**：2 周 ✅ 已完成

---

### 2.8 迭代器协议 ✅ 已完成 (v0.21)

```vora
let gen = iter(countTo(3))
print(next(gen))  // 1
print(next(gen))  // 2
print(next(gen))  // 3
// next(gen) 抛出 StopIteration
```

- `iter()` / `next()` 内建函数
- `yield` 生成器（暂停/恢复状态）
- Array/Dict/String 自动可迭代
- `StopIteration` 异常可被 catch 捕获

---

### 2.9 错误类型层级 ⭐⭐

```vora
Obj HttpError(code, message) : Error
Obj RangeError(msg) : Error

try { ... }
catch (e if HttpError) { print(e.code) }
catch (e) { print("unknown") }
```

- 内建 Error 类型 + 子类化
- 按类型捕获
- 堆栈追踪附加到 Error 对象

**预估工期**：1 周

---

### 2.10 C ABI / FFI ⭐⭐

Vora 当前仅 C++ embedding API。需要 C ABI 以达到 Lua 级别的嵌入能力：

```c
// C ABI — 任何语言都能宿主
VoraVM* vm = vora_create_vm();
vora_eval(vm, "print('hello')");
VoraValue result = vora_call(vm, "myFunc", 2, args);
vora_destroy_vm(vm);
```

**方案**：
1. 导出 `extern "C"` 薄封装层（`vora_c_api.h`）
2. 后续扩展：FFI 调用外部 `.dll`/`.so` 中的任意 C 函数

**收益**：嵌入到 C/Python/Rust/Go/Node.js 等任何支持 C ABI 的宿主。

**预估工期**：1-2 周（C ABI 薄封装）；FFI 额外 2 周

---

### 2.11 解构赋值 ✅ 已完成 (v0.22)

```vora
let [a, b] = [1, 2]           // 数组解构
let {name, age} = person       // 对象解构 (简写)
let {x: a, y: b} = obj         // 对象解构 (重命名)
let [first, ...rest] = arr     // 解构 + 剩余 (函数内)
let [a, [b, c]] = [1, [2, 3]]  // 嵌套解构
```

**已实现**：
- `let` / `const` 声明解构 (数组 `[...]` + 对象 `{...}`)
- 嵌套模式 (数组嵌套数组、对象嵌套对象、交叉嵌套)
- 数组 rest (`...rest`，函数内编译为 while 循环切片)
- 对象简写 (`{x}` ≡ `{x: x}`) 和重命名 (`{x: a}`)
- 全局/局部作用域均支持

**实现**：新建 `BindingPattern` 层次结构 (IdentifierBinding / ArrayBinding / ObjectBinding)，不依赖 Expr/Stmt 体系。编译器递归脱糖为 INDEX / GET_PROPERTY + addLocal / defineGlobal，无新 opcode。

**待完善 (v0.23)**：对象 rest (`{x, ...rest}`)、参数解构 (`func f({x,y})`)、解构赋值表达式 (`[a,b] = arr` 无 let)

---

### 2.12 列表/Dict 推导式 ⭐

```vora
let evens = [x for x in range(10) if x % 2 == 0]
let squares = {x: x*x for x in [1,2,3]}  // {1:1, 2:4, 3:9}
```

**预估工期**：1 周（AST 展开为 for-in + if + push）

---

## 三、工具链

### 3.1 LSP 服务器 ✅ 已完成

基于现有 lexer/parser/compiler，Vora-LSP 仓库（`D:\Vora-LSP`）提供：

- 诊断（编译错误实时提示）
- 补全（变量/函数/方法/属性）
- 跳转定义 / 查找引用
- 悬停类型提示
- 格式化（已有 SourceFormatter）
- VS Code 扩展

前置工作（全部完成）：
- ✅ ErrorReporter 抽象 + DiagnosticCollector（`src/common/`）
- ✅ 错误容忍解析（ErrorExpr/ErrorStmt + synchronize() + 括号深度跟踪）
- ✅ JSON-RPC 库（`src/json_rpc/`：parse/serialize + stdio transport + message router）
- ✅ 语义分析 Pass

---

### 3.2 调试器协议 (DAP) ⭐⭐

Debug Adapter Protocol 实现：

- 断点（行断点、条件断点）
- 单步（step in/out/over）
- 变量查看（stack frame locals/globals）
- 调用栈显示

**VM 侧**：在 `OP_DEBUG_BREAK` 处挂起，等待 DAP 命令。

**预估工期**：3 周

---

### 3.3 包管理器 ⭐

```bash
vora init          # 创建项目骨架
vpm install json   # 安装依赖
vora build         # 构建项目
vora run           # 运行主入口
```

- 集中注册表（GitHub-based）
- 语义化版本
- 锁文件

**预估工期**：4-6 周（长期工程）

---

### 3.4 文档生成器 ⭐

从源码注释生成 API 文档。

**预估工期**：1 周

---

## 四、质量工程

### 4.1 性能基准套件 ⭐⭐

```bash
cmake --preset windows-x64-release
./build/windows-x64-release/Release/Vora.exe benchmarks/
```

- 数值循环（fibonacci/matrix/sieve）
- 对象分配压力
- 函数调用深度
- GC 暂停时间
- 与 Lua/Python/JavaScript 对比

**预估工期**：1 周

---

### 4.2 Fuzzer 增强 ⭐⭐

- 当前已有基础 fuzzer（`tests/fuzz/`），但 discoverability 低
- 增加结构化 fuzzer（语法感知，生成合法 Vora 程序）
- Differential fuzzing（与自身旧版本对比）

**预估工期**：1-2 周

---

### 4.3 测试覆盖率提升 ⭐

- 当前 52 C++ 单元测试（~800 断言）+ 124 语言测试 + 44 示例
- 缺少：边界值测试、并发/压力测试、回归套件自动化
- 目标：行覆盖率 >85%

**预估工期**：持续

---

### 4.4 CI 矩阵扩展 ⭐

| 当前 | 建议增加 |
|------|---------|
| Linux x64 (GCC) | macOS ARM64 |
| Linux x64 (Clang sanitizers) | Windows ARM64 |
| Windows x64 (MSVC) | Linux x86 32-bit |
| | MSVC Release 构建 |

**预估工期**：1 周

---

## 五、时间线建议

```
2026 Q3 (7-9月) — 补齐基础设施 ⭐
├── 2.2 调用端展开 ...expr          ← 与 rest 对称，极低成本
├── 2.3 标准库 std/fs + std/os      ← 文件 I/O + 系统调用
├── 2.4 Set + Map 数据结构           ← ✅ 已完成 (v0.23)
├── 2.9 错误类型层级                  ← Error 基类 + 按类型 catch
├── 2.10 C ABI 导出                  ← Lua 级嵌入能力
├── 2.11 解构赋值                    ← ✅ 已完成 (v0.22)
└── 4.1 性能基准套件

2026 Q4 (10-12月) — 语言竞争力 ⭐⭐
├── 1.1 NaN-boxing                   ← 2-5× 数值性能
├── 1.2 Superinstruction 合并        ← 10-20% 解释器加速
├── 2.3 std/regex                    ← 正则表达式（PCRE2）
├── 2.7 模式匹配                     ← ✅ 已完成 (v0.24)
├── 2.12 列表/Dict 推导式            ← [x for x in arr if ...]
└── 2.11 解构赋值完善 (参数解构)       ← func f({x,y})

2027 Q1 (1-3月) — 生产级能力 ⭐⭐⭐
├── 2.6 异步/协程                    ← async/await + 事件循环
├── 1.3 GC 分代回收                  ← 暂停时间 1/10
├── 3.2 调试器 DAP                   ← 断点 + 单步 + 变量查看
├── 4.2 Fuzzer 增强
└── 4.4 CI 矩阵扩展

2027 Q2+ — 生态建设
├── 1.4 常量池共享 + 内联
├── 1.5 JIT 编译（研究阶段）
├── 3.3 包管理器
├── 3.4 文档生成器
└── 4.3 测试覆盖率持续提升
```

---

## 六、语言缺陷与限制

> 本节记录 Vora 当前存在的语法歧义、运行时限制、设计缺憾。区别于"技术债务"（实现质量问题），这些是**语言层面的缺陷**——用户写代码时会直接撞到。

### 6.1 语法缺陷

| 缺陷 | 严重度 | 说明 | 示例 |
|------|--------|------|------|
| **`return` 必须带值** | ✅ 已修复 | 现已支持 `return` / `return;` (void return) | `func f() { return }` ✅ |
| **一元负号绑定强于下标** | ✅ 已修复 | `-a[i]` 正确解析为 `-(a[i])` | `-nums[0]` ✅ |
| **控制流关键字后不能有分号** | ✅ 已修复 | `break;` / `continue;` / `return;` 现已支持 | `break;` ✅ |
| **不支持 C 风格 `for` 循环** | ✅ 已修复 | 现已支持 `for (let i=0; i<n; i=i+1) {...}` | ✅ |
| **不支持匿名函数表达式** | ✅ 已修复 | 现已支持 `func(x) { return x * 2 }` lambda | ✅ |
| **类定义关键字为 `Obj`（非直觉）** | 🟢 低 | `class` / `obj` (小写) 均为未定义变量，只有 `Obj` (大写 O) 是关键字 | `Obj Point(x, y) { ... }` |

### 6.2 闭包与递归限制

| 限制 | 严重度 | 说明 |
|------|--------|------|
| **闭包修改 + 局部递归 = VM 挂死** | ✅ 已修复 | Upvalue 间接引用（shared_ptr\<Upvalue\> 共享栈槽指针）修复 |
| **局部函数自递归边缘情况** | ✅ 已验证 | 12 项边缘测试覆盖 |
| **无尾调用优化 (TCO)** | ✅ 已修复 | `OP_TAIL_CALL` 复用当前帧，支持无限尾递归 |
| **局部函数互递归 (mutual recursion)** | 🟡 中 | 同一作用域内两个局部函数互相调用需要前向声明；全局函数互递归已支持 |
| **Upvalue 裸指针悬垂风险** | ✅ 已修复 | 基于索引的 Upvalue（vector\<Value\>* \+ slotIndex） |

### 6.3 类型系统缺陷

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **无 `const` / 不可变语义** | ✅ 已修复 | 编译期拒绝 `=`、`+=`、`++`/`--` 对 const 变量的修改 |
| **无类型标注** | 🟡 中 | 纯动态类型；参数类型错误只在运行时暴露 |
| **无泛型** | 🟢 低 | 无法参数化类型：`func identity(x) { return x }` 已是极限 |
| **`Value` 类型不可扩展** | 🟡 中 | `std::variant` 封闭了运行时类型集合；用户无法注册新类型 |

### 6.4 数据结构缺口

| 缺口 | 严重度 | 说明 |
|------|--------|------|
| **无 `Set` / `Map`** | 🔴 高 | 只有 `Array`（O(n)）和 `Dict`（仅字符串 key）。→ 已列入 2.4 |
| **`Dict` 为纯字符串键** | 🟡 中 | 无法用整数、对象等作为字典 key。→ 随 Map 一起解决 |
| **无 `Array.sort` / 内建排序** | 🟡 中 | 每次排序必须手写 |
| **字符串不可变且拼接低效** | 🟡 中 | 每次 `+` 都重新分配；高频拼接生成大量 GC 垃圾 |
| **无 `Array.indexOf` 返回 -1 的信号一致性检查** | 🟢 低 | 存在但无编译期提醒 |

### 6.5 标准库空白

| 缺口 | 状态 | 说明 |
|------|------|------|
| **`std/` 目录** | 🟡 部分完成 | `std/math.va` ✅、`std/json.va` ✅ |
| **文件 I/O** | 🔴 待实现 | 无法读/写文件。→ 已列入 2.3 |
| **JSON 解析/序列化** | ✅ 已完成 | `import "std/json"` |
| **数学函数** | ✅ 已完成 | `import "std/math"` |
| **正则表达式** | 🔴 待实现 | 无字符串模式匹配。→ 已列入 2.3 |
| **日期/时间** | 🔴 待实现 | 无时间戳或日期运算 |
| **网络/HTTP** | 🔴 待实现 | 无 |
| **`import` / 模块系统** | ✅ 已完成 | `import "module"` + `import { a, b } from "module"` |

### 6.6 工具链缺失

| 缺口 | 状态 | 说明 |
|------|------|------|
| **LSP 服务器** | ✅ 已完成 | Vora-LSP 仓库：诊断、补全、跳转、悬停、格式化 |
| **调试器 (DAP)** | 🔴 待实现 | 无法设断点、单步执行。→ 已列入 3.2 |
| **包管理器** | 🔴 待实现 | 无法安装/发布/版本化第三方库。→ 已列入 3.3 |
| **错误消息质量** | 🟡 改善中 | 已支持源码行 + ^ 插入符位置标记 |
| **profiler / 性能剖析器** | 🔴 待实现 | 无法量化哪段代码消耗最多时间 |

### 6.7 互操作性缺陷

| 缺陷 | 状态 | 说明 |
|------|------|------|
| **仅 C++ embedding API** | 🔴 待改进 | 无 C ABI 导出。→ 已列入 2.10 |
| **无 FFI** | 🔴 待实现 | 无法直接调用 C 动态库。→ 已列入 2.10 |
| **无跨进程序列化** | 🔴 待实现 | 无法将 `Value` 导出为二进制/字节码 |

### 6.8 I/O 与输入缺陷

> 本节涵盖 `input()` 内置函数实现缺陷、REPL 交互限制，以及 stdin 测试基础设施缺口。

#### 6.8.1 `input()` 内置函数缺陷

| 缺陷 | 状态 | 说明 |
|------|------|------|
| **EOF 无信号** | ✅ 已修复 | EOF 或流错误时 `input()` 返回 `null`，正常空行返回 `""` |
| **prompt 未 flush** | ✅ 已修复 | 已添加 `std::flush` |
| **无输入类型转换辅助** | 🟢 低 | 缺少 `readInt()`、`readFloat()` 等便捷函数 |
| **无输入超时 / 非阻塞读** | 🟡 中 | `std::getline` 无限阻塞 |
| **仅读一行** | 🟢 低 | 无 `readAll()` 或 `read(n)` 变体 |

#### 6.8.2 REPL 交互缺陷

| 缺陷 | 状态 | 说明 |
|------|------|------|
| **不支持多行输入** | ✅ 已修复 | 跟踪 `{}` 嵌套深度，`> ` → `... ` 提示符切换 |
| **无行编辑 / 历史** | 🟡 中 | 无 readline/editline。可集成 GNU readline |
| **局部变量跨行丢失** | ✅ 已修复 | `seedGlobals()` 合并 VM 现有全局表 |
| **无法中断运行中的代码** | ✅ 已修复 | SIGINT → `Interrupted` → `> ` 继续 |
| **无会话持久化** | 🟢 低 | 退出 REPL 后状态丢失 |

#### 6.8.3 输入测试基础设施

| 缺陷 | 状态 | 说明 |
|------|------|------|
| **`input()` 无自动化测试** | ✅ 已修复 | `test_input.va` + stdin 管道支持 |
| **REPL 无可测试性** | 🟢 低 | 可引入 expect-style 测试 |

#### 6.8.4 缺失的 I/O 功能

| 缺失 | 严重度 | 说明 |
|------|--------|------|
| **语言内无文件读取 API** | 🟡 中 | → 已列入 2.3 `std/fs` |
| **无 `print` 重定向** | 🟢 低 | `print()` 始终写入 stdout |

### 6.9 docs/13 审查新发现的缺口

> 基于 `docs/13-标准与反思.md` 对现代语言标准的审查，以下缺口此前未被 roadmap 追踪。

#### 6.9.1 现代语法标配缺失

| 缺口 | 严重度 | 对标语言 | 说明 |
|------|--------|---------|------|
| **空安全 `?.` + `??`** | 🔴 高 | TS/JS/C#/Swift/Kotlin | 无可选链和空值合并，多层判空 if 嵌套严重降低可读性 |
| **`do`-`while` 循环** | 🔴 高 | C/Java/JS/Go/Rust | 基础流程控制缺失，先执行后判断场景需手动 while 模拟 |
| **资源自动释放 (`defer`/`with`)** | 🔴 高 | Go/Python/C# | 无 finally 替代方案，文件/锁等资源管理依赖用户手写 try/finally |
| **访问控制 (`private`)** | 🔴 高 | 全部 OOP 语言 | 所有对象成员公开，封装仅是命名约定 |
| **命名参数** | 🟡 中 | Python/TS/C# | 函数调用缺乏自文档化能力 |
| **调用端展开 `...expr`** | 🟡 中 | JS/TS/Python | 定义端 rest 已实现但调用端未对称，已列入 2.2 |
| **列表/Dict 推导式** | 🟡 中 | Python/C#/Scala | 已列入 2.12，数据处理核心体验 |
| **Match 守卫 `if` + 穷尽性** | 🟡 中 | Rust/Python | Match 功能完整度不够，已列入 2.7 延后 |
| **参数解构 `func f({x,y})`** | 🟡 中 | JS/TS | 函数签名中直接解构，已列入 2.11 待完善 |
| **装饰器 `@`** | 🟢 低 | Python | AOP 语法糖，非核心 |

#### 6.9.2 类型与数据结构缺口

| 缺口 | 严重度 | 对标语言 | 说明 |
|------|--------|---------|------|
| **元组 Tuple** | 🟡 中 | Python/Rust/TS/C# | 轻量不可变聚合，多返回值场景更优雅 |
| **Char 独立类型** | 🟢 低 | 全部语言 | 单字符用 String 代替，语义模糊且性能差 |
| **可选类型标注** | 🟢 低 | TS/Python(typing) | 渐进式类型安全，长期加分项 |

#### 6.9.3 OOP 体系缺口

| 缺口 | 严重度 | 对标语言 | 说明 |
|------|--------|---------|------|
| **接口 / Trait** | 🟡 中 | Rust/Go/Java | 行为抽象基础，无继承多态 |
| **抽象方法 / 抽象类** | 🟡 中 | 全部 OOP | 无强制子类实现机制 |
| **静态方法 / 类方法** | 🟢 低 | 全部 OOP | 无 `static func` |
| **方法重载** | 🟢 低 | Java/C++/TS | 同名方法以最后定义覆盖 |

#### 6.9.4 错误处理缺口

| 缺口 | 严重度 | 对标语言 | 说明 |
|------|--------|---------|------|
| **错误类型层级** | 🔴 高 | Java/Python/JS | 无 Error 基类，catch 无法按类型过滤，已列入 2.9 |
| **`catch (e if Type)`** | 🔴 高 | Java/Python | 语法已规划随 2.9 |
| **错误传播运算符** | 🟢 低 | Rust `?` | Vora 有 try/catch，非必需 |

#### 6.9.5 集合操作缺口

| 缺口 | 严重度 | 对标语言 | 说明 |
|------|--------|---------|------|
| **链式调用 / 流式遍历** | 🟡 中 | JS/Python/C# | 无 `.filter().map()` 等，数据处理需手写循环 |
| **内建排序 `Array.sort`** | 🟡 中 | 全部语言 | 每次排序必须手写，已列入 6.4 |

---

| 条目 | 优先级 | 说明 |
|------|--------|------|
| `GcPtr` 无 null-check 语义 | 低 | 所有 `GcPtr` 解引用前应 assert non-null |
| `valueToString` 递归深度无限制 | 中 | 深层嵌套 Array/Dict 可能栈溢出 |
| `addValues` 字符串拼接每次分配 | 中 | 可引入 string builder 减少 GC 压力 |
| `const` 语义（仅编译期） | 低 | 运行期无 const 强制；闭包捕获后安全 |
| Chunk 常量池线形去重 | 低 | O(n²) 查找，大程序编译变慢 |
