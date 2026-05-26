# Vora


## Progress

> Source → Token ✔

> Token → AST ✔

> AST → Runtime / Interpreter

## Base

| 项目     | 名字      |
| :------- | --------- |
| 语言     | `Vora`    |
| 文件     | `.va`     |
| 解释器   | `vora`    |
| VM       | `Vora VM` |
| 包管理器 | `vpm`    |

## Aim

### 第一版

> AST Interpreter

> 不要 VM。

------

### 第二版

> Bytecode VM

------

### 第三版

> 优化/JIT

## Feature

### v0.01
Literal + Expression

支持：
```vora
1 + 2 * 3
"hello"
TRUE
NULL
```
目标：
```vora
Expression Parser
```
### v0.02
Variable + Scope

支持：
```vora
let a = 10
{
    let b = 20
}
```
目标：
```vora
Environment / Scope
```

### v0.03
Function

支持：
```vora
func add(a, b) {
    return a + b
}
```
目标：
```
Call Frame
```

### v0.04
Control Flow

支持：
```vora
if
while
for
break
continue
```
目标：
```vora
Jump / Branch
```

### v0.05
Object + Array

支持：
```vora
Obj Student(){}
[]
```
目标：
```vora
Heap Object
```

### v0.06
Closure

支持闭包。

目标：
```vora
Captured Environment
```
### v0.07
Exception

支持：
```vora
try catch finally
```

### v0.08
Module System

支持：
```vora
import "math"
```
## Perfer

> [目录 · Crafting Interpreters](https://zaslee.github.io/craftinginterpreters/contents.html)
>
> [Lua 5.4 source code](https://www.lua.org/source/5.4/)
>
> [– Wren](https://wren.io/)
>
> [CPython](https://github.com/python/cpython)
