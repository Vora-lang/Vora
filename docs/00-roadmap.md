# Vora 路线图

> 最后更新：2026-09-07（P1-F 位运算收尾，Phase 1 主体完成）
> 当前基线：v0.27.0（`ed722dd`）之上叠加 Phase 1 修复提交（见 `CHANGELOG.md [Unreleased]`）

本路线图依据 `CHANGELOG.md` 与源代码重新核对。**任何"声称完成"的功能必须同时出现在
`CHANGELOG.md` 的 `[0.27.0] / [Unreleased]` 部分，并由源码支撑。**仅出现在文档但
代码里没有的特性，已被全部移除或移入"已知问题"段。

---

## 定位

> **Vora — a dynamically typed scripting language with JavaScript-like syntax,
> Lua-level simplicity, and Wren-style object orientation.**

竞争对手：Lua、Wren、AngelScript。不是 Python、Rust、TypeScript 的替代品。

---

## 当前实际状态（2026-09-05，对照 `ed722dd` HEAD）

### 语言核心

| 特性 | 状态 | 备注 |
|------|------|------|
| JS 风格块语法（`{ }`，无显式分号） | ✅ | |
| Pratt 解析器 + 34 关键字 + 91 操作码 | ✅ | 见 `src/lexer/token.h`、`src/chunk.h`；33 + `not`（2.9）；85 + P1-F 新增 6 位运算 opcode |
| 字面量（数字 / 字符串 / Bool / Null / Array / Dict / Lambda） | ✅ | |
| 二元 / 一元 / 后缀（`?:` `??` `?.` `...spread`） | ✅ | `?:` 优先级低于 `\|\|`（P0 #2 修复后与 C 系列一致）；一元 `+x` 已识别（P1-E） |
| 控制流（`if/else`、`while`、`for-in`、括号 `for (x in xs)`、`c-for`、`do-while`、`try/catch/finally`、`throw`、`break/continue`） | ✅ | `if`/`while` 括号必需；P1-H 后 for-in 也接受 `for (x in xs)` 形式 |
| `func` 默认 / 命名 / `rest` 参数 / 闭包 / `yield` / TCO | ✅ | 命名参数 2-token 前瞻冲突已 lock-in（P0 #3）；顶层 rest 参数仍未实现（仅 class 方法体内合法） |
| `let` / `const` 解构（含顶层 `...rest`） | ✅ | 顶层 `{x, ...rest}` 与 `[a, ...rest]` P1-D 后可用 |
| `class` OOP（C3 MRO、`super`、`this`、构造、方法） | ✅ | |
| `import` / `export` / `from ... import` | ✅ | 路径含 `-` 时在缺 alias 下编译期报清晰错误（P0 #4）；显式 `as` alias 或 `from ... import` 不受影响 |
| `match` 表达式（含 or-pattern `1 \| 2 \| 3 =>`） | ✅ | P1-B 后 lexer 产 `TokenType::PIPE`，parser `matchExpression()` 循环累计 alternation |
| 推导式 `for x in xs yield x*2` | ✅ | v0.27 起 compiler_expr.cpp 走 iter()/next 脱糖，52/53 examples 全跑通 |
| 类型注解 `:int/:float/:bool/:str` | ✅ | 自动运行时转换 |

### 标准库（9 个模块）

| 模块 | 状态 | 文件 |
|------|------|------|
| `std/math` | ✅ | `std/math.va` |
| `std/json` | ✅ | `std/json.va` |
| `std/fs` | ✅ | `std/fs.va` |
| `std/os` | ✅ | `std/os.va` |
| `std/datetime` | ✅ | `std/datetime.va` |
| `std/array` | ✅ | `std/array.va` |
| `std/string` | ✅ | `std/string.va` |
| `std/regex` | ✅ | `std/regex.va` |
| `std/async` | ✅ | `std/async.va`（`async`/`await` 在 v0.27 已上） |

### 基础设施

```
构建：   CMake 跨平台（Win x64/x86/arm64、Linux x64/x86/aarch64/armhf、macOS Universal × Debug/Release）
打包：   .msi（WiX）/.deb/.rpm/.pkg.tar.xz/.zip/.tar.gz
嵌入：   include/vora.hpp 单头 + lib/vora_lib.lib；零外部依赖；VM NaN-box 字节于 v0.27
LSP：    Vora-LSP 仓库独立维护（C++ 服务端，复用 vora_lib，VS Code / Zed 客户端）
调试：   VM 暴露 traceHook / stepHook 给 C embedder；尚无 DAP 服务端
文档：   Doxygen 解析 34 个公开头 → API 文档（scripts/build-api-docs.py）；网站 docs 由 scripts/build-website-docs.py 生成
测试：   doctest C++ 单元测试；脚本测试与示例在仓库根下相应目录（数量请以本仓库 README.md 实测为准，不再硬编码）
```

### GC 与运行时

- **Generational GC**（commit `992907d`，v0.27）：minor + major 双代，512 KiB / 4 MiB 阈值，写屏障，promotion-after-3-survives。
- **NaN-boxed Value**（v0.27）：8 字节。`OP_GET_LOCAL_PROP` / `OP_GET_GLOBAL_PROP` superinstruction。
- **Async runtime**（commit `22a70ed`，v0.27）：generator 扩展 + `Task` 值类型 + `run(task)` builtin。

> 与 v0.27 前的文档（`docs/08-已实现功能总结.md`、`docs/00-roadmap.md` 原版）对比：
> 老文档把"分代 GC"标为 v0.28 **规划中**、把"async/await"标为 v0.28 **规划中**。
> 但 `ed722dd` 实际已包含这两项（commit `992907d`、`22a70ed` 于 7 月 3 日合入，
> 7 月 22 日 `ed722dd` 重新打底）。本路线图修正此差异。

### 已知 P0 设计缺陷 —— ✅ 全部修复（Phase 1，2026-09）

与 `docs/VORA_SYNTAX_REVIEW.md` §1 对应，修复详情见 `CHANGELOG.md [Unreleased]`：

1. ~~跨行吞并~~ → Go 式词法 ASI（statement-level 换行终止语句；括号/调用内不触发）。
2. ~~`?:` 优先级反转~~ → 三元降到 `\|\|`/`??` 同级（1），左结合先吃完 `\|\|`，与 C/JS/Go/Python 一致。
3. ~~命名参数 2-token 前瞻吞掉表达式~~ → 经评审决策 lock-in：保留 `f(name = value)` 记号（见 CHANGELOG 说明）。
4. ~~`import` 路径非合法标识符静默归并减法~~ → 含 `-` 等非法标识符字符的路径在缺 alias 时编译期报清晰错误。

---

## 路线图（修订后）

> "完成"判定标准（统一）：**单元测试 + 脚本测试 + 文档同步 + `CHANGELOG.md` 四项齐备**，
> 缺一不算完成。

### 即将进入 — 止血与可重现（2026-Q3 剩余）

```
[ ] GCC/MinGW 构建：补齐 missing transitive includes（本次 commit 已包含）
[ ] 文档同步：彻底拆掉对推导式 / 顶层 rest / match or-pattern / vpm / std/http 等的虚假声明（本次 commit 已包含）
[ ] CHANGELOG.md 落地（本 commit）
```

### Phase 1：语法锁版本 v1.0 文法（4–6 周）

```
[X] P0 #1 ASI / 跨行吞并（parser.cpp，Go 式词法 ASI）
[X] P0 #2 ?: 优先级降到 || 之下
[X] P0 #4 import 路径非法标识符时编译错误
[X] P0 #3 命名参数解析（评审决策 lock-in，保留 `f(name = value)` 记号）

> 注：P0 各项修复详情记录于 `CHANGELOG.md [Unreleased]`。早期 roadmap 版本
> 引用的 per-fix 提交哈希（970caa4 / 61366f9 / 830a2f2 / c3bc77a）在 2026-09
> 历史重写（`3f4f71e` snapshot）后已不可从 `main` 解析，不再作为引用依据。
> 重写前的原始历史保存在 `origin/Bytecode-VM` 与 `origin/AST-interpreter`
> 两个远端分支（与 `main` 无共同祖先），请勿删除——详见 CHANGELOG 顶部说明。
[X] 把上述冻结为 v1.0 EBNF               —— docs/16-v1.0-grammar-ebnf.md
[X] P1 推导式：实测已实现（v0.27 起 compiler_expr.cpp 走 iter()/next 脱糖，
    52/53 examples 全跑通）                  —— 本次 commit
[X] P1 修复：match or-pattern —— P1-B，lexer + parser 已通，EBNF §4.6 已同步
[X] docs/08 顶部加"时效性提示"指向 CHANGELOG.md
[X] P1-D 顶层 rest destructuring（compiler_stmt.cpp synthetic global-temp 路径）
[X] P1-E 一元 +x（parser primary + compiler visitUnaryExpr 折叠）
[X] P1-G `in` 表达式（新增 OP_IN；VM dispatch：Array → valuesEqual 扫描，Dict → key 查表，String → substring）
[X] P1-H `for (x in xs)` 括号 for-in（peekNext() 2-token lookahead + brace-aware scan）
[X] P1-F 位运算 `&` `|` `^` `~` `<<` `>>`（lexer 5 新 token + PIPE 复用；6 新 opcode；
    precedence 低于等值高于关系，见 EBNF §6；移位计数夹取；tests/runtime/test_bitwise.va + 三层单测锁定）
[X] `**=` 幂赋值（2.6；POWER_EQUAL token + compiler 映射 OP_POWER；右侧 float 语义与裸 `**` 一致；
    tests/runtime/test_compound_assign.va + 三层单测）
[X] `let x` 免初始化声明（2.7；无初值绑定 null，`let a:int` 等价 `let a:int = null` 得零值；
    const 仍强制初始化；tests/runtime/test_declarations.va + parser 单测）

[X] 对象字面量简写 `{x}`（2.11；等价 `{x: x}`，仅当标识符后紧跟 `,`/`}` 时识别；
    tests/runtime/test_dict_shorthand.va + 4 个 parser 单测）
[X] `not` 关键字（2.9；`TokenType::NOT` 别名 `!`，绑定紧于比较——与 Python 不同，已在 EBNF §5.1 显式标注；
    三端高亮同步）
[X] 插值转义 `\$`（2.10；词法层 `kEscapedDollar` 带内标记 + 编译器在 `emitConstant` 咽喉点还原；
    覆盖常量折叠与 formatter 往返；tests/runtime/test_string_escape.va + formatter 往返用例）
    —— 顺带修复既有缺陷：`"a" + "${x}"` 因常量折叠吞掉插值而**静默不插值**，现已在折叠前检查 `${`。

[X] 标签 break/continue（2.8；`outer: for ... { break outer }` / `continue outer` 可从任意嵌套深度
    指定目标循环。无新 AST 节点，只给 4 个循环语句与 break/continue 加可选 label 字段，故
    StmtVisitor 接口与 4 个实现都未改动。指向不存在的标签、重复标签、给非循环打标签三种情况
    均为编译期错误；`break` 换行后的标识符仍按 ASI 视为两条语句。
    tests/runtime/test_labeled_break_continue.va + parser/compiler 单测 + formatter 往返用例
    + 三端编辑器高亮）
    —— 行为变化：同一行的 `break <标识符>` 旧版是合法的（解析为两条语句，后一条是死代码），
    本次起改为引用标签。对 `tests/`、`examples/`、`std/` 全量 grep 无一处受影响。
    —— 设计过程中发现并修复 7 个既有静默缺陷，见 `CHANGELOG.md [Unreleased] → Fixed`。

Phase 1 剩余（语法评审第 5 节"第三批"项，尚未纳入本清单，实测确认仍未解决）：
  [ ] 尾随逗号（`[1,2,]` / `{a:1,}` / `f(1,2,)` / 形参表，实测均报错；EBNF §5.4 已改正声明）
```

### Phase 2：补能力（2–3 月）

```
[ ] stdlib 扩到 12-14 模块：增加 std/path、std/process、std/io、std/text
[ ] stdlib/http 拆为独立仓库（与 vpm 同理，不进 v1.0 主仓）
[ ] 顶层 rest 参数（class 方法体外 / 顶层函数场景）
[ ] 加密 RNG：覆盖 random() 实现，添加 bytes / uuid
[ ] 增加一组 Vora 自身写的工具：vora-fmt 已存在；新增 vora-lint（静态 AST 检查）
```

### Phase 3：工具链与生态（用户指示：vpm 暂不做）

```
[ ] CI 扩到 Windows / macOS（GitHub Actions 矩阵）
[ ] Vora-LSP 已支持 VS Code / Zed，再补一个轻量编辑器（vim / neovim / helix）
[ ] DAP 调试器服务端（基于 v0.27 已暴露的 debug hooks）
[ ] formatter 安全性审计：保证 AST ↔ source 双向幂等
[ ] 文档生成器（Doxygen → HTML）已实现（v0.27），扩到覆盖所有公开 API
```

### Phase 4：v1.0（与 Phase 2/3 重叠）

```
[ ] embed ABI 冻结
[ ] 语法 EBNF 冻结
[ ] std 模块按 Phase 2 列表冻结
[ ] 移除所有 "声称完成但实际不存在" 的条目
[ ] 移除 docs/14-技术债.md 中所有已闭环项，留下 Open
```

---

## 不会加入的特性

| 特性 | 原因 |
|------|------|
| 可选静态类型 | 动态类型是定位 |
| 方法重载 | 动态分发语义自然 |
| 装饰器 `@` | 与 Lua 级简洁定位冲突 |
| Char 独立类型 | 单字符是短字符串 |
| 错误传播 `?` | 有 try/catch |
| Sealed / Record | 与动态语言灵活性冲突 |
| 抽象方法 | `throw` 即可 |
| 包管理器 vpm | 用户 2026-09 暂缓指示 |

---

## 设计原则（不变）

1. **嵌入优先**：C++ 嵌入 API 是核心差异化武器。
2. **实用优先**：标准库 > 语法糖 > 性能优化。
3. **身份优先**：每个特性必须服务于"JS 语法 + Lua 简洁 + Wren OOP"。
4. **不贪多**：每季度 3–5 个核心特性，做到位。
5. **宁可少做，不可做错**：文档与代码必须同时为真。
6. **不静默**：做不到的功能，要么实现、要么明确标 ❌，绝不默写 "✅"。

---

## 竞争定位（不变）

| 维度 | Vora vs Lua | Vora 的优势 |
|------|-------------|-------------|
| 语法 | `end/if/then` | JS 风格大括号，零学习成本 |
| OOP | metatable hack | 原生类 + C3 多继承 + `super` |
| 异常 | pcall（函数式） | try/catch/finally（语法级） |
| 异步 | coroutine + yield | async/await + Task 类型 |
| GC | 单代 mark-sweep | **分代**（minor + major，写屏障） |
| 嵌入 | C API | C++ API（vora.hpp 单头） |
| 标准库 | 极小 | 9 个模块 |
| 性能 | 寄存器 VM，更快 | NaN-boxing + superinstruction |
| 构建 | Makefile | CMake 跨平台 + 原生打包 |

> **一句话**：Vora 不追求成为"下一个 X"，只追求成为唯一同时满足
> **可嵌入 + 现代语法 + 原生 OOP + 零依赖** 的语言。
