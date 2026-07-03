# Vora 路线图

> 最后更新：2026-06-29
> 当前版本：v0.27.0 (Phase 3 全部完成)

---

## 定位

> **Vora is a dynamically typed scripting language. It features JavaScript-like syntax, Lua-level simplicity, and Wren-style object orientation.**

竞争对手：Lua、Wren、AngelScript。不是 Python、Rust、TypeScript 的替代品。

---

## 当前状态

### 语言核心 ✅

```
语法：JS 风格（大括号、分号可选、Obj 关键字）
表达式：完整 Pratt 解析器（?:、?.、??、...spread、lambda、match、列表/Dict 推导式）
语句：if/else、while、do-while、for-in、c-for、try/catch/finally、throw、defer、break/continue
函数：默认参数、rest 参数、命名参数、闭包、生成器 yield、TCO
变量：let / const、解构赋值（数组 + 对象）、类型注解 (:float/:int/:bool/:str)
OOP：Obj 关键字、C3 MRO 多继承、super、this、构造函数、方法
模块：import/export、from...import、模块缓存
```

### 标准库 ✅ — 8 个模块

| 模块 | 功能 |
|------|------|
| `std/math` | abs/min/max/floor/ceil/sqrt/sin/cos/random |
| `std/json` | parse/stringify |
| `std/fs` | readFile/writeFile/exists/listDir/mkdir/delete |
| `std/os` | env/getcwd/exit/shell/args |
| `std/datetime` | now/nowMs/timestampToDate/formatDuration/sleep |
| `std/array` | sort/reverse/join/flatten/unique/chunk/fill/compact |
| `std/string` | repeat/padStart/padEnd/capitalize/titleCase/count/lines |
| `std/regex` | test/find/replace/split（ECMAScript 语法） |

### 基础设施 ✅

```
构建：CMake 跨平台（Windows x64/x86/arm64、Linux x64/x86/aarch64/armhf、macOS Universal）
打包：MSI（Windows）、DEB/RPM/pkg.tar（Linux）、tar.gz（macOS）
嵌入：vora.hpp 单头文件 + vora_lib.lib，零外部依赖
LSP：完整服务器（诊断、补全、跳转、悬停、引用、签名帮助、格式化、文档符号）
VS Code：语法高亮、语言配置、LSP 客户端扩展
测试：71 脚本测试 + 328 C++ 单元测试（1019 断言）+ 56 示例，100% 通过
```

### 稳定性 ✅ — 2026-06-26

```
P3 技术债：20 项已修复（死代码清理、防御性检查、版本号连接、语法高亮改进）
崩溃审计：18 项全部修复
  - assert() → 异常/编译器错误
  - std::get<>() → holds_alternative 守卫
  - catch(...) 全局兜底
  - 栈/数组越界 → RuntimeError
  - 递归深度限制 MAX_DEPTH=10000（AST Printer / Formatter / Compiler）

v0.25 缺陷修复（4 项）：
  - :int/:float 类型注解支持字符串解析
  - defer 在同函数 throw 前执行（RAII）
  - for-in 统一 iter()/next() 协议，支持 generator
  - ?. 可选链对不存在 key 返回 null
```

---

## 路线图

### Phase 3: 2026 Q3-Q4 — OOP 完善 + 性能基础

> 目标：消除 OOP 短板，建立性能基础

```
OOP 完善
├── ✅ 静态方法 / 类方法        ← 工厂方法、工具方法 (v0.25, this.func 语法)
├── ✅ 对象 rest 解构 {x, ...rest}  ← v0.25
└── ✅ 参数解构 func f({x, y})     ← v0.25

性能
├── ✅ NaN-boxing                 ← v0.27, Value 16→8 字节，性能 2-5×
└── ✅ Superinstruction 合并       ← v0.27, OP_GET_LOCAL_PROP / OP_GET_GLOBAL_PROP

嵌入增强
├── ✅ VM public API 扩展          ← v0.27, setGlobal/getGlobal/hasGlobal/registerNativeFunction
├── ✅ 嵌入文档 + 完整示例项目     ← USER_GUIDE.md 第15章 + examples/embed/
└── ✅ SDK 随安装包分发            ← MSI 包含 lib/vora_lib.lib + include/vora.hpp
```

### Phase 4: 2027 Q1-Q2 — 工具链 + 异步

> 目标：建立生产级工具链，解锁网络/并发场景

```
工具链
├── ✅ 调试器 DAP                 ← v0.27, vora-dap.exe + VM debug hooks (Vora-LSP repo)
├── 包管理器 vpm               ← install/publish/search
└── 文档生成器                  ← 源码注释 → API 文档

异步
└── ✅ async/await + 事件循环      ← 基于 generator 扩展 (v0.28)

性能
├── ✅ GC 分代回收                 ← 暂停时间 5-10× 降低 (v0.28)
└── 常量池共享 + 字节码内联
```

### Phase 5: 2027 Q3+ — 生态建设

```
标准库扩展
├── std/http — HTTP 客户端 + 服务器
├── std/net — TCP/UDP socket
├── std/path — 跨平台路径操作
└── std/process — 子进程管理

工程化
├── vora test — 内建测试框架
├── vora bench — 基准测试
└── vora check — 静态分析 + lint

长期研究
├── JIT 编译（Tiered：解释器 → 基线 → 优化）
└── WebAssembly 后端（.va → .wasm）
```

---

## 排除的特性

以下特性**不会**加入 Vora：

| 特性 | 原因 |
|------|------|
| 可选静态类型 | 动态类型是定位，渐进式类型需庞大工具链 |
| 方法重载 | 同名覆盖是动态语言自然行为 |
| 装饰器 `@` | 元编程语法糖，与 Lua-level simplicity 矛盾 |
| Char 独立类型 | 单字符=短字符串，不增加 Value 变体 |
| 错误传播 `?` 运算符 | 需要 Result 类型系统，Vora 有 try/catch |
| Sealed class / Record | Java/C# 专属，与动态语言灵活性冲突 |
| 抽象方法 / 抽象类 | 可通过 `throw` 实现，不需语言层面强制 |
| 海象赋值 `:=` | Vora 有替代方案 |

---

## 设计原则

1. **嵌入优先**：C++ 嵌入 API 是核心差异化武器
2. **实用优先**：标准库 > 语法糖 > 性能优化
3. **身份优先**：每个特性必须服务于"JS 语法 + Lua 简洁 + Wren OOP"
4. **不贪多**：每季度 3-5 个核心特性，做到位
5. **宁可少做，不可做错**：Vora 不是 Rust/Python/Go 的克隆

---

## 竞争定位

| 维度 | Vora vs Lua | Vora 的优势 |
|------|-------------|-------------|
| 语法 | `end/if/then` | JS 风格大括号，零学习成本 |
| OOP | metatable hack | 原生类 + C3 多继承 + super |
| 异常 | pcall（函数式） | try/catch/finally（语法级） |
| 嵌入 | C API | C++ API（vora.hpp 单头文件） |
| 标准库 | 极小 | 8 个模块，覆盖核心场景 |
| 性能 | 寄存器 VM，更快 | NaN-boxing 已实现 (v0.27) |
| 构建 | Makefile | CMake 跨平台 + 原生打包 |

> **一句话**：Vora 的路径不是"成为下一个 X"，而是成为唯一同时满足 **可嵌入 + 现代语法 + 原生 OOP + 零依赖** 的语言。
