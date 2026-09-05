# Vora 路线图

> 最后更新：2026-09-05（诚实重写，依据 `CHANGELOG.md`）
> 当前版本：v0.27.0（`ed722dd`）

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
| Pratt 解析器 + 33 关键字 + 85 操作码 | ✅ | 见 `src/lexer/token.h`、`src/chunk.h` |
| 字面量（数字 / 字符串 / Bool / Null / Array / Dict / Lambda） | ✅ | |
| 二元 / 一元 / 后缀（`?:` `??` `?.` `...spread`） | ⚠️ | `?:` 优先级高于 `\|\|`（与 C 系列反向），见 `VORA_SYNTAX_REVIEW.md` P0-2 |
| 控制流（`if/else`、`while`、`for-in`、`c-for`、`do-while`、`try/catch/finally`、`throw`、`break/continue`） | ✅ | 但 `if`/`while` 括号**实际必需**（旧的"无括号"措辞已过时） |
| `func` 默认 / 命名 / `rest` 参数 / 闭包 / `yield` / TCO | ⚠️ | 命名参数 2-token 前瞻冲突（`f(a = 1+2)`）；顶层 `rest` 不工作，仅 Obj 内可用 |
| `let` / `const` 解构 | ⚠️ | 顶层 `{x, ...rest}` 不可用 |
| `Obj` OOP（C3 MRO、`super`、`this`、构造、方法） | ✅ | |
| `import` / `export` / `from ... import` | ⚠️ | 路径含 `-` 时静默变成减法表达式（P0-4） |
| `match` 表达式 | ⚠️ | or-pattern `3 \| 4 =>` 不可用；lexer 不在 `\|\|` 外产 `\|` token |
| 推导式 `for x in xs yield x*2` | ⚠️ | **半坏**：解析通过，运行时报 `next() requires an iterator or generator` |
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

### 已知 P0 设计缺陷（待修，与 `VORA_SYNTAX_REVIEW.md` 同步）

1. **跨行吞并**：当前 parser 跨行不报错，导致 `let b = a \n -1` 被吞成减法。
2. **`?:` 优先级反转**：`a || b ? c : d` 与 C/JS/Go/Python 都相反。
3. **命名参数 2-token 前瞻吞掉表达式**：`f(a = 1+2)` 局部解析失败。
4. **`import` 路径非合法标识符时静默归并减法**：含 `-` 的合法文件名被吞。

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
[X] P0 #1 ASI / 跨行吞并（parser.cpp）  —— commit 970caa4
[X] P0 #2 ?: 优先级降到 || 之下          —— commit 61366f9
[X] P0 #4 import 路径非法标识符时编译错误 —— commit 830a2f2
[X] P0 #3 命名参数解析修复                —— commit c3bc77a（已 lock-in）
[X] 把上述冻结为 v1.0 EBNF               —— docs/16-v1.0-grammar-ebnf.md
[X] P1 修复：推导式要么实现完整，要么从 lexer / parser 删掉避免静默
[X] P1 修复：match or-pattern —— 本次 commit，lexer + parser 已通，EBNF §4.6 已同步
[X] docs/08 顶部加"时效性提示"指向 CHANGELOG.md
```

### Phase 2：补能力（2–3 月）

```
[ ] stdlib 扩到 12-14 模块：增加 std/path、std/process、std/io、std/text
[ ] stdlib/http 拆为独立仓库（与 vpm 同理，不进 v1.0 主仓）
[ ] 顶层 rest 参数、推导式（如决定做）或回退文档
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
