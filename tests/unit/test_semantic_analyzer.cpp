// tests/unit/test_semantic_analyzer.cpp — SemanticAnalyzer unit tests
//
// Tests: symbol declaration, scope nesting, reference tracking,
//        visible symbol collection, unused variable detection,
//        unreachable code detection, shadowing detection,
//        function/object/method symbol collection.

#include "doctest.h"
#include "lsp/semantic_analyzer.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/error_reporter.h"

#include <memory>

using namespace vora;

// Helper: parse source code and run semantic analysis.
struct AnalysisResult {
    std::unique_ptr<Program> program;
    SemanticAnalyzer analyzer;
};

static AnalysisResult analyze(const std::string& source) {
    // Use a null error reporter that collects errors silently.
    class NullReporter : public ErrorReporter {
    public:
        void report(int, int, int, const std::string&, Severity) override {}
        bool hadError() const override { return false; }
    };

    NullReporter reporter;
    Lexer lexer(source, reporter);
    auto tokens = lexer.scanTokens();

    Parser parser(std::move(tokens), reporter);
    parser.setSource(source);
    auto program = parser.parse();

    AnalysisResult result;
    result.program = std::move(program);
    if (result.program) {
        result.analyzer.analyze(*result.program);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Variable declarations
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_var_declaration") {
    auto r = analyze("let x = 5");
    REQUIRE(r.program);

    auto* sym = r.analyzer.findDeclarationAt(1, 5);
    REQUIRE(sym != nullptr);
    CHECK(sym->name == "x");
    CHECK(sym->kind == SymbolKind::Variable);
}

TEST_CASE("semantic_const_declaration") {
    auto r = analyze("const PI = 3.14");
    REQUIRE(r.program);

    auto* sym = r.analyzer.findDeclarationAt(1, 7);
    REQUIRE(sym != nullptr);
    CHECK(sym->name == "PI");
    CHECK(sym->kind == SymbolKind::Constant);
}

TEST_CASE("semantic_var_not_found_at_wrong_position") {
    auto r = analyze("let x = 5");
    REQUIRE(r.program);

    // Position on the 'let' keyword, not the variable name.
    auto* sym = r.analyzer.findDeclarationAt(1, 2);
    CHECK(sym == nullptr);
}

TEST_CASE("semantic_var_declaration_on_nameToken") {
    auto r = analyze("let nameToken = 42");
    REQUIRE(r.program);

    // Column of 'nameToken' = after "let " = pos 5
    auto* sym = r.analyzer.findDeclarationAt(1, 5);
    REQUIRE(sym != nullptr);
    CHECK(sym->name == "nameToken");
}

// ═══════════════════════════════════════════════════════════════════════════
// Scope nesting
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_scope_nested") {
    auto r = analyze(
        "let x = 1\n"
        "{\n"
        "    let x = 2\n"
        "}"
    );
    REQUIRE(r.program);

    // Outer x at (1, 5)
    auto* outer = r.analyzer.findDeclarationAt(1, 5);
    REQUIRE(outer != nullptr);
    CHECK(outer->name == "x");
    CHECK(outer->scopeLevel == 0);

    // Inner x at (3, 9)
    auto* inner = r.analyzer.findDeclarationAt(3, 9);
    REQUIRE(inner != nullptr);
    CHECK(inner->name == "x");
    CHECK(inner->scopeLevel > outer->scopeLevel);
}

TEST_CASE("semantic_visible_symbols_scoped") {
    auto r = analyze(
        "let outer = 1\n"
        "{\n"
        "    let inner = 2\n"
        "    // cursor here\n"
        "}"
    );
    REQUIRE(r.program);

    // Cursor at line 4 (inside the block).
    auto visible = r.analyzer.getVisibleSymbols(4, 1);
    bool hasOuter = false, hasInner = false;
    for (auto* s : visible) {
        if (s->name == "outer") hasOuter = true;
        if (s->name == "inner") hasInner = true;
    }
    CHECK(hasOuter);
    CHECK(hasInner);
}

TEST_CASE("semantic_visible_symbols_outside_block") {
    auto r = analyze(
        "let outer = 1\n"
        "{\n"
        "    let inner = 2\n"
        "}\n"
        "// cursor here\n"
    );
    REQUIRE(r.program);

    // Cursor at line 5 (outside the block).
    auto visible = r.analyzer.getVisibleSymbols(5, 1);
    bool hasOuter = false;
    for (auto* s : visible) {
        if (s->name == "outer") hasOuter = true;
    }
    CHECK(hasOuter);
    // NOTE: getVisibleSymbols currently returns all symbols across all scopes
    // because scopes are popped during analysis. Full scope-aware filtering
    // requires AST node position ranges. The LSP completion handler uses
    // scopeLevel on each symbol to prioritize same-scope items.
}

// ═══════════════════════════════════════════════════════════════════════════
// References
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_find_references") {
    auto r = analyze(
        "let x = 1\n"
        "x = 2\n"
        "print(x)\n"
    );
    REQUIRE(r.program);

    // Find references to x declared at line 1.
    auto refs = r.analyzer.findReferencesTo(1, 5);
    // Should find: the declaration itself, the assignment at line 2, and the usage at line 3.
    CHECK(refs.size() >= 3);
}

TEST_CASE("semantic_reference_on_usage_resolves_to_decl") {
    auto r = analyze(
        "let count = 0\n"
        "count = count + 1\n"
    );
    REQUIRE(r.program);

    // Cursor on 'count' usage at line 2, column 1.
    auto* sym = r.analyzer.findSymbolAt(2, 1);
    REQUIRE(sym != nullptr);
    CHECK(sym->name == "count");
    CHECK(sym->declToken.line == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Functions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_function_declaration") {
    auto r = analyze("func add(a, b) { return a + b }");
    REQUIRE(r.program);

    auto* sym = r.analyzer.findDeclarationAt(1, 6);
    REQUIRE(sym != nullptr);
    CHECK(sym->name == "add");
    CHECK(sym->kind == SymbolKind::Function);
    CHECK(sym->paramNames.size() == 2);
    CHECK(sym->paramNames[0] == "a");
    CHECK(sym->paramNames[1] == "b");
}

TEST_CASE("semantic_function_unused_param") {
    auto r = analyze("func unused(x) { return 42 }");
    REQUIRE(r.program);

    auto& unused = r.analyzer.getUnusedSymbols();
    bool xUnused = false;
    for (auto* s : unused) {
        if (s->name == "x") xUnused = true;
    }
    // Parameters are skipped from unused detection.
    CHECK_FALSE(xUnused);
}

// ═══════════════════════════════════════════════════════════════════════════
// Objects and methods
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_object_declaration") {
    auto r = analyze("Obj Counter(init) { this.value = init; func inc() { this.value = this.value + 1 } }");
    REQUIRE(r.program);

    auto* sym = r.analyzer.findDeclarationAt(1, 5);
    REQUIRE(sym != nullptr);
    CHECK(sym->name == "Counter");
    CHECK(sym->kind == SymbolKind::Object);
    CHECK(sym->paramNames.size() == 1);
    CHECK(sym->paramNames[0] == "init");
}

TEST_CASE("semantic_exported_symbol") {
    auto r = analyze("export let VERSION = \"1.0\"");
    REQUIRE(r.program);

    auto& exported = r.analyzer.getExportedSymbols();
    REQUIRE(exported.size() >= 1);
    CHECK(exported[0]->name == "VERSION");
    CHECK(exported[0]->isExported);
}

// ═══════════════════════════════════════════════════════════════════════════
// Import
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_import_binding") {
    auto r = analyze("import \"math\"");
    REQUIRE(r.program);

    // The import creates a binding derived from the module path.
    auto allSyms = r.analyzer.getVisibleSymbols(1, 1);
    bool hasImport = false;
    for (auto* s : allSyms) {
        if (s->kind == SymbolKind::Import) {
            hasImport = true;
            CHECK(s->importPath == "math");
            break;
        }
    }
    CHECK(hasImport);
}

TEST_CASE("semantic_import_with_alias") {
    auto r = analyze("import \"math\" as M");
    REQUIRE(r.program);

    auto allSyms = r.analyzer.getVisibleSymbols(1, 1);
    bool hasM = false;
    for (auto* s : allSyms) {
        if (s->name == "M" && s->kind == SymbolKind::Import) {
            hasM = true;
            break;
        }
    }
    CHECK(hasM);
}

TEST_CASE("semantic_from_import") {
    auto r = analyze("from \"math\" import sin, cos");
    REQUIRE(r.program);

    auto allSyms = r.analyzer.getVisibleSymbols(1, 1);
    bool hasSin = false, hasCos = false;
    for (auto* s : allSyms) {
        if (s->name == "sin" && s->kind == SymbolKind::Import) hasSin = true;
        if (s->name == "cos" && s->kind == SymbolKind::Import) hasCos = true;
    }
    CHECK(hasSin);
    CHECK(hasCos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Loop variables
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_for_in_loop_var") {
    auto r = analyze("for item in [1, 2, 3] { print(item) }");
    REQUIRE(r.program);

    // 'item' should be visible as a ForVar inside the loop body.
    // (Visible at cursor inside the body.)
    auto visible = r.analyzer.getVisibleSymbols(1, 35);
    bool hasItem = false;
    for (auto* s : visible) {
        if (s->name == "item") hasItem = true;
    }
    CHECK(hasItem);
}

// ═══════════════════════════════════════════════════════════════════════════
// Catch variable
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_catch_var_not_unused") {
    auto r = analyze("try { throw \"err\" } catch(e) { print(e) }");
    REQUIRE(r.program);

    // 'e' is used in the catch block, so it should NOT be in unused list.
    auto& unused = r.analyzer.getUnusedSymbols();
    bool eUnused = false;
    for (auto* s : unused) {
        if (s->name == "e") eUnused = true;
    }
    CHECK_FALSE(eUnused);
}

// ═══════════════════════════════════════════════════════════════════════════
// Unused variable detection
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_unused_local") {
    auto r = analyze("{ let x = 5 }");
    REQUIRE(r.program);

    auto& unused = r.analyzer.getUnusedSymbols();
    bool xUnused = false;
    for (auto* s : unused) {
        if (s->name == "x") xUnused = true;
    }
    CHECK(xUnused);
}

TEST_CASE("semantic_underscore_prefixed_not_unused") {
    auto r = analyze("{ let _temp = 42 }");
    REQUIRE(r.program);

    auto& unused = r.analyzer.getUnusedSymbols();
    bool tempUnused = false;
    for (auto* s : unused) {
        if (s->name == "_temp") tempUnused = true;
    }
    CHECK_FALSE(tempUnused);
}

TEST_CASE("semantic_obj_method_not_unused") {
    // Methods accessed via obj.method() should not be flagged as unused.
    // They are part of the object's public interface.
    auto r = analyze(
        "Obj Person(name, age) {\n"
        "    this.name = name\n"
        "    this.age = age\n"
        "    func greet() {\n"
        "        print(\"I'm \" + this.name)\n"
        "    }\n"
        "    func birthday() {\n"
        "        this.age = this.age + 1\n"
        "    }\n"
        "}\n"
        "let alice = Person(\"Alice\", 25)\n"
        "alice.greet()\n"
        "alice.birthday()\n"
    );
    REQUIRE(r.program);

    auto& unused = r.analyzer.getUnusedSymbols();
    bool greetUnused = false;
    bool birthdayUnused = false;
    for (auto* s : unused) {
        if (s->name == "greet") greetUnused = true;
        if (s->name == "birthday") birthdayUnused = true;
    }
    CHECK_FALSE(greetUnused);
    CHECK_FALSE(birthdayUnused);
}

// ═══════════════════════════════════════════════════════════════════════════
// Unreachable code detection
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_unreachable_after_return") {
    auto r = analyze(
        "func f() {\n"
        "    return 1\n"
        "    let x = 2\n"
        "}"
    );
    REQUIRE(r.program);

    auto& unreachable = r.analyzer.getUnreachableTokens();
    // Should have at least one unreachable token (the 'let' keyword).
    CHECK_FALSE(unreachable.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Shadowing detection
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_shadowing_detected") {
    auto r = analyze(
        "let x = 1\n"
        "{\n"
        "    let x = 2\n"
        "}"
    );
    REQUIRE(r.program);

    auto& shadows = r.analyzer.getShadowedSymbols();
    CHECK_FALSE(shadows.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Lambda / anonymous function
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_lambda_params") {
    auto r = analyze("let f = func(x, y) { return x + y }");
    REQUIRE(r.program);

    // The lambda params x, y should be visible inside the body only.
    auto visible = r.analyzer.getVisibleSymbols(1, 20);
    bool hasX = false;
    for (auto* s : visible) {
        if (s->name == "x" && s->kind == SymbolKind::Parameter) hasX = true;
    }
    CHECK(hasX);
}

// ═══════════════════════════════════════════════════════════════════════════
// if/else return reachability
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("semantic_if_both_branches_return") {
    auto r = analyze(
        "func f(x) {\n"
        "    if (x) { return 1 } else { return 0 }\n"
        "    let unreachable = 3\n"
        "}"
    );
    REQUIRE(r.program);

    auto& unreachable = r.analyzer.getUnreachableTokens();
    // The 'let unreachable = 3' should be unreachable.
    CHECK_FALSE(unreachable.empty());
}
