# 程序员视角：Vora 语言深度分析与 UX 完善路线图

> 作者按：本文从一线程序员的视角，基于对 Vora 源码（~9000 行 C++17）的完整阅读，剖析这门语言的设计哲学、实现质量、现有缺陷，并提出按优先级排序的用户体验完善路线图。2026-06-15 修订——反映 v0.18 最新状态。

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
| **Lua 5.4** | 极简类型系统、`Environment` 作用域链、闭包语义。栈式虚拟机设计。 |
| **Wren** | 对象模型——`Obj` 声明内嵌方法定义，`this` 自动绑定，单继承。 |
| **JavaScript (隐含)** | `let` 声明、`for-in`、字符串插值 `${}`、`&&`/`||` 短路返回值（非布尔化）、可选分号。 |
| **CPython** | 命名风格（`RuntimeError`、`RuntimeMode`），`std::variant` 作为 tagged union。 |

### 1.2 语言性格

Vora 已从"教学语言"跨越到**"可用的嵌入式脚本语言"**。字节码 VM 作为默认后端，支持闭包、对象继承、字符串插值、完整的异常处理（try/catch/finally/throw）——能力上已接近 Lua 子集。跨平台构建系统覆盖 3 平台 × 5 架构，原生打包（.msi/.deb/.rpm/.pkg.tar.xz）开箱即用。

**一句话概括**：Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation. 双后端全部就绪，栈式 VM 为默认引擎，正在向模块系统和标准库迈进。

### 1.3 版本演进速度

| 时间 | 版本 | 里程碑 |
|------|------|--------|
| 2026 Q1 | v0.1–v0.5 | Lexer + Parser + 基础 Interpreter |
| 2026-05 | v0.6–v0.7 | 字节码 VM Phase 1–2（表达式、控制流、函数、对象） |
| 2026-05 末 | v0.8–v0.9 | VM Phase 3–4（闭包、插值、异常）+ 性能优化（驻留、快速操作码、常量折叠） |
| 2026-06 | v0.10–v0.11 | VM 异常处理完善 + 多架构构建系统 + 原生打包 + CI |
| 2026-06 | v0.12 | int64/float64 双数值类型 + len()/type() + 数组/字符串内建方法（17 个） |
| 2026-06 | v0.13 | 内建函数集中化 + C++ 单元测试系统（doctest 6 模块） + P0/P1 修正 |
| 2026-06 | v0.14 | weak_ptr 切断继承环 + 运行时错误调用栈回溯 |
| 2026-06 | v0.15 | 默认参数 + 多继承 C3 线性化 + super 关键字 |
| 2026-06 | v0.16 | Dict 字典类型 + for-in 对象 + VM 栈动态扩容 + 常量池 16 位索引 |
| 2026-06 | v0.17 | 错误信息全链路增强：源码行 + 插入符 ^ 位置标记 |
| 2026-06 | v0.18 | 核心架构重构：Callable 拆分 + ClassDefinition 统一 + GC + vora fmt + LeetCode 测试 |

> 6 周内从解释器原型演进到具备完整 VM 后端 + GC + 格式化器的跨平台语言——开发效率出色。

---

## 二、实现架构质量评估

### 2.1 整体印象：**A 级代码**（~9000 行 C++17）

模块边界清晰，单一维护者下工程质量持续提升。零外部依赖，纯 C++17 + STL。字节码 VM 为唯一执行后端。标记-清除 GC 已就绪。跨平台构建系统（CMakePresets 20 预设 + 交叉编译工具链）+ CI/CD 完备。

**代码量变化**：~6500 → ~9400 → ~9000 行（v0.18 重构精简了约 400 行），其中 VM 子系统 ~3744 行（项目最大模块），GC ~300 行，Formatter ~630 行。其余模块保持克制增长。

### 2.2 Lexer（词法分析器）—— 评分：A-

**文件**：`src/lexer/lexer.cpp`（432 行）、`lexer.h`（~60 行）、`token.h`（~103 行）、`token.cpp`（77 行），合计 687 行。

**亮点**：
- 经典手写词法分析器，单遍扫描，代码简洁。
- 21 个关键字使用 `unordered_map` 做 O(1) 识别。
- 支持 `//` 行注释和 `/* */` 块注释，块注释支持嵌套（depth 计数器）。
- 正确处理全部多字符运算符：`++`、`--`、`**`、`&&`、`||`、`==`、`!=`、`<=`、`>=`、`+=`、`-=`、`*=`、`/=`、`%=`。
- 浮点数解析使用 `peekNext()` 避免 `a.b` 被误解析为数字。
- ✅ Unicode 标识符：`isAlpha()` 将所有 UTF-8 多字节字节（> 127）视为合法标识符字符。
- ✅ 转义序列：`\n`、`\t`、`\r`、`\0`、`\\`、`\"`、`\'`，未识别转义保留原文。
- ✅ 单引号字符串 `'...'` 与双引号等价。
- ✅ 十六进制/八进制/二进制数字字面量：`0x1A`、`0o12`、`0b101`。
- ✅ 错误恢复：非法字符报错后跳过继续扫描，不再产生 `INVALID` token 阻断解析。

**遗留缺陷**：
- Token 仅记录行号，无列号——错误定位精度受限（P1 级别问题）。

### 2.3 Parser（语法分析器）—— 评分：A-

**文件**：`src/parser/parser.cpp`（1114 行）、`parser.h`（71 行），合计 1185 行。

**亮点**：
- **Pratt 解析器**（优先级爬升法）实现正确，优先级表在 `getPrecedence()` 中集中管理，共 7 级。
- `**` 的右结合性正确处理；`? :` 作为特殊中缀运算符在 Pratt 循环内实现右结合短路语法。
- 赋值（`=`）和复合赋值（`+=` 等）在 Pratt 循环中统一处理，对 `VariableExpr` 和 `PropertyExpr` 分别生成对应节点。复合赋值通过 `clone()` 解糖为 `x = x + y`。
- `call()` 函数通过 `while(true)` 循环实现后缀链式调用：`foo()(1)[2].bar`。
- **Panic mode 错误恢复**：`synchronize()` 方法跳过 token 直到分号/右大括号/语句关键字，`parse()` 收集多条语句后统一判定 `hadError`。用户一次可看到多个语法错误。
- 14 种语句类型、14 种表达式类型，覆盖全面。

**遗留缺陷**：
- `funcStatement()` 和 `objStatement()` 中有冗余的 `dynamic_cast` + `unique_ptr` 所有权转移 hack（`BlockStmt` 转 `shared_ptr`），不够优雅。
- 错误信息格式简陋：`[line N] Error: message`，无列号、无源码行展示。

### 2.4 AST（抽象语法树）—— 评分：A-

**文件**：`src/ast/expr.h`（309 行，14 种 Expr）、`stmt.h`（271 行，13 种 Stmt）、`ast_printer.cpp`（345 行）、`expr_visitor.h`（41 行）、`stmt_visitor.h`（35 行），合计 1629 行。

**亮点**：
- **模板化 Visitor 模式**：`ExprVisitor<R>` 和 `StmtVisitor<R>` 按返回类型参数化——同一接口同时服务于解释器（`R=Value`/`void`）、字节码编译器（`R=void`）和 AST Printer（`R=string`）。添加新 pass 只需新增 `accept()` 重载，Visitor 接口无需复制。
- Expr 类型覆盖全面：Literal、Binary、Grouping、Unary、Variable、Assignment、Call、Array、Index、Property、PropertyAssignment、This、IncDec、Ternary——共 **14 种**。
- Stmt 类型覆盖全面：ExprStmt、LetStmt、BlockStmt、ReturnStmt、IfStmt、WhileStmt、ForStmt、FuncStmt、ObjStmt、BreakStmt、ContinueStmt、ThrowStmt、TryStmt——共 **13 种**。
- `Program::accept<R>()` 为非虚模板——Program 无子类无需虚分派，模板自动为所有 `R` 实例化。
- `LetStmt` 携带 `typeAnnotation`（`:int`/`:float`），语法上已为类型系统预留空间。

### 2.5 Interpreter（树遍历解释器）—— 评分：A-

**文件**：`src/interpreter/interpreter.cpp`（1476 行）、`interpreter.h`（145 行），合计 1621 行。

**亮点**：
- 正确实现词法作用域 + 闭包。`captureClosure()` 使用 `Environment::snapshot()` 深拷贝环境链。
- `ReturnSignal`/`BreakSignal`/`ContinueSignal`/`ThrowSignal` 四种异常模式实现非局部控制流，比维护标志位更安全可靠。
- 字符串插值功能完整：支持 `${var}` 和 `${obj.prop}` 多级属性访问，未找到变量时保留原文（降级优雅）。
- `BoundMethodCallable` 和 `ObjectConstructorCallable` 以内嵌 struct 实现，相关逻辑集中在一处。
- ✅ `+` 运算符：数字加法、`string + string`、`string + any`、`any + string`、`array + array`（合并）、`array + value`（追加）、`value + array`（前插）。
- ✅ `==`/`!=`：全类型相等性比较（10 种 variant 类型逐一处理，跨类型数值 `42 == 42.0` → `true`）。
- ✅ `&&`/`||`：短路求值，返回实际值（非布尔化），`and`/`or` 关键字形式等价。
- ✅ `++`/`--`：前缀/后缀，支持变量和属性。
- ✅ `? :`：三元条件，短路求值，右结合。
- ✅ Obj 单继承：`Obj Child : Parent` 语法，构造函数链根→叶执行，方法沿继承链查找。
- ✅ 12 个内建函数：`print`、`clock`、`input`、`int`、`float`、`len`、`type`、`range`、`assert`、`bin`、`oct`、`hex`。
- ✅ `try/catch/finally`：finally 始终执行（拦截所有控制流信号后重新抛出）。
- ✅ `throw` 自定义错误对象。
- ✅ 属性拦截：数组 7 个方法 + 字符串 11 个方法，双后端统一实现。

**遗留缺陷**：
- `isTruthy` 对引用类型（数组、函数、对象）始终返回 `true`——与主流脚本语言一致，不是 bug。
- `for-in` 已支持数组、字符串、`range()`，但不支持对象属性的遍历。
- 没有垃圾回收——`shared_ptr` 引用计数可处理大多数情况，但**循环引用导致内存泄漏**（如对象属性引用自身）。
- `interpolateString()` 每次求值都重新扫描字符串，对静态大文本有性能浪费。

### 2.6 VM（字节码编译器 + 栈式虚拟机）—— 评分：A-

**文件**：`src/vm/compiler.cpp`（1301 行）、`compiler.h`（183 行）、`vm.cpp`（1119 行）、`vm.h`（116 行）、`chunk.cpp`（325 行）、`chunk.h`（43 行）、`opcode.h`（88 行），合计 3175 行。**这是项目中最大、最复杂的子系统**（占代码总量的 34%）。

**架构**：双遍式编译器（AST → 字节码，无中间 IR）+ 栈式虚拟机。编译器实现 `ExprVisitor<void>` + `StmtVisitor<void>` + `ProgramVisitor<void>`，编译过程纯粹通过副作用完成（向 Chunk 写入字节码）。

**亮点**：

- **指令集**：50 条操作码，覆盖：
  - 栈操作（CONSTANT/NULL/TRUE/FALSE/POP）
  - 全局/局部变量（GET_GLOBAL/SET_GLOBAL/GET_LOCAL/SET_LOCAL）
  - 算术（8 条快速数值操作码跳过类型检查：ADD_NN/SUB_NN/MUL_NN/DIV_NN + 4 条常规操作码）
  - 比较 + 逻辑（EQUAL/GREATER/LESS/NOT/NEGATE）
  - 控制流（JUMP_IF_FALSE/JUMP/LOOP）
  - 函数/闭包（CALL/CLOSURE/RETURN/GET_UPVALUE/SET_UPVALUE）
  - 对象（CLASS/GET_PROPERTY/SET_PROPERTY/METHOD/INVOKE/INHERIT）
  - 异常（PUSH_CATCH/POP_CATCH/THROW/CLEAR_EXCEPTION/FINALLY_END）
  - 数组/索引（ARRAY/BUILD_SLICE/INDEX_SUBSCRIBE/STORE_SUBSCRIBE）
  - 字符串插值（INTERPOLATE）
- **全局变量驻留**（v0.9）：编译器对全局变量名进行整数驻留（`globalNames` 向量），VM 端使用 `OP_GET_GLOBAL <slot>` 实现 O(1) 数组索引取代 `unordered_map` 哈希查找。这是最有效的单点性能优化。
- **快速数值操作码**（v0.9）：`OP_ADD_NN` 等 8 条指令在 VM 层直接执行 `double` 运算，跳过 `std::variant` 类型检查分支。热路径性能接近原生。
- **常量折叠**（v0.9）：编译器在编译期对字面量算术/比较/一元运算直接求值，生成 `OP_CONSTANT` 而非多条指令。
- **闭包 upvalue 机制**（v0.8）：内层函数捕获外层局部变量通过 `OP_GET_UPVALUE`/`OP_SET_UPVALUE`，编译时 `resolveUpvalue()` 递归向外层搜索，运行时 VM 维护 open upvalue 链表正确共享捕获。
- **异常处理全覆盖**（v0.10）：
  - `OP_PUSH_CATCH <offset>` 注册 catch 处理器，`OP_THROW` 走 `throwException()` 查找最近处理器
  - `break`/`continue`/`return` 穿透 `finally` 块——编译器捕获这些非局部跳转，字节码级重放到 finally 块后跳至原目标
  - `OP_FINALLY_END`：通过 `exceptionInFlight` 标志位，finally 结束后自动将未捕获异常向外层 handler 传播
  - try 内 break/continue/return 自动发出 `OP_POP_CATCH` 清理 catch handler 栈
- **跳转回填**：`emitJump()`/`patchJump()`/`emitLoop()` 两遍式——先预留占位字节，后回填偏移量。无需中间表示（IR），编译效率高。
- **RLE 行号编码**：`Chunk` 使用游程编码存储行号和列号——密集指令序列在调试信息上节省大量内存。
- **调用帧栈**：`CallFrame` 结构保存函数闭包 + IP + 栈基址，支持深度递归。帧栈大小固定（256 帧）。
- **原生函数桥接**：`NativeFunction::callDirectly()` 绕过 `Interpreter&` 参数——VM 的 `OP_CALL` handler 可直接调用原生函数，无需 Interpreter 实例。

**遗留缺陷**：
- 常量池上限 256 条目（8-bit 索引）——对大型程序有潜在瓶颈。
- 无 GC——与解释器共享 `shared_ptr` 引用计数的内存模型。
- 编译器错误信息不如解释器详细——解析失败时用户需切换到 `--interpreter` 查看错误。

### 2.7 Runtime（运行时系统）—— 评分：A-

**文件**：`src/runtime/` 目录，合计 ~800 行。

**亮点**：
- `Value` 使用 `std::variant`（10 种类型），类型安全。`std::visit` 用于模式匹配。v0.12 新增 `int64_t`（整数类型）、`FunctionPrototype`（编译后函数原型）、`ClassData`（编译后类数据）。
- **双数值类型系统**：`int64_t` 和 `double` 共存，`isNumeric()`/`toDouble()`/`promoteToFloat()` 辅助函数统一处理类型提升。`int±int→int`，`int±float→float`，`/` 始终→`float64`。跨类型相等（`42 == 42.0` → `true`）。
- **`valueToString()`**：与 `printValue()` 并行，返回 `std::string` 用于字符串插值。
- `Environment` 支持两种 owning 模式：裸指针 `enclosing`（栈环境）和 `shared_ptr<Environment> enclosingOwned`（闭包环境）。设计体现了对 C++ 所有权语义的深刻理解。
- `Environment::snapshot()` 递归深拷贝环境链，正确实现闭包语义。
- `NativeFunction` 的 arity 为 `-1` 表示变参（`print`、`input`、`range`、`assert`），设计合理。`callDirectly()` 方法绕过 `Interpreter&` 参数供 VM 使用。`markAsBoundMethod()` 支持 VM 端方法绑定。
- `VoraFunction` 支持双模式构造：解释器模式（AST body + 闭包环境）和 VM 模式（`FunctionPrototype` + upvalues）。`isCompiled()` 区分两种模式。
- `Callable` 体系完整：`NativeFunction` → `VoraFunction` → `BoundMethodCallable` → `ObjectConstructorCallable`，通过 `getClassDef()` 虚方法支持继承。

**遗留缺陷**：
- `Environment::assign()` 使用 `const_cast` 移除 const（快照环境是 const 但赋值需要修改）。技术上不安全。
- **数组/字符串方法**（v0.12 已解决）：通过属性拦截在解释器和 VM 两后端均提供完整的 7 个数组方法 + 11 个字符串方法。
- `Value` 类型查询通过 `type()` 内建函数统一对外暴露。

### 2.8 构建系统 & 基础设施 —— 评分：A

**文件**：`CMakePresets.json`（20 个预设）、`cmake/toolchains/`（6 个交叉编译工具链）、`build.ps1`、`build.sh`、`CMakeLists.txt`（含 CPack）、`.github/workflows/ci.yml`。

**亮点**：
- CMakePresets v6 继承链：base → os-base → os-arch → os-arch-config，结构清晰。
- 交叉编译工具链覆盖 6 种场景：linux-i386、linux-aarch64、linux-armhf、windows-i386、windows-arm64、macos-universal。
- `build.ps1`/`build.sh` 提供一键构建体验：单命令覆盖 configure + build + package。
- CPack 打包输出：`.msi` + `.zip`（Windows）、`.deb` + `.rpm` + `.pkg.tar.xz`（Linux）、`.tar.gz`（macOS）。
- WiX v7 手动编写的 `.wxs` 生成 64-bit .msi，支持用户自定义安装目录 + 自动 PATH 注册。
- `.msi` ARP 元数据完整：ARPPRODUCTICON、ARPCOMMENTS、ARPNOREPAIR、ARPNOMODIFY——控制面板卸载体验干净。
- 应用图标：`.ico`（Windows 16/32/48/256px）、`.png`（512px）、`.iconset/`（macOS 9 文件）。
- CI：GitHub Actions 矩阵覆盖 Linux(4arch)×2config + Windows(3arch)×2config + macOS(1universal)×2config = 18 个组合。
- 整个构建系统零外部依赖——CMake 3.16+ 自身提供全部功能。

**意义**：构建系统质量在同类教学/小众语言项目中属于**顶尖水平**。大多数自定义语言项目只有 `make` + 单一平台二进制；Vora 直接做到了 CI-ready 的多架构交叉编译 + 原生系统包。

---

## 三、当前功能清单

### 3.1 类型系统

```
Value = null | int64 | float64 | bool | string | Array | Callable | ObjectInstance | FunctionPrototype | ClassData
```

10 种运行时类型。`int64` 和 `float64` 为双数值类型（`int±int→int`，`int±float→float`，`/` 始终→`float64`）。`let x:int` / `let x:float` 语法已解析但当前仅作文档用途。

### 3.2 运算符系统（完整实现）

| 优先级 | 运算符 | 结合性 | 状态 |
|--------|--------|--------|------|
| 1 | `=` | 右结合 | ✔ |
| 1 | `\|\|` `or` | 左结合 | ✔ 短路求值 |
| 2 | `&&` `and` | 左结合 | ✔ 短路求值 |
| 2 | `? :` | 右结合 | ✔ 短路求值 |
| 3 | `==` `!=` | 左结合 | ✔ 全类型 |
| 4 | `<` `<=` `>` `>=` | 左结合 | ✔ |
| 5 | `+` `-` | 左结合 | ✔ 数字+字符串+数组 |
| 6 | `*` `/` `%` | 左结合 | ✔ |
| 7 | `**` | 右结合 | ✔ |
| - | `++` `--` | 前后缀 | ✔ 变量和属性 |
| - | `+=` `-=` `*=` `/=` `%=` | 右结合 | ✔ 解糖为 `x = x + y` |

全部 21 个运算符已实现，在解释器和 VM 双后端均可用。

### 3.3 语句与流程控制

```vora
let x = 10                         // 变量声明（可选 :int / :float 标注）
x = 20                             // 赋值（沿作用域链查找）
{ let x = 30 }                     // 块作用域（遮蔽）
if (cond) { ... } else { ... }     // 条件分支
while (cond) { ... }               // 循环（支持 break/continue）
for item in iterable { ... }       // for-in（数组/字符串/range）
return value                       // 返回（函数内）
break / continue                   // 循环控制
throw value                        // 抛出任意值
try { ... } catch (e) { ... } finally { ... }  // 异常处理（VM 中 finally 路由完备）
```

### 3.4 函数与闭包

```vora
func makeCounter() {
    let count = 0
    func increment() { count = count + 1; return count }
    return increment
}
let c = makeCounter()
print(c())  // 1
print(c())  // 2
```

VM 闭包通过 upvalue 机制实现（`resolveUpvalue()` 编译期解析 + `openUpvalues` 运行时链表共享），比解释器的 `Environment::snapshot()` 全量深拷贝高效得多。

### 3.5 对象系统（含继承）

```vora
Obj Animal(name) {
    this.name = name
    func speak() { print("...") }
}

Obj Dog : Animal (name, breed) {
    this.breed = breed
    func speak() { print("Woof! I'm " + this.name) }
}

let d = Dog("Rex", "Husky")
d.speak()     // "Woof! I'm Rex"  — 子类覆盖
print(d.name) // "Rex"            — 父类构造函数设置
```

VM 对象编译：`visitObjStmt` 将构造函数体和方法分别编译为独立的 `FunctionPrototype`，包装为 `ClassData` 存入常量池。`OP_CLASS` 在运行时创建 `ObjectInstance` 并注册方法。继承通过 `OP_INHERIT` 实现父类原型链复制。

- ✅ 单继承：`Obj Child : Parent`
- ✅ 构造函数链：根→叶依次执行
- ✅ 方法查找沿继承链
- ✅ 方法调用自动绑定 `this`
- ❌ 无 `super` 关键字
- ❌ 无静态方法/类方法

### 3.6 字符串插值

```vora
let name = "World"
print("Hello ${name}!")           // "Hello World!"
print("(${p.x}, ${p.y})")         // 支持多级属性访问
print("${undefined_var}")         // 保留原文，不报错
```

解释器在 `LiteralExpr` 求值时完成插值；VM 编译器在编译期解析插值表达式，生成 `OP_INTERPOLATE` 指令。

### 3.7 内建函数（12 个）

| 函数 | 参数 | 说明 |
|------|------|------|
| `print(...)` | 变参 | 空格分隔输出，末尾换行 |
| `clock()` | 0 | Unix 时间戳（秒，毫秒精度） |
| `input(prompt?)` | 变参 | stdin 读取一行，可选 prompt |
| `int(value)` | 1 | 转 int64（截断），接受 number/string/bool |
| `float(value)` | 1 | 转 float64，接受 number/string/bool |
| `len(value)` | 1 | 返回数组长度或字符串字符数（int64） |
| `type(value)` | 1 | 返回类型名：`"int"`/`"float"`/`"string"`/`"array"`/`"bool"`/`"null"`/`"function"`/`"object"` |
| `range(...)` | 变参 | `range(stop)` / `range(start,stop)` / `range(start,stop,step)` |
| `assert(...)` | 变参 | `assert(condition, message?)` — 失败抛出 RuntimeError |
| `bin(num)` | 1 | 数字转二进制字符串 `"0b..."` |
| `oct(num)` | 1 | 数字转八进制字符串 `"0o..."` |
| `hex(num)` | 1 | 数字转十六进制字符串 `"0x..."` |

### 3.8 数组

```vora
let arr = [1, 2, "hello", true, null]
print(arr[0])           // 1
arr[1] = 42             // 索引赋值
let merged = arr + [4, 5]  // 数组合并
let appended = arr + 99    // 元素追加

// v0.12 新增内建方法
arr.add(42)             // 追加，无返回值
arr.pop()               // 弹出末尾，返回元素（空数组→null）
arr.length              // 元素个数（int64）
arr.insert(0, "first")  // 指定位置插入
arr.remove(2)           // 删除并返回指定位置元素
arr.indexOf("hello")    // 查找索引，未找到→-1
arr.clear()             // 清空所有元素
```

✅ 7 个内建方法：`.length`、`.add()`、`.pop()`、`.insert()`、`.remove()`、`.indexOf()`、`.clear()`。

### 3.9 字符串方法

```vora
let s = "Hello World"
s.length                // 11 (int64)
s.substring(0, 5)       // "Hello"；负数从末尾计
s.include("World")      // true
s.startsWith("He")      // true
s.endsWith("ld")        // true
s.upper()               // "HELLO WORLD"（新字符串）
s.lower()               // "hello world"
s.trim()                // 去除首尾空白
s.replace("l", "L")     // "HeLlo World"（首次）
s.replaceAll("l", "L")  // "HeLLo WorLd"（全部）
s.split(" ")            // ["Hello", "World"]
```

✅ 11 个内建方法：`.length`、`.substring()`、`.include()`、`.startsWith()`、`.endsWith()`、`.upper()`、`.lower()`、`.trim()`、`.replace()`、`.replaceAll()`、`.split()`。

### 3.10 测试框架

```
tests/
├── lexer/       (2 files)
├── parser/      (2 files)
├── runtime/     (3 files)
├── interpreter/ (4 files)
├── benchmarks/  (4 files)
├── run_tests.ps1
└── run_tests.sh
```

13 个 `.va` 测试文件 + 4 个基准测试。`run_tests.ps1`/`run_tests.sh` 支持 `-Interpreter`/`--interpreter` 参数切换后端。**当前全部 13 个测试在 VM 和解释器模式下均通过。**

---

## 四、程序员视角的痛点清单

以下仅列**仍然存在的问题**（已修复项不再占用篇幅）。相比上一版，VM 异常处理、finally 路由、跨平台构建等已从痛点清单移除。

### 4.1 🔴 致命级（v0.12 已全部解决 ✅）

1. ✅ **`len()` / 长度查询** — 已实现。`len("hello")` → `5`，`len([1,2,3])` → `3`（返回 int64）。

2. ✅ **数组内建方法** — 已实现。7 个方法：`.length`、`.add()`、`.pop()`、`.insert()`、`.remove()`、`.indexOf()`、`.clear()`。

3. ✅ **字符串内建方法** — 已实现。11 个方法：`.length`、`.substring()`、`.include()`、`.startsWith()`、`.endsWith()`、`.upper()`、`.lower()`、`.trim()`、`.replace()`、`.replaceAll()`、`.split()`。

### 4.2 🟠 严重级（破坏开发体验）

4. **错误信息不含列号**——只有行号，在长行中定位困难。无调用栈追踪。

5. ✅ **`type()` 类型查询** — v0.12 已实现。`type(42)` → `"int"`，`type(3.14)` → `"float"`，`type("hi")` → `"string"`，区分 int/float。

6. **`shared_ptr` 循环引用导致内存泄漏**——对象属性可引用自身，引用计数永不为零。

7. **REPL 不能多行输入**——无法在 REPL 中定义函数或类。

### 4.3 🟡 中度级（限制语言表达能力）

8. ✅ **整数类型** — v0.12 已实现。`int64_t` 和 `double` 双数值类型，`42` 解析为 int64，`3.14` 解析为 float64。自动提升规则（int±int→int, int±float→float, /→float）。跨类型相等（`42 == 42.0` → `true`）。

9. **没有 `switch` / `match`**——多分支只能用 `if-else if` 链。

10. **不支持默认参数**
    ```vora
    func greet(name = "World") { ... }  // ❌
    ```

11. **没有 `super` 关键字**——子类无法显式调用父类被覆盖的方法。

12. **`for-in` 不支持对象属性遍历**——仅数组、字符串、range。

### 4.4 🟢 轻度级（影响生态和工程化）

13. **没有模块系统**（`import`/`export`）——所有代码必须写在一个文件里。

14. **没有标准库**——文件 IO、数学函数、字符串操作等都需要手写。`std/` 目录为空。

15. **常量池上限 256 条目**——8-bit 索引，大型程序可能触发限制。

---

## 五、UX 完善路线图

按优先级分 P0-P3。已完成项标记 ✅。

### P0 — 立即修复（v0.12 全部完成 ✅）

#### 5.1 `len()` 内建函数 ✅

**已完成**（v0.12）。返回 int64 类型。支持数组和字符串。

#### 5.2 `type()` 内建函数 ✅

**已完成**（v0.12）。返回 `"int"`/`"float"`（区分双数值类型）、`"string"`、`"array"`、`"bool"`、`"null"`、`"function"`、`"object"`。

#### 5.3 数组内建属性和方法 ✅

**已完成**（v0.12）。7 个方法：`.length`、`.add()`、`.pop()`、`.insert()`、`.remove()`、`.indexOf()`、`.clear()`。通过属性拦截实现。

#### 5.4 字符串内建属性和方法 ✅

**已完成**（v0.12）。11 个方法：`.length`、`.substring()`、`.include()`、`.startsWith()`、`.endsWith()`、`.upper()`、`.lower()`、`.trim()`、`.replace()`、`.replaceAll()`、`.split()`。通过属性拦截实现。

#### 5.5 改进错误信息

**工作量**：2 小时

- Token 增加 `column` 字段
- 错误输出格式：`文件名:行号:列号: 错误类型: 详细信息`
- 打印出错行源代码 + `^` 指示符

---

### P1 — 短期完善（1-2 周）

#### 5.6 数组/字符串方法扩展 ✅

**已完成**（v0.12）。数组全部 7 个方法 + 字符串全部 11 个方法已在双后端（解释器 + VM）实现。

#### 5.7 默认参数

```vora
func greet(name = "World") { print("Hello " + name) }
```

**实现**：`FuncStmt` 增加默认值列表；`callFunction` 中用默认值填充缺失的实参。

**工作量**：3-4 小时

#### 5.8 `super` 关键字

```vora
Obj Dog : Animal (name, breed) {
    this.breed = breed
    func speak() {
        super.speak()   // 调用父类方法
        print(" (breed: " + this.breed + ")")
    }
}
```

**实现**：新增 `SuperExpr` AST 节点，`visitSuperExpr` 中沿 `parentClass` 链查找方法。VM 编译器需新增 `OP_GET_SUPER` 指令。

**工作量**：4-5 小时（需同时在双后端实现）

#### 5.9 `switch` / `match` 语句

```vora
match x {
    1 -> { print("one") }
    2, 3 -> { print("two or three") }
    else -> { print("other") }
}
```

**工作量**：4-5 小时

---

### P2 — 中期功能（1-3 个月）

#### 5.10 模块系统 `import` / `export`

```vora
import "math"       // 执行 math.va，返回模块对象
print(math.PI)
```

- 文件系统路径解析
- 模块缓存（避免重复加载）
- 相对路径支持
- 导出符号的命名空间隔离

**工作量**：8-12 小时（涉及双后端 + 路径解析）

#### 5.11 `int64` / `float64` 双数值类型 ✅

**已完成**（v0.12）。在 `Value` variant 中增加 `int64_t`：
- `int64 ±/* int64 → int64`
- `int64 ±/* float64 → float64`（自动提升）
- `/` 总是返回 `float64`
- 跨类型数值相等（`42 == 42.0` → `true`）
- `type()` 区分 `"int"` 和 `"float"`

#### 5.12 解析器错误信息增强

在当前的 `synchronize()` 基础上：
- Token 携带列号
- 错误信息显示源码行 + `^` 指示符
- 区分 Error 和 Warning 级别

**工作量**：4-6 小时

---

### P3 — 长期工程（3-6 个月+）

#### 5.13 字节码优化

当前 VM 已具备 Phase 1-4 全部功能，但仍可进一步优化：

- **常量池扩容**：从 8-bit（256 条目）升级到 16-bit（65536 条目），消除大型程序瓶颈
- **Superinstruction**：合并高频指令对（如 `GET_LOCAL + CALL` → `CALL_LOCAL`），减少 dispatch 开销
- **NaN Boxing**：将 `Value` 从 `std::variant`（24-32 bytes）压缩到 8 bytes（64-bit double NaN 空间编码所有类型），消除 variant 访问开销和 shared_ptr 控制块开销
- **内联缓存（Inline Cache）**：为属性访问和方法调用添加 PIC（多态内联缓存），预期 OOP 代码 3-5× 加速

**工作量**：2-4 周

#### 5.14 标记-清除垃圾回收器

替换 `shared_ptr` 引用计数，解决循环引用问题。三色标记算法。

**工作量**：1-2 周

#### 5.15 标准库

```
std/
├── math.va     // sqrt, abs, sin, cos, random, PI
├── string.va   // split, join, trim, replace
├── io.va       // readFile, writeFile, readLine
└── os.va       // args, exit, env
```

**工作量**：持续

#### 5.16 LSP 服务器

基于 Vora 解析器实现 Language Server Protocol：诊断、跳转定义、自动补全。

**工作量**：2-3 周（MVP：诊断 + 跳转定义）

#### 5.17 C++ 单元测试

- Google Test 框架测试 Lexer/Parser/Compiler/VM 各单元
- 补充 `.va` 集成测试覆盖率

**工作量**：1 周搭建 + 持续编写

---

### 已完成功能清单（供参考）

| 功能 | 版本 | 实现方式 |
|------|------|----------|
| 字节码 VM Phase 1（表达式/全局变量/控制流） | v0.6 | Compiler + 栈式 VM，32 条指令 |
| VM Phase 2（局部变量/break/continue/for-in/函数/try-catch） | v0.7 | 调用帧栈 + 异常处理栈 |
| VM Phase 3（对象/继承/方法调用） | v0.7 | OP_CLASS + OP_INHERIT + OP_METHOD |
| VM Phase 4（闭包 upvalue/字符串插值/异常 catch 完善） | v0.8 | resolveUpvalue + openUpvalues + OP_INTERPOLATE |
| VM 设为默认后端 | v0.8 | main.cpp 默认路径 |
| `++/--` 后缀修复 | v0.8 | IncDecExpr prefix/postfix 标记正确区分 |
| VM 性能优化（全局变量驻留/快速数值操作码/常量折叠/移动语义） | v0.9 | parallel arrays + 8 条 NN 操作码 + 编译期求值 |
| VM 异常处理完善（finally 路由/异常重抛/catch handler 清理） | v0.10 | 编译器捕获非局部跳转 + exceptionInFlight + OP_CLEAR_EXCEPTION |
| 多架构构建系统 + 交叉编译工具链（6 个） | v0.11 | CMakePresets v6（20 预设） + cmake/toolchains/ |
| 原生打包（.msi/.deb/.rpm/.pkg.tar.xz） | v0.11 | CPack + WiX v7 手动 .wxs |
| 64-bit MSI 安装程序（自定义目录 + PATH + 控制面板图标） | v0.11 | WiX v7 wix build -arch x64 |
| 应用图标（.ico/.png/.icns） | v0.11 | Python+Pillow SVG→PNG 渲染 |
| 跨平台构建脚本（build.ps1 + build.sh） | v0.11 | 一键构建 + 架构/配置/打包标志 |
| GitHub Actions CI（18 个 os×arch×config 组合） | v0.11 | 矩阵策略 + 条件交叉编译工具链安装 |
| 运行时错误调用栈回溯 | v0.14 | captureStackTrace + RLE 行列解码 + pre-unwind 快照 |
| C3 多继承 | v0.15 | C3 线性化 MRO + weak_ptr 无环 + 构造链逆序 |
| Dict 字典 | v0.16 | `{k: v}` 字面量 + 4 个内建方法 + Dict+Dict 合并 |
| for-in 对象/字典 | v0.16 | 遍历 object properties / dict keys |
| VM 栈动态扩容 | v0.16 | `std::vector` 按需增长，突破 1024 硬限制 |
| 常量池 16 位索引 | v0.16 | OP_CONSTANT_LONG，上限 65535 |
| 错误源码行+插入符 | v0.17 | printSourceLine 定位源码行 + ^ 列标记 |
| 字符串 `+` 拼接 | v0.4 | visitBinaryExpr 中 string + string/any |
| `break` / `continue` | v0.5 | BreakSignal / ContinueSignal 异常模式 |
| 复合赋值 `+=` 等 | v0.5 | Pratt 循环中解糖为 `x = x + y` |
| 三元运算符 `? :` | v0.6 | TernaryExpr，Pratt 优先级 2 右结合 |
| Obj 单继承 | v0.7 | Obj Child : Parent，构造函数链根→叶 |
| `&&` / `\|\|` 短路求值 | v0.4 | visitBinaryExpr 中最先处理 |
| Unicode 标识符 | v0.4 | isAlpha() 接受 >127 字节 |
| 字符串转义序列 | v0.4 | \n \t \r \0 \\ \" \' |
| 块注释嵌套 | v0.4 | blockComment() depth 计数器 |
| `try`/`catch`/`finally` | v0.7 | TryStmt + ThrowSignal，finally 始终执行 |
| `throw` 自定义错误 | v0.7 | ThrowSignal{Value}，catch 绑定原值 |
| 测试框架 | v0.7 | assert() + 13 个 .va 测试文件 + runner |
| 内建函数（12 个） | v0.5-v0.12 | print/clock/input/int/float/len/type/range/assert/bin/oct/hex |
| int64/float64 双数值类型 | v0.12 | Value variant 增加 int64_t，自动提升规则，跨类型相等 |
| 数组内建方法（7 个） | v0.12 | .length/.add/.pop/.insert/.remove/.indexOf/.clear，属性拦截 |
| 字符串内建方法（11 个） | v0.12 | .length/.substring/.include/.startsWith/.endsWith/.upper/.lower/.trim/.replace/.replaceAll/.split |
| 数字字面量前缀 | v0.5 | 0x/0o/0b |
| AST Printer + Token debug | v0.6 | S-expression 输出 + Token 流打印 |
| UTF-8 BOM 处理 | v0.4 | 自动去除 EF BB BF |

---

## 六、架构演进建议

### 6.1 短期架构调整（P1 阶段完成）

#### 6.1.1 统一 Value 类型查询

当前 `std::holds_alternative<T>()` 散落在解释器和 VM 各处。建议增加：

```cpp
enum class ValueType { Null, Number, Boolean, String, Array, Function, Object };
ValueType valueType(const Value& value);
```

#### 6.1.2 解决 `const_cast` 问题

`Environment::assign()` 对快照环境使用 `const_cast`。闭包环境不应是 const——属性赋值和变量修改需要可变环境。建议将快照环境改为非 const。现在 VM 中闭包走 upvalue 机制已经规避了这个问题，但解释器路径仍然存在。

### 6.2 中期架构升级（P2-P3 阶段）

#### 6.2.1 统一错误报告接口

```cpp
class ErrorReporter {
    virtual void error(int line, int col, const string& msg) = 0;
    virtual bool hadError() const = 0;
};
```

为 LSP 集成和测试打下基础。

#### 6.2.2 常量池容量扩展

`Chunk::writeConstant()` 当前使用 8-bit 索引（`uint8_t`），上限 256 个常量。扩展为 16-bit（`uint16_t`）需要：
- 新增 `OP_CONSTANT_LONG` 操作码（后跟 2 字节索引）
- 或统一所有 CONSTANT 指令为 16-bit 索引
- 向后兼容：编译器自动选择 8-bit 或 16-bit 编码

#### 6.2.3 NaN Boxing（值表示革命）

这是 Vora 性能的最大单点提升机会：

```
当前 std::variant<7 types> = 24-32 bytes per Value
NaN Boxing: 8 bytes per Value (double 的 NaN 空间编码指针/tag)
```

- 栈容量翻三倍（1024 → 3072 in same memory）
- 消除 `std::visit` / `std::get` 分支预测失败
- 消除 `shared_ptr` 控制块分配（小对象内联）
- 与 LuaJIT 的设计思路一致

**实现复杂度较高**：需处理 64-bit 架构下的指针压缩、tag 编码、跨平台兼容。建议在 VM 成熟稳定后（v0.3+）再启动。

---

## 七、总结

### 7.1 模块一句话评价

| 模块 | 行数 | 评价 |
|------|------|------|
| Lexer | 598 | 简洁高效：转义/Unicode/嵌套注释/错误恢复/0x/0o/0b 数字前缀。含列号。 |
| Parser | 986 | Pratt 实现优雅，panic-mode 恢复，15 Expr + 13 Stmt 种节点。含默认参数、多继承语法。 |
| AST | 1,430 | 模板化 Visitor 模式，类型体系完整（28 种节点），泛型接口支持 3 种返回类型。 |
| **Compiler+VM** | **3,744** | **项目核心**：50+ 条指令/栈式 VM/upvalue 闭包/异常全覆盖/全局驻留/快速数值操作码/常量折叠/属性拦截/Dict。v0.18 全特性完成。 |
| Runtime | 1,140 | `std::variant` 类型安全（11 类型），双数值系统+自动提升，Environment shared_ptr-only，Callable 统一接口（BoundMethod/ClassConstructor/VoraFunction/NativeFunction），Dict 内建方法。 |
| GC | 300 | GcHeap 标记-清除 + GcPtr 非拥有指针 + GcObject 基类。自动回收循环引用。 |
| Formatter | 630 | AST 驱动的源代码格式化器，`vora fmt` 命令。 |
| 构建系统 | - | **A 级**：CMakePresets(20)+交叉编译(6)+CPack(4 格式)+CI(18 组合)+图标。同类项目中顶尖。 |

### 7.2 Vora 的差异化定位（v0.17 更新）

| 维度 | Lua 5.4 | Python 3 | JavaScript (V8) | Wren | Vora (v0.18) |
|------|---------|----------|----------------|------|--------------|
| 实现复杂度 | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ |
| 语言简单性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| 执行性能 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ (VM 原生操作码) |
| OOP 模型 | 原型式 | 类式 | 原型式 | 类式 | 类式(轻量+C3多继承) |
| 标准库 | 极小 | 庞大 | 中等 | 小 | 极小（12 内建函数 + 22 内建方法） |
| 嵌入友好 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ (GC + 零依赖) |
| 构建 & 打包 | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ (CMakePresets + 原生包) |
| 内存管理 | GC | GC | GC | GC | GC (标记-清除) |

**Vora 的潜在优势领域**：
- **教学用语言实现**：比 Lox 更完整的对象系统和异常处理，比 Lua 源码更易阅读
- **C++ 应用嵌入式脚本**：零外部依赖 + CMake 集成，5 分钟即可嵌入
- **配置/DSL 语言**：类 JS 语法亲和、可选分号、字符串插值自然适合配置文件
- **快速原型工具**：REPL + 动态类型 + 对象系统，适合数据探索和脚本自动化

### 7.3 与上一版的对比

| 指标 | 2026-05-31 | 2026-06-13 (v0.17) | 2026-06-15 (v0.18) |
|------|-----------|-------------------|-------------------|
| 总代码量 | ~6500 行 | ~8700 行 | ~9000 行 |
| VM 代码量 | ~1000 行 | ~3400 行 | ~3744 行 |
| VM 指令数 | 32 | 50+ | 50+ |
| Value 类型数 | 7 | 11 | 11 |
| 数值类型 | double only | int64 + float64 | int64 + float64 |
| 内建函数 | 10 | 12 | 12 |
| 内建方法 | 0 | 22（7+11+4） | 22（7+11+4） |
| GC | 无（shared_ptr） | 无（shared_ptr） | 标记-清除（GcHeap） |
| Class 表示 | ClassData + ObjectClass | ClassDefinition | ClassDefinition（统一） |
| Environment | 裸指针 + shared_ptr | shared_ptr | shared_ptr-only |
| Callable | 含 Interpreter& | 含 Interpreter& | 纯 Value 接口 |
| 格式化器 | 无 | 无 | vora fmt |
| 测试 | 13/13 | 20/20 | 20/20 + 66 LeetCode |
| 构建系统 | CMakePresets | 不变 | 不变 |
| CI | GitHub Actions | 不变 | + windows-2022 固定 |

### 7.4 给贡献者的建议

1. ✅ **P0 已全部完成**（`len`、`type`、数组/字符串方法、int64/float64）——Vora 已从"能运行"跨越到"能写脚本"。
2. ✅ **P1 核心已全部完成**（默认参数、`super`、多继承 C3 MRO、GC）——语言表达力大幅提升
3. **当前 P0**（模块系统 + 标准库）——赋予 Vora 多文件工程能力，这是从嵌入式脚本到独立语言的质变
4. **P1**（`switch`/`match`、REPL 多行输入）——进一步提升开发体验
5. **P2**（NaN Boxing、尾调用优化）——性能和安全性
6. **P3**（LSP、调试器、包管理器）——生态和工具链

---

> **后记（2026-06-15 修订）**：Vora v0.18 在 ~9000 行 C++17 代码内完成了一个带有标记-清除垃圾回收器的完整动态类型脚本语言实现。核心架构重构（Callable 拆分 + ClassDefinition 统一 + Environment shared_ptr-only + GC）使代码更干净、更安全。vora fmt 格式化器和 66 个 LeetCode 集成测试进一步提升了工程质量。
>
> Vora 现在的架构是：Lexer → Parser → Compiler → Chunk → VM，加上 GcHeap 管理所有堆对象。模块系统和标准库是下一步自然目标。
