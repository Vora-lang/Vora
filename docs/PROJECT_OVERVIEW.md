# Vora 项目全貌概览

> 一份帮助快速掌握 Vora 语言整套代码库结构与运行方式的总览。
> 覆盖范围：`D:\Vora-lang` 下的四个仓库 —— `Vora`（核心）、`Vora-LSP`、`Vora-WASM`、`Vora-lang.github.io`。
> 位置：自 2026-09-10 起纳入版本控制，位于 `Vora` 仓库 `docs/`（原为 `D:\Vora-lang\` 工作区根目录下的未纳管文件）。

---

## 0. 一句话定位

**Vora** 是一门自研的动态类型脚本语言（JS 语法风格、Lua 级简洁、Wren 式面向对象），核心用 **C++17 纯手写实现**（无第三方语言运行时依赖），编译到**自研字节码 + 栈式虚拟机**执行。周边配套四个仓库：核心 CLI/VM、语言服务器（LSP+DAP）、浏览器运行（WASM）、官网文档站。

---

## 1. 顶层目录组织与模块职责

```
D:\Vora-lang\
├── Vora/                  # 【核心】C++17 实现的编译器 + VM + 标准库 + 构建系统
├── Vora-LSP/              # 【编辑器支持】C++ 写的 LSP/DAP 服务 + 极薄 TS 客户端
├── Vora-WASM/             # 【浏览器运行】Emscripten 把核心 VM 编成 WASM + 前端 Playground
└── Vora-lang.github.io/   # 【官网】纯静态 HTML 文档/API 站（GitHub Pages）
```

| 仓库 | 语言 | 规模/产物 | 在体系中的角色 |
|---|---|---|---|
| `Vora` | C++17 | ~29,400 行 C++（src/）+ 9 个 `.va` 标准库 | **唯一事实来源（single source of truth）**，产出 `vora` 可执行文件与静态库 `vora_lib` |
| `Vora-LSP` | C++（服务端）+ TS（客户端） | `vora-lsp.exe` / `vora-dap.exe` + `out/extension.js` | 消费 `vora_lib`，提供编辑器智能提示与调试 |
| `Vora-WASM` | C++（Emscripten 桥接）+ 原生 JS | `vora_wasm.wasm` + `www/` 前端 | 把核心 VM 整体编进浏览器运行 |
| `Vora-lang.github.io` | 手写 HTML/JS | 9 个静态文件 | 对外门户，文档/API 由核心仓库脚本生成 |

**关键关系**：`Vora-LSP` 与 `Vora-WASM` 都不重复实现语言逻辑，而是**直接复用 `Vora` 的头文件与源码**（链接 `vora_lib` 或 `file(GLOB)` 核心 `.cpp`）。

---

## 2. Vora 核心模块（编译器 + 运行时）

### 2.1 目录职责

| 目录 | 职责 |
|---|---|
| `src/` | 全部第一方 C++ 实现（唯一源码目录），9 个子模块 + `main.cpp` + `vora.h` |
| `include/` | 仅 vendored 第三方头：`nlohmann/json.hpp`（仅供 LSP/JSON-RPC） |
| `std/` | Vora 语言自身写的标准库（`.va` 脚本，通过 `import` 加载），9 个模块 |
| `cmake/` | 交叉编译工具链（linux-i386/aarch64/armhf、macos-universal、windows-i386/arm64） |
| `docs/` | 17 篇中文设计文档（项目结构、技术栈、已实现功能、构建指南、技术债等） |
| `examples/` | 55 个 `.va` 特性示例 + C++ 嵌入 demo |
| `tests/` | 多层测试：C++ 单测（doctest）、`.va` 脚本测试、LeetCode 实例回归、基准、fuzz |
| `res/` | Windows 打包资源（WiX MSI 模板、版本/图标、`.cmd` 启动器） |
| `scripts/` | Python 辅助：`amalgamate_headers.py`（生成单头）、`build-api-docs.py`、`build-website-docs.py` 等 |
| `build/`、`out/` | 构建产物（CMake preset 输出 / VS 打开文件夹模式输出） |

### 2.2 `src/` 内部流水线（核心数据流）

```
.va 源码
  └─ Lexer         src/lexer/        逐字符扫描 → Token 流（33 个关键字）
  └─ Parser        src/parser/       递归下降(语句) + Pratt(表达式)，容错解析 → AST
       AST         src/ast/          28 种表达式 + 19 种语句（模板化 Visitor 分派）
       ├─ ASTPrinter       → S 表达式（--ast-printer）
       ├─ SourceFormatter  → 格式化源码（vora fmt）
       ├─ SemanticAnalyzer src/lsp/  → 符号表/诊断（供 LSP，不产码）
       └─ Compiler    src/vm/        AST → 字节码 Chunk（操作码清单见 src/vm/opcode.h）
  └─ VM           src/vm/vm.cpp      栈式执行引擎（3816 行），含异常/生成器/异步/导入
  └─ Runtime      src/runtime/       Value（NaN-boxing 8 字节）、对象模型、内建函数
  └─ GC           src/gc/            分代标记-清除（Minor 512KiB / Major 4MiB + 写屏障）
```

**编译期↔运行期核心契约**：编译器在编译时收集全局名表，交给 VM `initGlobals()` 预分配槽位；VM 执行时用这个表解析全局变量。

### 2.3 语言核心特性

- **类型**：动态类型，9 种可见类型（Null/Boolean/Int64/Float64/String/Array/Dict/Function/Object），v0.23 增 Set/Map，另有 Iterator/Generator/Task。支持可选类型标注 `let a:float = 1`（运行时自动转换）。
- **面向对象**：`class Name(params){...}`，**多继承 + C3 线性化 MRO**，`super`，静态方法。
- **函数式**：一等函数、闭包、Lambda、高阶函数、**尾调用优化**（无限尾递归不爆栈）。
- **语法糖**：`match` 模式匹配（含 or-pattern）、`try/catch/finally`、`defer`、`yield`/`async`/`await`、`const` 不可变、`??` 空值合并、`?.` 可选链、`in` 成员测试、位运算 `& | ^ ~ << >>`、解构（含顶层 rest）、列表/字典推导、字符串插值、C 风格 `for`/括号 `for-in`/`do-while`。
- **模块**：`import/export`，按绝对路径缓存 + 循环导入检测。

### 2.4 构建、运行与环境

- **构建系统**：CMake ≥ 3.16 + `CMakePresets.json`（20 个 configure preset，命名 `<平台>-<架构>-<debug|release>`）+ CPack 打包。
- **产物目标**：`vora_lib`（静态库，被外部复用）、`Vora`（可执行）、`Vora_tests`、`Vora_fuzzer`、MSI、单头 `vora.hpp`。
- **平台矩阵**：Windows(x64/x86/arm64, MSVC→.msi/.zip)、Linux(x64/i386/aarch64/armhf, GCC→.deb/.rpm/.tar)、macOS(Universal, Clang)。
- **一键构建**：`build.sh` / `build.ps1`（Windows 无参数进入交互式菜单）。
- **运行方式**：
  - `vora script.va` 运行脚本
  - `vora`（或 `--repl`）进入交互式 REPL（单驻留 VM，跨行保留全局）
  - `vora fmt [-w] file.va` 格式化
  - 调试选项：`--tokens` / `--ast-printer` 查看中间产物
- **关键环境变量**：
  - `VORA_STD_PATH`：覆盖标准库目录（查找优先级：此变量 → exe 同级 `../std` → 编译期默认）
  - 任意环境变量可通过 `os.getenv()` 在脚本中读取
- **关键编译宏**：`VORA_VERSION`（取自 `project(... VERSION)`）、`VORA_DEFAULT_STD_DIR`、`VORA_GC_TRACE`（GC 追踪日志）、`DOCTEST_CONFIG_IMPLEMENT`。
- **版本号单点修改**：只改 `CMakeLists.txt` 顶部的 `project(Vora VERSION X.Y.Z)`，自动同步到 `--version`、MSI 文件名、`.exe` 版本资源。

### 2.5 入口串联（src/main.cpp）

`main()` 启动 → UTF-8 控制台初始化 → 解析命令行 → 三条路径分派：
1. **runScript()**：读文件 → `Lexer` → `Parser` → `Compiler`(→Chunk) → `VM::interpret()`，中间用 `StderrErrorReporter` 统一报错。
2. **runFmt()**：Lexer+Parser+`SourceFormatter`。
3. **runREPL()**：单个长驻 VM，`seedGlobals` 保证跨行全局一致，Ctrl+C 通过 `VM::requestInterrupt()` 在指令边界干净中断。

> 📌 说明：本文初版曾指出 README/CLAUDE.md 等多处偏离 v0.27 代码（Value 被称作 `variant`、58 条操作码、23 关键字、~18k 行）。**已于 2026-08-13 全面修订相关文档**（Vora 的 `CLAUDE.md`/`README.md`/`docs/01`/`docs/02`/`docs/07`/`docs/04`/`mainpage`/`docs/14`/`docs/10`、Vora-LSP 的 `README.md`/`CLAUDE.md`、Vora-lang.github.io 的 `CLAUDE.md`），使其与代码一致：Value 为 8 字节 NaN-boxing、85 条操作码、33 关键字、~29.4k 行、分代标记-清除 GC、LSP 能力已实现。**权威阅读顺序**：`CLAUDE.md` → `USER_GUIDE.md` → `docs/02-项目结构.md` → `src/main.cpp` → `src/vm/vm.h`、`src/vm/compiler.h` 的 Doxygen 头注释。

---

## 3. Vora-LSP 模块（编辑器智能 + 调试）

### 3.1 关键更正

**LSP 服务端是 C++ 写的，不是 TypeScript**。`server/` 下是真正的语言服务器（`lsp_server.cpp` 79KB + `dap_server.cpp`），通过**链接 `vora_lib` 直接复用 Vora 的 Lexer/Parser/Formatter/SemanticAnalyzer/VM**；`src/extension.ts` 仅是极薄的 VS Code 客户端（用 `vscode-languageclient` 拉起二进制）。

### 3.2 目录职责

| 路径 | 作用 |
|---|---|
| `server/` | C++ LSP/DAP 服务：`lsp_server.*`、`dap_server.*`、`main.cpp`、`dap_main.cpp` |
| `src/` | 唯一 TS 文件 `extension.ts`（VS Code 客户端），`tsc` 产出 `out/extension.js` |
| `syntaxes/` | `vora.tmLanguage.json`（TextMate 语法，供 VS Code 高亮） |
| `zed/` | Zed 编辑器扩展 + 独立的 tree-sitter-vora 语法（仅 Zed 用） |
| `build/`、`out/` | CMake 产物 / tsc 产物 |

### 3.3 已落地的能力

- **LSP**：实时诊断、格式化、语义感知补全、跳转定义、悬停、文档符号、引用查找、签名帮助（均由 `SemanticAnalyzer` 支撑，README/CLAUDE.md 表格已过时）。
- **DAP 调试**：`vora-dap.exe` 内嵌 Vora VM，支持 launch/断点/单步/变量查看/求值。

### 3.4 数据流（文档变更 → 诊断）

```
编辑器 didOpen/didChange → LspServer
  → Lexer(src) + Parser → DiagnosticCollector(实现 ErrorReporter)
  → SemanticAnalyzer（未用变量/不可达/遮蔽警告）
  → 1-based 行列转 0-based LSP Diagnostic → publishDiagnostics 推回
```
**交互方式**：直接内嵌 `vora_lib`，**不是子进程调用 `vora` 可执行文件**。

### 3.5 构建与配置

- **C++ 端**：先构建 Vora 产出 `vora_lib.lib`，再 `cmake -B build && cmake --build build` 产出 `vora-lsp.exe`/`vora-dap.exe`，POST_BUILD 拷贝到仓库根。
- **TS 端**：`npm install` + `npm run compile`（`tsc -p ./`）。
- **关键配置**：`language-configuration.json`（编辑体验）、`syntaxes/vora.tmLanguage.json`、`package.json`（`contributes.languages/grammars/configuration/debuggers`，`vora.lsp.serverPath` 默认 `"vora-lsp"`）、`tsconfig.json`（ES2020/commonjs/strict）。

---

## 4. Vora-WASM 模块（浏览器运行）

### 4.1 定位

把 **Vora 工具链本身（lexer→parser→compiler→VM）整体编成 WASM**，浏览器用 JS 把 `.va` 源码字符串喂给 WASM 解释执行（不是把"用户程序"编成 WASM）。工具链：**Emscripten**（emcc）。

### 4.2 目录职责

| 路径 | 作用 |
|---|---|
| `src/vora_wasm.cpp` | 唯一桥接层，导出 `vora_run(const char*)`：重定向 stdout → JSON 返回 |
| `www/` | 浏览器 Playground：原生 JS（`vora.js` API、`vora-highlight.js` 高亮），GSAP 动画 |
| `CMakeLists.txt` | Emscripten 构建配置 |
| `.github/workflows/build-deploy.yml` | 自动构建并部署 GitHub Pages |

### 4.3 数据流（浏览器内执行）

```
用户键入源码 → index.html runCode()
  → VoraWasm.run(src) [cwrap 调 vora_run]
  → vora_wasm.cpp: Lexer→Parser→Compiler→VM::interpret（CaptureGuard 捕获 stdout）
  → 返回 JSON {ok, output/error}
  → JS 解析；出错时 VoraHighlight.markErrors() 在 gutter 标红错误行
```

### 4.4 构建方式

`CMakeLists.txt` **用 `file(GLOB)` 直接收集 `../Vora/src` 的 `lexer/ast/parser/vm/runtime/common/gc` 的 `.cpp`**（排除 `main.cpp`、`lsp/`、`json_rpc/`、`formatter/`），与桥接文件一起编进 `add_executable(vora_wasm)`。通过 `-DVORA_SRC_DIR=...` 可覆盖源码位置。

**关键链接选项**：`EXPORTED_FUNCTIONS=[_vora_run,_malloc,_free]`、`EXPORTED_RUNTIME_METHODS=[ccall,cwrap]`、`MODULARIZE=1`（产出工厂函数 `Module`）、`ALLOW_MEMORY_GROWTH=1`、`INVOKE_RUN=0`（不自动跑 main）。

**本地构建运行**：
```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp build/vora_wasm.js build/vora_wasm.wasm www/
python -m http.server 8000 -d www   # 打开 http://localhost:8000
```
（MVP 限制：不支持 import/stdlib、REPL、Vora↔JS 双向调用；`vora_os_shell` 被覆盖为返回 null。）

---

## 5. Vora-lang.github.io 模块（官网）

### 5.1 定位

**纯手写静态 HTML**，无站点生成器（无 Jekyll/Hugo/Docusaurus/MkDocs）。全站 9 个文件，CSS/JS 全内联；仅首页引用 CDN 的 **GSAP 3.12.5 + ScrollTrigger** 做动画。

### 5.2 目录职责

| 文件 | 作用 |
|---|---|
| `index.html` | 落地首页（语言定位、快速开始、语法卡、特性、项目结构树、i18n 三语） |
| `docs/index.html` | 文档枢纽卡片导航 |
| `docs/language.html` | 语言参考（由 `Vora/USER_GUIDE.md` 生成） |
| `docs/user-guide.html` | 语言差异指南（由 `Vora/docs/15-写给用户.md` 生成） |
| `docs/getting-started.html` / `docs/examples.html` | 手写快速上手 / 示例 |
| `api/index.html` | C++ API 参考（91KB，由 `build-api-docs.py` 解析头文件 Doxygen 注释生成） |

### 5.3 文档生成（在核心仓库执行）

```
Vora/USER_GUIDE.md          → scripts/build-website-docs.py → docs/language.html
Vora/docs/15-写给用户.md    → scripts/build-website-docs.py → docs/user-guide.html
Vora 头文件(Doxygen注释)    → scripts/build-api-docs.py     → api/index.html
```

### 5.4 预览

纯静态，**无需构建**：`cd Vora-lang.github.io && python -m http.server 8000` → http://localhost:8000。文档/API 内容更新需在核心仓库跑上述 Python 脚本后同步。

---

## 6. 整体串联图（端到端）

```
                    ┌─────────────────────────────────────────────┐
                    │  Vora (核心 C++17, src/ → vora_lib)          │
                    │  lexer → parser → ast → compiler → vm → gc   │
                    └───┬───────────────┬───────────────┬──────────┘
                        │ 链接 vora_lib  │ GLOB 核心.cpp │ 脚本生成文档
                        ▼               ▼               ▼
                 ┌────────────┐  ┌─────────────┐  ┌──────────────────┐
                 │ Vora-LSP  │  │ Vora-WASM   │  │ Vora-lang.github  │
                 │ vora-lsp  │  │ vora_wasm   │  │ io (静态站)        │
                 │ vora-dap  │  │ .wasm+www   │  │ docs/ + api/      │
                 └─────┬──────┘  └──────┬──────┘  └──────────────────┘
                       │ stdio LSP/DAP   │ 浏览器 JS
                       ▼                 ▼
                  VS Code / Zed     浏览器 Playground
```

---

## 7. 快速上手清单

| 你想… | 去哪 / 怎么跑 |
|---|---|
| 编译并运行 Vora 脚本 | `cd Vora && ./build.sh`(或直接 cmake) → `./build/.../Vora.exe script.va` |
| 交互式 REPL | `Vora.exe` |
| 给编辑器装智能提示/调试 | 构建 `Vora-LSP`（产出 `vora-lsp.exe`/`vora-dap.exe`），VS Code 加载 `out/extension.js` |
| 浏览器里跑 Vora | 构建 `Vora-WASM` → 拷贝到 `www/` → `python -m http.server` |
| 看/改官网文档 | 改 `Vora` 的 `USER_GUIDE.md` 等 → 跑 `scripts/build-website-docs.py` → 同步到 `Vora-lang.github.io` |
| 理解语言语义 | 读 `Vora/USER_GUIDE.md`（单一真相源） |
| 理解架构决策 | 读 `Vora/CLAUDE.md` + `Vora/docs/02-项目结构.md` |

---

## 8. 阅读时务必注意的偏差

1. **文档滞后问题已修复**（2026-08-13）：原 README/部分 docs 称 Value 为 `std::variant`、58 操作码、23 关键字、~18k 行，现已统一更正为 v0.27 实际状态 —— **NaN-boxing、85 操作码、33 关键字、~29.4k 行、分代标记-清除 GC**。详见第 1–6 节正文。
   **（2026-09-07 追注）**：Phase 1 语法修复（P0×4 + P1 位运算/一元加/in/括号 for-in/顶层 rest/或模式）合入后，操作码数已增至 85+（以 `src/vm/opcode.h` 为准），文档中的脆数字已改为"以源码为准"；`Vora/docs/15-写给用户.md` 的"没有位运算"章节已重写。逐项事实以 `Vora/CHANGELOG.md [Unreleased]` 为准。
2. **Vora-LSP 是 C++ 服务**，不是 TS 实现（"323 个 .ts"是核心仓库的统计误植）。
3. **官网 CLAUDE.md 的 `api/` 与设计规范偏差已修正**（2026-08-13）：已补记 `api/index.html` 为第三套生成产物（来自 `build-api-docs.py`），并在设计系统中显式标注 docs/api 使用紫色强调色、与首页单色规范存在已知偏差（待统一）。
4. **跨仓库耦合**：`Vora-LSP`/`Vora-WASM` 依赖 `Vora` 的头文件与源码路径（默认 `../Vora/src`），移动核心仓库位置需同步调整 CMake 变量。
