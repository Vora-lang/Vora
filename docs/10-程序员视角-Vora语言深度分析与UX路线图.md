# 程序员视角：Vora 语言深度分析与 UX 完善路线图

> 作者按：本文从一线程序员的视角，基于对 Vora 源码（~5500 行 C++17）的完整阅读，剖析这门语言的设计哲学、实现质量、现有缺陷，并提出按优先级排序的用户体验完善路线图。

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
| **Crafting Interpreters (Lox)** | 架构骨架：Lexer → Pratt Parser → Tree-walking Interpreter。`Expr`/`Stmt` visitor 设计直接源自此书。 |
| **Lua 5.4** | 极简类型系统、`Environment` 作用域链、闭包语义。 |
| **Wren** | 对象模型——`Obj` 声明内嵌方法定义，`this` 自动绑定。 |
| **JavaScript (隐含)** | `let` 声明、`for-in`、字符串插值 `${}`、`&&`/`||` 短路返回值（非布尔化）、可选分号。 |
| **CPython** | 命名风格（`RuntimeError`、`RuntimeMode`），`std::variant` 作为 tagged union。 |

### 1.2 语言性格

Vora 处于 **"教学语言"与"实用脚本语言"的交叉地带**。解释器代码清晰、可读性强，适合学习语言实现原理；同时对象系统、异常处理、闭包等特性已超越玩具语言范畴。

**一句话概括**：Vora 是一门类 JavaScript 语法、Lua 级简单性、Wren 式面向对象的动态类型脚本语言，拥有双后端执行架构（树遍历解释器 + 字节码 VM Phase 1），正快速向实用化演进。

---

## 二、实现架构质量评估

### 2.1 整体印象：**B+ 级代码**（~6500 行 C++17）

模块边界清晰，单一人维护下工程质量良好。零外部依赖，纯 C++17 + STL。双后端架构（解释器 + 字节码 VM）已就位。

### 2.2 Lexer（词法分析器）—— 评分：A-

**文件**：`src/lexer/lexer.cpp`（321 行）、`lexer.h`（60 行）、`token.h`（103 行）

**亮点**：
- 经典手写词法分析器，单遍扫描，代码简洁。
- 21 个关键字使用 `unordered_map` 做 O(1) 识别。
- 支持 `//` 行注释和 `/* */` 块注释，块注释支持嵌套（depth 计数器）。
- 正确处理全部多字符运算符：`++`、`--`、`**`、`&&`、`||`、`==`、`!=`、`<=`、`>=`、`+=`、`-=`、`*=`、`/=`、`%=`。
- 浮点数解析使用 `peekNext()` 避免 `a.b` 被误解析为数字。
- ✅ Unicode 标识符：`isAlpha()` 将所有 UTF-8 多字节字节（> 127）视为合法标识符字符。
- ✅ 转义序列：`\n`、`\t`、`\r`、`\0`、`\\`、`\"`、`\'`，未识别转义保留原文。
- ✅ 单引号字符串 `'...'` 与双引号等价。

**缺陷**：
- Token 仅记录行号，无列号——错误定位精度受限。
- ✅ 错误恢复：非法字符报错后跳过继续扫描，不再产生 `INVALID` token 阻断解析。
- ✅ 十六进制/八进制/二进制数字字面量：`0x1A`、`0o12`、`0b101`，以及 `bin()`/`oct()`/`hex()` 转换函数。

### 2.3 Parser（语法分析器）—— 评分：B+

**文件**：`src/parser/parser.cpp`（816 行）、`parser.h`（71 行）

**亮点**：
- **Pratt 解析器**（优先级爬升法）实现正确，优先级表在 `getPrecedence()` 中集中管理，共 7 级。
- `**` 的右结合性正确处理；`? :` 作为特殊中缀运算符在 Pratt 循环内实现右结合短路语法。
- 赋值（`=`）和复合赋值（`+=` 等）在 Pratt 循环中统一处理，对 `VariableExpr` 和 `PropertyExpr` 分别生成对应节点。复合赋值通过 `clone()` 解糖为 `x = x + y`。
- `call()` 函数通过 `while(true)` 循环实现后缀链式调用：`foo()(1)[2].bar`。
- **Panic mode 错误恢复**：`synchronize()` 方法跳过 token 直到分号/右大括号/语句关键字，`parse()` 收集多条语句后统一判定 `hadError`。用户一次可看到多个语法错误。
- 14 种语句类型、14 种表达式类型，覆盖全面。

**缺陷**：
- `funcStatement()` 和 `objStatement()` 中有冗余的 `dynamic_cast` + `unique_ptr` 所有权转移 hack（`BlockStmt` 转 `shared_ptr`），不够优雅。
- `objStatement()` 的方法/语句区分依赖于 `dynamic_cast<FuncStmt*>`——`FuncStmt` 归入 `methods`，其他语句归入构造函数 `body`。这个隐式约定缺乏文档。
- 错误信息格式简陋：`[line N] Error: message`，无列号、无源码行展示。

### 2.4 AST（抽象语法树）—— 评分：A-

**文件**：`src/ast/expr.h`（309 行，14 种 Expr）、`stmt.h`（271 行，13 种 Stmt）、`ast_printer.cpp`（345 行）、`expr_visitor.h`（41 行）、`stmt_visitor.h`（35 行）

**亮点**：
- **模板化 Visitor 模式**：`ExprVisitor<R>` 和 `StmtVisitor<R>` 按返回类型参数化——同一接口同时服务于解释器（`R=Value`/`void`）、字节码编译器（`R=void`）和 AST Printer（`R=string`）。添加新 pass 只需新增 `accept()` 重载，Visitor 接口无需复制。
- Expr 类型覆盖全面：Literal、Binary、Grouping、Unary、Variable、Assignment、Call、Array、Index、Property、PropertyAssignment、This、IncDec、Ternary——共 **14 种**。
- Stmt 类型覆盖全面：ExprStmt、LetStmt、BlockStmt、ReturnStmt、IfStmt、WhileStmt、ForStmt、FuncStmt、ObjStmt、BreakStmt、ContinueStmt、ThrowStmt、TryStmt——共 **13 种**。
- `Program::accept<R>()` 为非虚模板——Program 无子类无需虚分派，模板自动为所有 `R` 实例化。
- `LetStmt` 携带 `typeAnnotation`（`:int`/`:float`），语法上已为类型系统预留空间。

### 2.5 Interpreter（解释器）—— 评分：B+

**文件**：`src/interpreter/interpreter.cpp`（1477 行）、`interpreter.h`（145 行）

**亮点**：
- 正确实现词法作用域 + 闭包。`captureClosure()` 使用 `Environment::snapshot()` 深拷贝环境链。
- `ReturnSignal`/`BreakSignal`/`ContinueSignal`/`ThrowSignal` 四种异常模式实现非局部控制流，比维护标志位更安全。
- 字符串插值功能完整：支持 `${var}` 和 `${obj.prop}` 多级属性访问，未找到变量时保留原文（降级优雅）。
- `BoundMethodCallable` 和 `ObjectConstructorCallable` 以内嵌 struct 实现，相关逻辑集中在一处。
- ✅ `+` 运算符：数字加法、`string + string`、`string + any`、`any + string`、`array + array`（合并）、`array + value`（追加）、`value + array`（前插）。
- ✅ `==`/`!=`：全类型相等性比较（7 种 variant 类型逐一处理，引用类型按指针相等）。
- ✅ `&&`/`||`：短路求值，返回实际值（非布尔化），`and`/`or` 关键字形式等价。
- ✅ `++`/`--`：前缀/后缀，支持变量和属性。
- ✅ `? :`：三元条件，短路求值。
- ✅ Obj 单继承：`Obj Child : Parent` 语法，构造函数链根→叶执行，方法沿继承链查找。
- ✅ 10 个内建函数：`print`、`clock`、`input`、`int`、`float`、`range`、`assert`、`bin`、`oct`、`hex`。 

**缺陷**：
- `isTruthy` 当前对引用类型（数组、函数、对象）始终返回 `true`——无法表达"空数组/空对象为假"（但这与主流脚本语言一致）。
- `for-in` 已支持数组、字符串、`range()`，但不支持对象属性的遍历。
- 没有垃圾回收——`shared_ptr` 引用计数可处理大多数情况，但**循环引用导致内存泄漏**（如对象属性引用自身）。
- `interpolateString()` 每次求值都重新扫描字符串，对于静态大文本有性能浪费。

### 2.6 Runtime（运行时系统）—— 评分：B+

**文件**：`src/runtime/` 目录，共 ~550 行

**亮点**：
- `Value` 使用 `std::variant`（7 种类型），类型安全。
- `Environment` 支持两种 owning 模式：裸指针 `enclosing`（栈环境）和 `shared_ptr<Environment> enclosingOwned`（闭包环境）。设计体现了对 C++ 所有权语义的深刻理解。
- `Environment::snapshot()` 递归深拷贝环境链，正确实现闭包语义。
- `NativeFunction` 的 arity 为 `-1` 表示变参（`print`、`input`、`range`、`assert`），设计合理。
- `Callable` 体系完整：`NativeFunction` → `VoraFunction` → `BoundMethodCallable` → `ObjectConstructorCallable`，通过 `getClassDef()` 虚方法支持继承。

**缺陷**：
- `Environment::assign()` 使用 `const_cast` 移除 const（快照环境是 const 的但赋值需要修改）。技术上不安全。
- `Value` 缺少类型查询接口——`std::holds_alternative<T>()` 散落在解释器各处。
- `Array` 过于简单：仅 `vector<Value> elements`，无 `.length`、`.push()` 等方法接口。

---

## 三、当前功能清单

### 3.1 类型系统

```
Value = null | double | bool | string | Array | Callable | ObjectInstance
```

7 种运行时类型。`double` 为唯一数值类型（`1/2 = 0.5`，与 Python 3 一致）。`let x:int` / `let x:float` 语法已解析但不强制校验。

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

全部 21 个运算符已实现，无缺失。

### 3.3 语句与流程控制

```vora
let x = 10                    // 变量声明（可选 :int / :float 标注）
x = 20                        // 赋值（沿作用域链查找）
{ let x = 30 }                // 块作用域（遮蔽）
if (cond) { ... } else { ... }  // 条件分支
while (cond) { ... }          // 循环（支持 break/continue）
for item in iterable { ... }  // for-in（数组/字符串/range）
return value                  // 返回（函数内）
break / continue              // 循环控制
throw value                   // 抛出任意值
try { ... } catch (e) { ... } finally { ... }  // 异常处理
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

闭包通过 `Environment::snapshot()` 实现——定义时深拷贝整条作用域链。语义正确但内存开销较大（每次创建闭包都完整拷贝）。

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

实现要点：
- `Obj Child : Parent` — 单继承，`:` + 父类名
- 构造函数链：从根到叶依次执行父类构造函数（参数按索引传递）
- 方法查找：实例 → 子类 methods → 父类 methods → ...
- 方法调用返回 `BoundMethodCallable`（自动绑定 `this`）
- ❌ 无 `super` 关键字（无法显式调用父类方法）
- ❌ 无静态方法/类方法

### 3.6 字符串插值

```vora
let name = "World"
print("Hello ${name}!")           // "Hello World!"
print("(${p.x}, ${p.y})")         // 支持多级属性访问
print("${undefined_var}")         // 保留原文，不报错
```

在 `LiteralExpr` 求值时完成插值。

### 3.7 内建函数（10 个）

| 函数 | 参数 | 说明 |
|------|------|------|
| `print(...)` | 变参 | 空格分隔输出，末尾换行 |
| `clock()` | 0 | Unix 时间戳（秒，毫秒精度） |
| `input(prompt?)` | 变参 | stdin 读取一行，可选 prompt |
| `int(value)` | 1 | 转整数（截断），接受 number/string/bool |
| `float(value)` | 1 | 转浮点数，接受 number/string/bool |
| `range(...)` | 变参 | `range(stop)` / `range(start,stop)` / `range(start,stop,step)` |
| `assert(...)` | 变参 | `assert(condition, message?)` — 失败抛出 RuntimeError |
| `bin(num)` | 1 | 数字转二进制字符串 `"0b..."` |
| `oct(num)` | 1 | 数字转八进制字符串 `"0o..."` |
| `hex(num)` | 1 | 数字转十六进制字符串 `"0x..."` |

数字字面量支持 `0b`（二进制）、`0o`（八进制）、`0x`（十六进制）前缀。

### 3.8 数组

```vora
let arr = [1, 2, "hello", true, null]
print(arr[0])       // 1
arr[1] = 42         // 索引赋值
let merged = arr + [4, 5]  // 数组合并
let appended = arr + 99    // 元素追加
```

❌ 无 `.length`、`.push()`、`.pop()` 等方法/属性。

### 3.9 测试框架

```
tests/
├── lexer/       (2 files)
├── parser/      (2 files)
├── runtime/     (3 files)
└── interpreter/ (4 files — 含 logical、errors、builtins、edge_cases)
```

11 个 `.va` 测试文件 + `run_tests.ps1`/`run_tests.sh`。使用 `assert()` 内建函数。CI 和 C++ 单元测试待建立。

---

## 四、程序员视角的痛点清单

以下仅列**仍然存在的问题**（已修复项不再占用篇幅）。

### 4.1 🔴 致命级（阻碍基本使用）

1. **没有 `len()` / 长度查询**
   ```vora
   print(len("hello"))  // ❌ 未定义
   print(len([1,2,3]))  // ❌ 未定义
   ```
   这是日常编程最常用的操作之一。

2. **数组没有基本方法/属性**
   ```vora
   let arr = [1, 2, 3]
   arr.push(4)     // ❌ 无法调用
   arr.length      // ❌ 属性不存在
   ```
   数组是"死"的数据结构——能创建和索引，但除此之外什么也做不了。

3. **字符串没有方法**
   ```vora
   let s = "hello"
   s.length()         // ❌
   s.substring(0, 3)  // ❌
   ```

### 4.2 🟠 严重级（破坏开发体验）

4. **错误信息不含列号**——只有行号，在长行中定位困难。无调用栈追踪。

5. **没有类型查询函数**（`type()`）——运行时无法知道值的类型，调试困难。

6. **`shared_ptr` 循环引用导致内存泄漏**——对象属性可引用自身，引用计数永不为零。

7. **REPL 不能多行输入**——无法在 REPL 中定义函数或类。

### 4.3 🟡 中度级（限制语言表达能力）

8. **没有整数类型**——无法进行位运算或精确整数运算。所有数字统一为 `double`。

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

15. **闭包快照开销大**——`Environment::snapshot()` 每次创建闭包都深拷贝整条作用域链。

---

## 五、UX 完善路线图

按优先级分 P0-P3。已完成项标记 ✅。

### P0 — 立即修复（本周内）

#### 5.1 `len()` 内建函数

**工作量**：30 分钟

```cpp
defineNative("len", 1, [](const std::vector<Value>& args) -> Value {
    const auto& v = args[0];
    if (std::holds_alternative<std::shared_ptr<Array>>(v))
        return (double)std::get<std::shared_ptr<Array>>(v)->elements.size();
    if (std::holds_alternative<std::string>(v))
        return (double)std::get<std::string>(v).size();
    throw RuntimeError("len() requires array or string", Token{});
});
```

#### 5.2 `type()` 内建函数

**工作量**：30 分钟。使用 `std::visit` 对 7 种 variant 类型返回 `"null"`/`"number"`/`"boolean"`/`"string"`/`"array"`/`"function"`/`"object"`。

#### 5.3 数组内建属性和方法

**工作量**：3-4 小时

在 `PropertyExpr` 求值时对 `Array` 类型特殊处理：
- `arr.length` → getter，返回元素数量
- `arr.push(val)` → 追加，返回新长度
- `arr.pop()` → 弹出末尾，返回该元素

#### 5.4 字符串内建属性和方法

**工作量**：2-3 小时

- `str.length` → 字符数
- `str.substring(start, end)` → 子串

#### 5.5 改进错误信息

**工作量**：2 小时

- Token 增加 `column` 字段
- 错误输出格式：`文件名:行号:列号: 错误类型: 详细信息`
- 打印出错行源代码 + `^` 指示符

---

### P1 — 短期完善（本月内）

#### 5.6 数组/字符串方法扩展

- 数组：`arr.insert(index, val)`、`arr.remove(index)`
- 字符串：`str.contains(sub)`、`str.upper()`、`str.lower()`、`str.trim()`

**工作量**：3-4 小时

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

**实现**：新增 `SuperExpr` AST 节点，`visitSuperExpr` 中沿 `parentClass` 链查找方法。

**工作量**：3-4 小时

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

**工作量**：8-12 小时

#### 5.11 `int64` / `float64` 双数值类型

在 `Value` variant 中增加 `int64_t`：
- `int64 ±/* int64 → int64`
- `int64 ±/* float64 → float64`（自动提升）
- `/` 总是返回 `float64`

**工作量**：6-8 小时（涉及所有二元/一元运算分支）

#### 5.12 解析器错误信息增强

在当前的 `synchronize()` 基础上：
- Token 携带列号
- 错误信息显示源码行 + `^` 指示符
- 区分 Error 和 Warning 级别

**工作量**：4-6 小时

---

### P3 — 长期工程（3-6 个月+）

#### 5.13 字节码虚拟机 ✅ Phase 1 已完成

AST 解释器 → 字节码编译器 + 栈式 VM：

- ✅ `OpCode` 指令集（32 条指令）
- ✅ `Compiler`：AST → `Chunk`（字节码 + 常量池），实现 `ExprVisitor<void>` + `StmtVisitor<void>` + `ProgramVisitor<void>`
- ✅ `VM`：指令 dispatch 循环，固定大小 Value 栈（1024 槽位），全局变量表
- ✅ 跳转回填：`emitJump()`/`patchJump()`/`emitLoop()` 两遍式
- ✅ 原生函数调用：`callDirectly()` 绕过 `Interpreter&` 依赖
- ✅ 10 个内建函数全部在 VM 中注册
- ✅ `--vm` 标志选择 VM 后端，`--vm --tokens` 输出字节码反汇编

**Phase 2（规划中）**：局部变量（编译时栈槽位）、函数 + 调用帧、闭包 + upvalue、break/continue、for-in、try/catch/throw

**Phase 3（规划中）**：对象系统（class/method/inheritance）、VM 替代解释器成为默认后端

**优势**：消除虚函数/`dynamic_cast` 开销，为 JIT 打基础。

**工作量**：Phase 1 已完成（~1000 行新增 C++），Phase 2/3 各约 1 周

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

#### 5.17 C++ 单元测试 + CI

- Google Test 框架测试 Lexer/Parser/Interpreter 各单元
- GitHub Actions 自动化构建 + 测试

**工作量**：1 周搭建 + 持续编写

---

### 已完成功能清单（供参考）

以下功能在路线图早期版本中为待办项，现已实现：

| 功能 | 实现方式 |
|------|----------|
| 字符串 `+` 拼接 | `visitBinaryExpr` 中 string + string/any |
| `break` / `continue` | `BreakSignal` / `ContinueSignal` 异常模式 |
| `for-in` 字符串/range | `visitForStmt` 中 string 逐字符、range 迭代 |
| 复合赋值 `+=` 等 | Pratt 循环中解糖为 `x = x + y` |
| 三元运算符 `? :` | `TernaryExpr`，Pratt 优先级 2 右结合 |
| Obj 单继承 | `Obj Child : Parent`，构造函数链根→叶 |
| `&&` / `\|\|` 短路求值 | `visitBinaryExpr` 中最先处理，短路返回实际值 |
| Unicode 标识符 | `isAlpha()` 接受 `> 127` 字节 |
| 字符串转义序列 | `\n`、`\t`、`\r`、`\0`、`\\`、`\"`、`\'` |
| 块注释嵌套 | `blockComment()` depth 计数器 |
| `try`/`catch`/`finally` | `TryStmt` + `ThrowSignal`，finally 始终执行 |
| `throw` 自定义错误 | `ThrowSignal{Value}`，catch 绑定原值 |
| 测试框架 | `assert()` + 11 个 `.va` 测试文件 + runner |
| 内建函数 | `print`、`clock`、`input`、`int`、`float`、`range`、`assert`、`bin`、`oct`、`hex` |
| 数字字面量 | `0x`/`0o`/`0b` 前缀（十六进制/八进制/二进制） |

---

## 六、架构演进建议

### 6.1 短期架构调整（P1 阶段完成）

#### 6.1.1 统一 Value 类型查询

当前 `std::holds_alternative<T>()` 散落在解释器各处。建议增加：

```cpp
enum class ValueType { Null, Number, Boolean, String, Array, Function, Object };
ValueType valueType(const Value& value);
```

#### 6.1.2 解决 `const_cast` 问题

`Environment::assign()` 对快照环境使用 `const_cast`。闭包环境不应是 const——属性赋值和变量修改需要可变环境。建议将快照环境改为非 const。

### 6.2 中期架构升级（P2-P3 阶段）

#### 6.2.1 Visitor 泛型化

当前 `ExprVisitor` 返回 `Value` 硬编码。推荐为每个新 pass 定义独立 visitor 接口（简单且类型安全）。

#### 6.2.2 字节码常量池

`Chunk` 类存储指令序列 + 常量池。常量池存储所有字面量，字节码引用索引。这自然解决字符串插值的"每次求值"问题。

#### 6.2.3 统一错误报告接口

```cpp
class ErrorReporter {
    virtual void error(int line, int col, const string& msg) = 0;
    virtual bool hadError() const = 0;
};
```

为 LSP 集成和测试打下基础。

---

## 七、总结

### 7.1 模块一句话评价

| 模块 | 评价 |
|------|------|
| Lexer | 简洁高效（321 行），转义/Unicode/嵌套注释/错误恢复/0x/0o/0b 数字前缀就位 |
| Parser | Pratt 实现优雅（816 行），有 panic-mode 恢复，14 Expr + 13 Stmt 种节点 |
| AST | 模板化 Visitor 模式，类型体系完整（27 种节点），接口支持 3 种返回类型 |
| Interpreter | 功能充实（1477 行）：全运算符、短路求值、闭包、继承、异常 |
| Compiler+VM | 字节码后端 Phase 1 完成（~1000 行新增）：32 条指令、栈式 VM、跳转回填 |
| Value | `std::variant` 类型安全，7 种运行时类型 |
| Environment | 作用域链 + 快照闭包正确，`const_cast` 隐患 |
| 测试 | 13 个 `.va` 测试文件 + 2 个 runner（支持 `-VM` 参数），VM 模式 8/13 通过 |

### 7.2 Vora 的差异化定位

| 维度 | Lua | Python | JavaScript | Vora (当前) |
|------|-----|--------|------------|-------------|
| 实现复杂度 | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| 语言简单性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ |
| 性能 | ⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐ (VM 后端) |
| OOP 模型 | 原型式 | 类式 | 原型式 | 类式(轻量+继承) |
| 标准库 | 极小 | 庞大 | 中等 | 无 |
| 嵌入友好 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ |

**Vora 的潜在优势领域**：C++ 应用嵌入脚本、教学用语言、配置/DSL 语言。

### 7.3 给贡献者的建议

1. **先修 P0**（`len`、`type`、数组/字符串方法、错误信息）——改动小、影响大
2. **再补 P1**（默认参数、`super`、`switch`）
3. **然后继续 VM Phase 2**（函数/闭包/局部变量）——Phase 1 已验证架构
4. **搭基础设施**（C++ 单元测试 + CI + 标准库起步）
5. **最后做架构升级**（模块系统 → VM Phase 3 对象 → GC）

---

> **后记**：Vora 目前约 6500 行 C++ 代码，零外部依赖。对于一个双后端脚本语言项目来说代码量克制。相比 Lox，Vora 在对象系统（继承）、字符串插值、异常处理、Pratt 解析（错误恢复）、字节码 VM 方面更先进；相比 Wren，它更简单、更容易理解。P0 问题解决后，Vora 可以成为一个非常好的嵌入式脚本引擎。

---

*分析日期：2026-05-31*

*基于 Vora 源码分支 `main`，~6500 行 C++17*
