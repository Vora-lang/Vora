# Vora 优化路线图

> 最后更新：2026-06-15
> 基于对代码库的全面审计，按收益/成本比排序。

---

## 目录

1. [性能优化](#一性能优化)
2. [语言特性](#二语言特性)
3. [工具链](#三工具链)
4. [质量工程](#四质量工程)
5. [时间线建议](#五时间线建议)

---

## 一、性能优化

### 1.1 NaN-boxing — Value 8 字节终极瘦身 ⭐⭐⭐

| 维度 | 当前 | 目标 |
|------|------|------|
| `sizeof(Value)` | 16 字节（GcString 迁移后） | **8 字节** |
| VM 栈每帧 256 locals | 4 KB | **2 KB** |
| L1 cache line (64B) Value 数 | 4 | **8** |
| 类型检查 | `std::holds_alternative` (跳转表) | 位运算 (2-3 指令) |

**方案**：IEEE 754 NaN-boxing — 所有非 NaN 的 64-bit 模式为 double，NaN 空间编码 null/bool/int/pointer。

**收益**：数值密集场景 2-5× 加速，cache 友好度翻倍。

**风险**：
- ~271 处 variant 访问需替换为位运算
- int64 → int51（溢出自动转 double）
- 依赖 GcObject 8 字节对齐（已满足）

**预估工期**：3-4 周

---

### 1.2 Superinstruction 合并 ⭐⭐

将高频连续 opcode 对合并为单条 superinstruction，减少 dispatch 开销：

| 合并前 | 合并后 | 出现频率 |
|--------|--------|---------|
| `OP_GET_LOCAL` + `OP_CALL` | `OP_CALL_LOCAL` | 极高（方法调用） |
| `OP_GET_GLOBAL` + `OP_CALL` | `OP_CALL_GLOBAL` | 高（函数调用） |
| `OP_CONSTANT` + `OP_GET_LOCAL` | — | 中 |
| `OP_GET_LOCAL` + `OP_GET_PROPERTY` | `OP_GET_LOCAL_PROP` | 高（链式访问） |

**收益**：解释循环 10-20% 加速。

**风险**：opcode 表膨胀；编译器需识别合并模式。

**预估工期**：1 周

---

### 1.3 GC 分代回收 ⭐⭐

| 维度 | 当前 | 目标 |
|------|------|------|
| 算法 | Mark-Sweep (全堆扫描) | Generational (minor GC 只扫新生代) |
| 触发频率 | 每 4MB 分配后全量 | Minor: 频繁轻量 / Major: 低频全量 |
| 写屏障 | 无 | 需 card-marking 或 remembered set |

**收益**：GC 暂停时间降低 5-10×，适合交互式场景。

**风险**：实现复杂度中等；需引入写屏障。

**预估工期**：2-3 周

---

### 1.4 常量池共享 + 字节码内联 ⭐

- 当前每个 Chunk 独立常量池 → 多个 Chunk 可共享字面量
- 小函数自动内联（消除 call/return 开销）

**收益**：内存 -15%，函数调用密集场景 15-25% 加速。

**预估工期**：2 周

---

### 1.5 JIT 编译（远期） ⭐

**方案**：Tiered compilation — 解释器 → 基线 JIT（模板化）→ 优化 JIT（方法级）。

参考：LuaJIT trace-based、Wren 方法级 JIT。

**收益**：数值密集循环 10-50× 加速。

**风险**：工程量大（>3 个月），需处理 deoptimization、GC 交互。

**建议**：NaN-boxing + superinstruction 之后再做，届时基础架构已优化到位。

---

## 二、语言特性

### 2.1 标准库 ⭐⭐⭐

当前 `std/` 目录为空。建议首批实现：

| 模块 | 内容 | 优先级 |
|------|------|--------|
| `std/math` | abs/min/max/floor/ceil/sqrt/sin/cos/random | 高 |
| `std/fs` | readFile/writeFile/exists/listDir | 高 |
| `std/json` | parse/stringify | 中 |
| `std/os` | env/getcwd/exit/shell | 中 |
| `std/regex` | match/replace | 低 |

**实现方式**：NativeFunction 注册，`import "std/math"` 语法糖。

**预估工期**：2-3 周（含 import 语法）

---

### 2.2 模块/导入系统 ⭐⭐⭐

```vora
import "math"         // 导入全部
import { sin, cos } from "math"  // 命名导入
```

- 解析 import → 定位文件 → 编译为独立 Chunk → 作为模块对象暴露
- 循环依赖检测
- 与标准库目录 `std/` 集成

**预估工期**：2 周

---

### 2.3 模式匹配 ⭐⭐

```vora
match value {
    {type: "error", code} => handleError(code)
    0 | null | false       => handleFalsy()
    n if n > 0             => handlePositive(n)
    else                   => handleDefault()
}
```

- 结构化匹配（Dict/Array/Object 解构）
- 守卫条件 (if 子句)
- 穷尽性检查

**预估工期**：2 周

---

### 2.4 迭代器协议 ⭐⭐

```vora
// 自定义可迭代对象
Obj Range(start, end) {
    func iter() {
        // 返回迭代器对象，含 next() 方法
    }
}
for (let x in Range(1, 10)) { print(x) }
```

- 统一 `for-in` 背后的迭代协议
- Array/Dict/String/Object 自动实现
- 惰性求值（map/filter/zip 链）

**预估工期**：1-2 周

---

### 2.5 错误类型层级 ⭐

```vora
Obj TypeError(msg) : Error
Obj RangeError(msg) : Error
try { ... } catch (e if TypeError) { ... }
```

- 内建 Error 类型 + 子类化
- 按类型捕获
- 堆栈追踪附加到 Error 对象

**预估工期**：1 周

---

## 三、工具链

### 3.1 LSP 服务器 ⭐⭐⭐

基于现有 lexer/parser/compiler，实现 Language Server Protocol：

- 诊断（编译错误实时提示）
- 补全（变量/函数/方法/属性）
- 跳转定义 / 查找引用
- 悬停类型提示
- 格式化（已有 formatter）

**预估工期**：3-4 周

---

### 3.2 调试器协议 ⭐⭐

Debug Adapter Protocol (DAP) 实现：

- 断点（行断点、条件断点）
- 单步（step in/out/over）
- 变量查看（stack frame locals/globals）
- 调用栈显示

**VM 侧**：在 `OP_DEBUG_BREAK` 处挂起，等待 DAP 命令。

**预估工期**：3 周

---

### 3.3 包管理器 ⭐

```bash
vora init          # 创建项目骨架
vora install json  # 安装依赖
vora build         # 构建项目
vora run           # 运行主入口
```

- 集中注册表（GitHub-based）
- 语义化版本
- 锁文件

**预估工期**：4-6 周（长期工程）

---

### 3.4 文档生成器 ⭐

从源码注释生成 API 文档。

**预估工期**：1 周

---

## 四、质量工程

### 4.1 性能基准套件 ⭐⭐

```bash
cmake --preset windows-x64-release
./build/windows-x64-release/Release/Vora.exe benchmarks/
```

- 数值循环（fibonacci/matrix/sieve）
- 对象分配压力
- 函数调用深度
- GC 暂停时间
- 与 Lua/Python/JavaScript 对比

**预估工期**：1 周

---

### 4.2 Fuzzer 增强 ⭐⭐

- 当前已有基础 fuzzer（`tests/fuzz/`），但 discoverability 低
- 加入 CI 定时运行
- 增加结构化 fuzzer（语法感知，生成合法 Vora 程序）
- Differential fuzzing（与自身旧版本对比）

**预估工期**：1-2 周

---

### 4.3 测试覆盖率提升 ⭐

- 当前 228 C++ 单元测试 + 23 语言测试 + 29 示例
- 缺少：边界值测试、并发/压力测试、回归套件自动化
- 目标：行覆盖率 >85%

**预估工期**：持续

---

### 4.4 CI 矩阵扩展 ⭐

| 当前 | 建议增加 |
|------|---------|
| Linux x64 (GCC) | macOS ARM64 |
| Linux x64 (Clang sanitizers) | Windows ARM64 |
| Windows x64 (MSVC) | Linux x86 32-bit |
| | MSVC Release 构建 |

**预估工期**：1 周

---

## 五、时间线建议

```
2026 Q3 (7-9月)
├── 2.1 标准库（math/fs/json）
├── 2.2 模块/导入系统
├── 4.1 性能基准套件
├── 4.4 CI 矩阵扩展
└── 1.2 Superinstruction 合并

2026 Q4 (10-12月)
├── 1.1 NaN-boxing
├── 2.4 迭代器协议
├── 2.5 错误类型层级
├── 3.4 文档生成器
└── 4.3 测试覆盖率提升

2027 Q1 (1-3月)
├── 1.3 GC 分代回收
├── 2.3 模式匹配
├── 3.1 LSP 服务器
└── 4.2 Fuzzer 增强

2027 Q2+
├── 1.4 常量池共享 + 内联
├── 3.2 调试器协议
├── 3.3 包管理器
└── 1.5 JIT 编译（研究阶段）
```

---

## 附录：技术债务清单

| 条目 | 优先级 | 说明 |
|------|--------|------|
| `GcPtr` 无 null-check 语义 | 低 | 所有 `GcPtr` 解引用前应 assert non-null |
| `valueToString` 递归深度无限制 | 中 | 深层嵌套 Array/Dict 可能栈溢出 |
| `addValues` 字符串拼接每次分配 | 中 | 可引入 string builder 减少 GC 压力 |
| 无 `const` 语义 | 低 | 变量可变性无编译期检查 |
| Chunk 常量池线形去重 | 低 | O(n²) 查找，大程序编译变慢 |
