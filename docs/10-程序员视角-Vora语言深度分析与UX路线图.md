# 程序员视角：Vora 语言深度分析与 UX 完善路线图

> 作者按：本文从一线 C/C++/Python 全栈程序员的视角，基于对 Vora 源码的完整阅读，剖析这门语言的设计哲学、实现质量、现有缺陷，并提出按优先级排序的用户体验完善路线图。

---

## 目录

1. [语言定位与设计哲学](#一语言定位与设计哲学)
2. [实现架构质量评估](#二实现架构质量评估)
3. [当前功能清单与代码级评注](#三当前功能清单与代码级评注)
4. [程序员视角的痛点清单](#四程序员视角的痛点清单)
5. [UX 完善路线图（分 P0-P3 优先级）](#五ux-完善路线图)
6. [架构演进建议](#六架构演进建议)
7. [总结](#七总结)

---

## 一、语言定位与设计哲学

### 1.1 语言基因

Vora 不是一门"拍脑袋"的语言。从 `README.md` 和源码结构可以清晰看到它的血脉：

| 灵感来源 | 借鉴方向 |
|----------|----------|
| **Crafting Interpreters (Lox)** | 整体架构骨架：Lexer → Pratt Parser → Tree-walking Interpreter。`Expr`/`Stmt` 的 visitor 设计直接源自此书。 |
| **Lua 5.4** | 简洁的类型系统（nil/number/boolean/string/table → null/number/boolean/string/object）、闭包实现、环境链。 |
| **Wren** | 对象模型——`Obj` 声明内嵌方法定义，`this` 绑定。 |
| **CPython** | 命名风格（`RuntimeError`、`RuntimeMode`），`Value` 作为 tagged union 的思路。 |
| **JavaScript (隐含)** | `let` 声明、`for-in` 语法、字符串插值 `${}`、可选分号。 |

### 1.2 语言性格

Vora 处于 **"教学语言"与"实用脚本语言"的交叉地带**。它目前更像前者——解释器代码清晰、可读性强，适合学习语言实现原理——但 `README.md` 中规划的 vpm 包管理器、模块系统、标准库表明作者有实用化的野心。

**一句话概括**：Vora 是一门类 JavaScript 语法、Lua 级简单性、Wren 式面向对象的动态类型脚本语言，目前处于 AST 解释器原型阶段。

---

## 二、实现架构质量评估

### 2.1 整体印象：**B+ 级代码**

代码组织清晰，模块边界合理。对于一个单人项目而言工程质量良好。以下是逐模块评价：

### 2.2 Lexer（词法分析器）—— 评分：A-

**亮点**：
- 经典的手写词法分析器，单遍扫描，代码简洁（326 行）。
- `keywords` 使用 `unordered_map` 做关键字识别，O(1) 查找。
- 支持 `//` 行注释和 `/* */` 块注释。
- 正确处理了 `++` / `--` / `**` 这类多字符运算符。
- 浮点数解析支持 `3.14` 格式（`peekNext()` 检查避免 `a.b` 被误解析为数字）。

**缺陷**：
- 字符串不支持转义序列（`\n`、`\"`、`\\` 等），这是**严重功能缺失**。
- ✅ 支持单引号字符串 `'...'`（与双引号等价）。
- 不支持十六进制/八进制/二进制数字字面量。
- ✅ 块注释支持嵌套（`/* /* */ */` 正确处理）。
- 没有错误恢复——遇到非法字符直接产生 `INVALID` token，后续解析大概率崩溃。
- ✅ 支持 Unicode 标识符（UTF-8 多字节字符可用于变量名、函数名等）。

**关键代码路径**：`src/lexer/lexer.cpp:137-303`（`scanToken()` 主循环）

### 2.3 Parser（语法分析器）—— 评分：B+

**亮点**：
- **Pratt 解析器**（优先级爬升法）实现正确，这是比递归下降更优雅的选择。优先级表在 `getPrecedence()` 中集中管理（`src/parser/parser.cpp:476-513`）。
- 正确处理了 `**` 的右结合性（`rightAssociative = (op.type == TokenType::POWER || op.type == TokenType::EQUAL)`）。
- 赋值在 Pratt 循环中统一处理，对 `VariableExpr` 和 `PropertyExpr` 分别生成对应节点。这是比很多教学实现更成熟的做法。
- `call()` 函数通过 `while(true)` 循环实现了后缀链式调用：`foo()(1)[2].bar`。

**缺陷**：
- **零错误恢复**——`parse()` 的任何子函数返回 `nullptr` 都会导致整个解析失败。用户只看到第一个错误。
- `funcStatement()` 中有冗余的 `dynamic_cast`（第 295-313 行）：`blockStatement()` 已经保证返回 `BlockStmt`，但代码先做了 `dynamic_cast` 判断，然后又 `dynamic_cast` 释放——这里是 `shared_ptr` 和 `unique_ptr` 的所有权转移 hack，容易出 bug。
- `objStatement()` 的方法/语句区分依赖于 `dynamic_cast<FuncStmt*>`——这意味着如果 Obj 体内有嵌套的 `if`/`while` 等非函数语句就会被放入 `body`，而函数定义被放入 `methods`。这个隐式约定缺乏文档说明。
- ✅ `break` / `continue` / `throw` / `try-catch-finally` 均已解析和执行。

### 2.4 AST（抽象语法树）—— 评分：B

**亮点**：
- Visitor 模式分离了数据结构和操作。`ExprVisitor`（返回 `Value`）和 `StmtVisitor`（返回 `void`）作为独立接口存在，比 Lox 原版的单一 `Visitor` 泛型模板更直观。
- `Expr` 类型覆盖全面：Literal, Binary, Grouping, Unary, Variable, Assignment, Call, Array, Index, Property, PropertyAssignment, This——共 12 种。
- `Stmt` 类型覆盖：ExprStmt, LetStmt, BlockStmt, ReturnStmt, IfStmt, WhileStmt, ForStmt, FuncStmt, ObjStmt——共 9 种。

**缺陷**：
- Visitor 接口的返回类型是硬编码的（`ExprVisitor` 返回 `Value`，`StmtVisitor` 返回 `void`）。未来如果要加字节码编译器（需要返回 `Chunk` 或 `void`），必须定义新的 visitor 接口——注释中已经提到这一点。但这也意味着每加一个 pass 就要更新所有 `accept()` 方法。
- `ASTPrinter` 强制返回 `Value`（即使只需要 `string`），这是一种类型污染。
- `Program` 没有实现 visitor 模式（独立的 `print(Program*)` 方法），与其余 AST 节点不一致。

### 2.5 Interpreter（解释器）—— 评分：B

**亮点**：
- 正确实现了词法作用域 + 闭包。`captureClosure()` 使用 `Environment::snapshot()` 深拷贝环境链，捕获定义时的变量绑定。
- `ReturnSignal` 异常模式实现 `return` 语句（比维护一个 `returnValue` 标志位更安全）。
- 字符串插值功能完整：支持 `${var}` 和 `${obj.prop1.prop2}` 的多级属性访问，找不到变量时保留原文（降级优雅）。
- `BoundMethodCallable` 和 `ObjectConstructorCallable` 以 inline lambda（实际上是内嵌 struct）方式实现，读代码时所有相关逻辑在一处。

**缺陷**：
- `+` 已支持 `string + string`、`array + array`、`array + value`、`string + any`。`&&`/`||` 逻辑运算符短路求值已实现。
- `for-in` 已支持数组、字符串、`range()`。`indexExpr` 已支持数组和字符串索引。
- `break` / `continue` 已实现。
- 没有垃圾回收。`shared_ptr` 的引用计数可以处理大多数情况，但**循环引用会导致内存泄漏**（例如对象属性引用自身）。

### 2.6 Runtime（运行时系统）—— 评分：B+

**亮点**：
- `Value` 使用 `std::variant`，类型安全且比 C union 更安全。
- `Environment` 支持两种 owning 模式：裸指针 `enclosing`（不拥有）和 `shared_ptr<Environment> enclosingOwned`（拥有）。前者用于栈上的临时环境（`scopeStack`），后者用于闭包捕获。这是对 C++ 所有权语义的深刻理解。
- `Environment::snapshot()` 递归深拷贝环境链，正确实现了闭包语义。
- `NativeFunction` 的 arity 为 `-1` 表示变参函数（`print`），设计合理。

**缺陷**：
- `Value` 缺少类型查询接口。当前只有 `std::holds_alternative<T>()` 在解释器代码中散落各处。应该提供一个 `Value::type()` 或 `valueTypeName()`。
- `Array` 过于简单——仅包含 `vector<Value>`，没有 `length()`、`push()` 等方法接口。
- `ObjectInstance` 的 `properties` 使用 `std::map`（有序），这是合理的（可预测的遍历顺序），但需要考虑未来是否需要更快查找。
- `Environment` 的 `assign()` 使用 `const_cast` 移除 const（第 77 行），技术上不安全。

---

## 三、当前功能清单与代码级评注

### 3.1 类型系统

```
Vora Value = null | double | bool | string | Array | Callable | ObjectInstance
```

**程序员评价**：这是一个**极简但实用的类型集合**。令人想起 Lua 5.x（nil, number, boolean, string, table, function, userdata）。Vora 把 table 拆成了 Array + ObjectInstance，这更符合大多数程序员的心理模型。

**关键缺失**：没有整数类型。`double` 作为唯一数值类型意味着 `1 / 2 = 0.5`（与 Python 3 一致），但无法表达 64 位整数（位运算、精确计数）。

### 3.2 运算符系统

| 优先级 | 运算符 | 结合性 | 状态 |
|--------|--------|--------|------|
| 0 | 无运算符 | - | - |
| 1 | `=` | 右结合 | ✔ 已实现 |
| 1 | `\|\|` `or` | 左结合 | ✔ 短路求值 |
| 2 | `&&` `and` | 左结合 | ✔ 短路求值 |
| 3 | `==` `!=` | 左结合 | ✔ 已实现 |
| 4 | `<` `<=` `>` `>=` | 左结合 | ✔ 已实现 |
| 5 | `+` `-` | 左结合 | ✔ 数字+字符串 |
| 6 | `*` `/` `%` | 左结合 | ✔ 已实现 |
| 7 | `**` | 右结合 | ✔ 已实现 |
| - | `++` `--` | 前缀/后缀 | ✔ 已实现（变量和属性） |
| - | `+=` `-=` `*=` `/=` `%=` | 右结合 | ✔ 已实现 |
| - | `? :` | 右结合 | ✔ 三元条件，短路求值 |

**程序员评价**：Pratt 解析器的优先级表设计良好。`**` 的右结合性正确处理。`? :` 作为特殊中缀运算符在 Pratt 循环内处理。

### 3.3 变量与作用域

```vora
let x = 10        // 全局或局部声明
x = 20            // 重新赋值（沿作用域链查找）
{
    let x = 30    // 遮蔽外部 x
}
```

**实现方式**：`Environment` 是一个单链表（通过 `parent()` 向上查找）。定义在当前帧，查找沿链向上。

**程序员评价**：这是教科书式的词法作用域实现。`Environment::snapshot()` 用于闭包捕获是正确但昂贵的方案——每次创建闭包都会深拷贝整个环境链。Lua 的 upvalue 方案更高效，但对 Vora 当前阶段来说过度优化。

### 3.4 函数与闭包

```vora
func makeCounter() {
    let count = 0
    func increment() {
        count = count + 1
        return count
    }
    return increment
}

let counter = makeCounter()
print(counter())   // 1
print(counter())   // 2
```

**实现方式**：`VoraFunction` 持有 `shared_ptr<Environment> closure_`，调用时通过 `pushScope(function.closure().get())` 将闭包环境设置为当前环境的 enclosing。

**程序员评价**：闭包实现是正确的，但有一个微妙的问题——`captureClosure()` 总是返回当前环境的快照（即使它在全局作用域）。这意味着全局函数也会持有全局环境的引用，虽然功能正确但浪费内存。

### 3.5 对象系统

```vora
Obj Person(name, age) {
    this.name = name      // 构造函数体设置属性
    this.age = age

    func greet() {        // 方法定义在类体内
        print("I'm ${this.name}, ${this.age} years old")
    }
}

let p = Person("Alice", 20)
p.greet()                // 方法调用自动绑定 this
p.age = 21               // 属性赋值
```

**实现方式**：
1. `ObjStmt` 的 `body` 作为构造函数体执行
2. `ObjStmt` 的 `methods` 是 `FuncStmt` 列表（通过 `dynamic_cast` 在解析时区分）
3. 方法调用返回 `BoundMethodCallable`（绑定 `this` 到当前实例）
4. 构造函数调用返回 `ObjectInstance`

**程序员评价**：这是 Vora 最巧妙的 feature。Wren 风格的对象语法简洁有力。
- ✅ 单继承：`Obj Child : Parent (params) { ... }`，构造函数链从根到叶自动执行，方法沿继承链查找
- ❌ 没有类方法（静态方法）
- ❌ 方法查找先查属性再查方法表，意味着属性可以 shadow 方法（可能是 feature 也可能是 bug）
- `ObjectConstructorCallable` 是内嵌在 `visitObjStmt` 中的 struct（现在通过 `getClassDef()` 虚方法暴露 classDef 供继承解析）

### 3.6 字符串插值

```vora
let name = "World"
let greeting = "Hello ${name}!"  // "Hello World!"

Obj Point(x,y) { this.x = x; this.y = y }
let p = Point(3, 4)
print("(${p.x}, ${p.y})")       // "(3, 4)"
```

**实现方式**：`interpolateString()` 在 `LiteralExpr` 求值时调用，扫描字符串中的 `${...}` 模式，查找变量并替换。

**程序员评价**：字符串插值在**字面量求值时**完成，而不是在解析时。这意味着：
- 优点：支持运行时变量替换，符合预期
- 缺点：字符串字面量不再是"常量"——每次求值都重新插值。如果有大量静态文本，有性能浪费。
- 边界情况处理得当：找不到变量时保留 `${var}` 原文，不会崩溃。

### 3.7 数组

```vora
let arr = [1, 2, "hello", true, null]
let mixed = [1, [2, 3], func() { return 42 }]
print(arr[0])     // 1
print(arr[10])    // RuntimeError: Index out of bounds
```

**实现方式**：`Array` 仅有 `vector<Value> elements`。`IndexExpr` 仅支持整数索引。

**程序员评价**：数组功能过于原始。没有 `.length`、`.push`、`.pop`、`.slice` 等基本方法。作为一个脚本语言，这是严重影响生产力的缺失。

### 3.8 内建函数

| 函数 | 说明 | 实现位置 |
|------|------|----------|
| `print(...)` | 变参打印，空格分隔，末尾换行 | `interpreter.cpp` |
| `clock()` | 返回 Unix 时间戳（秒，毫秒精度） | `interpreter.cpp` |
| `input(...)` | 从 stdin 读取一行，可选 prompt | `interpreter.cpp` |
| `int(value)` | 转为整数（截断）；接受 number/string/bool | `interpreter.cpp` |
| `float(value)` | 转为浮点数；接受 number/string/bool | `interpreter.cpp` |
| `range(...)` | `range(stop)` / `range(start,stop)` / `range(start,stop,step)` | `interpreter.cpp` |
| `assert(...)` | `assert(condition, message?)` — 断言失败抛错 | `interpreter.cpp` |

**程序员评价**：内建函数已逐步充实。仍缺少 `type()`、`len()`、`toString()` 等实用函数，但日常使用的基本工具已就位。

---

## 四、程序员视角的痛点清单

以下是我在阅读代码和设身处地想象日常使用后会遇到的实际问题。

> ✅ 已修复（原清单中已解决的项）：字符串 `+` 拼接、`break`/`continue`、`for-in` 字符串/range、
> 复合赋值 (`+=`/`-=`/`*=`/`/=`/`%=`)、三元运算符 `? :`、Obj 单继承、测试框架 (`assert()` + 10 测试文件)、
> Unicode 标识符 (`isAlpha()` 现已接受 UTF-8 多字节字符)、`&&`/`||` 短路求值。
> 以下仅列出**仍然存在的问题**。

### 4.1 🔴 致命级（阻碍基本使用）

1. **字符串没有转义序列**
   ```vora
   let s = "line1\nline2"     // ❌ 当前输出字面量 \n
   let q = "he said \"hi\""   // ❌ 语法错误：字符串提前终止
   ```
   这让处理任何包含特殊字符的文本变得不可能。

2. **数组没有基本方法**
   ```vora
   let arr = [1, 2, 3]
   arr.push(4)     // ❌ 无法调用
   arr.length      // ❌ 属性不存在
   ```
   数组是"死"的数据结构——你能创建它，能索引它，但除此之外什么也做不了。

3. **没有 `len()` / 长度查询**
   ```vora
   print(len("hello"))  // ❌ 未定义
   print(len([1,2,3]))  // ❌ 未定义
   ```

### 4.2 🟠 严重级（破坏开发体验）

4. **解析错误立即终止**——只报告第一个错误，修正后可能立即遇到第二个，迭代修复极慢。

5. **错误信息不含列号**——只有行号，在长行中定位困难。

6. **没有类型查询函数**（`type()`）——你无法在运行时知道一个值是什么类型。

7. **`shared_ptr` 循环引用导致内存泄漏**——对象属性可以引用自身，引用计数永不为零。

### 4.3 🟡 中度级（限制语言表达能力）

8. **没有整数类型**——无法进行位运算或精确整数运算。所有数字统一为 `double`。

9. **没有 `switch` / `match`**——多分支只能用 `if-else if` 链。

10. **不支持默认参数**
    ```vora
    func greet(name = "World") { ... }  // ❌
    ```

11. **没有函数重载或可选参数检查**。

### 4.4 🟢 轻度级（影响生态和工程化）

12. **没有模块系统**（`import`/`export`）——所有代码必须写在一个文件里。

13. **REPL 不能多行输入**——无法在 REPL 中定义函数或类。

14. **没有标准库**——文件 IO、字符串操作、数学函数等都需要手写。`std/` 目录为空。

---

## 五、UX 完善路线图

按优先级分 P0-P3，每个任务包含：**影响范围、工作量估算、实现要点**。

### P0 — 立即修复（本周内）

#### 5.1 字符串转义序列

**影响**：当前字符串无法表示换行、引号、反斜杠，基本功能残缺。

**工作量**：2-3 小时

**实现要点**（`src/lexer/lexer.cpp:115-134`）：
```cpp
void Lexer::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        if (peek() == '\\') {
            advance(); // consume backslash
            // handle next char: n→\n, t→\t, \"→", \\→\, etc.
        }
        advance();
    }
    // ...
}
```

支持的转义序列（对标 JSON）：`\\`, `\"`, `\n`, `\t`, `\r`, `\0`。

#### 5.2 字符串拼接（`+` 运算符） ✅ 已完成

`string + string`、`string + any`、`any + string`、`array + array`、`array + value` 均已实现。

#### 5.3 `len()` 内建函数

**影响**：解锁数组和字符串的基本操作。

**工作量**：30 分钟

**实现要点**：
在 `Interpreter` 构造函数中新增：
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

#### 5.4 `type()` 内建函数

**影响**：调试必备。

**工作量**：30 分钟

```cpp
defineNative("type", 1, [](const std::vector<Value>& args) -> Value {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
        else if constexpr (std::is_same_v<T, double>) return "number";
        else if constexpr (std::is_same_v<T, bool>) return "boolean";
        else if constexpr (std::is_same_v<T, std::string>) return "string";
        else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>) return "array";
        else if constexpr (std::is_same_v<T, std::shared_ptr<Callable>>) return "function";
        else if constexpr (std::is_same_v<T, std::shared_ptr<ObjectInstance>>) return "object";
    }, args[0]);
});
```

---

### P1 — 短期完善（本月内）

#### 5.5 数组内建属性和方法

**方案**：在 `PropertyExpr` 求值时对 `Array` 类型做特殊处理（类似 JavaScript 的 `Array.prototype`）。

**新增属性**：
- `arr.length` → 返回元素数量（getter）

**新增方法**：
- `arr.push(val)` → 追加元素，返回新长度
- `arr.pop()` → 弹出末尾元素，返回该元素
- `arr.insert(index, val)` → 指定位置插入

**工作量**：3-4 小时

#### 5.6 字符串内建属性和方法

- `str.length` → 字符串长度
- `str.substring(start, end)` → 子串
- `str.contains(substr)` → 是否包含
- `str.upper()` / `str.lower()` → 大小写转换

**工作量**：2-3 小时

#### 5.7 `break` / `continue` ✅ 已完成

`BreakStmt`/`ContinueStmt` 已实现，通过 `BreakSignal`/`ContinueSignal` 异常模式（与 `ReturnSignal` 一致）实现非局部跳转。`WhileStmt` 和 `ForStmt` 中 try-catch 捕获这两个信号。

#### 5.8 解析器错误恢复（Panic Mode）

**影响**：大幅改善开发体验——一次看到多个错误。

**方案**：
- 新增 `Parser::synchronize()` 方法：遇到错误后跳过 token 直到找到同步点（`;`、`}`、语句关键字）。
- `parse()` 中不要因一个错误就 `return nullptr`，而是记录错误并继续。

**工作量**：2-3 小时

#### 5.9 改进错误信息

**影响**：日常开发体验。

**方案**：
- 在 `RuntimeError` 中存储源文件路径
- 错误输出格式：`文件名:行号:列号: 错误类型: 详细信息`
- 打印出错行的源代码 + `^` 指示符

**工作量**：2 小时

---

### P2 — 中期功能（2-3 个月）

#### 5.10 复合赋值运算符 ✅ 已完成

`+=`、`-=`、`*=`、`/=`、`%=` 均已实现。在 `AssignmentExpr` 中处理，等价于对应的二元运算 + 赋值。支持数字运算、字符串拼接 (`s += "!"`)、数组追加 (`arr += [3, 4]`)。

#### 5.11 三元运算符 `?:` ✅ 已完成

```vora
let result = condition ? value1 : value2
```

**实现**：`TernaryExpr` AST 节点，在 Pratt 解析器中作为中缀运算符（优先级 2，右结合），解释器短路求值（只求值被选中的分支）。

#### 5.12 `int64` / `float64` 双数值类型

**当前状态**：`int()` / `float()` 内建函数已完成（字符串→数字转换）。`let x:int` / `let x:float` 类型标注已解析但运行时不校验。`Value` variant 中仍只有 `double`，无独立 `int64_t` 类型。

**方案**：在 `Value` variant 中增加 `int64_t`。算术运算遵循：
- `int64 ±/* int64 → int64`
- `int64 ±/* float64 → float64`（自动提升）
- `/` 总是返回 `float64`

**工作量**：4-6 小时（涉及所有二元/一元运算的分支更新）

#### 5.13 `for-in` 支持字符串遍历 ✅ 已完成

```vora
for ch in "hello" {
    print(ch)    // 逐个输出 h, e, l, l, o
}
```

`for-in` 现支持数组、字符串、`range()` 三种可迭代对象。

#### 5.14 异常处理 `try` / `catch` / `finally` ✅ 已完成

**实现**：
- ✅ `TryStmt` AST 节点（tryBlock / catchVar / catchBlock / finallyBlock）
- ✅ `ThrowSignal{Value}` — 携带任意 Vora 值
- ✅ `catch (e)` 同时捕获 `ThrowSignal`（绑定原值）和 `RuntimeError`（绑定为字符串）
- ✅ `finally` 始终执行（捕获所有控制流信号后重新抛出）
- ✅ 支持 `try/catch`、`try/finally`、`try/catch/finally` 三种组合

#### 5.15 模块系统 `import` / `export`

**实现**：
- `import "path/to/module.va"` 在新的 `Environment` 中执行文件
- 模块返回值绑定到导入变量
- 模块缓存（文件名 → 环境），避免重复加载
- 支持相对路径解析

**工作量**：8-12 小时

---

### P3 — 长期工程（3-6 个月+）

#### 5.16 字节码虚拟机

这是 README 中规划的第二版架构升级。

**方案**：
1. 新增 `OpCode` 指令集（`OP_CONSTANT`, `OP_ADD`, `OP_CALL`, `OP_JUMP` 等 ~30 条指令）
2. 新增 `Compiler`：AST → `Chunk`（字节码序列 + 常量池）
3. 新增 `VM`：栈式虚拟机，基于指令 dispatch 循环执行

**优势**：
- 消除树遍历开销（`dynamic_cast`、虚函数调用）
- 为后续 JIT 编译打基础
- 更紧凑的内存表示

**工作量**：2-3 周（核心实现） + 持续优化

#### 5.17 标记-清除垃圾回收器

替换当前的 `shared_ptr` 引用计数，解决循环引用问题。

**方案**：
- 所有堆对象继承 `GCObject` 基类（持有 `isMarked` 标志）
- VM 维护灰色对象集合
- 三色标记算法（白-灰-黑）
- 在内存分配超过阈值时触发 GC

**工作量**：1-2 周

#### 5.18 测试基础设施 ✅ 基础已完成

- ✅ Vora 层：`assert(condition, message)` 内建 + 10 个 `.va` 测试文件覆盖 lexer/parser/runtime/interpreter + `run_tests.ps1`/`run_tests.sh` 运行器
- ⏳ C++ 层：Google Test 框架测试 Lexer/Parser/Interpreter 各单元（待实现）
- ⏳ CI：GitHub Actions 自动化构建和测试（待实现）

**工作量**：1 周（C++ 框架搭建） + 持续编写用例

#### 5.19 LSP 服务器

实现 Language Server Protocol：
- 基于 Vora 解析器（复用 AST）
- 支持：诊断（语法错误）、跳转定义、悬停类型提示、自动补全、文档符号
- 可以独立进程运行，通过 JSON-RPC 与编辑器通信

**工作量**：2-3 周（MVP：诊断 + 跳转定义）

#### 5.20 标准库建设

```
std/
├── math.va     // sqrt, abs, sin, cos, random, PI
├── string.va   // split, join, trim, replace
├── io.va       // readFile, writeFile, readLine
├── os.va       // args, exit, env, system
├── json.va     // parseJson, toJson
└── datetime.va // now, format, parse
```

**工作量**：持续

---

## 六、架构演进建议

### 6.1 短期架构调整（应在 P1 阶段完成）

#### 6.1.1 统一 Value 类型查询

当前代码中 `std::holds_alternative<T>()` 散落在解释器各处。建议：

```cpp
// src/runtime/value.h
enum class ValueType { Null, Number, Boolean, String, Array, Function, Object };

ValueType valueType(const Value& value);
bool isNull(const Value& v);
bool isNumber(const Value& v);
bool isString(const Value& v);
// ...
double asNumber(const Value& v);    // 带类型检查的 getter
std::string asString(const Value& v);
```

这样不仅代码更可读，也为未来添加 `int64` 类型留出空间。

#### 6.1.2 Environment 的 const_cast 问题

`Environment::assign()` 第 77 行使用 `const_cast` 移除父环境的 const。这是因为 `snapshot()` 创建的闭包环境是 const 的。建议：

- 方案 A：闭包环境不应该是 const（属性赋值需要修改闭包捕获的变量）
- 方案 B：区分"不可变闭包环境"和"可变栈环境"

### 6.2 中期架构升级（P2-P3 阶段）

#### 6.2.1 Visitor 模式泛型化

当前 `ExprVisitor` 返回 `Value`（硬编码）。这对于一个 pass 足够，但未来需要多个 pass（字节码编译器返回 `Chunk`，类型检查器返回 `Type`）。

**C++ 方案**：使用 CRTP 或模板方法模式：
```cpp
template <typename R>
class ExprVisitor {
    virtual R visitLiteralExpr(const LiteralExpr&) = 0;
    // ...
};
```

或者更务实的方案：为每个新 pass 定义独立的 visitor 接口（注释中已提及）。我个人推荐后者——虽然重复，但简单且每个 pass 的类型安全。

#### 6.2.2 为字节码 VM 做准备的数据结构

当前的 AST 使用 `unique_ptr` 独占所有权，这对树遍历解释器是合适的。字节码编译器仍可以通过 AST 生成指令序列。关键在于：

- 新增 `Chunk` 类（指令序列 + 常量池）
- 常量池存储所有字面量（数字、字符串），字节码中只引用索引
- 这自然解决了字符串插值的"每次求值"问题（插值在编译时完成）

#### 6.2.3 错误报告系统重构

当前错误分散在 `std::cerr`（Parser）和异常（RuntimeError）中。建议统一为 `ErrorReporter` 接口：

```cpp
class ErrorReporter {
public:
    virtual void error(int line, int column, const std::string& message) = 0;
    virtual void warning(int line, int column, const std::string& message) = 0;
    virtual bool hadError() const = 0;
};
```

这为 LSP 集成（错误 → 诊断消息）和测试（错误 → 断言）打下基础。

---

## 七、总结

### 7.1 Vora 的差异化定位

如果把 Vora 放在编程语言的版图上：

| 维度 | Lua | Python | JavaScript | Wren | Vora (当前) |
|------|-----|--------|------------|------|-------------|
| 实现复杂度 | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ |
| 语言简单性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| 性能 | ⭐⭐⭐⭐(LuaJIT) | ⭐⭐(CPython) | ⭐⭐⭐⭐(V8) | ⭐⭐⭐ | ⭐(树遍历) |
| OOP 模型 | 原型式 | 类式 | 原型式 | 类式(轻量) | 类式(轻量) |
| 标准库 | 极小 | 庞大 | 中等(host) | 小 | 无 |
| 嵌入友好 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐(C++) |

**Vora 的潜在优势领域**：
- **嵌入 C++ 应用的脚本语言**——如果未来体积小、依赖零、API 简洁
- **教学用语言**——比 Lox 更现代，比 Python 更简单，比 Lua 更类 C
- **配置/DSL 语言**——如果添加了沙箱执行和良好的错误信息

### 7.2 一句话总结每个模块

| 模块 | 一句话 |
|------|--------|
| Lexer | 简洁高效，但缺少转义序列是致命伤 |
| Parser | Pratt 解析器实现优雅，但没有错误恢复 |
| AST | Visitor 模式标准实现，类型体系完整 |
| Interpreter | 功能逐步充实：四则运算、字符串拼接、比较、数组/对象操作 |
| Value | `std::variant` 是好的选择，缺少类型查询接口 |
| Environment | 词法作用域 + 闭包正确，但 env snapshot 昂贵 |

### 7.3 给 Vora 贡献者的建议

如果你是第一次参与这个项目，建议按以下顺序贡献：

1. **先修 P0 问题**（字符串转义、`len`、`type`）——改动小、影响大、反馈快
2. **再补 P1 问题**（数组/字符串方法、错误恢复）
3. **然后写测试**——没有测试的编程语言项目难以吸引贡献者
4. **最后做模块系统**——这是从"玩具语言"到"工具语言"的转折点

---

> **后记**：Vora 目前约 3000 行 C++ 代码（不含注释），对于一个单文件脚本语言来说代码量克制。相比 Lox（Crafting Interpreters 中的教学语言），Vora 在对象系统、字符串插值、Pratt 解析方面更先进；相比 Wren，它更简单、更容易理解。如果 P0-P1 的问题得到解决，Vora 可以成为一个非常好的嵌入式脚本引擎。这条路不容易，但方向是对的。

---

*分析日期：2026-05-30*

*基于 Vora 源码 commit `f24be02`，分支 `main`*
