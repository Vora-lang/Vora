# 标签 break / continue —— 设计与实现方案

> 状态：**设计阶段，未实现**
> 目标项：`VORA_SYNTAX_REVIEW.md` §2.8（P1，能力断头路）
> 覆盖面：AST / parser / compiler / 编辑器语法 / 文档
> 前置：Phase 1 其余 5 项语法缺口已完成（见 `CHANGELOG.md [Unreleased]`）

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

## 2. 现状勘察（以下行号基于 2026-09-10 `main`）

### 2.1 AST

- `BreakStmt`：`src/ast/stmt.h:444`，只有 `Token keyword` 字段
- `ContinueStmt`：`src/ast/stmt.h:465`，同上
- `StmtVisitor<R>`：`src/ast/stmt_visitor.h`，**20 个纯虚方法**（`visitExprStmt` … `visitErrorStmt`）
- **具体 visitor 实现者共 4 个**：`src/ast/ast_printer.h`、`src/lsp/semantic_analyzer.h`、`src/formatter/formatter.h`、`src/vm/compiler.h`

> 这个数字决定了方案取舍：新增一个 AST 节点要同步改 4 个 visitor + 接口 + `stmt.cpp` 的 accept 重载。

### 2.2 编译器循环簿记

`LoopContext`（`src/vm/compiler.h:537-545`）：

```cpp
struct LoopContext {
    size_t loopStart;                    // 循环条件处字节码偏移
    size_t continueTarget;               // continue 跳转目标
    std::vector<size_t> breakJumps;      // 待回填的 break 跳转占位
    std::vector<size_t> continueJumps;   // 待回填的 continue 跳转占位
    int enclosingScopeDepth;             // 循环入口处作用域深度
    int extraLocalsToPopOnBreak = 0;     // break 时需额外弹出的自动局部变量
    int extraLocalsToPopOnContinue = 0;  // continue 时需额外弹出的
};
std::vector<LoopContext> loopStack;      // 栈，栈顶=最内层（:547）
```

各循环编译函数（`src/vm/compiler_stmt.cpp`）：

| 函数 | 行号 | continueTarget |
|---|---|---|
| `visitWhileStmt` | 1015 | `= loopStart`（回头跳条件） |
| `visitDoWhileStmt` | 1054 | `= SIZE_MAX` 占位，后回填 |
| `visitForStmt`（for-in） | 1096 | 占位后回填 |
| `visitCForStmt` | 1224 | 增量段偏移 |

`visitBreakStmt`（`:1604`）/ `visitContinueStmt`（`:1641`）当前逻辑：

1. `loopStack.empty()` → 报错
2. `OP_POP_CATCH` × `tryNesting`（跳过 try 时清掉 VM 的 catch handler 栈）
3. 按**最内层**循环的 `enclosingScopeDepth` 计算需弹出的局部变量数，再加 `extraLocalsToPopOnBreak` / `...Continue`
4. `OP_POPN n` 弹栈
5. `emitJump` 并把占位登记进 **`loopStack.back()`** 的 `breakJumps` / `continueJumps`

### 2.3 ★ 关键发现：try/finally 路由**已经**支持跳转到外层循环

`visitTryStmt`（`:1684`）编译 try 体前后，对 **每一个** 循环层级做 diff 记录（`:1701-1741`）：

```cpp
struct CapturedJump { size_t offset; int loopIdx; };
for (size_t li = 0; li < loopStack.size(); li++) {     // ← 遍历全部层级
    auto& lc = loopStack[li];
    while (lc.breakJumps.size() > savedBreakCounts[li]) {
        capturedBreaks.push_back({lc.breakJumps.back(), (int)li});
        lc.breakJumps.pop_back();
    }
    // continueJumps 同理
}
```

随后在有 finally 时（`:1799-1828`）把这些跳转**改道**：先 `patchJump` 到当前点 → 重放 finally 字节码 → 重新 `emitJump` → 再登记回 **`loopStack[cj.loopIdx]`**（**原来的那一层**）。

**结论：只要带标签的 `break`/`continue` 把跳转登记进目标层 `loopStack[targetIdx]` 而不是 `loopStack.back()`，跨 try/finally 的 finally 重放就自动生效，无需改动 try 代码。** 这是本方案最大的简化来源。

### 2.4 for-in 内部会合成 break

`visitForStmt` 用 try/catch 捕获 `StopIteration`，其中一行：

```cpp
visitBreakStmt(BreakStmt(stmt.forToken));   // :1191
```

**注意**：这是编译器合成的 break，`BreakStmt` 新增 label 字段后必须让它取默认空标签（即"最内层"），否则会破坏 for-in 的迭代终止。

### 2.5 已确认的既有缺陷：break/continue 不关闭 upvalue

`Compiler::endScope`（`src/vm/compiler.cpp:310`）在局部变量出作用域时会为被闭包捕获的变量发射 `OP_CLOSE_UPVALUE`（`:315-319`）。但 `visitBreakStmt`/`visitContinueStmt` 只用 `OP_POPN` 弹栈，**不发射 `OP_CLOSE_UPVALUE`**。

实测（`continue` 用例，结论明确）：

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

即：闭包捕获到的是被后续迭代覆盖的栈槽。这属于项目原则第 7 条明令禁止的"**语法合法但语义非所见**"静默错误。

`break` 走同一段代码路径，**推定存在同一缺陷**；本次尝试独立复现时撞到一个无关的调用/索引异常（`g()` 返回了数组而非闭包返回值），**该复现尚未完成，须在实现阶段补一个干净的 break 用例确认**。

**要求：本项必须先于标签跳转实现修复。** 理由：标签跳转把"弹栈"从单层变成多层，若 upvalue 关闭仍缺失，错误面只会放大；且二者改的是同一段代码。

---

## 3. 方案选型

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

- `WhileStmt` / `DoWhileStmt` / `ForStmt` / `CForStmt`：加 `std::string label;`，构造参数追加 `std::string label = ""`
- `BreakStmt` / `ContinueStmt`：加 `std::string targetLabel;`，构造参数追加 `std::string targetLabel = ""`
  - **必须带默认值**，以保住 `compiler_stmt.cpp:1191` 的合成 break 调用点
- 若这些类的构造函数被别处按位置调用，需一并更新（实现时用编译器逐个暴露）

### 4.2 Parser（`src/parser/parser.cpp`）

**A. 识别标签循环**（`statement()`，`:158` 分派处）

在 `LET` / `FUNC` / ... 等关键字判断**之前**插入：

```
if (check(IDENTIFIER) && checkNext(COLON)) {
    // 需 2-token 前瞻；复用已有 peekNext()（src/parser/parser.h:724，P1-H 引入）
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
- `IDENT COLON` 在语句起始位置**当前不是合法语法**（已实测：`a: print(2)` → `Error: Unexpected token: :`），因此该前瞻无歧义
- 写入 label 字段时需 `dynamic_cast` 到 4 种循环类型之一；若不是循环（例如 `x: if ...`）→ 报错并丢弃标签
- 重复标签的检测放在编译期（见 4.3）而非语法期——同一层出现两个同名标签才是错，而语法期看不到嵌套结构

**B. `break` / `continue` 可带标签**

当前在 `statement()` 中处理（形如 `match(BREAK) → BreakStmt(previous())`）。改为：

```
if (match(BREAK)) {
    Token kw = previous();
    std::string target;
    if (check(IDENTIFIER)) {   // 注意：必须排除下一行开始的新语句
        ...
    }
    return std::make_unique<BreakStmt>(kw, target);
}
```

**⚠ ASI 陷阱**：Vora 已启用 Go 式词法 ASI（P0 #1）。`break` 后面**换行再跟标识符**时不应吞掉下一行：

```vora
break
foo()      // 这是两条语句，不是 "break foo"
```

判定规则须与 ASI 一致：**仅当标识符与 `break` 在同一行**时才算标签。实现时应对照 `parser.cpp` 中 ASI 的换行判定（`asiDepth_` / 行号比较，见 `:142-158`）复用同一判断，不要另造一套。

### 4.3 Compiler（`src/vm/compiler_stmt.cpp`）

**A. 记录标签**：4 个循环函数在 `loopStack.push_back(...)` 处填入 `label`：

```cpp
loopStack.push_back({loopStart, target, {}, {}, scopeDepth, 0, 0, stmt.label});
// LoopContext 追加字段： std::string label;
```

**B. 标签解析**（新增私有辅助）

```cpp
// 返回目标层在 loopStack 中的下标；-1 表示未找到
int Compiler::resolveLoopLabel(const std::string& label, const Token& at);
```

- 从 `loopStack.size()-1` 向下找到第一个 `label == 目标` 的层
- 未找到 → `error("no enclosing loop labeled '" + label + "'")`
- 同时负责重复标签检测：若在某层找到匹配，还要检查**更内层**是否已有同名（同层重名不应发生，因为是嵌套的）；简化做法：进入循环时若 `stmt.label` 非空且 `loopStack` 中已存在同名 → `error("duplicate loop label")`

**C. `visitBreakStmt` 泛化**（当前 `:1604`）

把"最内层"替换为"目标层 `ti`"：

```
1. loopStack.empty() → error
2. targetLabel 非空 → ti = resolveLoopLabel(...)；失败则 return
   否则 ti = loopStack.size()-1
3. OP_POP_CATCH × tryNesting                      ← 已是全局计数，无需改
4. 计算 localsToPop：
   a. 通用部分：locals[i].depth > loopStack[ti].enclosingScopeDepth
   b. 额外部分：中间各层的自动局部变量
      推测：for j in [ti .. back] 累加 loopStack[j].extraLocalsToPopOnBreak
      （★ 见 §5 风险 1：此式未经实测，很可能重复计数或漏计）
5. OP_POPN localsToPop
6. jump = emitJump(OP_JUMP)
7. loopStack[ti].breakJumps.push_back(jump)        ← 关键：登记到目标层
   （try/finally 的改道逻辑会自动处理，见 §2.3）
```

**D. `visitContinueStmt` 泛化**（当前 `:1641`）

与 break 同构，差异：
- 跳转登记进 `loopStack[ti].continueJumps`
- **不得弹出目标层自身的自动局部变量**（for-in 的 `_iter`/`_i`/`_len` 要跨迭代存活），但**必须**弹出中间各层的
- 因此额外弹出量用 `extraLocalsToPopOnContinue`（语义与 break 不同，勿混用）

**E. upvalue 关闭（前置修复）**

`visitBreakStmt`/`visitContinueStmt` 在当前作用域内、弹出局部变量之前，需为**被捕获的**局部变量发射 `OP_CLOSE_UPVALUE`——与 `endScope()`（`compiler.cpp:310-319`）的做法一致，但只针对从当前深度到目标深度之间出作用域的变量。建议抽一个共用辅助：

```cpp
void Compiler::emitCloseUpvaluesFor(int targetScopeDepth);
```

`locals[]` 中应已有"是否被捕获"的标记（`endScope` 用到了它）——实现时以 `endScope` 的现有判据为准，勿新造。

### 4.4 其他 visitor

| 文件 | 改动 |
|---|---|
| `src/formatter/formatter.h/.cpp` | 循环前输出 `label + ": "`；`break`/`continue` 后输出 ` + " " + targetLabel`（若有）。须加往返测试 |
| `src/lsp/semantic_analyzer.h/.cpp` | 遍历循环体（现有逻辑即可）；额外：`break foo` 指向不存在的标签时可出一致性警告（可选，非阻塞） |
| `src/ast/ast_printer.h` | 可选：S-表达式里带上标签，便于调试 |

### 4.5 编辑器语法（与语言同步，勿遗漏）

- `Vora-LSP/syntaxes/vora.tmLanguage.json`：新增"标签定义 + `break/continue` 带标签"的 pattern
- `Vora-LSP/zed/tree-sitter-vora/grammar.js`：`labeled_statement`（或给 4 种循环加可选 `label` 字段）+ `break_statement`/`continue_statement` 带可选标识符；改完须 `npx tree-sitter generate` **并重建 WASM**（步骤与坑见 `zed/README.md`）
- `Vora-WASM/www/vora-highlight.js`：把 `break`/`continue` 后的标识符当普通标识符即可，通常无需改；确认无回归

### 4.6 文档同步（四件套）

- `docs/16-v1.0-grammar-ebnf.md`：新增 `labeledLoop` / `breakStmt` / `continueStmt` 产生式；§7 状态表把 2.8 从"未实现"改为"已实现"
- `USER_GUIDE.md`：控制流章节加标签循环小节（含 ASI 注意事项）
- `docs/00-roadmap.md`：勾掉"标签 break/continue（2.8）"
- `CHANGELOG.md`：`[Unreleased] → Added` 登记；同时登记 upvalue 修复（`Fixed`）
- `VORA_SYNTAX_REVIEW.md`：顶部状态横幅把 2.8 从"仍未修复"移到"已修复"

---

## 5. 风险与缓解

| # | 风险 | 级别 | 缓解 |
|---|---|---|---|
| 1 | **多层局部变量弹出量算错**（§4.3-C.4b 的 `extraLocalsToPopOnBreak` 累加式未经实测）→ 栈失衡，可能表现为"能跑但值错"或延迟崩溃 | **高** | 不靠推理：实现后立刻用 `--tokens`（`src/main.cpp:398`，会打印字节码）逐例核对 `OP_POPN` 计数；并写"栈平衡"专项测试——在每个用例末尾让 VM 断言栈已清空（若无现成钩子，临时加断言，勿长期保留） |
| 2 | **upvalue 未关闭**（§2.5，既有缺陷）→ 闭包读到被复用的栈槽 | **高** | 作为前置修复项先落地并加回归测试（见 §6） |
| 3 | ASI 陷阱：`break` 换行后的标识符被误当标签 | 中 | 复用现有 ASI 换行判据；加"跨行不吞并"反例测试 |
| 4 | for-in 合成 break（`compiler_stmt.cpp:1191`）因新增字段而行为改变 | 中 | label 参数带默认值；单独跑 for-in 全套测试 |
| 5 | 未带标签的 `break`/`continue` 行为被改动（回归） | 中 | 现有 `tests/runtime` 中所有循环/异常用例必须全绿；新增测试不得修改既有断言 |
| 6 | 重复标签 / 指向不存在标签的静默容忍 | 中 | 两种情况都必须在**编译期报错**，且各配反例测试（违反原则第 7 条不可接受） |
| 7 | 编辑器语法与语言不同步 | 低 | §4.5 清单逐项核对；tree-sitter 改完必须重建 WASM，否则 Zed 端静默用旧语法 |

---

## 6. 测试计划

### 前置（upvalue 修复）

- `tests/runtime/test_loop_closure.va`（新建）：`continue` 用例（已知失败，先红后绿）+ `break` 用例 + 嵌套循环 + 捕获多个变量 + 无捕获对照组
- 断言要求：闭包返回**声明时**的值，而非后续迭代的值

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
| **标签 break 跨 try/finally** | finally 恰好执行一次，且顺序正确 |
| **标签 continue 跨 try/finally** | 同上 |
| 标签 break 跨越**内层 for-in**（含 `_iter` 清理） | 无栈泄漏、无 StopIteration 误捕获 |
| 标签 break/continue 在无标签循环内跨越 for-in 中间层 | 同上 |
| 标签跳转 + 闭包捕获循环体内变量 | 值正确（与前置修复联动） |
| 标签跳转在 try/catch 内、且 catch 不影响控制流 | 正确 |

### 回归与全量

- `Vora_tests`（当前 399 用例 / 1370 断言）全绿
- `tests/runtime` + `tests/interpreter` + `tests/formatter` + `tests/parser` + `tests/lexer` 全绿
  - 注意：`tests/interpreter/test_input.va` 需管道输入才通过（见 `run_tests.sh:69`），用 `printf 'hello\n\n42' | ...` 验证
- `examples/` 58/58 全绿
- 格式化往返：新增标签语法后 `vora fmt` 输出可再次解析且语义不变

---

## 7. 实施顺序（建议）

1. **修复 upvalue 关闭缺陷**（§4.3-E）→ 单独一个提交 + 回归测试。此项独立可验证，先摘下不确定性
2. AST 加字段（§4.1）→ 编译通过（合成 break 处不改）
3. Parser（§4.2）→ 用 `--ast-printer` 确认标签落到正确节点
4. Compiler 记录标签 + `resolveLoopLabel` + break/continue 泛化（§4.3）
5. **栈纪律专项验证**（风险 1）——此步不通过就不要继续
6. 编辑器语法（§4.5）
7. 文档四件套（§4.6）
8. 全量测试 + 提交推送

每步一个提交，便于二分定位。**在步骤 5 未通过前，不要推进到 6–8。**

---

## 8. 验收标准

- 上表全部测试用例通过（含反例）
- 无标签 `break`/`continue` 行为零变化，全部既有测试不改断言即通过
- `vora fmt` 对标签语法的往返幂等
- EBNF / USER_GUIDE / roadmap / CHANGELOG / syntax-review 五处状态一致，无"文档说支持但实际不支持"或反过来的情况
- 三端编辑器（VS Code tmLanguage、Zed tree-sitter+WASM、官网 Playground）对 `label:` 与 `break label` 均有合理高亮，且 tree-sitter 解析树正确

完成后即可进入 Phase 1 收官（`v0.30-syntax-freeze` 标签 + release notes）。
