# 抽象语法树（AST）开发文档

## 概述

AST 是词法分析和解释执行之间的中间表示。Vora 使用 **模板化 Visitor 模式** 实现多层双分派（double dispatch），支持不同返回类型的 AST 遍历。

- **相关文件**：`src/ast/expr.h`（14 种表达式）、`src/ast/stmt.h`（13 种语句）、`src/ast/program.h`（根节点）、`src/ast/expr_visitor.h`（泛型表达式 Visitor）、`src/ast/stmt_visitor.h`（泛型语句 Visitor）、`src/ast/ast_printer.h/.cpp`（S-expression 调试输出）、`src/ast/expr.cpp`、`src/ast/stmt.cpp`
- **命名空间**：`vora`

## 设计原则

1. **所有权**：AST 节点使用 `std::unique_ptr` 管理子节点，确保单一所有权。闭包/共享场景使用 `std::shared_ptr`
2. **模板化 Visitor 模式**：`ExprVisitor<R>` 和 `StmtVisitor<R>` 按返回类型 `R` 参数化——同一接口定义服务于所有 pass（`R=Value` → 解释器；`R=void` → 编译器/解释器语句执行；`R=std::string` → AST Printer）。添加新 pass 只需新增一个 `accept()` 重载
3. **不可变性**：AST 节点在构建后不修改，仅供读取
4. **Visitor 分发**：每个 Expr/Stmt 子类为每个使用的返回类型提供 `accept()` 重载（当前 Expr: `Value`/`void`/`std::string`；Stmt: `void`/`std::string`；Program 使用非虚模板 `accept<R>()`）

## 表达式节点（Expr）

基类支持按返回类型多路分派：

```cpp
class Expr {
public:
    virtual ~Expr() = default;
    virtual Value        accept(ExprVisitor<Value>& visitor)        const = 0;
    virtual void         accept(ExprVisitor<void>& visitor)         const = 0;
    virtual std::string  accept(ExprVisitor<std::string>& visitor)  const = 0;
    virtual std::unique_ptr<Expr> clone() const = 0;
};
```

共 **14 种**表达式子类。

### Visitor 接口

```cpp
template <typename R>
class ExprVisitor {
public:
    virtual R visitLiteralExpr(const LiteralExpr& expr) = 0;
    virtual R visitBinaryExpr(const BinaryExpr& expr) = 0;
    // ... 其余 12 个 visit* 方法
};
```

**当前使用的特化**：
| 特化 | 用途 |
|------|------|
| `ExprVisitor<Value>` | 解释器求值（返回运行时值） |
| `ExprVisitor<void>` | 字节码编译器（副作用式 emit） |
| `ExprVisitor<std::string>` | AST Printer（返回 S-expression 字符串） |

### LiteralExpr — 字面量

表示所有编译期可知的常量值。

```cpp
class LiteralExpr : public Expr {
public:
    Value value;  // std::variant<nullptr_t, double, bool, string, ...>
};
```

**Vora 用法**：
```vora
42          // double(42)
3.14        // double(3.14)
"hello"     // string("hello")
TRUE        // bool(true)
FALSE       // bool(false)
null        // nullptr_t
```

### BinaryExpr — 二元表达式

表示二元运算。

```cpp
class BinaryExpr : public Expr {
public:
    std::unique_ptr<Expr> left;   // 左操作数
    Token op;                     // 运算符 Token
    std::unique_ptr<Expr> right;  // 右操作数
};
```

**Vora 用法**：
```vora
1 + 2       // op = PLUS
a == b      // op = EQUAL_EQUAL
x ** 2      // op = POWER
```

### GroupingExpr — 分组表达式

表示括号分组。

```cpp
class GroupingExpr : public Expr {
public:
    std::unique_ptr<Expr> expression;
};
```

**Vora 用法**：`(1 + 2) * 3`

### UnaryExpr — 一元表达式

表示一元运算。

```cpp
class UnaryExpr : public Expr {
public:
    Token op;                     // MINUS 或 NOT
    std::unique_ptr<Expr> right;  // 操作数
};
```

**Vora 用法**：`-x`、`!flag`

### VariableExpr — 变量引用

表示变量名引用。

```cpp
class VariableExpr : public Expr {
public:
    std::string name;     // 变量名
    Token nameToken;      // 原始 Token（用于错误报告）
};
```

**Vora 用法**：`x`、`myVar`

### AssignmentExpr — 变量赋值

```cpp
class AssignmentExpr : public Expr {
public:
    std::string name;
    std::unique_ptr<Expr> value;   // 右侧表达式
    Token nameToken;
};
```

**Vora 用法**：`x = 10`

### CallExpr — 函数调用

```cpp
class CallExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;               // 被调用者
    std::vector<std::unique_ptr<Expr>> arguments; // 实参列表
    Token paren;                                  // 右括号 Token（错误报告用）
};
```

**Vora 用法**：`add(1, 2)`、`print("hello")`

### ArrayExpr — 数组字面量

```cpp
class ArrayExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;
    Token leftBracket;
};
```

**Vora 用法**：`[1, 2, 3]`、`[]`

### IndexExpr — 索引访问

```cpp
class IndexExpr : public Expr {
public:
    std::unique_ptr<Expr> array;  // 被索引对象
    std::unique_ptr<Expr> index;  // 索引表达式
    Token bracket;                // 方括号 Token
};
```

**Vora 用法**：`arr[0]`、`matrix[i][j]`

### PropertyExpr — 属性访问

```cpp
class PropertyExpr : public Expr {
public:
    std::unique_ptr<Expr> object;   // 对象表达式
    std::string property;           // 属性名
    Token dot;                      // 点号 Token
};
```

**Vora 用法**：`student.name`、`this.age`

### PropertyAssignmentExpr — 属性赋值

```cpp
class PropertyAssignmentExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string property;
    std::unique_ptr<Expr> value;
    Token dot;
};
```

**Vora 用法**：`student.name = "Alice"`

### ThisExpr — this 引用

```cpp
class ThisExpr : public Expr {
public:
    Token keyword;
};
```

**Vora 用法**：`this`（在对象方法中使用）

### IncDecExpr — 自增/自减表达式

```cpp
class IncDecExpr : public Expr {
public:
    Token op;                       // PLUS_PLUS 或 MINUS_MINUS
    std::unique_ptr<Expr> target;   // VariableExpr 或 PropertyExpr
    bool isPrefix;                  // true = ++x, false = x++
};
```

**Vora 用法**：`++x`（前缀）、`x--`（后缀）、`++obj.prop`

**求值规则**：前缀返回新值，后缀返回旧值。支持变量和属性两种目标。

### TernaryExpr — 三元条件表达式

```cpp
class TernaryExpr : public Expr {
public:
    std::unique_ptr<Expr> condition;   // 条件表达式
    std::unique_ptr<Expr> thenBranch;  // 条件为真时的分支
    std::unique_ptr<Expr> elseBranch;  // 条件为假时的分支
};
```

**Vora 用法**：`x > 0 ? "positive" : "negative"`

**求值规则**：短路求值——先求值 `condition`，为真则只求值 `thenBranch`，为假则只求值 `elseBranch`。右结合：`a ? b : c ? d : e` → `a ? b : (c ? d : e)`。

## 语句节点（Stmt）

基类支持 Visitor 模式：

```cpp
class Stmt {
public:
    virtual ~Stmt() = default;
    virtual void         accept(StmtVisitor<void>& visitor)         const = 0;
    virtual std::string  accept(StmtVisitor<std::string>& visitor)  const = 0;
};
```

共 **13 种**语句子类。

### Visitor 接口

```cpp
template <typename R>
class StmtVisitor {
public:
    virtual R visitExprStmt(const ExprStmt& stmt) = 0;
    virtual R visitLetStmt(const LetStmt& stmt) = 0;
    // ... 其余 11 个 visit* 方法
};
```

### ExprStmt — 表达式语句

```cpp
class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expression;
};
```

**Vora 用法**：`print("hello")`、`x + 1`（作为独立语句）

### LetStmt — 变量声明

```cpp
class LetStmt : public Stmt {
public:
    std::string name;
    std::unique_ptr<Expr> initializer;   // 初始值表达式
    std::string typeAnnotation;          // 空 = 无标注
};
```

**Vora 用法**：`let x = 10`、`let x:int = 42`、`let y:float = 3.14`

类型标注（`:int`/`:float`）当前仅作文档用途，运行时不强制校验。

### BlockStmt — 代码块

```cpp
class BlockStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;
};
```

**Vora 用法**：
```vora
{
    let x = 1
    print(x)
}
```

### ReturnStmt — 返回语句

```cpp
class ReturnStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;  // 返回值表达式
};
```

**Vora 用法**：`return a + b`

### IfStmt — 条件分支

```cpp
class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;  // 可为 nullptr
};
```

**Vora 用法**：
```vora
if (x > 0) {
    print("positive")
} else {
    print("non-positive")
}
```

### WhileStmt — while 循环

```cpp
class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};
```

**Vora 用法**：
```vora
while (i < 10) {
    i = i + 1
}
```

### ForStmt — for-in 循环

```cpp
class ForStmt : public Stmt {
public:
    std::string variable;           // 循环变量名
    std::unique_ptr<Expr> iterable; // 可迭代对象
    std::unique_ptr<Stmt> body;     // 循环体
    Token forToken;                 // for 关键字 Token
};
```

**Vora 用法**：
```vora
for item in [1, 2, 3] {
    print(item)
}
```

### FuncStmt — 函数声明

```cpp
class FuncStmt : public Stmt {
public:
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;  // 注意：shared_ptr（供 VoraFunction 共享）
};
```

**Vora 用法**：
```vora
func add(a, b) {
    return a + b
}
```

### ObjStmt — 对象声明

```cpp
class ObjStmt : public Stmt {
public:
    std::string name;                           // 类名
    std::string parentName;                     // 父类名（空 = 无继承）
    std::vector<std::string> params;            // 构造函数参数
    std::vector<std::unique_ptr<Stmt>> methods;  // 方法列表（FuncStmt）
    std::shared_ptr<BlockStmt> body;             // 构造函数体（非方法语句）
};
```

**Vora 用法**：
```vora
Obj Student(name, age) {
    // 以下语句进入 body（构造函数体）
    this.name = name
    this.age = age

    // 以下 func 进入 methods
    func greet() {
        print("Hi, I'm ${this.name}")
    }
}
```

> Parser 在解析 Obj 块时，会将 `FuncStmt` 归入 `methods`，其他语句归入 `body`。`ObjStmt` 支持 `parentName` 实现单继承（`Obj Child : Parent`）。

### BreakStmt / ContinueStmt — 循环控制

```cpp
class BreakStmt : public Stmt {
public:
    Token keyword;
};
class ContinueStmt : public Stmt {
public:
    Token keyword;
};
```

**Vora 用法**：`break`、`continue`（在 `while` / `for` 循环中使用）

### TryStmt — 异常处理

```cpp
class TryStmt : public Stmt {
public:
    std::unique_ptr<Stmt> tryBlock;      // try 块
    std::string catchVar;                // catch 变量名（空 = 无 catch）
    std::unique_ptr<Stmt> catchBlock;    // catch 块（nullptr = 无 catch）
    std::unique_ptr<Stmt> finallyBlock;  // finally 块（nullptr = 无 finally）
};
```

**Vora 用法**：
```vora
try {
    let x = 1 / 0
} catch (e) {
    print("Error: ${e}")
} finally {
    print("Done")
}
```

**执行规则**：`finally` 块始终执行（无论 try 正常完成 / 抛错 / return / break / continue）。`catch` 捕获 `ThrowSignal`（用户值）和 `RuntimeError`（转换为字符串）。

### ThrowStmt — 抛出异常

```cpp
class ThrowStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;  // 被抛出的表达式
    Token keyword;                 // throw 关键字 Token
};
```

**Vora 用法**：`throw "error message"`、`throw ValidationError("msg")`

**执行规则**：求值 `value` 表达式，以 `ThrowSignal{value}` 抛出，被最近的 `catch` 块捕获。

## Program — 程序根节点

```cpp
template <typename R>
class ProgramVisitor {
public:
    virtual R visitProgram(const Program& program) = 0;
};

class Program {
public:
    std::vector<std::unique_ptr<Stmt>> statements;

    // 非虚模板 accept()——Program 无子类，无需虚分派
    template <typename R>
    R accept(ProgramVisitor<R>& visitor) const {
        return visitor.visitProgram(*this);
    }
};
```

## ASTPrinter — AST 调试输出

```cpp
class ASTPrinter : public ExprVisitor<std::string>,
                   public StmtVisitor<std::string>,
                   public ProgramVisitor<std::string> {
public:
    std::string print(const Program* program);
    // visit* 方法直接返回 std::string（无副作用成员变量）
};
```

**使用方式**：`vora file.va --ast-printer`

**输出示例**：
```
Vora: let x = 1 + 2
AST:  (program (let x (+ 1 2)))
```

## AST 节点关系图

```
Program
 └── statements: vector<Stmt>
      ├── LetStmt ─── initializer: Expr
      ├── ExprStmt ── expression: Expr
      ├── BlockStmt ─ statements: vector<Stmt>
      ├── IfStmt ──── condition: Expr
      │               thenBranch: Stmt
      │               elseBranch: Stmt
      ├── WhileStmt ─ condition: Expr
      │               body: Stmt
      ├── ForStmt ─── iterable: Expr
      │               body: Stmt
      ├── FuncStmt ──── body: BlockStmt
      ├── ObjStmt ───── parentName: string
      │                 methods: vector<Stmt>
      │                 body: BlockStmt
      ├── ReturnStmt ── value: Expr
      ├── BreakStmt
      ├── ContinueStmt
      ├── TryStmt ───── tryBlock: Stmt, catchBlock: Stmt, finallyBlock: Stmt
      └── ThrowStmt ─── value: Expr

Expr
 ├── LiteralExpr
 ├── BinaryExpr ──── left: Expr, right: Expr
 ├── UnaryExpr ───── right: Expr
 ├── GroupingExpr ── expression: Expr
 ├── VariableExpr
 ├── AssignmentExpr ─ value: Expr
 ├── CallExpr ─────── callee: Expr, arguments: vector<Expr>
 ├── ArrayExpr ────── elements: vector<Expr>
 ├── IndexExpr ────── array: Expr, index: Expr
 ├── PropertyExpr ─── object: Expr
 ├── PropertyAssignmentExpr ── object: Expr, value: Expr
 ├── ThisExpr
 ├── IncDecExpr ───── target: Expr
 └── TernaryExpr ──── condition: Expr, thenBranch: Expr, elseBranch: Expr
```

## 扩展指南

### 添加新的表达式类型

1. 在 `expr.h` 中创建新的子类，继承 `Expr`，声明所有 `accept()` 重载（`Value`/`void`/`std::string`）和 `clone()`
2. 在 `expr_visitor.h` 的 `ExprVisitor<R>` 模板中添加纯虚 `visit*` 方法
3. 在 `expr.cpp` 中实现所有 `accept()` 重载（每行一个委托）和 `clone()`
4. 在 `parser.cpp` 的相应位置生成该节点
5. 在 `interpreter.h/.cpp` 中覆盖新的 `visit*` 方法（继承 `ExprVisitor<Value>`）
6. 在 `ast_printer.h/.cpp` 中覆盖新的 `visit*` 方法（继承 `ExprVisitor<std::string>`）
7. 在 `vm/compiler.h/.cpp` 中覆盖新的 `visit*` 方法（继承 `ExprVisitor<void>`）

### 添加新的语句类型

1. 在 `stmt.h` 中创建新的子类，继承 `Stmt`，声明 `accept()` 重载
2. 在 `stmt_visitor.h` 的 `StmtVisitor<R>` 模板中添加纯虚 `visit*` 方法
3. 在 `stmt.cpp` 中实现 `accept()` 重载
4. 在 `parser.cpp` 的 `statement()` 中添加解析逻辑
5. 在 `interpreter.h/.cpp` 中覆盖（继承 `StmtVisitor<void>`）
6. 在 `ast_printer.h/.cpp` 中覆盖（继承 `StmtVisitor<std::string>`）
7. 在 `vm/compiler.h/.cpp` 中覆盖（继承 `StmtVisitor<void>`）
