# 程序员视角：Vora 语言深度分析与 UX 完善路线图

> 作者按：本文从一线程序员的视角，基于对 Vora 源码（~17000 行 C++17）的完整阅读，剖析这门语言的设计哲学、实现质量、现有缺陷，并提出按优先级排序的用户体验完善路线图。2026-06-23 修订——反映 v0.24 最新状态。

---

## 目录

1. [语言定位与设计哲学](#一语言定位与设计哲学)
2. [实现架构质量评估](#二实现架构质量评估)
3. [当前功能清单](#三当前功能清单)
4. [程序员视角的痛点清单](#四程序员视角的痛点清单)
5. [UX 完善路线图](#五ux-完善路线图)
6. [架构演进建议](#六架构演进建议)
7. [总结](#七总结)

---

## 一、语言定位与设计哲学

### 1.1 语言基因

| 灵感来源 | 借鉴方向 |
|----------|----------|
| **Crafting Interpreters (Lox)** | 架构骨架：Lexer → Pratt Parser → Compiler → VM。`Expr`/`Stmt` visitor 设计直接源自此书。 |
| **Lua 5.4** | 极简类型系统、`Environment` 作用域链、闭包语义、栈式虚拟机、模块系统（`import`）。 |
| **Wren** | 对象模型——`Obj` 声明内嵌方法定义，`this` 自动绑定，单继承。 |
| **JavaScript (隐含)** | `let` 声明、`for-in`、字符串插值 `${}`、`&&`/`||` 短路返回值（非布尔化）、可选分号、解构赋值、展开运算符、可选链、`match`/`case`。 |
| **Go** | `defer` 语句（延迟执行）。 |
| **Python** | 列表推导式、字典推导式、生成器（`yield`）。 |
| **CPython** | 命名风格（`RuntimeError`、`RuntimeMode`），`std::variant` 作为 tagged union。 |

### 1.2 语言性格

Vora 已从"教学语言"跨越到**"可用的嵌入式脚本语言"**。字节码 VM 作为默认后端，支持闭包、对象继承、C3 多继承、字符串插值、完整的异常处理（try/catch/finally/throw）、模块系统（import/export）、标准库（8 模块）、Set/Map 集合类型、生成器、推导式——能力上已超越 Lua 子集。跨平台构建系统覆盖 3 平台 × 5 架构，原生打包（.msi/.deb/.rpm/.pkg.tar.xz）开箱即用。

**一句话概括**：Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation. 字节码 VM 为默认引擎，已具备模块系统、标准库和完整的工具链。

### 1.3 版本演进速度

| 时间 | 版本 | 里程碑 |
|------|------|--------|
| 2026 Q1 | v0.1–v0.5 | Lexer + Parser + 基础 Interpreter |
| 2026-05 | v0.6–v0.7 | 字节码 VM Phase 1–2（表达式、控制流、函数、对象） |
| 2026-05 末 | v0.8–v0.9 | VM Phase 3–4（闭包、插值、异常）+ 性能优化（驻留、快速操作码、常量折叠） |
| 2026-06 | v0.10–v0.11 | VM 异常处理完善 + 多架构构建系统 + 原生打包 + CI |
| 2026-06 | v0.12 | int64/float64 双数值类型 + len()/type() + 数组/字符串内建方法 |
| 2026-06 | v0.13 | 内建函数集中化 + C++ 单元测试系统（doctest） |
| 2026-06 | v0.14 | weak_ptr 切断继承环 + 运行时错误调用栈回溯 |
| 2026-06 | v0.15 | 默认参数 + 多继承 C3 线性化 + super 关键字 |
| 2026-06 | v0.16 | Dict 字典类型 + for-in 对象 + VM 栈动态扩容 + 常量池 16 位索引 |
| 2026-06 | v0.17 | 错误信息全链路增强：源码行 + 插入符 ^ 位置标记 |
| 2026-06 | v0.18 | 核心架构重构：Callable 拆分 + ClassDefinition 统一 + GC + vora fmt + LeetCode 测试 |
| 2026-06 | v0.19 | C-style for 循环 + 匿名函数（lambda）+ upvalue 间接引用 |
| 2026-06 | v0.20 | 尾调用优化（TCO）+ const 不可变绑定 + upvalue 索引安全 |
| 2026-06 | v0.21–v0.24 | 模块系统（import/export）+ 标准库（8 模块）+ Set/Map + match + do-while + spread/rest + 推导式 + 可选链 + defer + 生成器/yield |

> 8 周内从解释器原型演进到具备完整 VM 后端 + GC + 格式化器 + 模块系统 + 标准库的跨平台语言——开发效率出色。

---

## 二、实现架构质量评估

### 2.1 整体印象：**A 级代码**（~17000 行 C++17）

模块边界清晰，单一维护者下工程质量持续提升。零外部依赖，纯 C++17 + STL。字节码 VM 为唯一执行后端。标记-清除 GC 已就绪。跨平台构建系统（CMakePresets 20 预设 + 交叉编译工具链）+ CI/CD 完备。LSP 基础设施（JSON-RPC + SemanticAnalyzer）已内置。

**代码量变化**：~6500 → ~9000 → ~17000 行，其中 VM 子系统 ~6319 行（项目最大模块），Runtime ~2477 行，Parser ~2049 行，AST ~1834 行，Formatter ~916 行，LSP 基础设施 ~1846 行。

### 2.2 Lexer（词法分析器）—— 评分：A-

**文件**：`src/lexer/lexer.cpp`（417 行）、`lexer.h`（41 行）、`token.h`（122 行）、`token.cpp`（92 行），合计 672 行。

**亮点**：
- 经典手写词法分析器，单遍扫描，代码简洁。
- **74 个 Token 类型**（26 个关键字），使用 `unordered_map` 做 O(1) 识别。
- 支持 `//` 行注释和 `/* */` 块注释，块注释支持嵌套（depth 计数器）。
- 正确处理全部多字符运算符：`++`、`--`、`**`、`&&`、`||`、`==`、`!=`、`<=`、`>=`、`+=`、`-=`、`*=`、`/=`、`%=`、`??`、`?.`、`=>`、`...`。
- 浮点数解析使用 `peekNext()` 避免 `a.b` 被误解析为数字。
- ✅ Unicode 标识符：`isAlpha()` 将所有 UTF-8 多字节字节（> 127）视为合法标识符字符。
- ✅ 转义序列：`\n`、`\t`、`\r`、`\0`、`\\`、`\"`、`\'`，未识别转义保留原文。
- ✅ 单引号字符串 `'...'` 与双引号等价。
- ✅ 十六进制/八进制/二进制数字字面量：`0x1A`、`0o12`、`0b101`。
- ✅ 错误恢复：非法字符报错后跳过继续扫描，不再产生 `INVALID` token 阻断解析。

### 2.3 Parser（语法分析器）—— 评分：A

**文件**：`src/parser/parser.cpp`（1968 行）、`parser.h`（81 行），合计 2049 行。

**亮点**：
- **Pratt 解析器**（优先级爬升法）实现正确，优先级表在 `getPrecedence()` 中集中管理。
- `**` 的右结合性正确处理；`? :` 作为特殊中缀运算符在 Pratt 循环内实现右结合短路语法。
- 赋值（`=`）和复合赋值（`+=` 等）在 Pratt 循环中统一处理，对 `VariableExpr`、`PropertyExpr`、`IndexExpr` 分别生成对应节点。
- `call()` 函数通过 `while(true)` 循环实现后缀链式调用：`foo()(1)[2].bar`。
- **Panic mode 错误恢复**：`synchronize()` 方法跳过 token 直到分号/右大括号/语句关键字，`parse()` 收集多条语句后统一判定 `hadError`。用户一次可看到多个语法错误。
- **27 种表达式类型、19 种语句类型**，覆盖全面。
- ✅ 解构赋值：`let [a, b, c] = arr`、`let {x, y} = obj`。
- ✅ 展开运算符：`...arr`、`...args`，支持函数调用和数组/字典字面量。
- ✅ 可选链：`obj?.prop`、`arr?.[index]`、`func?.(args)`。
- ✅ `match` 表达式：多分支模式匹配，支持多值、范围、守卫。
- ✅ 列表推导式：`[expr for item in iterable if cond]`。
- ✅ 字典推导式：`{k: v for item in iterable if cond}`。
- ✅ `defer` 语句：作用域退出时延迟执行。
- ✅ Rest 参数：`func(a, b, ...rest) {}`。
- ✅ 剩余参数解构：`func([a, ...rest]) {}`。

### 2.4 AST（抽象语法树）—— 评分：A

**文件**：`src/ast/expr.h`（552 行，27 种 Expr）、`stmt.h`（324 行，19 种 Stmt）、`ast_printer.cpp`（602 行）、`expr_visitor.h`（47 行）、`stmt_visitor.h`（39 行）+ 其他辅助文件，合计 ~1834 行。

**亮点**：
- **模板化 Visitor 模式**：`ExprVisitor<R>` 和 `StmtVisitor<R>` 按返回类型参数化——同一接口同时服务于字节码编译器（`R=void`）和 AST Printer（`R=string`）。添加新 pass 只需新增 `accept()` 重载，Visitor 接口无需复制。
- Expr 类型覆盖全面：Literal、Binary、Grouping、Unary、Variable、Assignment、CompoundAssignment、Call、Array、Dict、Index、Property、PropertyAssignment、IndexAssignment、This、Super、IncDec、Ternary、Match、FuncExpr、Yield、DestructureAssignment、Spread、ListComp、DictComp、Error、OptionalChain——共 **27 种**。
- Stmt 类型覆盖全面：ExprStmt、LetStmt、BlockStmt、ReturnStmt、IfStmt、WhileStmt、DoWhileStmt、ForStmt、CForStmt、FuncStmt、ObjStmt、BreakStmt、ContinueStmt、ThrowStmt、TryStmt、ImportStmt、ExportStmt、DeferStmt、ErrorStmt——共 **19 种**。
- `Program::accept<R>()` 为非虚模板——Program 无子类无需虚分派。
- `LetStmt` 携带 `typeAnnotation`（`:int`/`:float`），语法上已为类型系统预留空间。
- `BindingPattern` 支持数组解构和字典解构两种模式。

### 2.5 VM（字节码编译器 + 栈式虚拟机）—— 评分：A

**文件**：`src/vm/` 目录，合计 ~6319 行。**这是项目中最大、最复杂的子系统**（占代码总量的 36%）。

**架构**：双遍式编译器（AST → 字节码，无中间 IR）+ 栈式虚拟机。编译器实现 `ExprVisitor<void>` + `StmtVisitor<void>` + `ProgramVisitor<void>`，编译过程纯粹通过副作用完成（向 Chunk 写入字节码）。

**亮点**：

- **指令集**：53 条操作码，覆盖：
  - 栈操作（CONSTANT/CONSTANT_LONG/NULL/TRUE/FALSE/POP/DUP/PRINT_POP/POPN）
  - 全局/局部变量（DEFINE_GLOBAL/GET_GLOBAL/SET_GLOBAL/GET_GLOBAL_SAFE + WIDE 变体 + GET_LOCAL/SET_LOCAL）
  - 算术（ADD/DIVIDE/MODULO/POWER + 8 条快速数值操作码跳过类型检查：SUB_NN/MUL_NN/DIV_NN/MOD_NN/LESS_NN/LESS_EQ_NN/GREATER_NN/GREATER_EQ_NN）
  - 比较 + 逻辑（EQUAL/NOT_EQUAL/LESS/NOT/NEGATE）
  - 控制流（JUMP_IF_FALSE/JUMP/LOOP/JUMP_IF_NULL）
  - 函数/闭包（CALL/CLOSURE/RETURN/GET_UPVALUE/SET_UPVALUE/CLOSE_UPVALUE）
  - 尾调用优化（TAIL_CALL）
  - 调用展开（PUSH_SPREAD/SPREAD/CALL_N）
  - 对象（CLASS/GET_PROPERTY/SET_PROPERTY/GET_SUPER/DEFAULT_PARAM）
  - 异常（PUSH_CATCH/POP_CATCH/THROW/CLEAR_EXCEPTION/FINALLY_END）
  - 数组/字典（ARRAY/DICT/INDEX/SET_INDEX）
  - 模块系统（IMPORT）
  - 生成器（YIELD）

- **全局变量驻留**（v0.9）：编译器对全局变量名进行整数驻留，VM 端使用 `OP_GET_GLOBAL <slot>` 实现 O(1) 数组索引取代 `unordered_map` 哈希查找。WIDE 变体支持 16-bit 全局变量索引。
- **快速数值操作码**（v0.9）：8 条指令在 VM 层直接执行 `double` 运算，跳过 `std::variant` 类型检查分支。
- **常量折叠**（v0.9）：编译器在编译期对字面量算术/比较/一元运算直接求值，生成 `OP_CONSTANT` 而非多条指令。
- **闭包 upvalue 机制**（v0.8）：内层函数捕获外层局部变量通过 `OP_GET_UPVALUE`/`OP_SET_UPVALUE`/`OP_CLOSE_UPVALUE`，编译时 `resolveUpvalue()` 递归向外层搜索，运行时 VM 维护 open upvalue 链表正确共享捕获。
- **尾调用优化**（v0.20）：`OP_TAIL_CALL` 在函数尾位置复用当前调用帧而非推送新帧，消除深递归的栈溢出风险。
- **异常处理全覆盖**（v0.10）：`OP_PUSH_CATCH`/`OP_POP_CATCH`/`OP_THROW`/`OP_CLEAR_EXCEPTION`/`OP_FINALLY_END`，break/continue/return 穿透 finally 块——编译器捕获这些非局部跳转，字节码级重放到 finally 块后跳至原目标。
- **跳转回填**：`emitJump()`/`patchJump()`/`emitLoop()` 两遍式——先预留占位字节，后回填偏移量。
- **RLE 行号编码**：`Chunk` 使用游程编码存储行号和列号。
- **调用帧栈**：`CallFrame` 结构保存函数闭包 + IP + 栈基址。帧栈大小固定（256 帧）。
- **模块系统**（v0.21）：`OP_IMPORT` 从文件系统加载模块，模块对象通过全局缓存避免重复加载，`OP_EXPORT` 控制导出符号。

### 2.6 Runtime（运行时系统）—— 评分：A

**文件**：`src/runtime/` 目录，合计 ~2477 行。

**亮点**：
- **Value 使用 `std::variant`（15 种类型）**：`nullptr_t`、`double`、`int64_t`、`bool`、`GcPtr<GcString>`、`GcPtr<Array>`、`GcPtr<Dict>`、`GcPtr<Callable>`、`GcPtr<ObjectInstance>`、`GcPtr<FunctionPrototype>`、`GcPtr<ClassDefinition>`、`GcPtr<Iterator>`、`GcPtr<Generator>`、`GcPtr<Set>`、`GcPtr<Map>`。类型安全，`std::visit` 用于模式匹配。
- **双数值类型系统**：`int64_t` 和 `double` 共存，`isNumeric()`/`toDouble()`/`promoteToFloat()` 辅助函数统一处理类型提升。`int±int→int`，`int±float→float`，`/` 始终→`float64`。跨类型相等（`42 == 42.0` → `true`）。
- **`Environment`** 使用 `shared_ptr<Environment> enclosing_` 链——全 GC 管理，无裸指针。
- **`NativeFunction`** 的 arity 为 `-1` 表示变参（`print`、`input`、`range`、`assert`），`callDirectly()` 方法绕过 `Interpreter&` 参数供 VM 使用。
- **`VoraFunction`** 支持双模式构造：解释器模式（AST body + 闭包环境）和 VM 模式（`FunctionPrototype` + upvalues）。
- **`Callable` 体系**：`NativeFunction` → `VoraFunction` → `BoundMethod` → `ClassConstructor`，均继承自 `GcObject`。
- **内置方法集中管理**（`builtins.cpp`，1459 行）：数组 7 个方法 + 字符串 11 个方法 + Dict 4 个方法 + Set 4 个方法 + Map 4 个方法 = **30 个内建方法**。
- **新增集合类型**：
  - `Set`（v0.23）：基于 `std::unordered_set<Value>`，支持 `.add()`、`.remove()`、`.has()`、`.size`。
  - `Map`（v0.23）：基于 `std::unordered_map<Value, Value>`，支持 `.get()`、`.set()`、`.has()`、`.size`，键为任意 Value 类型。
- **生成器**（v0.24）：`Generator` 对象 + `YieldExpr` + `OP_YIELD` 指令，支持惰性求值。

### 2.7 GC（垃圾回收器）—— 评分：A-

**文件**：`src/gc/gc_heap.h/.cpp`（270 行）、`gc_object.h`（27 行）、`gc_ptr.h`（40 行），合计 ~337 行。

**亮点**：
- **标记-清除算法**：`GcHeap` 单例管理所有堆对象的侵入式链表。
- **`GcPtr<T>`**（40 行）：非拥有指针，trivially copyable（与 `void*` 同大小），支持隐式上转型（`GcPtr<Derived>` → `GcPtr<Base>`）。
- **根集合**：VM 提供栈、全局变量、upvalues、调用帧栈、异常处理器栈作为 GC 根。
- **动态阈值**：起始 4MB，堆超过阈值一半时翻倍，最小 4MB。
- **编译期调试日志**：`VORA_GC_TRACE` 宏控制。

### 2.8 Formatter（源码格式化器）—— 评分：A-

**文件**：`src/formatter/formatter.h`（139 行）、`formatter.cpp`（801 行），合计 916 行。

**亮点**：
- AST 驱动的源代码格式化器，`vora fmt` 命令。
- 实现 `ExprVisitor<std::string>` + `StmtVisitor<std::string>` + `ProgramVisitor<std::string>`。
- 覆盖全部 27 种表达式和 19 种语句类型。
- 4-space 缩进，always-brace 风格，优先级感知的括号化。

### 2.9 LSP 基础设施 —— 评分：A-

**文件**：`src/lsp/`（1130 行）、`src/json_rpc/`（716 行），合计 ~1846 行。

**亮点**：
- **SemanticAnalyzer**（307 行 header + 880 行 impl）：作用域感知的 AST 分析——符号表、跳转定义、查找引用、自动补全、悬停提示、诊断（未使用变量、不可达代码、变量遮蔽）。
- **JSON-RPC 层**：消息类型 + parse/serialize + stdio Content-Length framing + handler 注册/分发。
- 外部 LSP 服务器位于 `D:\Vora-LSP`（独立仓库）。

### 2.10 构建系统 & 基础设施 —— 评分：A

**文件**：`CMakePresets.json`（20 个预设）、`cmake/toolchains/`（6 个交叉编译工具链）、`build.ps1`、`build.sh`、`CMakeLists.txt`（含 CPack）、`.github/workflows/ci.yml`。

**亮点**：
- CMakePresets v6 继承链：base → os-base → os-arch → os-arch-config，结构清晰。
- 交叉编译工具链覆盖 6 种场景：linux-i386、linux-aarch64、linux-armhf、windows-i386、windows-arm64、macos-universal。
- `build.ps1`/`build.sh` 提供一键构建体验：单命令覆盖 configure + build + package。
- CPack 打包输出：`.msi` + `.zip`（Windows）、`.deb` + `.rpm` + `.pkg.tar.xz`（Linux）、`.tar.gz`（macOS）。
- CI：GitHub Actions 矩阵覆盖 Linux(4arch)×2config + Windows(3arch)×2config + macOS(1universal)×2config = 18 个组合。
- **零外部依赖**——整个构建系统仅依赖 CMake 3.16+。

---

## 三、当前功能清单

### 3.1 类型系统

```
Value = null | int64 | float64 | bool | string | Array | Dict | Callable | ObjectInstance
      | FunctionPrototype | ClassDefinition | Iterator | Generator | Set | Map
```

15 种运行时类型。`int64` 和 `float64` 为双数值类型（`int±int→int`，`int±float→float`，`/` 始终→`float64`）。

### 3.2 运算符系统（完整实现）

| 优先级 | 运算符 | 结合性 | 状态 |
|--------|--------|--------|------|
| 1 | `=` | 右结合 | ✔ |
| 1 | `\|\|` `or` | 左结合 | ✔ 短路求值 |
| 2 | `&&` `and` | 左结合 | ✔ 短路求值 |
| 2 | `? :` | 右结合 | ✔ 短路求值 |
| 3 | `??` | 左结合 | ✔ 空值合并 |
| 4 | `==` `!=` | 左结合 | ✔ 全类型 |
| 5 | `<` `<=` `>` `>=` | 左结合 | ✔ |
| 6 | `+` `-` | 左结合 | ✔ 数字+字符串+数组 |
| 7 | `*` `/` `%` | 左结合 | ✔ |
| 8 | `**` | 右结合 | ✔ |
| - | `++` `--` | 前后缀 | ✔ 变量和属性 |
| - | `+=` `-=` `*=` `/=` `%=` | 右结合 | ✔ 解糖为 `x = x + y` |
| - | `?.` | 前缀 | ✔ 可选链 |
| - | `...` | 前缀 | ✔ 展开运算符 |

### 3.3 语句与流程控制

```vora
let x = 10                         // 变量声明（可选 :int / :float 标注）
const PI = 3.14                    // 常量声明（不可变）
x = 20                             // 赋值（沿作用域链查找）
{ let x = 30 }                     // 块作用域（遮蔽）
if (cond) { ... } else { ... }     // 条件分支
while (cond) { ... }               // while 循环（支持 break/continue）
do { ... } while (cond)            // do-while 循环
for item in iterable { ... }       // for-in（数组/字符串/range/dict/object）
for (let i = 0; i < n; i++) { ... } // C-style for 循环
return value                       // 返回（函数内）
break / continue                   // 循环控制
throw value                        // 抛出任意值
try { ... } catch (e) { ... } finally { ... }  // 异常处理
import "module"                    // 导入模块
export symbol                      // 导出符号
defer { ... }                      // 延迟执行（作用域退出时）
```

### 3.4 match 表达式

```vora
let result = match x {
    1 -> { "one" }
    2, 3 -> { "two or three" }
    in 10..100 -> { "in range" }
    in [101, 200] -> { "in set" }
    when x > 0 -> { "positive" }
    else -> { "other" }
}
```

支持多值匹配、范围匹配、集合匹配、守卫条件（`when`）、`else` 默认分支。

### 3.5 函数与闭包

```vora
// 普通函数
func greet(name, greeting = "Hello") {
    return greeting + ", " + name + "!"
}

// Lambda 匿名函数
let double = (x) => x * 2
let add = (a, b) => a + b

// Rest 参数
func sum(...numbers) {
    let total = 0
    for n in numbers { total = total + n }
    return total
}

// 解构参数
func process([a, b, ...rest]) { ... }
func unpack({name, age = 0}) { ... }

// 闭包
func makeCounter() {
    let count = 0
    return () => { count = count + 1; return count }
}
let c = makeCounter()
print(c())  // 1
print(c())  // 2
```

VM 闭包通过 upvalue 机制实现（`resolveUpvalue()` 编译期解析 + `openUpvalues` 运行时链表共享）。尾调用优化（`OP_TAIL_CALL`）消除尾递归栈增长。

### 3.6 对象系统（含 C3 多继承）

```vora
Obj Animal(name) {
    this.name = name
    func speak() { print("...") }
}

Obj Dog : Animal (name, breed) {
    this.breed = breed
    func speak() {
        super.speak()   // 调用父类方法
        print(" (breed: " + this.breed + ")")
    }
}

// C3 多继承
Obj MixinA {
    func methodA() { print("A") }
}
Obj MixinB {
    func methodB() { print("B") }
}
Obj Multi : MixinA, MixinB (name) {
    this.name = name
}

let m = Multi("test")
m.methodA()  // "A" — 继承自 MixinA
m.methodB()  // "B" — 继承自 MixinB
```

- ✅ 单继承 + C3 多继承
- ✅ 构造函数链（根→叶，多继承逆序）
- ✅ 方法查找沿 MRO 线性化链
- ✅ 方法调用自动绑定 `this`
- ✅ `super` 关键字（调用父类被覆盖的方法）

### 3.7 字符串插值

```vora
let name = "World"
print("Hello ${name}!")           // "Hello World!"
print("(${p.x}, ${p.y})")         // 支持多级属性访问
print("${undefined_var}")         // 保留原文，不报错
```

### 3.8 模块系统

```vora
// math.va
export const PI = 3.14159
export func sqrt(x) { ... }
export func abs(x) { return x < 0 ? -x : x }

// main.va
import "math"
print(math.PI)          // 3.14159
print(math.sqrt(4))     // 2

// 选择性导入
import {PI, sqrt as squareRoot} from "math"
```

- 文件系统路径解析
- 模块缓存（避免重复加载）
- 导出符号的命名空间隔离

### 3.9 标准库（8 个模块）

| 模块 | 功能 | 行数 |
|------|------|------|
| `std/math` | sqrt, abs, sin, cos, PI 等 | 20 |
| `std/array` | 数组操作函数 | 130 |
| `std/string` | 字符串操作函数 | 113 |
| `std/regex` | 正则表达式 | 38 |
| `std/json` | JSON 解析/序列化 | 18 |
| `std/datetime` | 日期时间操作 | 80 |
| `std/fs` | 文件系统操作 | 16 |
| `std/os` | 操作系统交互 | 16 |

### 3.10 集合类型

#### 数组

```vora
let arr = [1, 2, "hello", true, null]
print(arr[0])           // 1
arr[1] = 42             // 索引赋值
let merged = arr + [4, 5]  // 数组合并

// 7 个内建方法
arr.length              // 元素个数
arr.add(42)             // 追加
arr.pop()               // 弹出末尾
arr.insert(0, "first")  // 指定位置插入
arr.remove(2)           // 删除并返回
arr.indexOf("hello")    // 查找索引，未找到→-1
arr.clear()             // 清空
```

#### 字典

```vora
let d = {"name": "Vora", "version": 0.24}
print(d["name"])        // "Vora"
d["lang"] = "C++"       // 添加键值对
let merged = d1 + d2    // 字典合并

// 4 个内建方法
d.length                // 键值对个数
d.keys()                // 所有键
d.values()              // 所有值
d.items()               // 所有 [key, value] 对
d.clear()               // 清空
```

#### Set（v0.23）

```vora
let s = Set([1, 2, 3])
s.add(4)                // 添加元素
s.has(2)                // true
s.remove(1)             // 删除元素
s.size                  // 元素个数

// Set 运算
let a = Set([1, 2, 3])
let b = Set([2, 3, 4])
a & b                    // 交集: Set([2, 3])
a | b                    // 并集: Set([1, 2, 3, 4])
a - b                    // 差集: Set([1])
```

#### Map（v0.23）

```vora
let m = Map()
m.set("key", "value")   // 设置键值对
m.get("key")            // "value"
m.has("key")            // true
m.size                  // 键值对个数
m.delete("key")         // 删除
```

### 3.11 推导式

```vora
// 列表推导式
let squares = [x * x for x in 1..10]
let evens = [x for x in 1..100 if x % 2 == 0]
let nested = [x + y for x in [1, 2] for y in [10, 20]]

// 字典推导式
let word_lengths = {word: len(word) for word in ["hello", "world"]}
let filtered = {k: v for k, v in data if v > 10}
```

### 3.12 生成器

```vora
func fibonacci() {
    let a = 0
    let b = 1
    while true {
        yield a
        let temp = a
        a = b
        b = temp + b
    }
}

let fib = fibonacci()
print(fib.next())  // 0
print(fib.next())  // 1
print(fib.next())  // 1
print(fib.next())  // 2

for n in fibonacci() {
    if (n > 100) break
    print(n)
}
```

### 3.13 内建函数（12 个）

| 函数 | 参数 | 说明 |
|------|------|------|
| `print(...)` | 变参 | 空格分隔输出，末尾换行 |
| `clock()` | 0 | Unix 时间戳（秒，毫秒精度） |
| `input(prompt?)` | 变参 | stdin 读取一行，可选 prompt |
| `int(value)` | 1 | 转 int64（截断），接受 number/string/bool |
| `float(value)` | 1 | 转 float64，接受 number/string/bool |
| `len(value)` | 1 | 返回长度（数组/字符串/dict/set/map） |
| `type(value)` | 1 | 返回类型名（15 种） |
| `range(...)` | 变参 | `range(stop)` / `range(start,stop)` / `range(start,stop,step)` |
| `assert(...)` | 变参 | `assert(condition, message?)` — 失败抛出 RuntimeError |
| `bin(num)` | 1 | 数字转二进制字符串 `"0b..."` |
| `oct(num)` | 1 | 数字转八进制字符串 `"0o..."` |
| `hex(num)` | 1 | 数字转十六进制字符串 `"0x..."` |

### 3.14 defer 语句

```vora
func processData() {
    let file = openFile("data.txt")
    defer { closeFile(file) }   // 退出作用域时自动执行
    // ... 处理文件
    if (error) throw "failed"   // defer 仍然执行
    return result
}
```

### 3.15 测试套件

```
tests/
├── lexer/       (4 files)
├── parser/      (4 files)
├── runtime/     (29 files)
├── interpreter/ (32 files)
├── formatter/   (1 file)
├── bench/       (4 files)
├── ans/         (66 files — LeetCode 集成测试)
├── fuzz/        (1 file — libFuzzer harness)
├── run_tests.ps1
├── run_tests.sh
└── run_examples.ps1
```

**140 个 `.va` 测试文件 + 328 个 C++ 单元测试用例（doctest，9 个模块，802 个断言）**。

---

## 四、程序员视角的痛点清单

### 4.1 🔴 致命级（已全部解决 ✅）

| 问题 | 解决版本 |
|------|----------|
| 无 `len()` / 长度查询 | v0.12 ✅ |
| 无数组内建方法 | v0.12 ✅ |
| 无字符串内建方法 | v0.12 ✅ |
| 无整数类型（仅 double） | v0.12 ✅ |
| 无 `type()` 类型查询 | v0.12 ✅ |
| 循环引用内存泄漏 | v0.14（weak_ptr）+ v0.18（GC）✅ |
| 无 `super` 关键字 | v0.15 ✅ |
| 无默认参数 | v0.15 ✅ |
| 常量池 256 条目限制 | v0.16（16-bit）✅ |
| 无 `switch`/`match` | v0.21（match）✅ |
| 无模块系统 | v0.21 ✅ |
| 无标准库 | v0.21–v0.23 ✅ |

### 4.2 🟠 严重级（影响日常开发）

1. **REPL 不能多行输入**——无法在 REPL 中定义函数或类。多行表达式需要粘贴输入。

2. **错误信息不含调用栈位置**——虽然 v0.17 增加了源码行和 `^` 标记，但缺少函数名和调用链信息。跨文件错误信息尤其不足。

### 4.3 🟡 中度级（限制语言表达能力）

3. **无解构赋值的可选默认值**——`let [a, b = 0] = arr` 语法尚不支持。

4. **for-in 不支持键值对遍历**——`for k, v in dict` 语法尚不支持（需通过 `.items()` 间接实现）。

5. **无枚举类型（enum）**——只能用常量模拟。

6. **无迭代器协议**——自定义对象不可 for-in 遍历（需内建方法支持）。

### 4.4 🟢 轻度级（影响生态和工程化）

7. **标准库覆盖有限**——文件 IO、数学函数仅提供基础接口，缺少网络、进程、加密等模块。

8. **无包管理器**——第三方库的发现、安装、版本管理需要外部工具。

9. **文档和教程不足**——USER_GUIDE.md 覆盖基本语法，但缺少深入教程和最佳实践。

---

## 五、UX 完善路线图

### P0 — 当前优先（模块系统 + 标准库完善）

#### 5.1 模块系统完善 ✅

**已完成**（v0.21）。`import`/`export`、文件系统路径解析、模块缓存、命名空间隔离。

#### 5.2 标准库扩展

**工作量**：持续

- 文件 IO：`readFile()`、`writeFile()`、`readLines()`
- 数学：`sqrt()`、`sin()`、`cos()`、`random()`
- 字符串：`split()`、`join()`、`trim()`
- 进程：`exec()`、`args`

#### 5.3 REPL 多行输入

**工作量**：4-6 小时

- 检测括号/大括号未闭合时自动进入多行模式
- 支持 `func`/`obj`/`try` 等块语句的多行输入
- 提供行号提示符（`...`）

#### 5.4 错误信息增强（函数名 + 调用栈）

**工作量**：4-6 小时

- 运行时错误显示完整调用栈（函数名 + 文件 + 行号）
- 编译时错误区分 Error 和 Warning 级别
- 嵌套 try/catch 的异常传播链可视化

---

### P1 — 短期完善（1-2 周）

#### 5.5 for-in 键值对遍历

```vora
for key, value in myDict {
    print(key + ": " + value)
}
```

**工作量**：2-3 小时

#### 5.6 解构赋值默认值

```vora
let [a, b = 0, c = "default"] = someArray
let {name = "anon", age = 0} = someObj
```

**工作量**：3-4 小时

#### 5.7 枚举类型

```vora
enum Color { Red, Green, Blue }
enum Status { Active = 1, Inactive = 0 }
```

**工作量**：4-6 小时

#### 5.8 迭代器协议

允许自定义对象实现 `next()` 方法支持 for-in 遍历。

**工作量**：3-4 小时

---

### P2 — 中期功能（1-3 个月）

#### 5.9 字节码优化

当前 VM 已具备 Phase 1-4 全部功能，但仍可进一步优化：

- **Superinstruction**：合并高频指令对（如 `GET_LOCAL + CALL` → `CALL_LOCAL`），减少 dispatch 开销
- **NaN Boxing**：将 `Value` 从 `std::variant`（24-32 bytes）压缩到 8 bytes（64-bit double NaN 空间编码所有类型），消除 variant 访问开销和 shared_ptr 控制块开销
- **内联缓存（Inline Cache）**：为属性访问和方法调用添加 PIC（多态内联缓存），预期 OOP 代码 3-5× 加速

**工作量**：2-4 周

#### 5.10 标准库完善

```
std/
├── math.va       // sqrt, abs, sin, cos, random, PI
├── fs.va         // readFile, writeFile, readLines, listDir
├── os.va         // args, exit, env, exec
├── datetime.va   // 时间操作
├── json.va       // JSON 解析/序列化
├── string.va     // 字符串操作
├── array.va      // 数组操作
└── regex.va      // 正则表达式
```

**工作量**：持续

#### 5.11 LSP 服务器完善

基于 Vora 解析器 + SemanticAnalyzer 实现完整的 Language Server Protocol：

- 诊断（实时语法/语义错误）
- 跳转定义
- 自动补全
- 悬停提示
- 查找引用

**工作量**：2-3 周（MVP：诊断 + 跳转定义）

---

### P3 — 长期工程（3-6 个月+）

#### 5.12 包管理器

第三方库的发现、安装、版本管理。

**工作量**：4-6 周

#### 5.13 调试器

断点、单步执行、变量检查、调用栈浏览。

**工作量**：3-4 周

#### 5.14 类型系统增强

渐进式类型检查：`let x: int = 42`、函数返回类型标注、接口/协议。

**工作量**：4-6 周

---

## 六、架构演进建议

### 6.1 短期架构调整

#### 6.1.1 统一 Value 类型查询

当前 `std::holds_alternative<T>()` 散落在各处。建议增加：

```cpp
enum class ValueType { Null, Int, Float, Bool, String, Array, Dict, Callable,
                       Object, FunctionPrototype, ClassDefinition,
                       Iterator, Generator, Set, Map };
ValueType valueType(const Value& value);
```

### 6.2 中期架构升级

#### 6.2.1 NaN Boxing（值表示革命）

这是 Vora 性能的最大单点提升机会：

```
当前 std::variant<15 types> = 24-32 bytes per Value
NaN Boxing: 8 bytes per Value (double 的 NaN 空间编码指针/tag)
```

- 栈容量翻三倍（1024 → 3072 in same memory）
- 消除 `std::visit` / `std::get` 分支预测失败
- 消除 `shared_ptr` 控制块分配（小对象内联）
- 与 LuaJIT 的设计思路一致

**实现复杂度较高**：需处理 64-bit 架构下的指针压缩、tag 编码、跨平台兼容。建议在 VM 成熟稳定后再启动。

#### 6.2.2 JIT 编译器

基于 NaN Boxing 的值表示，可以考虑添加 JIT 编译层（x86_64/ARM64），将热点函数编译为原生代码。

**工作量**：2-3 个月

### 6.3 长期愿景

#### 6.3.1 并发模型

- 协程（coroutine）—— 已有生成器基础
- 线程池（thread pool）—— C++ `std::thread` 封装
- 异步 I/O（event loop）—— 非阻塞文件/网络操作

#### 6.3.2 编译到 WebAssembly

将 Vora 源码编译为 WASM 模块，支持浏览器端运行。

---

## 七、总结

### 7.1 模块一句话评价

| 模块 | 行数 | 评价 |
|------|------|------|
| Lexer | 672 | 简洁高效：转义/Unicode/嵌套注释/错误恢复/0x/0o/0b 数字前缀。74 个 Token 类型。 |
| Parser | 2049 | Pratt 实现优雅，panic-mode 恢复，27 Expr + 19 Stmt 种节点。解构/展开/可选链/match/推导式/defer。 |
| AST | 1834 | 模板化 Visitor 模式，类型体系完整（46 种节点），泛型接口支持多种返回类型。 |
| **Compiler+VM** | **6319** | **项目核心**：53 条指令/栈式 VM/upvalue 闭包/异常全覆盖/全局驻留/快速数值操作码/常量折叠/TCO/模块系统/生成器/yield。 |
| Runtime | 2477 | `std::variant` 类型安全（15 类型），双数值系统+自动提升，Environment shared_ptr-only，Callable 统一接口，30 个内建方法，Set/Map 集合类型。 |
| GC | 337 | GcHeap 标记-清除 + GcPtr 非拥有指针 + GcObject 基类。自动回收循环引用。动态阈值。 |
| Formatter | 916 | AST 驱动的源代码格式化器，`vora fmt` 命令，覆盖全部 46 种节点类型。 |
| LSP 基础设施 | 1846 | SemanticAnalyzer（作用域分析/符号表/补全/诊断）+ JSON-RPC 层（消息/传输/路由）。 |
| 构建系统 | - | **A 级**：CMakePresets(20)+交叉编译(6)+CPack(4 格式)+CI(18 组合)+图标。同类项目中顶尖。 |
| 测试 | - | **140 个 .va 文件 + 328 个 C++ 用例（802 断言）+ 66 LeetCode 集成测试 + 1 个 fuzzer**。 |

### 7.2 Vora 的差异化定位（v0.24 更新）

| 维度 | Lua 5.4 | Python 3 | JavaScript (V8) | Wren | Vora (v0.24) |
|------|---------|----------|----------------|------|--------------|
| 实现复杂度 | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ |
| 语言简单性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| 执行性能 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ (VM + TCO) |
| OOP 模型 | 原型式 | 类式 | 原型式 | 类式 | 类式(轻量+C3 多继承) |
| 标准库 | 极小 | 庞大 | 中等 | 小 | 中等（8 模块 + 30 内建方法） |
| 嵌入友好 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ (GC + 零依赖) |
| 构建 & 打包 | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ (CMakePresets + 原生包) |
| 内存管理 | GC | GC | GC | GC | GC (标记-清除) |
| 模块系统 | require | import | import | 模块类 | import/export |
| 集合类型 | table | dict/set | Object/Set/Map | 无 | Array/Dict/Set/Map |
| 推导式 | 无 | 有 | 无 | 无 | 有 (列表+字典) |
| 生成器 | 无 | 有 | 有 (function*) | 无 | 有 (yield) |

**Vora 的潜在优势领域**：
- **教学用语言实现**：比 Lox 更完整的对象系统和异常处理，比 Lua 源码更易阅读
- **C++ 应用嵌入式脚本**：零外部依赖 + CMake 集成，5 分钟即可嵌入
- **配置/DSL 语言**：类 JS 语法亲和、可选分号、字符串插值自然适合配置文件
- **快速原型工具**：REPL + 动态类型 + 对象系统 + 推导式，适合数据探索和脚本自动化

### 7.3 与上一版的对比

| 指标 | 2026-06-15 (v0.18) | 2026-06-23 (v0.24) | 变化 |
|------|-------------------|-------------------|------|
| 总代码量 | ~9000 行 | ~17000 行 | +89% |
| VM 代码量 | ~3744 行 | ~6319 行 | +69% |
| Runtime 代码量 | ~800 行 | ~2477 行 | +210% |
| VM 指令数 | 50 | 53 | +3 |
| Value 类型数 | 11 | 15 | +4 |
| Expr 类型数 | 14 | 27 | +13 |
| Stmt 类型数 | 13 | 19 | +6 |
| Token 类型数 | ~50 | 74 | +24 |
| 内建函数 | 12 | 12 | 不变 |
| 内建方法 | 22 | 30 | +8 |
| 标准库模块 | 0 | 8 | +8 |
| GC | 标记-清除（GcHeap） | 标记-清除（GcHeap，动态阈值） | 改进 |
| 模块系统 | 无 | import/export | 新增 |
| 集合类型 | Array/Dict | Array/Dict/Set/Map | +2 |
| 推导式 | 无 | 列表推导+字典推导 | 新增 |
| 生成器 | 无 | yield + Generator | 新增 |
| defer | 无 | 有 | 新增 |
| TCO | 无 | 有 | 新增 |
| LSP 基础设施 | 无 | SemanticAnalyzer + JSON-RPC | 新增 |
| 测试 | 20 .va + 66 LeetCode | 140 .va + 328 C++ 用例 | 大幅增长 |

### 7.4 给贡献者的建议

1. ✅ **P0 已全部完成**（模块系统、标准库基础、GC、格式化器）——Vora 已从"能运行"跨越到"能写工程项目"。
2. **当前 P0**（REPL 多行输入、错误信息增强）——提升日常开发体验。
3. **P1**（for-in 键值对遍历、解构默认值、枚举、迭代器协议）——进一步提升语言表达力。
4. **P2**（NaN Boxing、标准库完善、LSP）——性能和工具链。
5. **P3**（包管理器、调试器、类型系统、JIT）——生态和长期竞争力。

---

> **后记（2026-06-23 修订）**：Vora v0.24 在 ~17000 行 C++17 代码内完成了一个带有标记-清除垃圾回收器的完整动态类型脚本语言实现。从 v0.18 到 v0.24，项目新增了模块系统（import/export）、8 个标准库模块、Set/Map 集合类型、match 表达式、生成器/yield、推导式、可选链、defer、尾调用优化等核心功能。LSP 基础设施（SemanticAnalyzer + JSON-RPC）的引入为 IDE 集成奠定了基础。
>
> Vora 现在的架构是：Lexer → Parser → Compiler → Chunk → VM，加上 GcHeap 管理所有堆对象，加上模块加载器和标准库。下一步自然目标是 REPL 改进、错误信息增强、NaN Boxing 性能优化和 LSP 完善。
