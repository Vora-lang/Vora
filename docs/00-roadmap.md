# Vora 优化路线图

> 最后更新：2026-06-15
> 基于对代码库的全面审计，按收益/成本比排序。

---

## 目录

1. [性能优化](#一性能优化)
2. [语言特性](#二语言特性)
3. [工具链](#三工具链)
4. [质量工程](#四质量工程)
5. [时间线建议](#五时间线建议)
6. [语言缺陷与限制](#六语言缺陷与限制)
   - 6.8 [I/O 与输入缺陷](#68-io-与输入缺陷)

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
| `OP_CONSTANT` + `OP_GET_LOCAL` | — | 中 |
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

### 2.1 标准库 ⭐⭐⭐

当前 `std/` 目录为空。建议首批实现：

| 模块 | 内容 | 优先级 |
|------|------|--------|
| `std/math` | abs/min/max/floor/ceil/sqrt/sin/cos/random | 高 |
| `std/fs` | readFile/writeFile/exists/listDir | 高 |
| `std/json` | parse/stringify | 中 |
| `std/os` | env/getcwd/exit/shell | 中 |
| `std/regex` | match/replace | 低 |

**实现方式**：NativeFunction 注册，`import "std/math"` 语法糖。

**预估工期**：2-3 周（含 import 语法）

---

### 2.2 模块/导入系统 ⭐⭐⭐

```vora
import "math"         // 导入全部
import { sin, cos } from "math"  // 命名导入
```

- 解析 import → 定位文件 → 编译为独立 Chunk → 作为模块对象暴露
- 循环依赖检测
- 与标准库目录 `std/` 集成

**预估工期**：2 周

---

### 2.3 模式匹配 ⭐⭐

```vora
match value {
    {type: "error", code} => handleError(code)
    0 | null | false       => handleFalsy()
    n if n > 0             => handlePositive(n)
    else                   => handleDefault()
}
```

- 结构化匹配（Dict/Array/Object 解构）
- 守卫条件 (if 子句)
- 穷尽性检查

**预估工期**：2 周

---

### 2.4 迭代器协议 ⭐⭐

```vora
// 自定义可迭代对象
Obj Range(start, end) {
    func iter() {
        // 返回迭代器对象，含 next() 方法
    }
}
for (let x in Range(1, 10)) { print(x) }
```

- 统一 `for-in` 背后的迭代协议
- Array/Dict/String/Object 自动实现
- 惰性求值（map/filter/zip 链）

**预估工期**：1-2 周

---

### 2.5 错误类型层级 ⭐

```vora
Obj TypeError(msg) : Error
Obj RangeError(msg) : Error
try { ... } catch (e if TypeError) { ... }
```

- 内建 Error 类型 + 子类化
- 按类型捕获
- 堆栈追踪附加到 Error 对象

**预估工期**：1 周

---

## 三、工具链

### 3.1 LSP 服务器 ⭐⭐⭐

基于现有 lexer/parser/compiler，实现 Language Server Protocol：

**目标功能**：
- 诊断（编译错误实时提示）
- 补全（变量/函数/方法/属性）
- 跳转定义 / 查找引用
- 悬停类型提示
- 格式化（已有 formatter）

**预估总工期**：6-8 周（含前置工作 3-5 周 + 协议实现 2-3 周）

#### 3.1.1 前置工作（阻塞级 — 必须先行完成）

##### #1 ErrorReporter 抽象（2-3 天）
将分散在各组件中的 `printSourceLine(std::cerr, ...)` 调用统一为 `ErrorReporter` 接口：
- 创建 `Severity` 枚举、`Diagnostic` 结构体、`ErrorReporter` 抽象基类
- 提供 `StderrErrorReporter`（保持现有 CLI 行为）
- Lexer、Parser、Compiler、VM 全部通过 `ErrorReporter&` 报告错误
- 为 LSP 的 `textDocument/publishDiagnostics` 打下基础

##### #2 错误容忍解析 + AST 保留（3-5 天）
当前 Parser 在 `hadError` 时返回 `nullptr`，AST 全部丢弃。LSP 需要：
- 引入 `ErrorExpr` / `ErrorStmt` 占位节点
- 增强 `synchronize()` 恢复粒度
- 即使有语法错误也保留尽可能多的 AST 节点
- 支持补全、跳转等需要部分 AST 的场景

##### #3 JSON-RPC 库引入（1 天）
项目目前零 JSON 处理能力。LSP 基于 JSON-RPC 2.0：
- 引入 nlohmann/json（header-only，MIT 协议）
- 实现 stdio 传输层（Content-Length 头部解析）
- 请求/响应/通知消息路由框架

##### #4 独立语义分析 Pass（5-7 天）
当前变量解析、作用域检查、const 检查全部夹杂在 Compiler 的字节码生成中。需要抽取：
- `SemanticAnalyzer : ExprVisitor<void>, StmtVisitor<void>, ProgramVisitor<void>`
- `SymbolTable`（作用域链：`{name → Definition}`）
- 引用收集（每个符号的定义点 + 引用点列表）
- 类型信息推断（从类型标注和使用模式）
- 诊断产出（未定义变量、未使用变量、类型不匹配…）

#### 3.1.2 重要前置（影响功能完整度）

##### #5 UTF-16 位置映射（1-2 天）
LSP 要求 UTF-16 码元偏移，Vora 内部使用 1-based line + column（UTF-8 字节）：
- `TextPosition` 双向映射
- 对 ASCII 源码恒等，非 ASCII 需处理

##### #6 Per-Document 状态管理（2-3 天）
- `DocumentState`：URI、源码、Token[]、AST、SymbolTable、Diagnostic[]
- 增量更新支持（`textDocument/didChange`）
- 常驻内存、响应增量变更

##### #7 跨文件模块索引（3-4 天）
- `ModuleIndex`：不执行代码解析 import 路径、提取导出符号
- `resolveModulePath()` 复用
- `workspaceSymbol` 查询基础

#### 3.1.3 协议实现（前置完成后）

| LSP 方法 | 依赖的前置工作 | 工作量 |
|----------|--------------|--------|
| `textDocument/publishDiagnostics` | #1, #4 | 2 天 |
| `textDocument/completion` | #1, #2, #4 | 3 天 |
| `textDocument/definition` | #1, #2, #4, #7 | 2 天 |
| `textDocument/references` | #1, #2, #4, #7 | 2 天 |
| `textDocument/hover` | #1, #2, #4 | 2 天 |
| `textDocument/formatting` | 已有 formatter | 1 天 |
| `textDocument/documentSymbol` | #2, #4 | 1 天 |
| `workspace/symbol` | #4, #7 | 1 天 |
| `initialize` / `shutdown` / 生命周期 | #3 | 2 天 |

#### 3.1.4 实施路线

```
第 1 周：  #1 ErrorReporter 抽象 + #3 JSON-RPC 引入
第 2 周：  #2 错误容忍解析 (前半)
第 3 周：  #2 错误容忍解析 (后半) + #4 语义分析 (前半)
第 4 周：  #4 语义分析 (后半) + #5 位置映射
第 5 周：  #6 Document 状态管理 + #7 跨文件索引
第 6-7 周：协议实现（diagnostics → completion → goto-def → hover）
第 8 周：  测试、打磨、编辑器插件（VS Code extension）
```

---

### 3.2 调试器协议 ⭐⭐

Debug Adapter Protocol (DAP) 实现：

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
vpm install json  # 安装依赖
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

- 当前 228 C++ 单元测试 + 23 语言测试 + 29 示例
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
2026 Q3 (7-9月)
├── 2.1 标准库（math/fs/json）
├── 2.2 模块/导入系统
├── 4.1 性能基准套件
├── 4.4 CI 矩阵扩展
└── 1.2 Superinstruction 合并

2026 Q4 (10-12月)
├── 1.1 NaN-boxing
├── 2.4 迭代器协议
├── 2.5 错误类型层级
├── 3.4 文档生成器
└── 4.3 测试覆盖率提升

2027 Q1 (1-3月)
├── 1.3 GC 分代回收
├── 2.3 模式匹配
├── 3.1 LSP 服务器
└── 4.2 Fuzzer 增强

2027 Q2+
├── 1.4 常量池共享 + 内联
├── 3.2 调试器协议
├── 3.3 包管理器
└── 1.5 JIT 编译（研究阶段）
```

---

## 六、语言缺陷与限制

> 本节记录 Vora 当前存在的语法歧义、运行时限制、设计缺憾。区别于"技术债务"（实现质量问题），这些是**语言层面的缺陷**——用户写代码时会直接撞到。

### 6.1 语法缺陷

| 缺陷 | 严重度 | 说明 | 示例 |
|------|--------|------|------|
| **`return` 必须带值** | ~~🔴 高~~ ✅ 已修复 | ~~不支持 void return；空返回必须写 `return null`。~~ 现已支持 `return` / `return;` (void return) | `func f() { return }` ✅ / `func f() { return; }` ✅ |
| **一元负号绑定强于下标** | ~~🔴 高~~ ✅ 已修复 | ~~`-a[i]` 被解析为 `(-a)[i]`（先对数组 a 取负，再下标），而非 `-(a[i])`。~~ 现已修复：`-a[i]` 正确解析为 `-(a[i])`，一元 `-` / `!` 的操作数改用 `call()` 解析，后置运算符绑定更紧 | `-nums[0]` ✅ → `-(nums[0])` |
| **控制流关键字后不能有分号** | ~~🟡 中~~ ✅ 已修复 | ~~`break;` / `continue;` / `return;` (无值) 即报解析错误。~~ `break;` / `continue;` / `return;` 现已支持（分号可选） | `break;` ✅ / `break` ✅ |
| **不支持 C 风格 `for` 循环** | ~~🟡 中~~ ✅ 已修复 | ~~必须用 `for i in range(...)` / `for v in arr` 替代。~~ 现已支持 `for (let i=0; i<n; i=i+1) { ... }`，与 for-in 并存，通过 `for (` vs `for IDENTIFIER` 区分 | `for (let i=0; i<5; i=i+1) {...}` ✅ |
| **不支持匿名函数表达式** | ~~🟡 中~~ ✅ 已修复 | ~~所有函数必须有名字。~~ 现已支持 `func(x) { return x * 2 }` lambda 表达式，可用作值传递、赋值给变量、作为回调、支持闭包捕获 | `let f = func(x) { return x * 2 }` ✅ |
| **类定义关键字为 `Obj`（非直觉）** | 🟢 低 | `class` / `obj` (小写) 均为未定义变量，只有 `Obj` (大写 O) 是关键字：特性 | `Obj Point(x, y) { ... }` |

### 6.2 闭包与递归限制

| 限制 | 严重度 | 说明 |
|------|--------|------|
| **闭包修改 + 局部递归 = VM 挂死** | ~~🔴 高~~ ✅ 已修复 | ~~同时在一个局部函数内修改捕获变量且递归调用自身，VM 进入无限循环。~~ Upvalue 间接引用（shared_ptr&lt;Upvalue&gt; 共享栈槽指针）修复了此问题。闭包修改和局部递归可安全组合使用 | `x = x + 1; return inner(n-1)` ✅ |
| **局部函数自递归边缘情况** | ~~🟡 中~~ ✅ 已验证 | ~~函数名在编译函数体之前预分配为局部变量；边缘情况未充分测试。~~ 已添加 12 项边缘测试覆盖：嵌套递归、多 upvalue 捕获、闭包返回自递归、Ackermann、try/catch 内递归、深递归(50层)等 | `func f() { func fact(n) { ... fact(n-1) } }` ✅ |
| **无尾调用优化 (TCO)** | ~~🟡 中~~ ✅ 已修复 | ~~深度递归必然栈溢出；CPS 转换风格不现实。~~ 现已实现 TCO：当 `return` 后直接跟函数调用且不在 try/finally 内时，编译器发射 `OP_TAIL_CALL` 复用当前调用帧。支持无限尾递归（已验证 10000 层），对非 Vora 函数优雅降级为常规调用 | `return f(args)` ✅ |
| **局部函数互递归 (mutual recursion)** | 🟡 中 | 同一作用域内两个局部函数互相调用需要前向声明；全局函数互递归已支持。局部互递归需编译器两遍扫描（先收集函数名再编译体），当前单遍编译器不支撑 | `func a(){b()} func b(){a()}` ❌ (局部，需全局定义) |
| **Upvalue 裸指针悬垂风险** | ~~🟡 中~~ ✅ 已修复 | ~~Upvalue 以原始 Value* 指向栈向量；若栈因 push_back 扩容重分配，指针全部作废。~~ 已改为基于索引的访问：Upvalue 存储 `std::vector&lt;Value&gt;*` + `size_t slotIndex` + `bool isClosed`，栈向量重分配不影响索引有效性 |

### 6.3 类型系统缺陷

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **无 `const` / 不可变语义** | ~~🟡 中~~ ✅ 已修复 | ~~变量永远可变；无法表达"此值不应被修改"的意图。~~ 现已支持 `const` 关键字声明不可变绑定。编译期拒绝 `=`、`+=`、`++`/`--` 对 const 局部变量、全局变量、上值捕获变量的修改 | `const x = 5; x = 10;` ❌ 编译错误 |
| **无类型标注** | 🟡 中 | 纯动态类型；参数类型错误只在运行时暴露；无编译期检查：特性 |
| **无泛型** | 🟢 低 | 无法参数化类型：`func identity(x) { return x }` 已是极限 |
| **`Value` 类型不可扩展** | 🟡 中 | `std::variant` 封闭了运行时类型集合；用户无法注册新类型（如 `Date`），只能通过 `Obj` 模拟：特性 |

### 6.4 数据结构缺口

| 缺口 | 严重度 | 说明 |
|------|--------|------|
| **无 `Set` / `Map`** | 🔴 高 | 只有 `Array`（线形查找 O(n)）和 `Dict`（字符串 key）。整数 key 的 Map 和去重 Set 必须绕路手写 |
| **`Dict` 为纯字符串键** | 🟡 中 | 无法用整数、对象等作为字典 key |
| **无 `Array.sort` / 内建排序** | 🟡 中 | 每次排序必须手写冒泡/插入；`tests/ans/` 里大量算法题文件均自己排序 |
| **字符串不可变且拼接低效** | 🟡 中 | 每次 `+` 都重新分配新 `GcString`；高频拼接生成大量 GC 垃圾 |
| **无 `Array.indexOf` 返回 -1 的信号一致性检查** | 🟢 低 | 存在但无编译期提醒——多数算法题依赖此 API |

### 6.5 标准库空白

| 缺口 | 说明 |
|------|------|
| **`std/` 目录为空** | 没有任何标准库模块 |
| **文件 I/O** | 无法读/写文件 |
| **JSON 解析/序列化** | 无，尽管 `Dict`/`Array` 语法天然贴合 JSON |
| **数学函数** | 仅有内置 `+` `-` `*` `/` `%` `**`；无 `abs`/`min`/`max`/`sqrt`/`sin`/`cos`/`random` |
| **正则表达式** | 无字符串模式匹配 |
| **日期/时间** | 无时间戳或日期运算 |
| **网络/HTTP** | 无 |
| **`import` / 模块系统** | 不存在；所有代码必须放入单个 `.va` 文件 |

### 6.6 工具链缺失

| 缺口 | 说明 |
|------|------|
| **无 LSP 服务器** | 编辑器无自动补全、跳转定义、实时错误诊断 |
| **无调试器 (DAP)** | 无法设断点、单步执行、查看变量/调用栈 |
| **无包管理器** | 无法安装/发布/版本化第三方库 |
| **错误消息质量一般** | 仅行号 + 错误类型 + 原文；无"你是否想写 X？"类建议；多行错误位置偶尔漂移 |
| **无 profiler / 性能剖析器** | 无法量化哪段代码消耗最多时间 |

### 6.7 互操作性缺陷

| 缺陷 | 说明 |
|------|------|
| **仅 C++ embedding API** | 无 C ABI 导出；嵌入方必须链接 C++；不支持其他语言宿主 |
| **无 FFI** | 无法直接调用 C 动态库 (.dll/.so) 中的函数 |
| **无跨进程序列化** | 无法将 `Value` 导出为二进制/字节码供其他进程消费 |

### 6.8 I/O 与输入缺陷

> 本节涵盖 `input()` 内置函数实现缺陷、REPL 交互限制，以及 stdin 测试基础设施缺口。

#### 6.8.1 `input()` 内置函数缺陷

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **EOF 无信号** | ~~🔴 高~~ ✅ 已修复 | ~~`std::getline` 到达 EOF 后静默返回空字符串，与纯回车无法区分。~~ 现已修复：EOF 或流错误时 `input()` 返回 `null`，正常空行返回 `""`，可明确区分 |
| **prompt 未 flush** | ~~🟡 中~~ ✅ 已修复 | ~~`std::cout << valueToString(arguments[0])` 输出 prompt 后未 flush。~~ 已添加 `std::flush`，确保 prompt 在阻塞等待输入前立即可见 |
| **无输入类型转换辅助** | 🟢 低 | `input()` 永远返回字符串，用户需手动 `int(input(...))`。缺少 `readInt()`、`readFloat()` 等便捷内置函数：特性 |
| **无输入超时 / 非阻塞读** | 🟡 中 | `std::getline` 无限阻塞。无法实现"等待 N 秒后超时"或"有数据则读，无数据则返回 null"的非阻塞模式：特性 |
| **错误时无区分** | 🟢 低 | `std::cin` 流错误（如二进制数据混入）与 EOF 均返回 `null`。已通过 `std::cin.clear()` 确保流错误后后续 `input()` 调用仍可用（REPL 中 Ctrl+Z 不会永久破坏 stdin）。流错误本身极为罕见（需底层设备故障），与 EOF 显式区分的实用价值极低 |
| **仅读一行** | 🟢 低 | 无 `input()` 变体读取多行直到 EOF（`readAll()`）或读取固定字节数（`read(n)`）：特性 |

#### 6.8.2 REPL 交互缺陷：除了“支持多行输入”，其他无用

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **不支持多行输入** | ~~🔴 高~~ ✅ 已修复 | ~~REPL 逐行独立编译执行，函数/Obj/if/while/for 块必须写在同一行。~~ 现已支持多行输入：REPL 跟踪 `{` `}` 嵌套深度（忽略字符串和注释内的括号），深度 > 0 时切换为 `... ` 提示符并累积输入，平衡后整段编译执行。嵌套块同理 | `func f() {`↵`... return 1`↵`... }` ✅ |
| **无行编辑 / 历史** | 🟡 中 | 原生 `std::getline` 无 readline/editline 支持。无命令历史（↑/↓ 回翻）、无 Tab 补全、无行编辑（Ctrl+A/E 跳转行首尾）、无语法高亮。可集成 GNU readline 或 Windows 等效库 |
| **局部变量跨行丢失** | ~~🟡 中~~ ✅ 已修复 | ~~每行新建 `Compiler` + `Chunk`，局部变量作用域在行结束时销毁。~~ 现已修复：① `initGlobals` 从替换改为合并，已存在全局变量的值被保留；② REPL 编译前通过 `seedGlobals()` 将 VM 现有全局表预注入编译器，确保 slot 编号一致。top-level `let`/`const`/`func` 自动跨行持久化 | `let x = 5`↵`print(x)` → `5` ✅ |
| **无法中断运行中的代码** | ~~🟢 低~~ ✅ 已修复 | ~~Ctrl+C 直接终止整个 REPL 进程。~~ 现已安装 `SIGINT` 处理器：VM 主循环在每个 opcode 前检查 `interruptFlag`（由信号处理器设置），检测到中断后打印 `Interrupted` 并返回 `RUNTIME_ERROR`；REPL 捕获后继续显示 `> ` 提示符 | Ctrl+C → `Interrupted`↵`> ` ✅ |
| **无会话持久化** | 🟢 低 | 退出 REPL 后变量状态全部丢失，无 `.vora_history` 文件保存历史，无 session save/restore |

#### 6.8.3 输入测试基础设施

| 缺陷 | 严重度 | 说明 |
|------|--------|------|
| **`input()` 无自动化测试** | ~~🟡 中~~ ✅ 已修复 | ~~`run_tests.ps1` / `run_tests.sh` 无 stdin 管道机制，input 测试被注释掉。~~ 已添加 `tests/interpreter/test_input.va`（5 项测试：正常读取、空行、无换行符输入、EOF→null、prompt 输出）。测试框架已支持 stdin 管道：所有测试默认 `</dev/null` → EOF→null，`test_input.va` 通过 `printf` / `"data\n"` 管道获得真实测试数据 |
| **REPL 无可测试性** | 🟢 低 | REPL 行为完全无自动化测试。可引入 expect-style 测试（如 `printf "1+2\n" | Vora.exe --repl`），验证输出含预期结果 |

#### 6.8.4 缺失的 I/O 功能

| 缺失 | 严重度 | 说明 |
|------|--------|------|
| **语言内无文件读取 API** | 🟡 中 | 用户无法在 Vora 程序中读取文件——`input()` 仅读 stdin，无 `readFile(path)` 或 `open(path).read()` |
| **无 `print` 重定向** | 🟢 低 | `print()` 始终写入 stdout；无法重定向到 stderr 或文件 |

---

## 附录：技术债务清单

| 条目 | 优先级 | 说明 |
|------|--------|------|
| `GcPtr` 无 null-check 语义 | 低 | 所有 `GcPtr` 解引用前应 assert non-null |
| `valueToString` 递归深度无限制 | 中 | 深层嵌套 Array/Dict 可能栈溢出 |
| `addValues` 字符串拼接每次分配 | 中 | 可引入 string builder 减少 GC 压力 |
| `const` 语义（仅编译期） | 低 | ~~已实现编译期 `const` 不可变绑定~~ 运行期无 const 强制；`const` 变量通过 upvalue 被闭包捕获后，若闭包不尝试写入则安全。当前已覆盖所有编译期 mutation 路径 |
| Chunk 常量池线形去重 | 低 | O(n²) 查找，大程序编译变慢 |
