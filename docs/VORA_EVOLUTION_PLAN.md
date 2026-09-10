# Vora 进化计划 / Evolution Plan

> 版本：v1.0 · 2026-09-05
> 视角：技术负责人视角，面向 v0.x → v1.0 的真实路径
> 依据：`docs/00-roadmap.md`、`docs/08-已实现功能总结.md`、源码现状调查、`docs/VORA_SYNTAX_REVIEW.md`（语法缺陷 28 项）、`docs/PROJECT_OVERVIEW.md`、CI 与 stdlib 速摸
> 位置：自 2026-09-10 起纳入版本控制，位于 `Vora` 仓库 `docs/`（原为 `D:\Vora-lang\` 工作区根目录下的未纳管文件）。

> **📌 进度更新（2026-09-10）**：正文的「现状盘点」与第 12 节任务清单是 **2026-09-05 的快照，已大幅过时**。
> 截至当前：**阶段 S 已完成**；**阶段 1 的语法工作已全部完成**——P0×4（跨行吞并→Go 式 ASI、三元优先级、
> 命名参数 lock-in、import 路径校验）＋ P1-A..H（推导式验证、match 或模式、顶层 rest 解构、一元 `+`、
> `in` 表达式、括号 for-in、位运算 `& | ^ ~ << >>`）＋ 第三批缺口（`\$` 转义、`not`、`**=`、
> `let` 免初始化、对象字面量简写、标签 break/continue）全部落地；EBNF 冻结已建
> （`docs/16-v1.0-grammar-ebnf.md`）。阶段 1 仅剩「尾随逗号」一项未决，见 `docs/00-roadmap.md`。
> 另外，实现上述特性期间共发现并修复 **10 个既有的静默语义缺陷**（控制流与 try/finally 交互 9 个、
> 常量折叠 1 个）——详见 `CHANGELOG.md [Unreleased] → Fixed`，它们比新增语法更影响既有代码的正确性。
> vpm 按用户指示暂缓。逐项事实以 `CHANGELOG.md [Unreleased]` 为准。
>
> **继续更新（2026-09，v0.30 冻结前）**：语法工作收尾阶段又落地两项破坏性变更——
> 类声明关键字 `Obj` → **`class`**（§3.6），以及 `match` 无匹配臂由静默返回 `null`
> 改为抛运行时错误（§3.8）；尾随逗号纳入 v1.0。九项冻结裁决与执行计划见
> `docs/18-v0.30-冻结计划.md`。

---

## 0. 一页纸

Vora 现在的真正状态**不是** `docs/00-roadmap.md` 上写的"Phase 3 全部完成"。它是一份**完成度高但有几处结构性伤疤**的小型语言：语法层面有 4 处会**静默改变代码含义**的陷阱；3 个文档承诺的语法特性实测**完全不可用**；stdlib 只有 9 个模块，其中 `async` 只有 4 行；CI 只覆盖 Linux。

但优势也真实存在——C++ 嵌入 API（`vora.hpp` + `vora_lib.lib`、零第三方依赖）是硬差异化；CMake 跨平台矩阵 + 原生安装包（`.msi/.deb/.rpm`）已就位；字节码 VM + NaN-boxing 让 Value 降到 8 字节。

**核心判断**：Vora 不需要"加更多特性"，而是需要"把已经写进文档的东西真正跑通，再修掉那 4 个静默陷阱"。v1.0 的全部价值就是**"不再背弃现有用户"**。

按下面四阶段推进，每个阶段都有**可验收的退出标准**：

| 阶段 | 时长 | 主题 | 退出标准 |
|---|---|---|---|
| **S · 止血** | 1–2 周 | 修阻塞性构建、推未推送的提交、填真实 CHANGELOG | HEAD 在 MSVC / GCC / Clang 三编译器都干净；所有 CHANGELOG 条目在 CI 可见 |
| **1 · 锁定语法** | 4–6 周 | 修 4 个 P0 静默错意 + 关键 P1；冻结 v1.0 文法 | `docs/VORA_SYNTAX_REVIEW.md` 第 5 节前 10 项全部通过；语法冻结 commit |
| **2 · 补齐核心能力** | 2–3 个月 | 修实际不工作的"已完成"特性；stdlib 长出 5 个模块；REST 解构到顶层 | 推导式 5/ 5 用例跑通；`stdlib` 模块数 ≥ 14；top-level rest 解构可用 |
| **3 · 工具与生态** | 3–6 个月 | 包管理器、CI 扩平台、编辑器发布、formatter 安全化 | `vpm install` 可用；CI 覆盖 Win/macOS/Linux × Debug/Release；`vpm` 注册表上线 beta |
| **4 · v1.0 稳定化** | 3–6 个月（与阶段 3 重叠） | API/ABI 冻结、向后兼容策略、迁移指南 | 三个 minor 版本无破坏性变更；`vora@1.0` 标签 |

---

## 1. 现状的诚实盘点

### 1.1 与现有 roadmap 的偏差

| 项 | `docs/00-roadmap.md` 声称 | 实测 / 源码实情 |
|---|---|---|
| 列表 / 字典推导式 | v0.26 ✅ 已完成 | **完全不可用**：range/数组/生成器遍历全抛 `next() requires iterator or generator` |
| 顶层 rest 解构 `let [first, ...rest]` | 未提及限制 | 实测被拒绝 "only supported inside functions"；`USER_GUIDE.md` 顶层示例是错的 |
| `async/await + 事件循环` | v0.28 ✅ 完成 | 编译期有 `async`/`await` 关键字与代码生成；但 `std/async.va` 只有 4 行——底层并发原语基本空白 |
| `stdlib 8 个模块` | ✅ | 实际 **9 个**（多了 `array.va`，但 `async.va` 4 行等于不存在） |
| `vpm 包管理器` | Phase 4 计划 | 不存在；import 只支持 std 路径 + 相对路径，无注册表、无版本约束 |
| `WebAssembly 后端` | ✅ 完成 | `Vora-WASM/` 用 Emscripten 编 VM，前端是裸 JS；与核心仓库部分功能不同步（如类型注解、NaN-boxing 版本差） |
| CI | 未写细节 | 仅 `ci.yml` 一份，**仅 Linux**（4 架构 × 2 配置 = 8 矩阵）；Windows / macOS 离线 |
| `v0.27.0 (Phase 3 全部完成)` | 2026-06-29 | 本地 HEAD 仍在 v0.27.0；远程已含 v0.28 + 后续多个提交（含分代 GC、属性内联缓存、字面量 string_view 等）。仓库文案明显**漂在真实代码前面** |

### 1.2 当前真正可用的能力

✅ **稳定可信**：
- 字节码 VM（栈式、85 操作码、含 OP_TAIL_CALL + 超指令）
- NaN-boxing Value（8 字节）
- 分代 GC（minor / major + 写屏障，远程已落地）
- C++ 嵌入 API（`vora.hpp` 单头 + `vora_lib.lib`，零依赖）
- CMake 跨平台（Linux 4 架构 + Windows x64/x86/arm64 + macOS Universal）
- CPack 原生打包（`.msi/.deb/.rpm/pkg.tar.xz/tar.gz`）
- LSP 服务器：诊断、补全、跳转、悬停、引用、签名帮助、格式化、文档符号
- DAP 调试适配器（vora-dap.exe）
- C++ 单元测试（doctest，328 用例 ~1019 断言）
- 151 个脚本测试、57 个示例

⚠️ **文档承诺但实际有 bug**：
- 列表/字典推导式（完全不可用）
- rest 解构（仅函数内可用）
- match 或模式 `3 | 4`（词法无 `|` token）
- 一元加号 `+x`（无）
- 位运算符全系（无）
- `**=`（无）

❌ **结构性缺陷**：
- 4 处静默错意（见 `docs/VORA_SYNTAX_REVIEW.md` §1）
- 跨行吞并、formatter 不能安全换行
- import 绑定名从路径字符串派生，无校验

### 1.3 资源与节奏假设

- **团队规模假设**：1–3 人核心贡献者。CI 单平台、C++ 嵌入文档成熟度、stdlib 模块平均 30–40 行——这些都指向一个**精干但人手紧**的项目。
- **节奏假设**：每年 2 个 minor 版本，每个 minor 推 3–5 个核心特性，与 `00-roadmap.md` 的"不贪多"原则一致。
- **目标用户**：C++ 嵌入工程师（脚本化、游戏、配置 DSL）、脚本语言爱好者、学生。**不是**通用应用开发。

---

## 2. 设计原则（继承并强化 `00-roadmap.md`）

原 5 条全部保留，下面**只新增或强化**：

1. **嵌入优先**（保留）— C++ 嵌入 API 仍是唯一差异化武器，所有破坏性改动先过嵌入 SDK 一关
2. **实用优先**（保留）— 标准库 > 语法糖 > 性能优化
3. **身份优先**（保留）— JS 语法 + Lua 简洁 + Wren OOP
4. **不贪多**（保留）— 3–5 个核心特性 / 季度
5. **宁可少做，不可做错**（强化）— **新增第 6 条：**

6. **文档即契约**：v1.0 起，`USER_GUIDE.md` / `CLAUDE.md` / `docs/` 中描述的任何语法或行为，CI 必须有对应**正例 + 反例**测试。文档与实现不一致时，**测试优先于文档**——文档必须改到与测试一致。

7. **不可静默**：任何会改变用户代码语义的运行时决定（隐式类型转换、隐式装箱、静默后备），要么显式可见（lint/告警），要么语法层拒绝。**绝不允许"语法合法但语义非所见"**。这一条直接对应语法评审的 4 个 P0。

---

## 3. 阶段 S：止血（1–2 周内）

> 目标：让任何人在干净克隆后 30 分钟内能跑通 `vora hello.va`。

### S.1 修复 GCC/Clang 编译
- `src/ast/binding_pattern.h:42` 缺 `#include <cstdint>`
- `src/gc/gc_heap.cpp:123` 缺 `#include <algorithm>`
- 排查所有 `.h` 的**间接依赖**：在 CI 加 `gcc -Wall -Wextra -fsyntax-only` 单独编译每个头；改为**显式包含**。
- 退出标准：MSVC 2022 / GCC 12+ / Clang 15+ 三编译器 `cmake --build` 0 warning 0 error。

### S.2 推送未发布的本地提交
- `Vora` 仓库当前 `9698b9b`（HEAD）尚未推送（origin/main 之前领先 4 个提交）。
- **先 rebase**：抓取远端 `ed722dd` 等 4 个新提交，rebase 本地 `9698b9b`，确保无冲突。
- 退出标准：远端 HEAD 与本地 HEAD 一致；本地 commit `9698b9b` 已落远端。

### S.3 真实 CHANGELOG
- `00-roadmap.md` 自称"v0.27.0 Phase 3 全部完成"，但和代码不一致——这就是为什么没人看 roadmap。
- 新建 `CHANGELOG.md`，按 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 规范：
  - 每个 minor 版本一个 section
  - 标注 Added / Changed / Deprecated / Removed / Fixed / Security
  - 从 `git log` 反向回填 v0.20 至今的实际变化
- 退出标准：`CHANGELOG.md` 首版提交；CI 校验未来每次 release 必更新。

### S.4 修文档中的语法谎言
- 删除或重写 `USER_GUIDE.md` 的：
  - 列表/字典推导式章节（功能未实现，写"planned for v0.30"）
  - match 或模式 `3 | 4`（要么实现，要么标注"planned"）
  - 顶层 rest 解构示例（标注"only inside functions"）
  - `if`/`while` 括号"可选"那一行（写明"current version requires parentheses"）
- 退出标准：所有非实现的语法特性在文档中标 `[planned v0.x]` 或直接删。

---

## 4. 阶段 1：锁定语法（4–6 周）

> 目标：在 v0.30 上**冻结语法**。v1.0 起任何破坏性语法变更走 deprecation 流程。

### 1.1 修复 P0 静默错意（必须最先）

每项有**修复方案 + 验收测试**，详细见 `docs/VORA_SYNTAX_REVIEW.md` 第 5 节：

| # | 缺陷 | 修复 | 验收测试 |
|---|---|---|---|
| 1 | 跨行吞并 | 强制 `;` 终止语句（推荐）/ Go 式词法 ASI | 14 个跨行用例全部"语义独立" |
| 2 | 三元 vs `\|\|` 优先级倒置 | `QUESTION` 优先级调到 0（低于 OR） | `true \|\| false ? "A":"B"` 测得 `"A"` |
| 3 | 命名参数吞掉赋值 | 命名参数改用 `:` 或 `:=`；`=` 留给赋值 | `f(a = 5)` 必须是赋值表达式 |
| 4 | import 路径非法静默 | 强制 `as` 或具名导入；非法标识符路径报错 | `import "./foo-bar"` 必须报错或要求别名 |

### 1.2 补齐 P1 关键缺口

| # | 缺口 | 修复 | 验收测试 |
|---|---|---|---|
| 5 | `in` 不是表达式 | 加 `BINARY in` 算符（优先级等同 `<`） | `if (x in arr) {}` 通过 |
| 6 | `for (x in arr)`` 死路 | 消费 `(` 后先试探 `IDENT + in` | 该写法可用 |
| 7 | 无位运算 | 实现 `& \| ^ << >>` 与一元 `~` | 标志位与掩码示例通过 |
| 8 | match 或模式 | 加 `PIPE` token；改 match 模式文法 | `3 \| 4 => "small"` 可用 |
| 9 | `**=` | 加 `POWER_EQUAL` token | `x **= 3` 可用 |
| 10 | `let x` 不允许未初始化 | 移除 `Expected '=' after variable name` 这条约束；声明默认 `null` | `let x; if (c) { x = 1 }` 可用 |
| 11 | 插值转义 | 实现 `\$` 转义：词法层识别 `\$` 后下一个 `$` 不开始插值 | `"\${x}"` 输出字面 `${x}` |
| 12 | 无 `not` | 加 `NOT` 关键字，与 `and`/`or` 配齐 | `not (x > 0)` 可用 |

### 1.3 修补"已实现"特性

| # | 缺陷 | 修复 | 验收测试 |
|---|---|---|---|
| 13 | 列表/字典推导式完全不可用 | 改推导式为 `iter()`-first：`for x in iter(arr)` 已支持；推导式应走相同路径 | `[i*2 for i in range(5)]` 跑出 `[0,2,4,6,8]` |
| 14 | 顶层 rest 解构失败 | 解构路径独立于 declaration | `let [a, ...rest] = arr` 在顶层可用 |
| 15 | 一元加号 | 加 `PLUS` 前缀解析 | `+x` 与 `+"42"` 可用 |
| 16 | 对象字面量无简写 | 与解构简写对齐 | `{x}` 当 `{x: x}` |

### 1.4 文法演进

- 解构提升为**独立文法产生式** `pattern → arrayPattern | objectPattern | identifier | wildcard`，不再走 `convertDictExprToBinding`/`convertArrayExprToBinding` 的回溯转换。
- 删除 `parser.h:693` 注释里残留的 `BITWISE_OR / XOR / AND` 三行（占位假象），与代码一致。
- `as` token 注释删"type cast"（不存在该用法）；要么补 cast 语法。

### 1.5 退出标准

- `docs/VORA_SYNTAX_REVIEW.md` 第 5 节前 10 项全部通过；
- `docs/grammar.ebnf`（新建）写出完整 EBNF，作为 v1.0 文法契约；
- 所有破坏性变更在 `CHANGELOG.md` 标注 `BREAKING`；
- `git tag v0.30-syntax-freeze` 并发 release notes。

---

## 5. 阶段 2：补齐核心能力（2–3 个月）

> 目标：从"声称完成"到"实测可用"，并把 stdlib 从 9 个模块扩到 14+。

### 2.1 实际修复"已实现"特性

| 项 | 现状 | 目标 |
|---|---|---|
| 顶层 rest 解构 | 仅函数内 | 顶层可用（与 1.3 #14 共用） |
| 对象 rest `{x, ...rest}` | 文档承诺未实现 | 解构文法独立后开放（phase 1 文法改完直接解锁） |
| for-in 模式解构（远程新增） | 本地未合入 | 把远端提交 `ed722dd` 的 for-in pattern 测试拉到本地实现 |
| async/await | 编译过、stdlib 无实现 | `std/async.va` 落地，至少提供 `Task`, `run()`, `wait_all()`, `delay(ms)` |
| 字符串 `length` 是字节数 vs `for ch` 是字符 | 不一致 | 文档明示语义 + 加 `.utf8Length` 属性；不改默认避免破坏 |

### 2.2 stdlib 扩展

当前：array, async, datetime, fs, json, math, os, regex, string（9 个）。目标 +5 个：

| 新模块 | 内容 | 优先级 |
|---|---|---|
| `std/path` | 跨平台路径拼接、dirname/basename/extname、glob | P0（任何用到 fs 的程序都要） |
| `std/log` | 等级、时间戳、彩色输出 | P0 |
| `std/http` | HTTP 客户端（基于 `std/net`） | P1 |
| `std/net` | TCP/UDP socket | P1 |
| `std/process` | spawn/pipe/环境变量 | P2 |
| `std/encoding` | base64 / url-encode / json5 | P2 |

每个新模块必须有：
- 模块顶层文档注释（what + why + 1 用法）
- ≥ 5 个测试用例
- ≥ 1 个 `examples/*.va` 演示
- CI 跑通

### 2.3 运行时 / 性能

- 远程已加：分代 GC、内联属性缓存、常量池 string_view。**先合入**到本地 HEAD（rebase 时已带，但要跑回归）。
- `tests/bench/`（已存在）建立基线，每次 minor 跑一次，记录在 `docs/perf-history.md`。
- 退出标准：分代 GC 在 100MB 堆下 minor pause < 1ms；property cache hot-loop 提速 ≥ 3×（对比未 cache）。

### 2.4 退出标准

- 阶段 1 + 阶段 2 所有承诺语法特性在 `tests/parser/`、`tests/runtime/`、`tests/lexer/` 中各跑通。
- stdlib 模块数 ≥ 14；每个模块覆盖率 ≥ 80%。
- `git tag v0.40-capability-complete`。

---

## 6. 阶段 3：工具与生态（3–6 个月，与阶段 4 重叠）

### 3.1 包管理器 `vpm`

最被现有 roadmap 提及但**完全不存在**的能力。这块单独投入 1 人月。

设计要点（不展开方案，先给目标）：
- 命令：`vpm install <pkg>`、`vpm add <pkg>`、`vpm remove <pkg>`、`vpm publish`、`vpm search`、`vpm lock`
- 仓库：`Vora-packages/`（独立组织），纯静态文件 + JSON 索引（与 crates.io 类似）
- 解析：`import "foo"` 走 `vpm` 解析为 `$VORA_HOME/pkg/foo/1.2.3/mod.va`
- 锁定：`vpm.lock` 记录依赖树
- 版本：semver 严格
- 与现有 `import "./..."` / `import "std/..."` 共存；`vpm` 解析优先级最高
- 注册表：先用 GitHub Pages 托管静态 JSON（成本为零）；后续可迁

### 3.2 CI 扩到 Win/macOS

当前 CI 仅 Linux（`ci.yml` 一份 workflow）。扩展：

- **GitHub Actions**：
  - `ci-linux.yml`：保留并拆分，按 preset 单文件
  - `ci-windows.yml`：windows-latest × x64 / arm64 × Debug / Release
  - `ci-macos.yml`：macos-latest × x64 / arm64 × Debug / Release
- **缓存**：用 `actions/cache` 缓存 CMake build dir
- **冒烟测试**：每个 matrix 跑 `vora hello.va`、`tests/runtime/test_*.va`（已存在 151 个）
- **退出标准**：所有 6 平台 × 2 配置 × 完整测试，PR 阻断。

### 3.3 编辑器集成

- VS Code 扩展（`Vora-LSP/` 内有）：发布到 Marketplace（不是只放在仓库）
- Zed 扩展（`Vora-LSP/zed/` 未跟踪）：纳入仓库、补 README、补 syntax 截图
- Vim/Neovim：补 tree-sitter grammar（如果远端提交里有，跟进；否则新建第三方仓库）
- Helix：复用 tree-sitter
- 退出标准：4 个编辑器都有可发布的扩展包。

### 3.4 Formatter 安全化

依赖阶段 1 #1（强制 `;` 或 ASI 规则）之后，formatter 才能可靠地：
- 在 `}` 前保留空行（避免误解析）
- 在方法链调用 `.method()` 后接 `;`
- 在长行折行时不改变语义

### 3.5 文档站点

`Vora-lang.github.io/` 当前是手写 HTML，每次发版要手改。引入 Docusaurus 或 MkDocs（**保留手写主页**）：
- 自动化：GitHub Action 监听 `Vora/USER_GUIDE.md` 变更 → 自动 build → push 到 `gh-pages`
- API 文档：源码 `///` 注释 → Doxygen → 现有 `scripts/generate_api.py` → 站点 `api/`
- 退出标准：每次 release 自动更新 `vora-lang.github.io`。

### 3.6 退出标准

- `vpm install` 在 3 个平台上可用；
- CI 12 矩阵全绿；
- 4 个编辑器扩展都发布到对应 marketplace；
- 文档站点自动发布跑通。

---

## 7. 阶段 4：v1.0 稳定化（3–6 个月，与阶段 3 重叠）

### 7.1 API/ABI 冻结

- `vora.hpp` 公共 API：列出所有 export 符号，逐个标 stable / unstable / deprecated
- 字节码格式：v1.0 字节码版本号定死；旧字节码文件可读但不能跑（带清晰错误）
- stdlib API：v1.0 之前所有 stdlib 模块标 "experimental"；v1.0 切 stable
- 退出标准：`docs/api-stability.md` 明确每个公开符号的状态

### 7.2 兼容策略

- v1.0 起严格 semver：minor 不破坏、major 不兼容
- 任何破坏性变更必须走 3 步 deprecate：warning（minor） → runtime error（major） → remove
- 旧脚本通过 `vora --compat=v0.x` 标志在 v1.x 上跑

### 7.3 迁移指南

- `docs/migration/` 下放 `0.x → 1.0` 指南
- `vora check --migrate` 自动检测可自动修复的差异（基于 P0/P1 的 fix）
- 退出标准：v1.0 发布当天 `0.30` 用户能在一小时内升级

### 7.4 退出标准 = v1.0

- 所有阶段 1/2/3 退出标准通过
- 三个连续 minor 版本（v0.40 / v0.41 / v0.42）零破坏性变更
- `vora@1.0` 标签 + 1.0 release notes
- 嵌入 SDK（`vora.hpp` + `vora_lib.lib`）ABI 锁定
- 至少 50 个第三方包在 `vpm` 注册表上

---

## 8. 横切关注点（贯穿所有阶段）

### 8.1 测试策略

当前：328 C++ 单元 + 151 脚本测试 + 57 示例 + 1 fuzz 入口 + 1 bench。改进：
- fuzz：扩到所有公开 API（不只是 parser）
- coverage：目标 `gcov` ≥ 75%（core 模块 ≥ 90%）
- property-based：stdlib 加 `tests/property/` 用随机输入验证不变量

### 8.2 性能

- 每 minor 跑 `tests/bench/`，记录到 `docs/perf-history.md`，回归 > 5% 自动告警
- 不做 JIT（写在 roadmap 长期研究里），但保留字节码 verifier / 优化器扩展点

### 8.3 安全

- 沙箱：脚本调用 native C++ 函数没有边界，危险大；提供 `vora_sandbox.h` API 限制 native 调用范围（阶段 2 末）
- 字节码完整性：`vora_lib` 提供 `loadChunk(unsigned char*, size_t)` 但不做 HMAC，文档明示
- deserialization：json / regex / fs / os 模块加 fuzz 入口

### 8.4 文档

- 文档即契约（原则 6）：每个新语法特性提交必须含至少 1 个反例测试
- 文档变更与代码变更**同一个 PR**
- 每 minor 强制 review 全部 `*.md`（防止漂移）

### 8.5 国际化

- 错误信息支持中英双语（沿用现有 `CHANGELOG` 双语风格）
- 文法/示例保留英文（国际化字符当前 OK，但避免与关键字冲突——`nul` 等本地化拼写不会出问题？目前不展开，留到 i18n 阶段）

---

## 9. 显式不做（继承并扩展 `00-roadmap.md`）

原表保留。新增：

| 特性 | 不做的理由 |
|---|---|
| 静态类型系统 | 定位不变 |
| 方法重载 | 同上 |
| 装饰器 `@` | 同上 |
| Char 独立类型 | 同上 |
| `?` 错误传播 | 同上 |
| Sealed class / Record | 同上 |
| 抽象方法 / 抽象类 | 同上 |
| 海象赋值 `:=` | 同上 |
| **eval / exec / REPL 加载外部脚本** | 阶段 1 重新评估：默认禁用，由 `vora_sandbox.h` 显式开启 |
| **运算符重载** | 表面"看起来有用"，但 Vora 的 Value 是 NaN-boxing，加重载等于重写字节码调度，效益与复杂度不匹配 |
| **多返回值** | 解构 + 数组已足够，且与 Wren/Lua 价值观一致 |
| **switch 关键字** | `match` 已足够；新增 `switch` 是冗余 |
| **协程关键字 `go`** | async/await 已是同一套机制；`go` 会让人误以为是另一种并发 |

---

## 10. 风险与缓解

| 风险 | 等级 | 缓解 |
|---|---|---|
| 阶段 1 语法修复破坏嵌入 SDK ABI | 高 | 任何 byte-level 变更前先 review `vora.hpp` 公开符号；提供 `VORA_ABI_VERSION` 宏 |
| 阶段 1 #1 强制分号得罪"分号可选派" | 中 | 在 release notes 明示原因；保留 `--compat=v0.x` 旧行为 |
| `vpm` 没人用变成空架子 | 中 | 阶段 3 起所有 stdlib 第二版模块**仅**通过 `vpm` 分发（不内置）；用项目自身 dogfood |
| 团队人手不够，4 阶段串行拖 18 个月 | 高 | 阶段 1 和 阶段 3 可并行（不同人）；阶段 4 与阶段 3 重叠 |
| 远端 vs 本地再次分叉（这次就是） | 中 | 引入 rebase 规范：本地分支 `wip/*` 必须每天 rebase 到 main；main 不接受本地长分支 PR |
| 文档/代码再次漂移 | 高 | 原则 6：每个语法 PR 必有反例测试；CI 检查 `USER_GUIDE.md` 的示例代码全部能跑 |
| Fuzz 发现严重崩溃 | 中 | fuzz 输出自动建 GitHub issue；阶段 1 末做完整 crash audit（v0.18 做过一次） |

---

## 11. 成功度量（v1.0 发布日看）

- **质量**：6 平台 × 2 配置 CI 全绿 90 天无 flaky；core 模块 coverage ≥ 90%
- **文档**：v0.30 → v1.0 升级路径文档 + 自动检测工具
- **生态**：`vpm` 注册表 ≥ 50 包、≥ 5 个第三方项目在 GitHub 用 Vora
- **嵌入**：`examples/embed/` 在 3 个实际 C++ 项目中跑通（找一个开源项目嵌入 demo）
- **性能**：基线已记录，minor 内无 > 5% 回归
- **破坏性**：v1.0.0 → v1.1.0 间零破坏性 commit

---

## 12. 现在该做什么（按下回车前的 5 件事）

按依赖顺序：

1. **本地 commit `9698b9b` 推上去**（rebase 后 push `Vora`）—— `git fetch && git rebase origin/main && git push`（如果 rebase 有冲突，先 rebase 再 push）。这是阶段 S 的入口。
2. **修两个 missing include**（`binding_pattern.h:42`、`gc_heap.cpp:123`），让 GCC 编译过 —— 不然谁也没法贡献。
3. **新建 `CHANGELOG.md`** 并回填 v0.20 → v0.27 的实际变化 —— 比改任何语法都紧急。
4. **写一份诚实版的 `docs/00-roadmap.md`** —— 把"列表推导式 ✅"改成"列表/字典推导式 ❌（计划 v0.30）"，同步其他撒谎项。
5. **从阶段 1 #1 开始**（P0 跨行吞并）—— 这是最有杠杆的修复，影响所有用户。

---

> "进化计划"不是承诺时间表的借口，而是"今天该做什么、下周该做什么、下个月该做什么"的诚实回答**。**