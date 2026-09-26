# GC 安全性设计 —— 标记阶段与安全点

> 状态：**设计阶段，未实现**
> 目标项：本文开头所述 GC 缺陷（`CHANGELOG.md [Unreleased] → Known limitation`、`docs/00-roadmap.md`「已知限制 → 仍开放」）
> 严重性：**本仓库目前唯一确定性崩溃的缺陷**，也是剩余风险最高的区域，故先设计后实现
> 基线：`main@56ffa0e`。单测 488 用例 / 30686 断言，其中
> `bigint_survives_gc_while_referenced_from_a_constant_pool` 在**本机**失败 —— 那正是本文要修的
> 缺陷之一（见 §1.3、§4.2）。脚本 97、示例 58、fuzz 8。
>
> **证据约定**（上一份大整数设计文档把推测写成事实，被实现者抓出三处错误，本版不再重犯）：
>
> - 【代码】= 静态阅读源码确认，附 `文件:行号`
> - 【实测】= 有可执行验证，附命令与输出
> - 【推测】= **尚未证实**，必须显式标注，不得当作结论使用
>
> 凡本文未标注证据等级的判断，均视为【推测】。

---

## 0. 结论摘要

**一句话：GC 的标记阶段只标记了「直接根」，从未标记任何经 `trace()` 到达的对象 —— 于是大量
仍然可达的对象在 sweep 时被释放，程序随后读到的就是已释放内存。**

这是**两个彼此独立**的缺陷，症状与修法都不同，必须分开处理：

| 编号 | 缺陷 | 证据 | 后果 |
|---|---|---|---|
| **D1** | 标记阶段不标记传递闭包：`collectMinor`/`collectMajor` 的闭包循环只调 `obj->trace()`，从不 `setMarked()`；`GcHeap::mark()`（正是为此而写）是**死代码** | 【代码】+【实测】 | 可达对象被释放 → 用后释放：垃圾值、帧信息错乱、`bad_alloc`、segfault；**若对象图有环，遍历本身不终止 → 确定性挂死** |
| **D2** | 安全点只有调用类 opcode（4 处），没有循环回边 | 【代码】+【实测】 | 只分配、不调用任何函数的循环**永不回收** → 内存无界增长（bigint 用例约 8 GB → `bad_alloc`） |

两者叠加的后果是：**内存阈值附近的行为既不确定又不可预测** —— 有的程序挂死，有的段错误，
有的报错，有的"看起来正常"（因为释放后的内存往往还留着原值）。这解释了为什么该缺陷长期
没被测试抓住，也解释了为什么它同时是三个此前独立失败项的共同底层原因（§1.5）。

**修复要点**（细节见 §3）：先修 D1（标记），它消除绝大多数不确定性；再补 D2（安全点），
它让"长时间分配而不调用"不再无界；最后按 §3.3 的审计表补齐两处 rooting 缺口。

---

## 1. 复现与特征化

### 1.1 最小复现

**R1 —— 确定性挂死（10 行，推荐作为主验收用例）**【实测】

```vora
func tick() { return 1 }
let a = [1]
let b = [a]
a.add(b)                       // a -> b -> a，从根可达的环
let i = 0
while (i < 30000) { let junk = [i] i = i + tick() }
print("cycle survived:", len(a))
```

实测：`timeout 30 vora R1.va` → **exit 124（超时被杀），3/3 全部挂死**。

为什么它挂死：`a.add(b)` 是调用 → 触发一次 GC → 标记阶段从根 `a` 出发，`trace()` 把 `b` 压入
worklist、`b` 的 `trace()` 又把 `a` 压回，而**两者都没被 `setMarked(true)`**，于是
`while (!worklist.empty())` 永不结束（见 §2.1）。这是**确定性**的，不依赖内存布局、不依赖
释放块是否被复用 —— 因此它是可固化进测试套件的那一个（§4.1）。

**R2 —— 段错误（原始报告）**【实测】

```vora
func build(n) {
    let arr = []
    let i = 0
    while (i < n) { arr.add(1); i += 1 }
    return len(arr)
}
print(build(20000))
```

实测（6 次）：`139 139 139 0 139 139` —— **5/6 段错误**。同一程序在不同时刻反复运行会翻转
（另一次实测为 3/4），因此**它不是"必然会崩"，而是"高概率会崩"**。

**R3 —— 无段错误、纯"内存无界"复现（D2 专属）**【实测】

```vora
func tick() { return 1 }
func f(n) { let s = "" let i = 0 while (i < n) { s = s + "x" i = i + tick() } return len(s) }
print(f(20000))
```

带 `tick()` 时有安全点，会进入 GC；不带安全点时（bigint 用例的形状）则完全不回收。
D2 的最纯粹复现是**不带任何调用**的分配循环：分配量随 n 二次增长而回收为 0，直到
`bad_alloc`。bigint 用例即此形状（§4.2）。

### 1.2 阈值：与 512 KiB minor 阈值的关系

阈值常量【代码】：`nextGCThreshold_ = 4 MiB`（major）、`minorGCThreshold_ = 512 KiB`
（`src/gc/gc_heap.h:226-227`），且**minor 阈值会在"本轮无晋升"时 ×1.5 自适应放大**
（`gc_heap.cpp:129-132`，上限 4 MiB）。`allocate()` **只置标志位**
（`pendingMinorGC_` / `pendingMajorGC_`，`gc_heap.h:118-124`），真正的回收发生在
`collectIfNeeded(roots)`。

**实测：这**不是**一个随 n 增长的阈值，而是"只要触发过一次回收就可能崩"。** 用一个**完全不增长**
容器的循环（每轮新建小数组、调用一次方法、随即丢弃）测量【实测】：

```vora
func f(n) { let i = 0 while (i < n) { let b = [1, 2, 3] b.add(4) i += 1 } return i }
```

| n | 4 次运行的退出码 |
|---|---|
| 4000 | `0 139 139 0` |
| 6000 | `139 0 0 0` |
| 8000 | `0 139 139 0` |
| 10000 | `139 139 0 0` |
| 12000 | `139 139 139 0` |
| 15000 | `139 139 0 0` |
| 20000 | `0 0 0 0` |

即：**崩溃从 n≈4000 起就概率性出现，且不随 n 单调加剧**；n=20000 那次反而 4/4 通过，
而同一形状的 `arr.add(1)` 循环在同一次会话里 3/3 失败。结论是**触发条件是"是否发生过一次
回收"，而不是"分配了多少"**：一次回收就足以释放一批仍然可达的对象，之后是否可见取决于
释放块在被解引用前有没有被复用。

> 因此**不要**把"某个 n 以上才崩"写进文档当作阈值 —— 那是概率的表象。设计上应当假定：
> 只要发生一次 minor GC，任何经 `trace()` 到达的对象都可能已被释放。

### 1.3 确定性程度

| 复现 | 结果 | 性质 |
|---|---|---|
| R1 环 + 一次回收 | 挂死 3/3 | **确定性**（可用作门禁用例） |
| R2 build(20000) | 段错误 5/6（另一次 3/4） | 高概率，随运行翻转 |
| 每轮新建数组 + `add`（n≥4000） | 0～3 次崩 / 4 次 | 概率，不随 n 单调 |
| E：`a = a + [1]` + `tick()`（函数内） | `139 / 1(报错) / 139` | 概率，两种表现混杂 |
| 顶层 `arr.add(1)` 循环 n=20000 | **0/6 全部正常** | 现象见 §1.4 |
| 顶层同形状但循环**无调用** | 正常 | 因为**根本没发生过回收** |
| bigint 用例（无调用的 8 GB 分配循环） | `bad_alloc`（本机空闲 7.1 GB） | **环境依赖**：内存够就过 |

「挂死」是唯一确定性的表现。这也是 §4 把 R1 作为主验收用例的原因。

### 1.4 顶层 vs 函数内 —— 实测边界与**未解释**部分

原报告称"顶层写同样的循环则正常"，这与实测一致【实测】：

```
顶层   let arr = [] ; while (i < 20000) { arr.add(1); i += 1 }   → 6/6 正常
函数内 func build(n) { 同上 }  build(20000)                      → 5/6 段错误
```

但**"顶层正常"不是一条普遍规律**，它的边界比原报告窄：

- 把**环**放在顶层同样挂死：`a.add(b)` 之后的任何一次回收都会挂（§2.1），**3/3 实测**。
- 顶层的**带调用**循环（`a = a + [1]` 且每轮调用 `tick()`）同样失败。
- 顶层**无调用**的循环之所以正常，最可能只是因为**它从未触发过回收**（无安全点，§2.2）。

所以正确的表述是：**该缺陷与作用域无关；"顶层正常"只是"顶层那个写法恰好没有触发回收"的巧合。**
至于"同为带调用的 `arr.add(1)` 循环，函数内崩、顶层不崩"这一具体差异，**本文未能给出代码级
解释**，候选假设见 §2.6，明确标注为【推测】。

### 1.5 所有已知表现清单

| 表现 | 证据 | 归类 |
|---|---|---|
| `segfault`（exit 139） | 【实测】§1.1 R2 | D1 用后释放 |
| 确定性**挂死**（R1 的环） | 【实测】§1.1 | D1 遍历不终止 |
| `std::bad_alloc`（bigint 用例、bench/03） | 【实测】 | D1 与 D2 都可致；见 §4.2 |
| 帧号错乱：20 行文件里报 `[368:368]` | 【实测】上一轮会话 | D1 释放了 FunctionPrototype |
| **负数**列号：`[594:-229376] in tick` | 【实测】本次（E 用例） | D1 同上 |
| 不可能的列号：`Invalid property name in constant pool` 指向第 714 列 | 【实测】上一轮会话 | D1 同上 |
| "看起来正常"（读了已释放但未被复用的内存） | 【实测】§1.3 | D1 的**静默**表现，最危险 |
| 内存无界增长 → `bad_alloc` | 【实测】§1.1 R3 | **D2** |

关于帧号错乱，`E` 用例给出了明确的因果链：栈回溯里 `tick` 那一帧显示
`[594:-229376]`（3 行文件里的第 594 行、负列号）。帧保存的是 `GcPtr<VoraFunction>`，
而 `VoraFunction::trace()` 会 trace 其 `prototype_`（`vora_function.cpp:35-41`）；由于闭包
循环从不标记它【代码】§2.1，`FunctionPrototype` 在 sweep 时被释放，帧随后从**已释放的
prototype** 里读函数名/行号/列号 → 得到的就是这种垃圾【实测 + 代码推理，因果链完整】。

---

## 2. 机制分析

> 本节按"先查清、再结论"组织。每条都标注证据等级。

### 2.1 已证实（核心）：标记阶段不标记传递闭包

`GcHeap::collectMinor`（`src/gc/gc_heap.cpp:53`）的标记阶段【代码】：

```cpp
    // Mark from roots
    for (GcObject* root : roots) {
        if (root && !root->isMarked()) { root->setMarked(true); worklist.push_back(root); }
    }
    // Mark from remembered set (old objects that may reference young)
    for (GcObject* oldObj : rememberedSet_) {
        if (oldObj && !oldObj->isMarked()) { oldObj->setMarked(true); worklist.push_back(oldObj); }
    }
    // Transitive closure: trace marked objects.
    while (!worklist.empty()) {                      // gc_heap.cpp:92-98
        GcObject* obj = worklist.back();
        worklist.pop_back();
        // trace() pushes children unconditionally — we filter in the next
        // iteration via isMarked() guard (objects already marked are skipped)
        obj->trace(worklist);                        // ← 只 trace，从不 setMarked
    }
```

注释声称"在下一轮用 `isMarked()` 守卫过滤"，但**循环体里没有任何 `isMarked()` 判断，也没有
任何 `setMarked()`**。`collectMajor`（`gc_heap.cpp:147`）在 `:173-177` 是**同样**的循环。

而正好为此而写的辅助函数**是死代码**：

```cpp
void GcHeap::mark(GcObject* obj, std::vector<GcObject*>& worklist, bool isMinor) {  // gc_heap.cpp:39
    if (!obj || obj->isMarked()) return;
    if (isMinor && obj->isOld()) return;      // minor 时跳过老对象（正确）
    obj->setMarked(true);
    worklist.push_back(obj);
}
```

`grep -rn "\bmark(" src/` 只命中它的**声明**（`gc_heap.h:210`）和**定义**（`gc_heap.cpp:39`）
——**没有任何调用点**【代码】。

**后果一（用后释放）**：mark 位只对「直接根」与「remembered set 成员」置位；凡经 `trace()`
到达的对象一律保持未标记。sweep 阶段 `sweepMinor()`（`gc_heap.cpp:217-249`）对
「年轻且未标记」直接 `delete curr`（`:238-243`），对老对象一律保留（`:230-237`）。于是：
**活着的父对象（根）保住了，其子对象（数组元素、字典值、原型、嵌套容器、被持有的字符串）
被释放，父对象里仍指向它们的 Value 变成悬垂指针**【代码，因果明确】。

**后果二（遍历不终止）**：因为子节点从不被标记，`trace()` 会反复把同一批节点压回 worklist。
- 有环 → worklist 永不空 → **无限循环**（R1 实测挂死）；
- 无环但是共享结构（DAG）→ 同一节点被反复展开，worklist 规模随重复引用指数增长 →
  表现为内存暴涨 / `bad_alloc`【推测：DAG 的指数增长未单独实测，环的无限循环已实测】。

### 2.2 已证实：安全点只有调用类 opcode

`needsGC()` 全仓库只有 **4** 处【代码】：

| 位置 | 触发它的 opcode |
|---|---|
| `vm.cpp:1115`（`executeCallKw`） | `OP_CALL_KW` / `OP_CALL_KW_WIDE` |
| `vm.cpp:2153` | `OP_CALL` |
| `vm.cpp:2189` | `OP_CALL_N` |
| `vm.cpp:2249` | `OP_TAIL_CALL` |

**四处全是调用类**。循环回边（`OP_LOOP`）、分配类 opcode（`OP_ARRAY`、`OP_DICT`、字符串拼接的
`OP_ADD`）**都没有检查点**。因此一个只分配、不调用任何函数的循环永远不会回收【代码】，
实测后果：bigint 用例的脚本在无安全点的循环里累计约 8 GB 分配，本机空闲 7.1 GB → `bad_alloc`
【实测】。这是 **D2**，与 D1 相互独立 —— 即使 D1 完全修好，这个循环依然不会回收。

### 2.3 已证实：sweep 语义（决定"什么被释放"）

- `sweepMinor()`（`gc_heap.cpp:217-249`）：**年轻且未标记 → 释放**；已标记 → 保留并清标记；
  老且未标记 → 保留（注释说明老对象在 minor 中不被 trace、故必然未标记，属正常）。
- `sweepMajor()`（`gc_heap.cpp:263`）：未标记即释放（`delete curr`，`:276-278`），不区分年龄。
- minor 结束后清 `youngBytesSinceLastMinorGC_`、按需 ×1.5 放大 minor 阈值
  （`gc_heap.cpp:117-132`）；major 走 `clearRememberedSet()`（`gc_heap.h:180-185`）。

### 2.4 已证实：根集合清单与两处 rooting 缺口

`VM::collectGarbage()`（`src/vm/vm.cpp:1301` 起；声明在 `vm.h:522`，**private**）收集的根：

| # | 根 | 位置 | 审计结论 |
|---|---|---|---|
| 1 | 值栈 `stack[0, stackTopIndex)` | `vm.cpp:1304` 起 | 完整 |
| 2 | 全局表 `globalValues` | `:1309` 起 | 完整 |
| 3 | **打开**的 upvalue（`openUpvalues`） | `:1314` 起 | 完整（但仅"打开"的那些，见缺口 A） |
| 4 | 调用帧：`frame.function`、`frame.generator`、`frame.deferStack` | `:1319` 起 | 完整 |
| 5 | `pendingGenerator` / `currentGenerator` | `:1332` 起 | 完整 |
| 6 | 模块缓存 `moduleCache` | `:1340` 起 | 完整 |
| 7 | `nativeErrorValue` | `:1345` 起 | 完整 |
| 8 | 当前/待执行 chunk 的常量池 | `:1350` 起 | 完整（顶层脚本 chunk 无 GcObject 拥有，故显式 root） |

被 root 的对象，其 `trace()` 实现本身**基本完整**【代码】：`Array`/`Dict`/`Set`/`Map`/
`ObjectInstance`/`ClassDefinition`/`Iterator`/`Generator`/`Task`
（`src/runtime/value.cpp:315-378`）、`FunctionPrototype`（含常量池，`src/vm/compiler.h:76-78`）、
`BoundMethod`（`bound_method.cpp:23-26`）都正确 push 了各自持有的引用。

**但有两处缺口**，且都是"对象持有的引用不在 trace 的覆盖范围内"：

**缺口 A：`VoraFunction::trace()` 不 trace `upvalues`**【代码】

```cpp
void VoraFunction::trace(std::vector<GcObject*>& wl) {          // vora_function.cpp:35-41
    if (prototype_) wl.push_back(const_cast<FunctionPrototype*>(prototype_));
    // ← upvalues 未 trace
}
```

而 `VoraFunction` 有 `std::vector<std::shared_ptr<Upvalue>> upvalues;`
（`vora_function.h:96`），`Upvalue` 持有 `Value closed`（`value.h:1021-1032`）。Upvalue 本身
由 `shared_ptr` 拥有、不是 GcObject，因此**它持有的那个 Value 必须在别处被 root** —— 目前
**只有在 upvalue 仍"打开"时才被 root**（根 #3）。闭包返回、upvalue 被 close 之后，
`closed` 里的堆对象**没有任何根**，于是被 sweep 释放。**修法**：`trace()` 里对每个 upvalue
`pushGcRefs(uv->get(), wl)`（打开的会重复一个栈根，无害）。

**缺口 B：native function 的 lambda 捕获对 GC 不可见**【代码】

`NativeFunction::trace()` 是**刻意的空实现**：

```cpp
void NativeFunction::trace(std::vector<GcObject*>& /*wl*/) {
    // NativeFunction no longer owns any GcObjects.
}                                                   // native_function.cpp:27-29
```

但数组/字典/集合/映射方法工厂**把接收者捕获进了 lambda**，例如：

```cpp
GcPtr<NativeFunction> getArrayMethod(const std::string& name, GcPtr<Array> arr) {
    if (name == "add") {
        return GcHeap::instance().alloc<NativeFunction>("add", 1,
            [arr](const std::vector<Value>& args) -> Value {      // ← arr 被捕获
                arr->elements.push_back(args[0]);
                writeBarrier(arr.get(), args[0]);
                return nullptr;
            });
    }                                                             // builtins.cpp:617-628
```

`std::function` 的捕获变量位于 NativeFunction 对象内部，`trace()` 无法枚举 —— 这**违反
`src/gc/gc_ptr.h` 文件头注释明文写下的不变式**（"GC 只需扫描存在于 VM 栈、常量池或可达堆对象
中的 GcPtr；堆对象在内容被修改时自行实现写屏障"）。此处的 `arr` 三者都不是。
**但必须诚实说明**：数组同时通常还是栈/全局局部变量（本身是根），因此缺口 B 我**没有实测出
一次真实故障**，它的危险性是**潜在的**（一旦某个 native 持有的是"只被它自己引用"的对象就
会立刻失效）【代码 + 未证实后果】。修法见 §3.3。

### 2.5 与分代 GC 的交互（已证实部分 / 推测部分）

**已证实**【代码】：

- 写屏障 `writeBarrier(src, dst)`（`gc_heap.cpp:27-33`）规则：`src` 必须已老
  （`isOld()`）且 `dst` 是年轻（`age_ == 0`）才把 `src` 加入 remembered set。
- 调用点：数组/字典/映射的索引赋值（`vm.cpp:3227/3236/3258`）、属性赋值（`vm.cpp:3627`）、
  数组 `add`/`insert`（`builtins.cpp:627/649`）、`Set.add`（`:766`）、`Map.set`（`:816-817`）。
- `PROMOTION_THRESHOLD = 3`，`promote()` 每轮存活 age+1（`gc_object.h:33/123/126`）。
  minor 的晋升阶段只对**已标记**对象调用 `promote()`（`gc_heap.cpp:100-115`）。
- 容器"增长"（`std::vector<Value>::push_back` 触发 realloc）**本身不会使元素失效**：
  Value 是逐字节拷贝到新缓冲区，被指向的堆对象不受影响；需要屏障的是"老容器新增年轻引用"，
  这一条在各 `add`/`set` 路径上**有**调用【代码】。

**注意一个被 D1 掩盖的交互**：因为晋升只发生在已标记对象上，而经 `trace()` 到达的对象从未被
标记，**它们永远不会晋升为老对象**，于是每个 minor GC 都反复把它们当成"年轻且未标记"释放
【代码】。这意味着 D1 修好之后，晋升行为会**首次真正生效**，堆的驻留量会上升 —— 见 §5 步骤 1
的风险说明。

**推测（未证实）**：remembered set 的剪枝（`gc_heap.cpp:122-126` 只保留 `isOld()` 的条目）
与 ×1.5 阈值自适应在修复后是否需要重新调参，需要靠 benches 观察，本文不给结论。

### 2.6 仍未解释：顶层 vs 函数内（候选假设，均为【推测】）

现象见 §1.4：同为带调用的 `arr.add(1)` 循环，函数内 5/6 崩、顶层 0/6 崩。**本文未能给出
代码级解释**，以下是候选，**不得当作结论**：

1. 【推测】顶层 `let` 在 `scopeDepth == 0` 时被编译为**全局**（`compiler_stmt.cpp:40/124/305`
   等处按 `scopeDepth == 0` 分支处理），于是对象由根 #2（`globalValues`）直接持有；
   函数内局部变量则由根 #1（值栈区间）持有。两者"是否被标记"都成立，因此差异**不在**
   这两条路径本身，需要进一步定位。
2. 【推测】差异可能来自**首次回收的时机**：顶层循环的对象在第一次 minor GC 之前可能已累积
   足够多，或者相反 —— 但 §1.2 的测量显示崩溃概率与 n 不单调，这一假设与数据不完全吻合。
3. 【推测】差异可能来自**函数帧的生命周期**：函数内每轮调用的 native function、绑定方法、
   帧相关对象更多，被误释放的"可达但未标记"对象更多，命中悬垂的概率更高。

**结论：这一点必须在实现阶段用 GC trace 定位，不能凭上述假设动手。**

### 2.7 明确未证实的三件事（禁止在实现中当作已知）

1. 顶层/函数内差异的**具体**成因（§2.6）。
2. R2 那次段错误**具体**对应哪个被释放对象 —— 因果链（标记缺失 → 释放 → 悬垂）已由代码与
   R1/E 用例证实，但"这次段错误就是这个悬垂指针"未逐指针证实。
3. DAG 形状下 worklist 的**指数增长**（环的无限循环已实测，DAG 未单独实测）。
4. 缺口 B（native lambda 捕获）**是否曾导致真实故障** —— 未观察到，属潜在风险。

---

## 3. 修复方案

### 3.1 修 D1：让标记阶段真的标记（**先做**）

`collectMinor` 与 `collectMajor` 的闭包循环改为调用现成的 `mark()`，并复用一块 scratch 向量
避免每个节点一次堆分配：

```cpp
    // Transitive closure. trace() appends children; each child is marked before
    // it is queued, which is what makes a shared or cyclic graph terminate and
    // what keeps a reachable object from being swept.
    std::vector<GcObject*> children;
    while (!worklist.empty()) {
        GcObject* obj = worklist.back();
        worklist.pop_back();
        children.clear();
        obj->trace(children);
        for (GcObject* child : children) {
            mark(child, worklist, /*isMinor=*/isMinor);
        }
    }
```

要点：

- **两个收集器都要改**（`gc_heap.cpp:92-98` 与 `:173-177`）—— 只改一个会留下同一类缺陷。
- 用 `mark()` 而不是就地写 `setMarked`：它已实现"minor 时跳过老对象"这一正确规则
  （老对象由 `sweepMinor` 无条件保留、且其年轻引用由 remembered set 覆盖）。
- 删掉那条与代码不符的注释（"we filter in the next iteration"），或改成描述实际行为。
- `mark()` 从此不再是死代码；建议顺手确认编译器不再对它报警。
- **工作量**：约 10 行，两个函数各一处。

### 3.2 修 D2：补安全点（**第二步**）

**布点策略：只在 `OP_LOOP` 增加一个检查点**，不碰别处。理由与代价：

- `OP_LOOP` 是循环回边，**每轮迭代恰好执行一次**，是"长时间不调用"的唯一入口；
  加上它之后，任何循环都能到达回收点，bigint 用例那一类无界增长随之消失。
- **不在 `allocate()` 里回收**：分配器是在任意 C++ 上下文里被调用的（native function 内部、
  容器构造过程中、甚至 GC 自身的晋升/扫描路径），此时的引用往往只存在于 C++ 局部变量或
  lambda 捕获里（§2.4 缺口 B），在那里触发回收会把这些对象直接扫掉。**回收必须发生在
  "所有活跃引用都已登记为根"的位置**，即解释器主循环的安全点。这是一条硬约束。
- 代价：`needsGC()` 读单例上的两个 bool（`pendingMinorGC_ || pendingMajorGC_`），加一次
  分支。`OP_LOOP` 每次迭代多一次装载 + 一次高度可预测的分支；对 NaN-boxing 快路径
  **不引入任何装箱/拆箱或额外内存访问**，也不改变值的表示。真正的热路径（算术 opcode）
  一行不动。
- **可测量的验收标准**（全部必须满足才认为该步通过）：
  1. `tests/bench/01-loop-pressure.va`、`02-fibonacci.va`、`04-prime-sieve.va` 的耗时
     **回归 ≤ 3%**（同机同构建类型，前后各跑 5 次取中位数；3% 是留给测量噪声的上限，
     若超过则先查测量方法再考虑优化，例如把 `needsGC()` 的两个 bool 合并成一个字节）。
  2. 另一个"最紧循环"微基准（纯整数累加、无调用、无分配）**回归 ≤ 1%**。
  3. 修复后：一个**无任何调用**的分配循环，其 `minorGC+_majorGC` 计数必须增长
     （即 D2 真的被修掉了），且完成时的驻留量有界 —— 该断言通过 §4.3 的 `gc()` 钩子做成
     **确定性**测试，而不是靠观察内存。
  4. bigint 用例在本机（空闲内存约 7 GB）通过，且不再依赖空闲内存（§4.2）。

### 3.3 rooting 审计与补齐（**第三步**）

按 §2.4 的审计表，补齐两处缺口，并**把不变式写进代码注释**：

1. **缺口 A**：`VoraFunction::trace()` 增加对 `upvalues` 的处理：
   ```cpp
   for (const auto& uv : upvalues) {
       if (uv) pushGcRefs(uv->get(), wl);   // 打开的会重复一个栈根，无害
   }
   ```
2. **缺口 B**：给 `NativeFunction` 一个**可被 trace 的根通道**，例如内部
   `std::vector<GcObject*> capturedRoots_;`，`trace()` 遍历 push 之；各 `getXxxMethod`
   工厂在捕获接收者时调用一个登记接口（如 `native->addCapturedRoot(arr.get())`），
   并把 `native_function.cpp:28` 的注释从"不再持有任何 GcObject"改成真实规则：
   **凡 native 持有的 GcPtr 都必须登记，否则它只对 GC 不可见。**
   同时更新 `src/gc/gc_ptr.h` 文件头的不变式描述，把"native 捕获"这一情形显式写进去。
   备选方案：让方法工厂不再捕获接收者，改为经既有的 bound-method 机制把接收者作为
   `this` 传递（改动面更大，但在架构上更干净）—— 实现时二选一，需在提交信息里说明取舍。
3. 审计表（§2.4）应作为**永久文档**保留在本文，后续新增 GcObject 子类或 native 时对照。

### 3.4 对嵌入 API（`src/vora.h`）的影响

- **ABI 不变**：`isBigInt()` / `fitsInt64()` / `toInt64Exact()` / `valueToString()` 等
  已冻结的入口不受影响，无需改签名。
- **契约变化（需要在 `src/vora.h` 注释里写明）**：修复前"GC 只在调用点发生"实际上让嵌入者
  可以安全地把 `Value` 存在 C++ 局部变量里跨越长循环；补上 `OP_LOOP` 安全点之后，
  **回收会在长循环中途发生**，因此**嵌入者必须把跨调用存活的 Value 登记为根**。
  这不是新增限制，而是把 gc_ptr.h 早已声明的规则落到实处。
- **建议新增一个显式的"现在回收"入口**：把 `VM::collectGarbage()` 从 private
  （`vm.h:522`）提升为 public（或加一个 `collectNow()` 包装）。理由是**测试与嵌入双方都需要
  确定性回收点**（§4），而它本来就在做这件事，没有语义风险。若维护者认为不宜扩大公开面，
  备选是在 `VM` 上加一个 `#ifdef VORA_TESTING` 的友元，仅测试可见 —— 两条路都要在提交信息里
  写明取舍。

---

## 4. 测试方案

原则：**任何修复都必须红/绿验证**（改动前失败、改动后通过）。本项目已多次证明这是唯一可信的
验收方式。

### 4.1 新增 `tests/unit/test_gc.cpp`（确定性，主验收）

单元测试可以直接构造对象图并**显式**调用 `GcHeap::collectMinor(roots)` /
`collectMajor(roots)`（两者都是 public，`gc_heap.h:140` / `:145`；`public:` 在 `:87`），因此**完全确定性**、
不依赖阈值、不依赖内存、不依赖运行次数：

| 用例 | 期望 | 改动前 |
|---|---|---|
| 可达**环**（A→B→A）存活于 minor | 遍历终止，两者都活 | **挂死**（红） |
| 可达**环**存活于 major | 同上 | **挂死**（红） |
| 只经父对象可达的子对象（`parent[0]` 是个数组）存活 | 子对象仍可用 | 被释放（红） |
| 深链（A→B→C→…→Z，Z 只经链可达） | 全部存活 | 除 A 外全被释放（红） |
| 无环 DAG（父被两个子共享）遍历终止 | 终止 | 应终止（此项可能已通过，作为对照） |
| 不可达对象被回收 | 释放 | 通过（对照，防"修成不回收"） |
| 老容器新增年轻值后 minor 不丢 | 值存活（remembered set 路径） | 通过/红待测 |
| 闭包 close 后其捕获对象存活（缺口 A） | 存活 | 红 |
| native 捕获的接收者存活（缺口 B） | 存活 | 红（需先补 §3.3） |

前四项是 D1 的直接门禁；「环」两项是**唯一在改动前就确定性失败**的（R1 已实测），
因此它们应当作为该提交的"红"证据。

### 4.2 改造 `bigint_survives_gc_while_referenced_from_a_constant_pool`

现状（`tests/unit/test_bigint.cpp:485-517`）：脚本用 O(n²) 字符串拼接制造约 8 GB 分配，
断言 `result == OK` 与 `gcAfter > gcBefore`。**它测的是空闲内存**，必须改造成不依赖内存、
且保留原意（"常量池里的 BigInt 能活过 GC"）：

- 保留脚本里"把大整数字面量放进常量池、再制造回收压力"的结构，但把压力规模降到
  与内存无关的量级；
- **把"回收确实发生过"从"靠分配压力撞上"改成"显式驱动"**：测试里直接调用
  `vm.collectGarbage()`（§3.4 使它可见）或 `GcHeap::instance().collectMinor/Major(roots)`，
  然后断言 `gcAfter > gcBefore` 由**显式的**那次调用保证；
- 断言"常量池里的 BigInt 仍然精确"（`toString` 与字面量一致）—— 这才是用例的本意；
- 修好后**在任何空闲内存下都必须通过**，并作为 D2 的回归门禁之一。

### 4.3 运行时压力用例 + `gc()` 钩子（确定性化的关键）

要"把复现固化成确定性运行时测试"，需要一个**脚本可用的确定性回收点**。建议新增内建
`gc()`：立即执行一次回收，返回累计回收次数。它同时是 §3.4 提到的嵌入者钩子，理由一致。

- `tests/runtime/test_gc_stress.va`：
  - 可达环：构造环 → `gc()` → 断言环内容正确、遍历返回（**改动前挂死**，是"红"证据）；
  - 函数内局部容器增长（数组 / 字典 / Map / Set 各一）→ `gc()` → 断言内容正确；
  - 大字符串增长 → `gc()` → 断言长度与内容；
  - 深递归 + 闭包捕获（覆盖缺口 A）；
  - 常量池大整数 + `gc()`（与 §4.2 同一意图的运行时版本）；
  - **D2 专项**：无任何调用的分配循环 → 断言 `gc()` 报告的计数在循环中曾自增
    （即"长循环中途也会回收"），这条同时验证 §3.2 的安全点，且**与内存无关**。
- 所有断言都是"值正确 + 不挂死"，不含"跑四次看崩不崩"。

### 4.4 红/绿验证的具体做法

- 每个提交前，用**上一提交的二进制**（或 `git stash` 后重建）跑新增用例，确认**失败**；
  再在修复后确认**通过**。保存对照二进制（本仓库上一轮已用过此法：`/tmp/.../Vora_prejson.exe`）。
- 对"挂死"类用例（R1/环），"红"的证据是 `timeout` 退出码 124，需在提交信息里贴出。
- 全量门禁：单测（488 用例 / 30686 断言，本机目标**全绿**）、脚本 97、示例 58、fuzz 8。
  注意 §4.2 修好之后，本机才第一次具备"单测全绿"这个前提。

---

## 5. 分阶段实施顺序

每步一个提交、可独立验证；每步都给出风险与回退点。

| 步骤 | 内容 | 验证（红→绿） | 风险 | 回退 |
|---|---|---|---|---|
| **1** | 修 D1：两个收集器的闭包循环改用 `mark()`；删除失实注释 | 新增 `test_gc.cpp` 的"环"与"只经父可达"用例：改动前**挂死/失败**，改动后通过；全量单测 | **堆驻留量上升**：修复前几乎所有传递可达对象都被误释放，等于一个"过度激进"的收集器；修复后晋升**首次真正生效**，内存占用会上升，可能暴露**真实的**泄漏（那是另一个缺陷，不要混入本步修）。另需确认没有依赖"对象被提前释放"的巧合行为 | 回退本提交即可（改动局限在两个闭包循环） |
| **2** | 修 D2：`OP_LOOP` 安全点；`VM::collectGarbage()` 可见性（§3.4，二选一）；`gc()` 内建 | `test_gc_stress.va` 的 D2 专项（无调用循环中计数自增）；`bigint_...` 用例改造后**本机通过**；benches 回归 ≤3%（§3.2 标准） | 性能回归；**安全点位置若放错（例如放进 `allocate()`）会立刻放大 §2.4 缺口 B 的危险** —— 必须只放解释器主循环 | 回退本提交（安全点是独立的一处检查） |
| **3** | 补 rooting 缺口 A（upvalues）与 B（native 捕获），并更新 `gc_ptr.h` 不变式注释 | `test_gc.cpp` 的缺口 A/B 用例：改动前红、改动后绿；全量 | 缺口 B 有"登记接口"与"改用 bound-method 传 this"两种做法，后者改动面更大 | 缺口 A、B 可分两个提交，便于单独回退 |
| **4** | 文档同步：`CHANGELOG.md` 的 Known limitation 条目改为已修；`docs/00-roadmap.md`「仍开放」段相应更新；本文状态改为已实现 | 文档评审 | 无代码风险 | 纯文档回退 |
| **5** | 顺带项 §6（`tests/ans/61-combination-sum.va`） | 该文件可解析、可运行 | 无 | 独立提交 |

**顺序不可颠倒**：第 1 步消除不确定性（也是让 §4 的确定性测试成为可能的前提），第 2 步处理
无界增长，第 3 步补齐剩余泄漏面。把 2 提到 1 之前会让"补了安全点、回收更频繁"直接撞上
未修的标记缺陷 —— 崩溃率会**上升**。

---

## 6. 顺带项：`tests/ans/61-combination-sum.va`

该文件用保留字 `match` 作变量名（`let match = true;`，第 47 行），因此**根本无法解析**：

```
  --> 47:17
  47 |             let match = true;
     = Error: Expected variable name
```

这**不是**缺陷 —— `match` 是保留字（match 表达式），不能作标识符，报错是正确行为。
问题在于这个文件**被 git 跟踪**，任何"全语料库"工具（`vora fmt` 往返、语法统计、
批量扫描）都会在它身上失败，也让"语料库有多少文件是合法的"这类数字失真。

**建议：保留字改名**（`match` → 例如 `matched`），随 GC 轮一起收掉。理由：
`tests/ans/` 是 LeetCode 题解语料，不是语言行为测试，它的价值在于"一大坨真实风格代码能跑"；
改一个标识符即可恢复该价值，且不会掩盖任何语言语义。**不**建议把"保留字可作标识符"当作
要支持的特性 —— 那会与 `match` 表达式直接冲突。

（附带发现，记在这里避免丢失：`tests/interpreter/test_input.va` 需要管道输入才能通过，
属测试运行器的正常约定，不是缺陷。）

---

## 附录 A 证据命令清单（可复跑）

```bash
export PATH="/c/Program Files/mingw64/bin:$PATH"
cmake --build build --target Vora Vora_tests

# D1 核心：标记阶段不标记（读代码）
sed -n '53,141p' src/gc/gc_heap.cpp      # collectMinor，闭包循环在 92-98
sed -n '147,190p' src/gc/gc_heap.cpp     # collectMajor，闭包循环在 173-177
sed -n '38,51p'   src/gc/gc_heap.cpp     # mark() 定义
grep -rn '\bmark(' src/                  # 只有声明与定义，无调用点

# D2：安全点只有调用类 opcode
grep -n 'needsGC()' src/vm/vm.cpp        # 1115 / 2153 / 2189 / 2249

# rooting 缺口
sed -n '35,41p' src/runtime/vora_function.cpp   # 不 trace upvalues
sed -n '27,29p' src/runtime/native_function.cpp # 空 trace
sed -n '617,628p' src/runtime/builtins.cpp      # lambda 捕获接收者

# 复现（R1 确定性挂死；R2 概率段错误）
timeout 30 build/Vora.exe R1.va ; echo "exit=$?"     # 期望修好后 exit=0
for i in 1 2 3 4 5 6; do build/Vora.exe R2.va >/dev/null 2>&1; echo -n "$? "; done
```

## 附录 B 根集合审计表

见 §2.4。**结论：8 类根全部被收集，各 GcObject 子类的 `trace()` 基本完整；缺口只有两处**
（`VoraFunction` 未 trace `upvalues`；`NativeFunction` 捕获的 GcPtr 不可见）。
新增 GcObject 子类或 native 时必须对照该表。

## 附录 C 本文与既有文档的关系

- `CHANGELOG.md [Unreleased] → Known limitation`：本缺陷的当前记录，条目措辞以本文为准，
  修复后按 §5 步骤 4 更新。
- `docs/00-roadmap.md`「已知限制 → 仍开放」：同上，附指向本文的链接。
- `docs/19-bignum-value-design.md`：其中"`FunctionPrototype::trace()` 曾是空壳"的修复，
  与本缺陷**同属"对象持有的引用未被 trace"这一类**。本次的缺口 A/B 是同一类问题的另外两处
  —— 建议实现时**一次性搜一遍所有 `trace()` 实现**，而不是只补这两处。
