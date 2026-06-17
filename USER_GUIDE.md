# Vora 语言用户手册 / User Guide

> **版本 / Version**: v0.21 | **最后更新 / Last updated**: 2026-06-17
>
> Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.

---

## 目录 / Table of Contents

1. [快速开始](#1-快速开始--quick-start)
2. [类型系统](#2-类型系统--type-system)
3. [变量与作用域](#3-变量与作用域--variables--scope)
4. [运算符](#4-运算符--operators)
5. [控制流](#5-控制流--control-flow)
6. [函数与闭包](#6-函数与闭包--functions--closures)
7. [生成器](#7-生成器--generators) **v0.21+**
8. [对象与继承](#8-对象与继承--objects--inheritance)
9. [异常处理](#9-异常处理--exception-handling)
10. [模块系统](#10-模块系统--modules)
11. [字符串插值](#11-字符串插值--string-interpolation)
12. [内建函数](#12-内建函数--built-in-functions)
13. [标准库](#13-标准库--standard-library)
14. [附录：方法速查](#14-附录方法速查--method-reference)

---

## 1. 快速开始 / Quick Start

### Hello World

```vora
print("Hello, World!")
```

### 运行程序

```bash
# 执行脚本文件
vora hello.va

# 交互模式 (REPL)
vora --repl
```

---

## 2. 类型系统 / Type System

> 引入版本 / Since: v0.1

Vora 是动态类型语言，变量无固定类型，值有类型。

| 类型 / Type | 字面量 / Literal | 说明 |
|-------------|-----------------|------|
| **Null** | `null` | 空值，falsy 值之一 |
| **Boolean** | `true`, `false` | 布尔值 |
| **Int** | `42`, `-7`, `0xFF`, `0o77`, `0b101` | 64 位有符号整数 |
| **Float** | `3.14`, `1e10`, `2.5e-3` | 64 位浮点数 |
| **String** | `"hello"`, `'hello'` | UTF-8 字符串，双引号或单引号 |
| **Array** | `[1, 2, 3]` | 动态数组 |
| **Dict** | `{key: "value"}` | 字符串键字典 |
| **Function** | `func(x) { return x * 2 }` | 一等公民函数 |
| **Object** | `Obj Foo() { ... }` | 类实例 |

### 类型检查

```vora
type(42)         // "int"
type(3.14)       // "float"
type("hi")       // "string"
type([1, 2])     // "array"
type({a: 1})     // "dict"
type(null)       // "null"
type(true)       // "boolean"
type(func(){})   // "function"
```

### Truthy / Falsy

`null`、`false`、`0`、`0.0`、`""`、`[]`、`{}` 是 **falsy**。其他所有值均为 **truthy**（与 Python 一致）。

```vora
if (0)     { print("no")  }  // ❌ 不输出 — 0 是 falsy
if ("")    { print("no")  }  // ❌ 不输出 — 空字符串是 falsy
if ([])    { print("no")  }  // ❌ 不输出 — 空数组是 falsy
if ({})    { print("no")  }  // ❌ 不输出 — 空字典是 falsy
if (null)  { print("no")  }  // ❌ 不输出 — null 是 falsy
if (false) { print("no")  }  // ❌ 不输出 — false 是 falsy
if (1)     { print("yes") }  // ✅ 输出 yes — 非零是 truthy
if ("hi")  { print("yes") }  // ✅ 输出 yes — 非空字符串是 truthy
if ([1])   { print("yes") }  // ✅ 输出 yes — 非空数组是 truthy
```

---

## 3. 变量与作用域 / Variables & Scope

> `let` 引入版本: v0.1 | `const` 引入版本: v0.20

### 声明

```vora
let x = 10          // 可变绑定
const y = 42        // 不可变绑定，声明时必须初始化
```

### 赋值

```vora
x = 20              // ✅ 可变变量可重新赋值
// y = 10           // ❌ 编译错误：不可对 const 变量赋值
// y += 1           // ❌ 编译错误
// y++              // ❌ 编译错误
```

### 作用域

Vora 使用**块级作用域**（`{}` 界定）。变量沿作用域链向上查找。

```vora
let a = "outer"
{
    let b = "inner"
    print(a)        // "outer" — 外层变量可见
    print(b)        // "inner"
}
// print(b)         // ❌ 错误：b 不在作用域内
```

---

## 4. 运算符 / Operators

### 算术运算符

| 运算符 | 说明 | 示例 |
|--------|------|------|
| `+` `-` `*` `/` `%` | 加减乘除取模 | `3 + 4 * 2` → `11` |
| `**` | 幂运算（右结合） | `2 ** 3 ** 2` → `512` |
| `++` `--` | 自增/自减（前后缀） | `x++`, `--y` |

### 比较运算符

`<` `<=` `>` `>=` `==` `!=`

- `int == float` 跨类型比较值：`42 == 42.0` → `true`
- `null == null` → `true`

### 逻辑运算符（短路求值）

`&&` 和 `||` 返回**实际值**而非布尔值：

```vora
null || "fallback"   // "fallback" — null 是 falsy
"hi" && 42           // 42 — "hi" 是 truthy
0 || "default"       // "default" — 0 是 falsy（Python 风格）
[] || "fallback"     // "fallback" — 空数组是 falsy
```

### 三元运算符 `?:`

```vora
let result = score > 60 ? "pass" : "fail"
// 右结合：a ? b : c ? d : e  →  a ? b : (c ? d : e)
```

### 复合赋值

```vora
x += 5    // x = x + 5
x -= 3    // x = x - 3
x *= 2    // x = x * 2
x /= 3    // x = x / 3
x %= 5    // x = x % 5
```

### 数字系统字面量

```vora
0xFF      // 255   (十六进制 / hex)
0o77      // 63    (八进制 / octal)
0b101     // 5     (二进制 / binary)
```

---

## 5. 控制流 / Control Flow

### if / else

```vora
if (x > 0) {
    print("positive")
} else if (x < 0) {
    print("negative")
} else {
    print("zero")
}
```

条件周围括号可选。

### while 循环

```vora
let i = 0
while (i < 5) {
    print(i)
    i += 1
}
```

### for-in 循环

> 引入版本: v0.5 | 对象遍历: v0.16 | 迭代器协议: v0.21

```vora
// 遍历数组
for item in [1, 2, 3] { print(item) }

// 遍历字符串
for ch in "Vora" { print(ch) }

// 遍历 range
for i in range(5) { print(i) }    // 0, 1, 2, 3, 4
for i in range(1, 5) { print(i) } // 1, 2, 3, 4

// 遍历字典（迭代键）
for key in dict { print(key) }
```

### C 风格 for 循环

> 引入版本: v0.19

```vora
// 完整形式
for (let i = 0; i < 5; i = i + 1) {
    print(i)
}

// 省略初始化
let k = 0
for (; k < 3; k = k + 1) { print(k) }

// 无条件循环 + break
for (let j = 0; ; j = j + 1) {
    if (j >= 10) { break }
}
```

`for-in` 与 `C-for` 通过 `for (` 前缀区分。

### break / continue

> 引入版本: v0.5

```vora
while (true) {
    if (done) { break }       // 退出循环
    if (skip) { continue }    // 跳过本次迭代
}
```

---

## 6. 函数与闭包 / Functions & Closures

### 函数声明

> 引入版本: v0.7

```vora
func add(a, b) {
    return a + b
}
add(3, 4)  // 7
```

无 `return` 或 `return;` 时，函数返回 `null`。

### 匿名函数（Lambda）

> 引入版本: v0.19

```vora
let square = func(x) { return x * x }
square(7)  // 49

// 作为回调
[1, 2, 3].map(func(x) { return x * 2 })

// 立即调用 (IIFE)
let r = func(a, b) { return a + b }(3, 4)  // 7
```

### 闭包

函数捕获定义时所在作用域的变量：

```vora
func makeCounter() {
    let count = 0
    return func() {
        count = count + 1
        return count
    }
}

let c = makeCounter()
c()  // 1
c()  // 2
c()  // 3
```

### 默认参数

> 引入版本: v0.15

```vora
func greet(name, greeting = "Hello") {
    return greeting + ", " + name
}
greet("World")              // "Hello, World"
greet("Vora", "Hi")         // "Hi, Vora"

// 默认值可引用前置参数
func addOne(a, b = a + 1) { return b }
addOne(5)                   // 6
```

### 尾调用优化 (TCO)

> 引入版本: v0.20

当 `return` 后直接跟函数调用时，自动复用栈帧。支持无限尾递归：

```vora
func fact(n, acc) {
    if (n <= 1) { return acc }
    return fact(n - 1, n * acc)    // 尾调用 — 不增长栈
}
fact(10000, 1)  // ✅ 正常完成
```

---

## 7. 生成器 / Generators

> 引入版本: v0.21

### yield 语法

```vora
func countTo(n) {
    let i = 1
    while (i <= n) {
        yield i
        i = i + 1
    }
}
```

### iter() / next() 协议

```vora
let gen = iter(countTo(3))
print(next(gen))  // 1
print(next(gen))  // 2
print(next(gen))  // 3
// next(gen) 抛出 StopIteration — 可被 try/catch 捕获
```

### for-in 消费生成器

```vora
for x in countTo(5) {
    print(x)   // 1, 2, 3, 4, 5
}
```

---

## 8. 对象与继承 / Objects & Inheritance

### Obj 声明

> 引入版本: v0.7

```vora
Obj Person(name, age) {
    this.name = name
    this.age = age

    func greet() {
        print("Hi, I'm " + this.name)
    }
}

let p = Person("Alice", 25)
p.greet()           // Hi, I'm Alice
print(p.name)       // Alice
p.age = 26          // 属性赋值
```

### 单继承

```vora
Obj Animal(name) {
    this.name = name
    func speak() { print("...") }
}

Obj Dog : Animal (name, breed) {
    this.breed = breed
    func speak() {
        print("Woof! I'm " + this.name)
    }
}

let d = Dog("Rex", "Husky")
d.speak()  // Woof! I'm Rex
```

### 多继承 (C3 MRO)

> 引入版本: v0.15

```vora
Obj A() { func who() { return "A" } }
Obj B() { func who() { return "B" } }
Obj C : A, B () {}

let c = C()
c.who()  // "A" — C3 线性化确定方法解析顺序
```

### super 关键字

> 引入版本: v0.15

```vora
Obj Parent() {
    func greet() { return "Hello from Parent" }
}

Obj Child : Parent () {
    func greet() {
        return super.greet() + " and Child"
    }
}

Child().greet()  // "Hello from Parent and Child"
```

---

## 9. 异常处理 / Exception Handling

> 引入版本: v0.7 | finally 完善: v0.10

### try / catch / finally

```vora
try {
    throw "something went wrong"
} catch (e) {
    print("Caught: " + e)
} finally {
    print("Always runs")
}
```

### throw

```vora
throw "error message"
throw 404
throw {code: 500, msg: "Server Error"}
```

`throw` 可以抛出任何类型的值。

---

## 10. 模块系统 / Modules

> 引入版本: v0.21

### import

```vora
import "json"                    // 导入整个模块
import "math" as m              // 导入并重命名

// 使用
let data = json.parse("[1, 2]")
print(m.sqrt(16))               // 4.0
```

### from ... import

```vora
from "json" import parse, stringify

let data = parse("[1, 2, 3]")
print(stringify(data))
```

### export

```vora
// mylib.va
export let PI = 3.14159
export func greet(name) { return "Hi, " + name }

// main.va
import "./mylib"
print(mylib.PI)
print(mylib.greet("World"))
```

模块路径支持：
- 裸名 → 搜索 `std/` 目录 → 当前目录
- `"./foo"` → 相对路径
- `"../bar"` → 父目录相对路径

---

## 11. 字符串插值 / String Interpolation

> 引入版本: v0.8

```vora
let name = "World"
print("Hello ${name}!")         // Hello World!

let obj = Person("Alice", 25)
print("Name: ${obj.name}")      // Name: Alice
```

`${...}` 内支持表达式和属性访问。未找到变量时保留原始文本。

---

## 12. 内建函数 / Built-in Functions

### 输出 / Output

| 函数 | 说明 |
|------|------|
| `print(args...)` | 打印参数到 stdout，空格分隔，自动换行 |

### 类型 / Type

| 函数 | 说明 |
|------|------|
| `type(v)` | 返回类型名：`"int"`, `"float"`, `"string"`, `"array"`, `"dict"`, `"boolean"`, `"null"`, `"function"`, `"object"` |
| `int(v)` | 转换为整数 |
| `float(v)` | 转换为浮点数 |
| `toString(v)` | 转换为字符串 |

### 工具 / Utility

| 函数 | 说明 |
|------|------|
| `len(v)` | 返回数组元素数 / 字符串长度 / 字典键数 |
| `clock()` | 返回自 Epoch 以来的秒数（浮点） |
| `assert(cond, msg?)` | 条件为 falsy 时抛出异常 |
| `input(prompt?)` | 从 stdin 读取一行，EOF 返回 `null` |
| `range(stop)` / `range(start, stop)` / `range(start, stop, step)` | 生成数值数组 |

### 进制转换 / Number Base

| 函数 | 说明 | 示例 |
|------|------|------|
| `bin(n)` | 转二进制字符串 | `bin(42)` → `"0b101010"` |
| `oct(n)` | 转八进制字符串 | `oct(63)` → `"0o77"` |
| `hex(n)` | 转十六进制字符串 | `hex(255)` → `"0xff"` |

### 迭代器 / Iterator

> 引入版本: v0.21

| 函数 | 说明 |
|------|------|
| `iter(collection)` | 创建迭代器（支持 Array、String、Dict、Generator） |
| `next(it)` | 前进迭代器，返回下一个元素。耗尽时抛出 `StopIteration` |

---

## 13. 标准库 / Standard Library

### std/math

> 引入版本: v0.21

```vora
import "math"

math.abs(-5)           // 5
math.sqrt(16)          // 4.0
math.sin(math.PI / 2)  // ~1.0
math.cos(0)            // 1.0
math.min([3, 1, 2])    // 1
math.max([3, 1, 2])    // 3

// 常量
math.PI      // 3.141592653589793
math.E       // 2.718281828459045
math.TAU     // 6.283185307179586

// 随机数
math.random.int(1, 100)           // 1~100 随机整数
math.random.float(0, 1, 2)        // 0~1 随机浮点, 2 位小数
```

### std/json

> 引入版本: v0.21

```vora
import "json"

// 解析
json.parse("42")                  // 42
json.parse("[1, 2, 3]")           // [1, 2, 3]
json.parse('{"a": 1}')            // {a: 1}
json.parse("not json")            // null (解析失败)

// 序列化
json.stringify({a: 1})            // '{"a":1}'
json.stringify([1, 2, 3], 2)      // 2 空格缩进的 pretty-print

// 往返
let data = {items: [1, 2], ok: true}
let s = json.stringify(data)
let copy = json.parse(s)
```

---

## 14. 附录：方法速查 / Method Reference

### Array 方法

| 方法 | 说明 |
|------|------|
| `arr.length` | 数组长度 |
| `arr.add(v)` | 末尾添加元素 |
| `arr.pop()` | 移除并返回末尾元素（空数组返回 null） |
| `arr.insert(i, v)` | 在索引 i 处插入 v |
| `arr.remove(i)` | 移除并返回索引 i 的元素 |
| `arr.indexOf(v)` | 查找 v 的索引，未找到返回 -1 |
| `arr.clear()` | 清空数组 |

### String 方法

| 方法 | 说明 |
|------|------|
| `str.length` | 字符串长度（字节数） |
| `str.substring(start, end?)` | 提取子串 |
| `str.include(sub)` | 是否包含子串 |
| `str.startsWith(prefix)` | 是否以 prefix 开头 |
| `str.endsWith(suffix)` | 是否以 suffix 结尾 |
| `str.upper()` | 转大写 |
| `str.lower()` | 转小写 |
| `str.trim()` | 去除首尾空白 |
| `str.replace(old, new)` | 替换第一个匹配 |
| `str.replaceAll(old, new)` | 替换全部匹配 |
| `str.split(delim)` | 按分隔符切分，返回数组 |

### Dict 方法

| 方法 | 说明 |
|------|------|
| `dict.length` | 键值对数量 |
| `dict.keys()` | 返回所有键的数组 |
| `dict.values()` | 返回所有值的数组 |
| `dict.has(key)` | 是否存在该键 |
| `dict.remove(key)` | 移除键值对，返回被移除的值 |

### 字符串拼接与索引

```vora
let s = "Hello" + " " + "World"   // 拼接
s[0]                                // "H" — 索引访问
```

### Array + 运算符

```vora
[1, 2] + [3, 4]    // [1, 2, 3, 4] — 合并
[1, 2] + 99         // [1, 2, 99] — 追加
99 + [1, 2]         // [99, 1, 2] — 前插
```
