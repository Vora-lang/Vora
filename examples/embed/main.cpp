// examples/embed/main.cpp
// ============================================================================
// Vora C++ 嵌入示例 —— 8 个演示，只需 vora.hpp + vora_lib.lib
//
// 依赖:
//   vora.hpp       单文件头文件
//   vora_lib.lib   预编译静态库
//
// 构建 (Vora 树内):
//   cmake -B build -DVORA_EMBED_DEMO=ON
//   cmake --build build --target embed_demo
//
// 构建 (独立项目):
//   cmake -B build -S . -DVORA_SDK=<install-prefix>
//   cmake --build build
//
// 演示:
//   1. 从 C++ 字符串执行 Vora 代码
//   2. 注册 C++ 原生函数供 Vora 调用
//   3. C++ ↔ Vora 双向数据交换
//   4. 从文件加载并执行 Vora 脚本
//   5. 错误隔离 —— Vora 错误不抛向 C++
//   6. 在嵌入环境中运行 Vora 类和对象
//   7. REPL 式增量执行（共享 VM）
//   8. 自定义 ErrorReporter — 收集错误到 C++
// ============================================================================

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// 只需这一行
#include "vora.hpp"

using namespace vora;

#ifdef _WIN32
extern "C" {
    int __stdcall SetConsoleOutputCP(unsigned int);
    int __stdcall SetConsoleCP(unsigned int);
}
#define CP_UTF8 65001
#endif

// ── 辅助: 执行一段 Vora 源码 ──────────────────────────────────────────
static int runString(VM& vm, const std::string& source,
                     const std::string& desc = "<string>") {
    StderrErrorReporter reporter(source);
    Lexer lexer(source, reporter);
    auto tokens = lexer.scanTokens();

    Parser parser(tokens, reporter);
    parser.setSource(source);
    auto program = parser.parse();

    if (parser.hasError()) {
        std::cerr << "[ERROR] Parse failed: " << desc << "\n";
        return 1;
    }

    Compiler compiler(reporter);
    compiler.setSource(source);
    compiler.seedGlobals(vm.getGlobalNames());

    Chunk chunk = compiler.compile(program.get());

    if (compiler.hadError) {
        std::cerr << "[ERROR] Compile failed: " << desc << "\n";
        return 1;
    }

    vm.initGlobals(compiler.getGlobalNames());

    InterpretResult result = vm.interpret(chunk);
    if (result != InterpretResult::OK) {
        std::cerr << "[ERROR] Runtime error: " << desc << "\n";
        return 1;
    }
    return 0;
}

// ── Helper: Value → number ─────────────────────────────────────────────
static double toNumber(const Value& v) {
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
    return 0.0;
}

// ── 分隔线 ─────────────────────────────────────────────────────────────
static void sep(const char* title) {
    std::cout << "\n---------------------------------------------------\n";
    std::cout << title << "\n";
    std::cout << "---------------------------------------------------\n";
}

// ============================================================================
// 演示 1: 从字符串执行 Vora 代码
// ============================================================================
static void demo1_hello_world() {
    sep("演示 1: 从 C++ 字符串执行 Vora 代码");

    VM vm;
    registerBuiltins(vm);

    const char* script = R"(
        print("Hello from embedded Vora!")
        let x = 10
        let y = 20
        print("x + y =", x + y)
        print("x * y =", x * y)
    )";

    runString(vm, script);
}

// ============================================================================
// 演示 2: 注册 C++ 原生函数供 Vora 调用
// ============================================================================
static void demo2_native_functions() {
    sep("演示 2: 注册 C++ 原生函数供 Vora 调用");

    VM vm;
    registerBuiltins(vm);

    vm.defineNative("cpp_add", 2,
        [](const std::vector<Value>& args) -> Value {
            return toNumber(args[0]) + toNumber(args[1]);
        });

    vm.defineNative("cpp_greet", 1,
        [](const std::vector<Value>& args) -> Value {
            auto& s = args[0].asGcString();
            return GcHeap::instance().alloc<GcString>(
                "Hello, " + s->value + "! (from C++)"
            );
        });

    vm.defineNative("cpp_sum", -1,  // arity = -1 → 可变参数
        [](const std::vector<Value>& args) -> Value {
            double total = 0;
            for (const auto& v : args) total += toNumber(v);
            return total;
        });

    vm.defineNative("cpp_host_info", 0,
        [](const std::vector<Value>&) -> Value {
            return GcHeap::instance().alloc<GcString>(
                "Vora embedded in C++17 | "
                "sizeof(Value) = " + std::to_string(sizeof(Value)) + " bytes"
            );
        });

    const char* script = R"(
        print("--- 调用 C++ 原生函数 ---")
        print("cpp_add(3.5, 2.5) =", cpp_add(3.5, 2.5))
        print("cpp_greet('World') =", cpp_greet("World"))
        print("cpp_sum(1, 2, 3, 4, 5) =", cpp_sum(1, 2, 3, 4, 5))
        print("cpp_host_info() =", cpp_host_info())
    )";

    runString(vm, script);
}

// ============================================================================
// 演示 3: C++ ↔ Vora 双向数据交换
// ============================================================================
static void demo3_bidirectional_exchange() {
    sep("演示 3: C++ ↔ Vora 双向数据交换");

    VM vm;
    registerBuiltins(vm);

    // C++ 数据通过闭包暴露给 Vora
    struct GameState {
        int score = 0;
        std::string player = "Alice";
    };
    auto state = std::make_shared<GameState>();

    vm.defineNative("game_get_score", 0,
        [state](const std::vector<Value>&) -> Value {
            return static_cast<int64_t>(state->score);
        });

    vm.defineNative("game_set_score", 1,
        [state](const std::vector<Value>& args) -> Value {
            if (args[0].isInt())
                state->score = static_cast<int>(args[0].asInt());
            else if (args[0].isDouble())
                state->score = static_cast<int>(args[0].asDouble());
            return nullptr;
        });

    vm.defineNative("game_get_player", 0,
        [state](const std::vector<Value>&) -> Value {
            return GcHeap::instance().alloc<GcString>(state->player);
        });

    vm.defineNative("game_print_state", 0,
        [state](const std::vector<Value>&) -> Value {
            std::cout << "  [C++ GameState] player=" << state->player
                      << " score=" << state->score << "\n";
            return nullptr;
        });

    const char* script = R"(
        print("--- Vora 读写 C++ 数据 ---")
        print("初始状态:")
        game_print_state()
        print("当前分数:", game_get_score())
        game_set_score(100)
        print("修改后:")
        game_print_state()

        // Vora 侧计算，结果存入全局变量
        func fib(n) {
            if (n <= 1) { return n }
            return fib(n-1) + fib(n-2)
        }
        let g_fib_10_val = fib(10)
    )";

    runString(vm, script);
    std::cout << "  Vora 完成 fib(10) 计算，结果已存入全局变量\n";
}

// ============================================================================
// 演示 4: 从文件加载脚本
// ============================================================================
static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("Cannot open file: " + path);
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

static void demo4_load_from_file() {
    sep("演示 4: 从文件加载并执行 Vora 脚本");

    VM vm;
    registerBuiltins(vm);
    vm.currentModuleDir = ".";

    try {
        for (const auto& file : {"01-literals.va", "02-arithmetic.va"}) {
            std::string path = std::string("examples/") + file;
            std::cout << "--- 执行: " << path << " ---\n";
            runString(vm, readFile(path), path);
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "  文件加载失败: " << e.what()
                  << "\n  (请在项目根目录运行)\n";
    }
}

// ============================================================================
// 演示 5: 错误隔离
// ============================================================================
static void demo5_error_handling() {
    sep("演示 5: 错误隔离 — Vora 错误不抛向 C++");

    VM vm;
    registerBuiltins(vm);

    std::cout << "--- 语法错误（不影响宿主）---\n";
    runString(vm, "let x = ;", "<syntax error test>");

    std::cout << "--- 运行时错误 + try/catch ---\n";
    const char* safeScript = R"(
        print("开始执行...")
        try {
            let arr = [1, 2, 3]
            let bad = arr[10]
        } catch (e) {
            print("捕获到错误:", e)
        }
        print("程序继续运行")
    )";
    runString(vm, safeScript);

    std::cout << "  ✓ C++ 宿主完全不受 Vora 错误影响\n";
}

// ============================================================================
// 演示 6: Vora 类和对象
// ============================================================================
static void demo6_objects_and_methods() {
    sep("演示 6: 在嵌入环境中运行 Vora 类和对象");

    VM vm;
    registerBuiltins(vm);

    const char* script = R"(
        Obj Counter(start) {
            this.count = start
            func inc()  { this.count = this.count + 1 }
            func dec()  { this.count = this.count - 1 }
            func value() { return this.count }
        }

        let c = Counter(0)
        print("Counter 初始值:", c.value())
        c.inc()
        c.inc()
        c.inc()
        print("加 3 次后:", c.value())
        c.dec()
        print("减 1 次后:", c.value())
    )";

    runString(vm, script);
}

// ============================================================================
// 演示 7: REPL 式增量执行
// ============================================================================
static void demo7_repl_style() {
    sep("演示 7: REPL 式增量执行（共享 VM）");

    VM vm;
    registerBuiltins(vm);

    const char* steps[] = {
        "let a = 42",
        "let b = 100",
        "let c = a + b",
        "print('a =', a, ' b =', b, ' c =', c)",
        "func multiply(x, y) { return x * y }",
        "print('multiply(6, 7) =', multiply(6, 7))",
    };

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        std::cout << "  [" << (i + 1) << "] >>> " << steps[i] << "\n";
        runString(vm, steps[i], "step " + std::to_string(i + 1));
    }

    std::cout << "  ✓ 全局变量在整个会话中持续存在\n";
}

// ============================================================================
// 演示 8: 自定义 ErrorReporter
// ============================================================================
class DemoErrorCollector : public ErrorReporter {
public:
    struct Entry { int line, column, length; std::string msg; Severity sev; };
    std::vector<Entry> errors;
    bool hadError_ = false;

    void report(int l, int c, int len, const std::string& msg, Severity sev) override {
        errors.push_back({l, c, len, msg, sev});
        if (sev == Severity::Error) hadError_ = true;
    }
    bool hadError() const override { return hadError_; }
};

static void demo8_custom_error_reporter() {
    sep("演示 8: 自定义 ErrorReporter — 收集错误到 C++");

    const char* source = R"(
        let x = 1
        let y = unknown_var
        print(x + )
    )";

    DemoErrorCollector reporter;
    Lexer lexer(source, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(tokens, reporter);
    parser.setSource(source);
    parser.parse();

    std::cout << "  收集到 " << reporter.errors.size() << " 个错误:\n";
    for (const auto& e : reporter.errors) {
        std::cout << "    行 " << e.line << ":" << e.column << " — " << e.msg << "\n";
    }
}

// ============================================================================
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << R"(
+==========================================================+
|     Vora -- C++ Embedding Example                        |
|     JavaScript-like syntax, Lua-level simplicity,        |
|     Wren-style object orientation.                       |
+==========================================================+
)";

    demo1_hello_world();
    demo2_native_functions();
    demo3_bidirectional_exchange();
    demo4_load_from_file();
    demo5_error_handling();
    demo6_objects_and_methods();
    demo7_repl_style();
    demo8_custom_error_reporter();

    sep("全部 8 个演示完成!");
    return 0;
}
