# 标签 break / continue —— 设计与实现方案

> 状态：**设计阶段，未实现**
> 目标项：`VORA_SYNTAX_REVIEW.md` §2.8（P1，能力断头路）
> 覆盖面：AST / parser / compiler / 编辑器语法 / 文档
> 前置：Phase 1 其余 5 项语法缺口已完成（见 `CHANGELOG.md [Unreleased]`）
> 基线：`main@59dd146`（2026-09-10）。**行号以该提交为准**。行号会随改动漂移，因此本版
> 一律采用「函数名 / 结构名为主、行号为辅」的引用方式；引用处若只有行号，请以函数名为准重新定位。
>
> **修订记录**
> - **v1**（`cf074cf`，2026-09-10）：初稿。
> - **v2**（本版）：v1 把若干**未经实测的推理写成了结论**，而这些推理恰好是错的。实现前的
>   前置修复阶段（`cf074cf..59dd146`，5 个提交）实际发现了 **6 个既有静默缺陷**，修了 5 个。
>   本版据此重写 §2.3、修正 §4.3-C 的弹栈公式与 try handler 计数、新增前置项 Bug F（未修）、
>   更新 §5 / §6 / §7 / §8，并重新核对全部行号。
>   §3 方案选型经 v1 → v2 的源码核对仍然成立，未改动。

---

## 1. 目标与范围

### 目标

```vora
outer: for i in [1, 2, 3] {
    for j in [1, 2, 3] {
        if (j == 2) { continue outer }   // 跳到 outer 的下一轮
        if (i == 3) { break outer }      // 直接跳出 outer
        print(toString(i) + "," + toString(j))
    }
}
```

- `label: <loop>` 为循环命名
- `break <label>` / `continue <label>` 从任意嵌套深度跳到**指定**循环的退出点 / 下一轮
- 未带标签的 `break` / `continue` 语义**完全不变**（最内层循环）

### 明确不做（non-goals）

| 项 | 理由 |
|---|---|
| `goto` | 与语言定位冲突，且 Vora 无此计划 |
| 给非循环语句打标签（`label: if ...`） | 只服务 `break`/`continue`，无 `goto` 时对非循环标签无意义；语法层直接拒绝，避免"能写但没用"的表面特性 |
| 跨函数跳转 | 无意义（`break` 不得指向外层函数的循环） |

### 语法定案（写进 EBNF）

```
labeledLoop     = identifier ":" ( whileStmt | doWhileStmt | forInStmt | cForStmt )
breakStmt       = "break" [ identifier ] ";"
continueStmt    = "continue" [ identifier ] ";"
```

---

## 2. 现状勘察

> 以下行号基于 `main@59dd146`。v1 的行号已因 5 个前置修复失效，本版已逐条改正，
> 并给出 v1 的旧值以便对照。

### 2.1 AST

- `BreakStmt`：`src/ast/stmt.h:444`，只有 `Token keyword` 字段
- `ContinueStmt`：`src/ast/stmt.h:465`，同上
- 4 个循环语句类：`WhileStmt`（`stmt.h:223`）、`DoWhileStmt`（`:248`）、`ForStmt`（`:274`）、
  `CForStmt`（`:311`）
- `StmtVisitor<R>`：`src/ast/stmt_visitor.h:44`，**19 个纯虚方法**（`visitExprStmt` … `visitErrorStmt`）
  - ⚠ v1 写的是「20 个」，实测为 **19**（`grep -c 'virtual R visit' src/ast/stmt_visitor.h` = 19）。
- **具体 visitor 实现者共 4 个**：`src/ast/ast_printer.h`、`src/lsp/semantic_analyzer.h`、
  `src/formatter/formatter.h`、`src/vm/compiler.h`
- `break` / `continue` 的 visitor 声明位置：`ast_printer.h:131` / `:132`、
  `semantic_analyzer.h:420` / `:422`、`compiler.h:351` / `:356`、
  `formatter.h:727` / `:737`

> 这个数字决定了方案取舍：新增一个 AST 节点要同步改 4 个 visitor + 接口 + `stmt.cpp` 的 accept 重载。

### 2.2 编译器循环簿记

`LoopContext`（`src/vm/compiler.h:537-550`，`loopStack` 在 `:552`）当前共 **8 个字段**：

```cpp
struct LoopContext {
    size_t loopStart;                    // 循环条件处字节码偏移
    size_t continueTarget;               // continue 跳转目标（见下方 SIZE_MAX 说明）
    std::vector<size_t> breakJumps;      // 待回填的 break 跳转占位
    std::vector<size_t> continueJumps;   // 待回填的 continue 跳转占位
    int enclosingScopeDepth;             // 循环入口处作用域深度
    int extraLocalsToPopOnBreak = 0;     // break 时需额外弹出的循环基础设施局部变量
    int extraLocalsToPopOnContinue = 0;  // continue 时需额外弹出的
    int tryDepthAtEntry = 0;             // ★ v2 新增：循环入口处的 tryNesting 快照（见 §2.3 缺陷 B）
};
```

各循环编译函数（`src/vm/compiler_stmt.cpp`，另有两个推导式在 `compiler_expr.cpp`）：

| 函数 | 行号（v1 旧值） | `continueTarget` 的取法 |
|---|---|---|
| `visitWhileStmt` | 1015（未变） | 入口置 `SIZE_MAX`，在**体尾回边**处回填（`:1033`）★ v2 已改 |
| `visitDoWhileStmt` | 1061（1054） | 入口置 `SIZE_MAX`，在**条件段**回填（`:1076`） |
| `visitForStmt`（for-in） | 1103（1096） | 入口置 `SIZE_MAX`，在**体尾回边**处回填（`:1214`）★ v2 已改 |
| `visitCForStmt` | 1239（1224） | 入口置 `SIZE_MAX`，在**增量段**回填（`:1293`） |
| 列表推导式 | `compiler_expr.cpp:694` | 同 for-in（该循环体是表达式，不可能出现 break/continue） |
| 字典推导式 | `compiler_expr.cpp:863` | 同上 |

> ★ 四处 `loopStack.push_back` 分别在 `compiler_stmt.cpp:1021 / 1068 / 1141 / 1277`。

`emitLoopExitCleanup`（声明 `compiler.h:574`，文档注释起于 `:554`；定义 `compiler_stmt.cpp:1619`）是 v2 新增的共用辅助，
集中处理「跳出循环时对沿途作用域的欠账」：为被丢弃的、且被闭包捕获的局部变量发射
`OP_CLOSE_UPVALUE`，并返回调用方所需的 `OP_POPN` 操作数。**它是 §4.3-C/D/E 的统一落点。**

`visitBreakStmt`（`:1654`，v1 记 1604）、`visitContinueStmt`（`:1687`，v1 记 1641）当前逻辑：

1. `loopStack.empty()` → 报错
2. 弹 catch handler：`tryNesting - loopStack[ti].tryDepthAtEntry` 个 `OP_POP_CATCH`
   （★ v2 已修正，见 §2.3 缺陷 B）
3. 调 `emitLoopExitCleanup(ti, isBreak)` 得到需弹出的局部变量数，其中包含对捕获变量的
   `OP_CLOSE_UPVALUE`（★ v2 已修复，见 §2.3 缺陷 A、§2.5）
4. `OP_POPN n` 弹栈
5. 发射跳转并把占位登记进目标层的 `breakJumps` / `continueJumps`

### 2.3 ★ 关键发现（v2 修正）：try/finally 路由只是**部分**可用

> **v1 的原话是错的。** v1 声称 try/finally 路由「已经」支持跳转到外层循环，因此
> 「只要把跳转登记进目标层 `loopStack[targetIdx]`，finally 重放就自动生效，**无需改动 try 代码**」。
> 实现前的前置修复阶段证明该判断不成立：为让标签跳转跨 try/finally 正确工作，需改动
> `visitTryStmt`、`visitBreakStmt`、`visitContinueStmt` 与 `visitWhileStmt` / `visitForStmt`
> 五个函数，外加 `LoopContext` 一个字段和两处推导式循环的 `push_back` 调用点（后者只是补该字段）。

#### 路由机制实际覆盖的范围

`visitTryStmt`（`compiler_stmt.cpp:1722`）确实会遍历**全部**循环层做 diff 记录
（快照 `:1737`，收集循环 `:1763`），并把捕获到的跳转在 `finally` 存在时改道
（break 改道 `:1846`、continue `:1858`、return `:1867`），再登记回**原来的那一层**
`loopStack[cj.loopIdx]`。这部分 v1 的描述是对的。

但它的成立条件比 v1 说的窄得多：

> **跳转必须是前向跳转，且该 try 必须真的有 `finally` 子句。**
> 并且即便两者都满足，「finally 在局部变量被弹出之前还是之后运行」这件事 v1 完全没有意识到（见 Bug F）。

#### 前置修复阶段发现并修掉的 5 个既有缺陷

均为**静默改变语义**的既有缺陷（非本设计引入），修复前均先以修复前的二进制复现确认。

| 编号 | 症状（修复前**实测**） | 修法 | 提交 |
|---|---|---|---|
| **A** | `break`/`continue` 用 `OP_POPN` 弹栈但不发射 `OP_CLOSE_UPVALUE`（`endScope` 会发射，见 `compiler.cpp:315-322`）。闭包捕获循环体局部变量后读到被后续迭代复用的栈槽：`continue` 用例返回 `20`（期望 `10`），`break` 用例返回 `21/21`（期望 `11/21`）。**v1 §2.5 只完成了 `continue` 复现，`break` 复现「尚未完成」——本版已补，并额外发现 C-for 初始化局部变量与 for-in 循环变量两个同类漏网场景** | 新增 `emitLoopExitCleanup`，在弹栈前为被丢弃的捕获变量发射 `OP_CLOSE_UPVALUE`；同时覆盖「体局部变量」与「目标循环自身的基础设施局部变量」两类 | `8696cb5` |
| **B** | v1 §4.3-C 步骤 3 写「`OP_POP_CATCH × tryNesting` 已是全局计数，**无需改**」——**该判断是错的**。`tryNesting` 统计**全部**外层 try，包括位于目标循环**之外**的。实测：`break` 落在「循环在 try 之内」的形态时，会把**包住循环**的 try 的 handler 一并弹掉，于是同一 try 里稍后的 `throw` 变成未捕获。因 for-in 的合成 break 在**每轮正常结束**时都会执行，**任何 `for x in [...]` 都会干掉外层 try 的 handler**；推导式的合成 try 同理；两层嵌套 try 夹一个循环时，外层 catch 会抢走本应由内层捕获的异常 | `LoopContext` 新增 `tryDepthAtEntry`（循环入口快照 `tryNesting`），弹栈数改为 `tryNesting - loopStack[ti].tryDepthAtEntry`。详见 §4.3-C 步骤 3 | `ad57950` |
| **C** | try **没有** `finally` 时，被 `visitTryStmt` 收集的 break/continue 跳转**永远不会被放回**，占位操作数停留在 `0xFF`。实测：`break` 从 try 里跳出会跳到无关指令中间，VM 报 `Unknown opcode`。波及所有循环类型的 `break`，以及 continue 目标是前向跳转的 do-while / C-for 的 `continue` | `visitTryStmt` 补 `else` 分支，把捕获到的跳转按原 `loopIdx` 放回 `breakJumps` / `continueJumps`（`:1885`） | `2a4668e` |
| **D** | `while` / for-in 的 `continue` 被直接编成**后向** `OP_LOOP` 回到条件处，该跳转完全绕开了 `visitTryStmt`，于是**外层 `finally` 被整个跳过**（每次 continue 少跑一次）。实测：`while` + try/finally + continue 返回 `3`（期望 `33`）。附带地，后向跳转**无法被拦截改道**，因此它是标签 continue 跨 finally 的硬前置 | `visitContinueStmt` 对所有循环类型统一发射**前向** `OP_JUMP`；`while` 与 for-in 各获得一个真实的 continue 目标（体尾回边），见 §2.2 表中 ★ 两行 | `77aa5ad` |
| **E** | finally 的**重放块**紧跟在正常路径的 finally 之后发射，且**没有任何跳转跨越**它们，于是 try 体正常结束时会直接掉进重放块。实测：`break` + finally 的循环**只跑 1 轮就退出**（返回 `21`，期望 `22`），且 finally 执行两遍 | 在重放块之前发射跳过跳转（`:1877`），正常路径跳过后再继续 | `59dd146` |

**为什么既有测试没抓到这些？** 实测确认：`tests/interpreter/test_edge_cases.va` 中
「break in try-finally」「continue in try-finally」两例在**修复前的二进制上同样全绿**——
它们只断言 finally「跑过」（`bf`/`cf` 为真），既没断言「恰好跑一次」，也没断言「循环在正确的
轮次退出」。缺陷 B/C/D/E 因此全部漏网。这是 §6 必须补强断言语义、而不是只看「有没有跑」的原因。

#### 路由机制修正后的**真实**能力边界

修正 A–E 之后，`visitTryStmt` 的改道对以下形态是正确工作的：

- 跳转是**前向**跳转（v2 起四种循环的 `continue` 都是，见缺陷 D）
- 该 try **有** `finally` 子句（无 finally 时按缺陷 C 的方式放回，属正常回填，不需改道）
- 嵌套 finally 按「内层先、外层后」重放
- `return` 路径一直不受缺陷 A/B/C/D/E 影响（`visitReturnStmt`（`:934`）不做局部变量清理，
  靠帧销毁；其 `pendingReturnJumps`（`compiler.h:590`）仅在 `finallyNesting > 0` 时登记）

**仍有一处未修**：Bug F（finally 读到已被弹出的局部变量），见 §2.6 与 §4.3-F。

### 2.4 for-in 内部会合成 break

`visitForStmt` 用 try/catch 捕获 `StopIteration`，其中一行：

```cpp
visitBreakStmt(BreakStmt(stmt.forToken));   // compiler_stmt.cpp:1199（v1 记 1191）
```

**注意**：这是编译器合成的 break，`BreakStmt` 新增 label 字段后必须让它取默认空标签（即"最内层"），
否则会破坏 for-in 的迭代终止。

另注：该合成 break 落在 for-in **自己的** try/catch 内，而 for-in 的自有 try 是**手写**的
（`visitForStmt` 内 `tryNesting++`），不经过 `visitTryStmt` 的收集/改道逻辑。其 handler 弹栈数
由 §4.3-C 步骤 3 的公式自然算出（`tryDepthAtEntry` 在 `loopStack.push_back` 时快照，
此时 for-in 自己的 `tryNesting++` 尚未发生），实测该处 `OP_POPN 2`（`_exn` + `_iter`）正确。

### 2.5 既有缺陷 A：break/continue 不关闭 upvalue（**已修复** `8696cb5`）

`Compiler::endScope`（`src/vm/compiler.cpp:310`）在局部变量出作用域时会为被闭包捕获的变量发射
`OP_CLOSE_UPVALUE`（发射循环在 `:315-322`，v1 记 `:315-319`）。但 `visitBreakStmt` /
`visitContinueStmt` 只用 `OP_POPN` 弹栈，**不发射 `OP_CLOSE_UPVALUE`**。

实测（`continue` 用例，v1 已确认）：

```vora
let fns = []
for i in [1, 2] {
    let v = i * 10
    fns += [func() { return v }]
    if (i == 1) { continue }
}
print(fns[0]())   // 实测 20，期望 10   ← 槽位被下一轮复用
print(fns[1]())   // 实测 20，期望 20
```

`break` 走同一段代码路径。**v1 提到「break 的同类复现尚未完成」——本版已补齐**，干净用例是
「从内层循环 break、外层循环继续跑」，使被 break 丢弃的槽位被外层下一轮复用：

```vora
let fns = []
for i in [1, 2] {
    for j in [1, 2] {
        let v = i * 10 + j
        fns += [func() { return v }]
        if (j == 1) { break }
    }
}
print(fns[0]())   // 实测 21，期望 11
print(fns[1]())   // 实测 21，期望 21
```

v1 的「只针对体局部变量」设想还**漏了两处**（本版实测确认）：

| 场景 | 捕获对象 | 修复前实测 | 期望 |
|---|---|---|---|
| C-for 的初始化局部变量 | `for (let k = 0; ...)` 中的 `k`，其 depth **等于** `esd`，属 `extraLocalsToPopOnBreak`，通用扫描看不见 | `777` | `0` |
| for-in 的循环变量 | `for x in [...]` 中的 `x`（由 `compileBindPattern` 在内层作用域绑定） | `888` | `5` |

**修法已落地**：`emitLoopExitCleanup`（`compiler_stmt.cpp:1619`）同时覆盖「depth > `esd` 的体
局部变量」与「目标循环自身的 `extraLocalsToPop*` 基础设施局部变量」两类，逐类发射
`OP_CLOSE_UPVALUE`。回归测试 `tests/runtime/test_loop_closure.va` 覆盖 continue / break /
嵌套 break / 四种循环 / 每轮多个捕获变量 / 可变捕获 / 两组对照，每个用例都先确认修复前失败。

### 2.6 未修复的既有缺陷：Bug F —— finally 读到已被弹出的局部变量

**状态：未修复，v2 新增前置项。** 这是 `visitTryStmt` 路由机制里最后一处结构性缺陷，
按「文档即契约」需要先在设计层定案。

复现（**实测**，`main@59dd146`）：

```vora
let trace = ""
for i in [1, 2, 3] {
    try {
        trace = trace + "b" + toString(i)
        if (i == 2) { break }
    } finally {
        trace = trace + "f" + toString(i)
    }
}
print(trace)   // 实测 b1f1b2f<native fn toString>，期望 b1f1b2f2
```

**根因**：`visitBreakStmt` / `visitContinueStmt` 发射的 `OP_POPN`（及 `OP_CLOSE_UPVALUE`）
位于**跳到** finally 重放之前——顺序是「弹栈 → 跳转到重放块」，而正确顺序是
「跑 finally → 弹栈 → 跳到退出点」。于是 finally 在**局部变量已被弹出**的状态下求值它自己的
表达式，压栈会把值写回那些被弹出的槽位，`GET_LOCAL` 便读到被自己覆盖后的内容。上例中
`toString(i)` 的被调用者 `toString` 先入栈、正好落进 `i` 的槽位，随后 `GET_LOCAL i` 读到的
就是 `toString` 自身。

该缺陷是**既有**的（在原二进制上同样错，只是被缺陷 E 的提前退出掩盖）。`break` 与 `continue`
都受影响；`return` **不受影响**（`visitReturnStmt` 不做局部变量清理，靠帧销毁）。

**注意它对本设计的影响面**：§6 中「标签 break / continue 跨 try/finally —— finally 恰好执行
一次，且**顺序**正确」这条验收用例，在 Bug F 未修时无法通过。因此 Bug F 必须排在标签实现之前
（见 §7）。修法设计见 §4.3-F。

---

## 3. 方案选型

> 本节经 v2 的源码核对仍然成立，未改动。

### 方案 A：新增 `LabeledStmt` 包装节点

`label: <loop>` 解析为 `LabeledStmt(label, innerStmt)`。

- 需要：`stmt_visitor.h` 加 1 个纯虚 + 4 个 visitor 各加 1 个实现 + `stmt.h`/`stmt.cpp` 加类与两个 accept 重载
- 代价：接口churn 大；且允许给任意语句打标签，需额外校验"内层必须是循环"
- 收益：语法上更通用（为将来的 `goto` 留口子——但本项目明确不做）

### 方案 B：给 4 个循环语句 + break/continue 加可选 `label` 字段 ★ 推荐

```cpp
// WhileStmt / DoWhileStmt / ForStmt / CForStmt 各加：
std::string label;   // 空 = 无标签

// BreakStmt / ContinueStmt 各加：
std::string targetLabel;   // 空 = 最内层
```

- **不需要新增 AST 节点 → 不需要改 `StmtVisitor` 接口 → 4 个 visitor 一处不动**
- 标签天然附着在它所命名的循环上，语义直白
- 编译器在各循环函数入口直接读 `stmt.label` 填入 `LoopContext`，管道最短
- 只有 formatter / semantic_analyzer 需要小幅改动（打印标签 / 遍历内层，都是可选）

**选定方案 B。** 唯一损失是不能给非循环打标签——而这正是我们明确不做的事。

---

## 4. 详细设计

### 4.1 AST（`src/ast/stmt.h`）

- `WhileStmt`（`:223`）/ `DoWhileStmt`（`:248`）/ `ForStmt`（`:274`）/ `CForStmt`（`:311`）：
  加 `std::string label;`，构造参数追加 `std::string label = ""`
- `BreakStmt`（`:444`）/ `ContinueStmt`（`:465`）：加 `std::string targetLabel;`，
  构造参数追加 `std::string targetLabel = ""`
  - **必须带默认值**，以保住 `compiler_stmt.cpp:1199` 的合成 break 调用点
- 若这些类的构造函数被别处按位置调用，需一并更新。**实测调用点清单**（`grep` 确认）：
  - `parser.cpp` 6 处：`forStatement()` 的 3 个返回点（`:726`、`:798`、`:868`）、
    `whileStatement()`（`:1497`）、`doWhileStatement()` 的 2 个返回点（`:1514`、`:1540`）
  - `compiler_stmt.cpp:1199` 合成 break（靠默认参数覆盖，**不需要改**）
  - 其余为各 visitor 的声明/定义，不构造节点
  - 因此实际只有 parser 需要传新参数；建议按 §4.2 的做法**在拿到循环节点后再写 label 字段**，
    这样 6 个构造点一处都不用改

### 4.2 Parser（`src/parser/parser.cpp`）

**A. 识别标签循环**（`statement()`，`:158` 分派处）

在 `LET` / `FUNC` / ... 等关键字判断**之前**插入：

```
if (check(IDENTIFIER) && checkNext(COLON)) {
    // 需 2-token 前瞻；复用已有 peekNext()（parser.h:724 声明，parser.cpp:1558 定义）
    Token labelTok = advance();   // 标识符
    advance();                    // ':'
    // 内层必须是循环
    if (!(check(WHILE) || check(DO) || check(FOR))) {
        error("labeled statement must be a loop (while / do-while / for)");
    }
    auto stmt = statement();      // 递归拿到循环语句
    // 把 labelTok.lexeme 写入该循环节点的 label 字段
    return stmt;
}
```

要点：

- `IDENT COLON` 在语句起始位置**当前不是合法语法**。**本版重新实测确认**：
  `a: print(2)` → `Error: Unexpected token: :`（parse 阶段即失败）。因此该前瞻无歧义。
  （注意 `:` 是单一 `COLON` token，`lexer.cpp` 中 `case ':'` 只产出它，不存在 `::` 双字符 token。）
- 写入 label 字段时需 `dynamic_cast` 到 4 种循环类型之一；若不是循环（例如 `x: if ...`）→ 报错并丢弃标签
- 重复标签的检测放在编译期（见 §4.3）而非语法期——同一层出现两个同名标签才是错，而语法期看不到嵌套结构

**B. `break` / `continue` 可带标签**

当前 `breakStatement()`（`:2604`）/ `continueStatement()`（`:2610`）只做
`make_unique<BreakStmt>(previous())` 然后 `match(SEMICOLON)`。改为：

```
if (match(BREAK)) {
    Token kw = previous();
    std::string target;
    if (check(IDENTIFIER) && <标识符与 kw 在同一行>) {
        target = advance().lexeme;
    }
    return std::make_unique<BreakStmt>(kw, target);
}
```

**⚠ ASI 陷阱**：Vora 已启用 Go 式词法 ASI（P0 #1）。`break` 后面**换行再跟标识符**时不应吞掉下一行：

```vora
break
foo()      // 这是两条语句，不是 "break foo"
```

判定规则须与 ASI 一致：**仅当标识符与 `break` 在同一行**时才算标签。实现时应对照
`parser.cpp` 中 ASI 的换行判定（判定语句在 `:2432`：
`asiDepth_ > 0 && parenDepth_ == 0 && peek().line > previous().line`，标志位 `asiDepth_`
声明于 `parser.h:167`）复用同一判据，不要另造一套。

**★ 兼容性变化（v2 新增实测结论，v1 遗漏）**：当前 `break foo` 写在同一行**是合法的**，
且被解析为两条语句 `break; foo`——因为 `breakStatement()` 不消费标识符，`foo` 落到下一条语句。
本版实测：

- `break foo()` 在循环内 → 只输出 `done`，**`foo` 从不执行**（`break` 无条件转移控制，后一条语句是死代码）
- `break foo()` 在循环外 → 报 `'break' outside of loop`（编译器报错，说明 parser 已接受为两条语句）
- `break` + 换行 + `foo()` → 正常解析为两条语句

因此引入标签后，源码里的 `break <标识符>`（同一行）**语义会改变**：从「break 后跟一条死语句」
变成「跳到名为该标识符的循环」。好消息是**实测确认现有代码零影响**：对 `tests/`、`examples/`、
`std/` 全量 grep，没有任何一处出现「`break`/`continue` 后跟标识符」的语句
（命中的全是注释与 `assert(bf, "xBr")` 之类的字符串）。但这条兼容性变化必须写进
CHANGELOG（见 §4.6），因为它把一类原本「能编过、只是死代码」的写法变成硬编译错误。

### 4.3 Compiler（`src/vm/compiler_stmt.cpp`）

**A. 记录标签**：4 个循环函数在 `loopStack.push_back(...)` 处填入 `label`。
`LoopContext` 当前（`compiler.h:537-550`）是 **8 个字段**（v1 写的是当时正确的 7 个，
`tryDepthAtEntry` 已在 `ad57950` 落地，见 §2.2），追加 `label` 后为 9 个：

```cpp
// LoopContext 末尾追加： std::string label;
loopStack.push_back({loopStart, SIZE_MAX, {}, {}, scopeDepth, 0, 0, tryNesting, stmt.label});
//                   ^^^^^^^^^  ^^^^^^^^                                    ^^^^^^^^^^  ^^^^^^^^^^
//                   loopStart  continueTarget（四种循环现在都是 SIZE_MAX，入口后回填）
//                                                                 tryDepthAtEntry  label
```

`compiler_expr.cpp:694` / `:863` 两个推导式循环没有用户标签，传 `""` 即可。

**B. 标签解析**（新增私有辅助）

```cpp
// 返回目标层在 loopStack 中的下标；-1 表示未找到
int Compiler::resolveLoopLabel(const std::string& label, const Token& at);
```

- 从 `loopStack.size()-1` 向下找到第一个 `label == 目标` 的层 → 返回 `ti`
- 未找到 → `error("no enclosing loop labeled '" + label + "'")` 并返回 -1（调用方 `return`）
- 重复标签检测：进入循环时若 `stmt.label` 非空且 `loopStack` 中已存在同名 →
  `error("duplicate loop label")`。放在编译期是因为语法期看不到嵌套结构（§4.2-A 已说明）

**C. `visitBreakStmt` 泛化**（当前 `:1654`）

把"最内层"替换为"目标层 `ti`"：

```
1. loopStack.empty() → error
2. targetLabel 非空 → ti = resolveLoopLabel(...)；失败则 return
   否则 ti = loopStack.size()-1
3. 弹 catch handler：handlersToPop = tryNesting - loopStack[ti].tryDepthAtEntry    ← ★ v2 修正
4. localsToPop = emitLoopExitCleanup(ti, /*isBreak=*/true)                        ← ★ 两段式，见下
5. OP_POPN localsToPop
6. jump = emitJump(OP_JUMP)
7. loopStack[ti].breakJumps.push_back(jump)        ← 关键：登记到目标层
   （try/finally 的改道逻辑会处理，见 §2.3；但注意 §2.3 列出的 5 个已修条件）
```

**★ v2 修正：弹栈公式（v1 §4.3-C.4b 的累加式是错的）**

v1 写「额外部分为 `for j in [ti .. back]` 累加 `loopStack[j].extraLocalsToPopOnBreak`」。
**该式会重复计数**。正确写法是**两项**，对中间各层**不求和**：

```cpp
localsToPop = count(locals[i].depth > loopStack[ti].enclosingScopeDepth)   // 通用项
            + (isBreak ? loopStack[ti].extraLocalsToPopOnBreak              // 仅目标层
                       : loopStack[ti].extraLocalsToPopOnContinue);
```

已由 `emitLoopExitCleanup(loopIdx, isBreak)`（`compiler_stmt.cpp:1619`）实现，
其内部就是上述两段：先向后扫「depth > `esd_ti`」的体局部变量，再取紧随其下的
`extraLocalsToPop*` 个基础设施局部变量。

**为什么累加会错**：中间各层 j（`ti < j ≤ back`）的循环基础设施局部变量位于
`depth == esd_j`。关键是 **带 `extra` 的循环（for-in、C-for）在进入时都调用了 `beginScope()`**
（`visitForStmt` 与 `visitCForStmt` 开头的 `beginScope()`，对应 `compiler_stmt.cpp:1141` / `:1277`
之前的那次调用），因此 `esd_j ≥ esd_ti + 1 > esd_ti`，**它们的 `_iter` / 初始化变量已经被通用项
`depth > esd_ti` 收进去了**。只有**目标层 ti 自己**的基础设施局部变量位于 `depth == esd_ti`，
通用扫描看不见，才需要那个 `extra` 项。v1 的式子会把中间层的 `_iter` 数两次，`OP_POPN`
过量 → 栈失衡。

> 精度补充：`while` / do-while 不调用 `beginScope()`，两者直接嵌套且都不带花括号时
> （如 `while (a) while (b) { ... }`）可能出现 `esd_j == esd_ti`。但这两类循环的
> `extraLocalsToPopOnBreak` 恒为 0，不参与 `extra` 项，故对该情形无害——**上式的正确性只依赖
> 「带 `extra` 的层必然 `esd` 更深」这一条**。

**为什么这个错误只在标签场景暴露**：未带标签时 `ti == back`，「累加中间各层」退化为
「只加一项」，两种写法**恰好等价**。所以它无法被现有测试或未带标签的用例发现——这正是
§7 步骤 5 必须用 `--tokens` 逐例核对 `OP_POPN` 操作数的原因。

> **⚠ 未验证声明**：上面的两段式，其「与未带标签现有行为完全等价」已通过
> `ti == back` 的退化关系**推得**，但**标签场景（`ti < back`）的 `OP_POPN` 计数尚未逐字节实测**——
> 因为标签功能尚未实现。§7 步骤 5 的 `--tokens` 核对通过之前，请勿把本节当成已验证结论。
> 核对方法（`--tokens` 会打印字节码及其操作数，实测可用）：
>
> ```
> $ vora --tokens case.va
> 0021    | OP_POPN             2
> ```
>
> 逐例对照手算值：`OP_POPN` 的操作数应等于「被放弃的体局部变量数 + 目标层基础设施局部变量数」。

**D. `visitContinueStmt` 泛化**（当前 `:1687`）

与 break 同构，差异：

- 跳转登记进 `loopStack[ti].continueJumps`
- **不得弹出目标层自身的自动局部变量**（for-in 的 `_iter` 要跨迭代存活），但**必须**弹出中间各层的
  ——而中间各层的基础设施局部变量已由通用项覆盖（理由同上），所以 `extra` 只能取
  `loopStack[ti].extraLocalsToPopOnContinue`
- 因此额外弹出量用 `extraLocalsToPopOnContinue`（语义与 break 不同，勿混用）
- 跳转统一为**前向** `OP_JUMP`（缺陷 D 的修法，`77aa5ad`），并对所有循环类型统一使用；
  v1 中「while 用后向 `OP_LOOP`」的分支已随缺陷 D 一并移除

**E. upvalue 关闭（原前置修复，**已完成** `8696cb5`）**

v1 设想的「抽一个 `emitCloseUpvaluesFor(int targetScopeDepth)`」已以更完整的形式落地为
`emitLoopExitCleanup(size_t loopIdx, bool isBreak)`（声明 `compiler.h:574`，定义 `compiler_stmt.cpp:1619`）。
标签版本**无需新写**：只要把 `ti` 传进去，它就会按目标层计算出正确的 `OP_CLOSE_UPVALUE`
序列与 `OP_POPN` 操作数。注意它比 v1 的设想多覆盖了「基础设施局部变量也可被捕获」这一情形
（C-for 的初始化变量，见 §2.5），这是 v1 漏掉的。

**F. ★ v2 新增前置项：Bug F —— 把清理挪到 finally 重放之后**

现状（问题）：`visitBreakStmt` / `visitContinueStmt` 的发射顺序是

```
OP_POP_CATCH × handlersToPop
OP_CLOSE_UPVALUE × c        ┐ 清理
OP_POPN n                   ┘
OP_JUMP  ──────────────►（被 visitTryStmt 改道到 finally 重放块）
```

`OP_JUMP` 是**清理之后**发射的，所以 finally 重放运行时局部变量已经没了 —— 这就是 §2.6 的 Bug F。

**修法设计（草案；未实现、未验证）**

核心：把清理指令从「跳出点 → 跳转」之间，移到「finally 链跑完之后、退出之前」。为此跳出点
需要产出**两类语义不同**的跳转：

- **pre-jump**：语义是「进入 / 继续 finally 链」。会被各层带 `finally` 的 try 依次捕获改道；
  若最终**没有**任何 try 捕获它，则由收尾阶段回填到该跳出点的 **cleanup 落点**。
- **exit jump（J2）**：语义是「清理已完成，去退出目标」。只登记进 `breakJumps` / `continueJumps`，
  **不得**被 try 捕获。

**为什么必须分开**：`visitTryStmt` 的收集逻辑（`:1763`）会把 `loopStack[li]` 中新增的跳转
**全部**捕获。若清理后的 J2 也登记在那里，外层 `finally` 就只会在**清理之后**才运行——又回到
Bug F。因此 pre-jump 需要一个与 `breakJumps` / `continueJumps` 并列的独立收集通道
（暂称 `LoopContext.preJumps`），由 `visitTryStmt` 的快照（`:1737`）与收集/改道循环（`:1763`）
一并处理。

```
跳出点（break / continue 现场）：
    OP_POP_CATCH × handlersToPop
    J1 = OP_JUMP ──► 有外层带 finally 的 try 时由其改道；否则回填到下面的 cleanup 落点
cleanup 落点:                          ← 固定地址；位于这些重放块之前，故只能用后向跳转抵达
    OP_CLOSE_UPVALUE × c
    OP_POPN n
    J2 = OP_JUMP ──► 登记进目标层 breakJumps / continueJumps（收尾时回填到退出点 / continue 目标）

带 finally 的 try 层（visitTryStmt 的改道处）：
    <pre-jump 落点>: replay finally bytes
                     OP_FINALLY_END
                     最后一跳 ──► 见下「谁负责最后一跳」
```

**谁负责最后一跳（嵌套的关键）**：改道块末尾**不能**再无条件发射前向 `emitJump` 并登记回
`breakJumps`（那是现状，也正是 Bug F 的成因——它让执行流在**清理之后**才去跑外层 `finally`）。
需要按 `finallyNesting` 分支：

- 若**还有**外层带 `finally` 的 try 会捕获 → 末尾发射一个新的 **pre-jump**（登记进 `preJumps`），
  交给外层继续改道；
- 若**没有**外层 → 末尾发射一次**后向** `OP_LOOP` 跳回该跳出点的 cleanup 落点；
  `emitLoop()` 支持已知目标的后向跳转。

`finallyNesting` 正是这个判据：它在 try 体编译前 `++`（`:1748`）、体编译后 `--`（`:1753`），
因此在改道阶段它的值就是**外层**带 `finally` 的 try 数量。执行顺序因此为
「内层 finally → 外层 finally → 清理 → 退出」，与语义要求一致。

其余要点：

4. **无 finally 时保持现状**：跳出点若不在任何带 `finally` 的 try 内（`finallyNesting == 0`），
   可直接沿用 `main@59dd146` 的现行发射形状（不生成 J1 / cleanup 落点），行为零变化，
   也避免多余跳转。
5. **与 §4.3-C/D/E 的关系**：本节只改**发射顺序**，不改 `emitLoopExitCleanup` 算出的清理**数量**。
   即两段式公式（§4.3-C/D）、`OP_CLOSE_UPVALUE` 序列（§4.3-E）、handler 计数（§4.3-C 步骤 3）
   三者都不变，产出的指令只是从「J1 之前」搬到「重放之后」。因此**不引入新的栈计数风险**，
   风险集中在控制流布线。
6. **新增的布线不变量（须写进 §7 步骤 5 的判据）**：cleanup 落点**不得**能被正常路径或
   其他任何落点穿透——每个落点都必须以 J2（无条件跳转）收尾；且所有 pre-jump 最终都必须被
   回填（要么被某层 finally 改道，要么落到本跳出点的 cleanup 落点），不得留下 `0xFF` 占位
   （这正是缺陷 C 的教训）。
7. **`return` 不受影响**：`visitReturnStmt`（`:934`）不做局部变量清理（不清 `OP_POPN`、不发射
   `OP_CLOSE_UPVALUE`），靠帧销毁，因此没有「清理早于 finally」的问题；其 `pendingReturnJumps`
   的处理（`:1867`）不在本项改动范围内。
8. **对标签设计的依赖**：标签跳转同样经过这条路径。若 Bug F 未修，标签 `break`/`continue`
   跨 finally 时会继承同一个「finally 看不到循环体局部变量」的错误，§6 中「顺序正确」的
   验收用例无法通过。因此 Bug F 排在标签实现之前（§7）。

> **⚠ 本节是设计草案**：上述跳转分类、`preJumps` 通道、以及「谁负责最后一跳」的判据均**未实现、
> 未实测**。实现时必须以 §6 前置表的用例逐条验证，特别是「嵌套 finally 的执行顺序」与
> 「所有 pre-jump 都被回填」两项。

### 4.4 其他 visitor

| 文件 | 改动 |
|---|---|
| `src/formatter/formatter.cpp`（循环 `:766`/`:775`/`:785`/`:800`；break/continue 见 `formatter.h:727`/`:737`） | 循环前输出 `label + ": "`；`break`/`continue` 后输出 `" " + targetLabel`（若有）。须加往返测试 |
| `src/lsp/semantic_analyzer.h/.cpp`（break/continue 声明 `:420`/`:422`） | 遍历循环体（现有逻辑即可）；额外：`break foo` 指向不存在的标签时可出一致性警告（可选，非阻塞） |
| `src/ast/ast_printer.h`（`:131`/`:132`） | 可选：S-表达式里带上标签，便于调试 |

### 4.5 编辑器语法（与语言同步，勿遗漏）

- `Vora-LSP/syntaxes/vora.tmLanguage.json`：新增"标签定义 + `break/continue` 带标签"的 pattern
- `Vora-LSP/zed/tree-sitter-vora/grammar.js`：`labeled_statement`（或给 4 种循环加可选 `label` 字段）+ `break_statement`/`continue_statement` 带可选标识符；改完须 `npx tree-sitter generate` **并重建 WASM**（步骤与坑见 `zed/README.md`）
- `Vora-WASM/www/vora-highlight.js`：把 `break`/`continue` 后的标识符当普通标识符即可，通常无需改；确认无回归

### 4.6 文档同步（四件套）

- `docs/16-v1.0-grammar-ebnf.md`：新增 `labeledLoop` / `breakStmt` / `continueStmt` 产生式；§7 状态表把 2.8 从"未实现"改为"已实现"
- `USER_GUIDE.md`：控制流章节加标签循环小节（含 ASI 注意事项，以及 §4.2-B 的兼容性变化）
- `docs/00-roadmap.md`：勾掉"标签 break/continue（2.8）"
- `CHANGELOG.md`：`[Unreleased] → Added` 登记标签语法（含 §4.2-B 的兼容性变化：
  同一行的 `break <标识符>` 由「死代码」变为引用标签，原本能编过的写法将变为编译错误）；
  5 个前置修复已单独登记在 `Fixed`（**本版已补齐**，见 CHANGELOG）
- `VORA_SYNTAX_REVIEW.md`：顶部状态横幅把 2.8 从"仍未修复"移到"已修复"

---

## 5. 风险与缓解

| # | 风险 | 级别 | 缓解 |
|---|---|---|---|
| 1 | **多层局部变量弹出量算错** → 栈失衡（可能表现为"能跑但值错"或延迟崩溃）。注意 v1 的累加式**本身就是错的**（§4.3-C），已改为两段式；但两段式的标签场景仍**未实测** | **高** | 不靠推理：实现后立刻用 `--tokens`（`src/main.cpp:398`，会打印字节码与操作数）逐例核对 `OP_POPN` 计数——**尤其是标签跳转（`ti < back`）的用例，这是唯一能暴露 v1 式错误的形态**；并写"栈平衡"专项测试。未通过不得推进 §4.5 / §4.6 |
| 2 | upvalue 未关闭（§2.5）→ 闭包读到被复用的栈槽 | **高** | **已修复**（`8696cb5`）。回归测试 `tests/runtime/test_loop_closure.va`。标签场景由 `emitLoopExitCleanup(ti, ...)` 自动覆盖，实现时须补一个「标签跳转 + 闭包捕获循环体变量」用例 |
| 3 | **Bug F：finally 读到已被弹出的局部变量**（§2.6）→ finally 顺序错误、读到垃圾值 | **高** | 设计见 §4.3-F（草案，未实现）；**修复排在标签实现之前**（§7 步骤 1）。验收判据：`finally` 中读取循环体局部变量（含 for-in 循环变量）得到的值正确；且 §4.3-F 点 6 的两条布线不变量成立（cleanup 落点不可被穿透、所有 pre-jump 都被回填） |
| 4 | ASI 陷阱：`break` 换行后的标识符被误当标签 | 中 | 复用现有 ASI 换行判据（`parser.cpp:2432`）；加"跨行不吞并"反例测试 |
| 5 | 兼容性：同一行的 `break <标识符>` 由「死代码」变为标签引用，原本能编过的写法变成硬编译错误 | 中 | **实测现有 `tests/`+`examples/`+`std/` 零命中**（§4.2-B）；在 CHANGELOG 与 USER_GUIDE 明写；反例测试覆盖「指向不存在的标签」 |
| 6 | for-in 合成 break（`compiler_stmt.cpp:1199`）因新增字段而行为改变 | 中 | label 参数带默认值；单独跑 for-in 全套测试 |
| 7 | 未带标签的 `break`/`continue` 行为被改动（回归） | 中 | 现有 `tests/runtime` 中所有循环/异常用例必须全绿；新增测试不得修改既有断言。注意 A–E 修复前后**既有 `test_edge_cases.va` 都是绿的**（§2.3 末尾），所以「全绿」不足以证明无回归——必须另加语义断言 |
| 8 | 重复标签 / 指向不存在标签的静默容忍 | 中 | 两种情况都必须在**编译期报错**，且各配反例测试（违反原则第 7 条不可接受） |
| 9 | 编辑器语法与语言不同步 | 低 | §4.5 清单逐项核对；tree-sitter 改完必须重建 WASM，否则 Zed 端静默用旧语法 |

---

## 6. 测试计划

> v2 补强说明：§2.3 末尾已实测证明，既有 `tests/interpreter/test_edge_cases.va` 的
> break/continue-in-try-finally 用例在缺陷 B/C/D/E 存在时**依然全绿**——它们只断言 finally
> 「跑过」。因此下表凡涉及 finally 的用例，**必须断言执行次数与执行顺序**，而不是「是否跑过」。

### 前置（Bug F，§4.3-F）——**排在标签之前**

| 用例 | 期望 |
|---|---|
| finally 读取 for-in 循环变量 | 读到**当轮**变量值（当前实测为 `<native fn toString>` 之类垃圾，见 §2.6） |
| finally 读取循环体内声明的局部变量 | 同上 |
| finally 读取循环体内的捕获变量 | 同上，且闭包值不受影响 |
| finally 执行次数 | 每条退出路径（正常 / break / continue / return）**恰好一次** |
| 嵌套 finally 顺序 | 内层先、外层后，且都在清理之前 |
| 所有 pre-jump 都被回填 | 无 `0xFF` 残留占位（缺陷 C 的教训）；`visitTryStmt` 的收集通道与 `breakJumps` / `continueJumps` 一致地成对处理 |
| cleanup 落点不可被穿透 | 正常路径与其他落点都不得掉进 cleanup 落点（每个落点以无条件跳转收尾） |
| 无 finally 的退出 | 与 `main@59dd146` 字节码行为一致（不得引入空跳导致的行为差异） |
| 栈平衡 | §7 步骤 5 的 `--tokens` 核对全部通过 |

### 前置（upvalue 修复，**已完成** `8696cb5`）

- `tests/runtime/test_loop_closure.va`：`continue` 用例 + `break` 用例 + 嵌套循环 +
  C-for 初始化局部变量 + for-in 循环变量 + 捕获多个变量 + 可变捕获 + 无捕获对照组。
  **每个用例都已确认修复前失败**。
- 断言要求：闭包返回**声明时**的值，而非后续迭代的值。

### 标签功能

| 用例 | 期望 |
|---|---|
| 双层循环 `break outer` | 直接跳出外层，内层不继续 |
| 双层循环 `continue outer` | 外层进入下一轮（内层重置） |
| 三层循环 `break` 到最外层 | 正确 |
| 无标签 `break`/`continue` 在嵌套内 | 仍作用于最内层（回归） |
| `break` 指向不存在标签 | 编译期报错 |
| 重复标签 | 编译期报错 |
| `x: if (...) {}`（给非循环打标签） | 编译期报错 |
| `break` 换行后跟标识符 | 视为两条语句 |
| **标签 break 跨 try/finally** | finally 恰好执行一次，且顺序正确（依赖 Bug F 已修） |
| **标签 continue 跨 try/finally** | 同上 |
| 标签 break 跨越**内层 for-in**（含 `_iter` 清理） | 无栈泄漏、无 StopIteration 误捕获 |
| 标签 break/continue 在无标签循环内跨越 for-in 中间层 | 同上（**这是唯一能暴露 v1 §4.3-C.4b 累加式错误的形态，必须配 `--tokens` 核对**） |
| 标签跳转 + 闭包捕获循环体内变量 | 值正确（与前置修复联动） |
| 标签跳转在 try/catch 内、且 catch 不影响控制流 | 正确 |
| 标签跳转跨越的 try **没有 finally** | 正确（缺陷 C 的形态；回归） |
| 标签跳转落在「循环在 try 之内」的形态 | 同 try 里稍后的 `throw` **仍被该 try 捕获**（缺陷 B 的形态；回归） |

### 回归与全量

- `Vora_tests`（`main@59dd146` 实测 399 用例 / 1370 断言）全绿
- `tests/runtime` + `tests/interpreter` + `tests/formatter` + `tests/parser` + `tests/lexer` 全绿
  - 注意：`tests/interpreter/test_input.va` 需管道输入才通过（见 `tests/run_tests.sh:69`），用 `printf 'hello\n\n42' | ...` 验证
- `examples/` 58/58 全绿
- 格式化往返：新增标签语法后 `vora fmt` 输出可再次解析且语义不变

---

## 7. 实施顺序（v2 修订）

1. **修复 Bug F**（§4.3-F）→ 单独一个提交 + 回归测试（§6 前置表）。此项独立可验证，
   且**必须先于标签实现**：标签跳转跨 finally 的正确性依赖它
2. AST 加字段（§4.1）→ 编译通过（合成 break 处不改，靠默认参数）
3. Parser（§4.2）→ 用 `--ast-printer` 确认标签落到正确节点
4. Compiler 记录标签 + `resolveLoopLabel` + break/continue 泛化（§4.3-A/B/C/D）
   + 重复标签 / 不存在标签的编译期报错
5. **栈纪律专项验证**（风险 1）——此步不通过就不要继续。判据至少四条：
   a. `--tokens` 逐例核对 `OP_POPN` 操作数，**必须包含 `ti < back` 的标签用例**
      （未带标签时两段式与 v1 式等价，无法据此判断）
   b. 每个用例末尾 VM 栈已清空（若无现成钩子，临时加断言，勿长期保留）
   c. **跨 finally 时，清理指令（`OP_CLOSE_UPVALUE` / `OP_POPN`）必须位于 finally 重放之后**
      —— 这是 Bug F 的同一条判据，标签实现不得把它改回去
   d. §4.3-F 点 6 的两条布线不变量：**所有 pre-jump 都被回填**（无 `0xFF` 残留占位，
      缺陷 C 的教训）、**cleanup 落点不可被正常路径或其他落点穿透**
6. **在步骤 5 未通过前，不要推进到 7–9。**
7. 编辑器语法（§4.5）
8. 文档四件套 + `VORA_SYNTAX_REVIEW.md` 横幅（§4.6）
9. 全量测试 + 提交推送

每步一个提交，便于二分定位。

---

## 8. 验收标准

- 上表全部测试用例通过（含反例）
- 无标签 `break`/`continue` 行为零变化，全部既有测试不改断言即通过
  - 注意 §2.3 末尾的教训：「既有测试全绿」不是充分证据，A–E 修复前它同样全绿；
    因此本项另需 §6 前置表与标签表中「未带标签形态」的语义断言佐证
- Bug F 已修复：`finally` 能读到循环体局部变量（含 for-in 循环变量）的正确值，
  且每条退出路径 finally 恰好执行一次、顺序正确
- §7 步骤 5 的四条判据全部通过（`--tokens` 计数、栈清空、清理位于重放之后、
  布线不变量成立）
- `vora fmt` 对标签语法的往返幂等
- EBNF / USER_GUIDE / roadmap / CHANGELOG / syntax-review 五处状态一致，无"文档说支持但实际不支持"或反过来的情况
- 三端编辑器（VS Code tmLanguage、Zed tree-sitter+WASM、官网 Playground）对 `label:` 与 `break label` 均有合理高亮，且 tree-sitter 解析树正确

完成后即可进入 Phase 1 收官（`v0.30-syntax-freeze` 标签 + release notes）。
