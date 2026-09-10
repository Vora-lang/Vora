# Changelog

All notable changes to **Vora** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Honesty note (added 2026-09-05):** Earlier prose docs (`docs/00-roadmap.md` and
> `docs/08-已实现功能总结.md`) listed some features as "✅ 完成" that were in fact
> still aspirational, and referenced others as "Phase 4 规划中" that had already
> shipped. Going forward, this changelog is the **single source of truth** for
> what's actually in `main`. Roadmap may follow with version bumps; it is no
> longer authoritative on its own.

---

## [Unreleased]

> **Scope note (2026-09):** the P0/P1 fixes below were implemented across the
> 2026-09 Phase 1 work. Part of that history was later rewritten into a single
> snapshot commit (`3f4f71e`), so the original per-fix commit hashes cited in
> older roadmap revisions (`970caa4`, `61366f9`, `830a2f2`, `c3bc77a`) are no
> longer resolvable on `main`. This section is the authoritative record.

### Added
- **`**=` power-assignment** (Phase 1, syntax-review #2.6): `POWER_EQUAL`
  token (lexed as one token, distinct from `**` and `*=`); parser accepts it
  as a right-associative compound assignment; compiler maps it to `OP_POWER`.
  Works on variables, properties, and indices; result is **float** since the
  bare `**` operator is float (`2 ** 3` → `8.0`).
  Tests: `tests/runtime/test_compound_assign.va` + lexer/parser/VM units.
- **Bitwise operators `&` `|` `^` `~` `<<` `>>`** (Phase 1 P1-F):
  - Lexer: 5 new tokens (`AMPERSAND`, `CARET`, `TILDE`, `LESS_LESS`,
    `GREATER_GREATER`); `PIPE` reused from P1-B (bitwise OR in expressions,
    or-pattern separator inside `match` arms).
  - Compiler/VM: 6 new opcodes (`OP_BITWISE_AND/OR/XOR/NOT`,
    `OP_SHIFT_LEFT/RIGHT`); int64 only, runtime error on non-int operands.
  - Precedence follows C/JS (`<<`/`>>` > `&` > `^` > `|`, all *looser* than
    `==`/`!=` — see EBNF §6 for the surprise note). Shift counts `<0` or
    `>=64` clamp to `0` (`-1` sign-fill for negative right-shift).
  - Constant folding for int-literal operands (binary and unary `~`).
  - Tests: `tests/runtime/test_bitwise.va` (35+ assertions) plus lexer /
    parser / VM unit tests.
- **Match or-patterns** `1 | 2 | 3 =>` (P1-B): lexer `PIPE` token + parser
  alternation loop in `matchExpression()`; EBNF §4.6.
- **Top-level rest destructuring** `let [a, ...rest] = arr` (P1-D):
  compiler synthetic global-temp path; previously only valid inside functions.
- **Unary `+x`** (P1-E): parser primary; int-literal constant folding,
  runtime identity.
- **`in` expression operator** (P1-G): new `OP_IN`; Array → linear scan with
  `valuesEqual`, Dict → key lookup, String → substring. Precedence with
  relational so `a + b in arr` = `(a + b) in arr` (Python-style).
- **Parenthesized for-in** `for (x in xs)` (P1-H): 2-token lookahead +
  brace-aware scan; previously a syntax dead-end.

### Fixed
- **Cross-line statement swallowing** (P0 #1): Go-style lexical ASI — a
  newline now terminates a statement at statement level (paren/bracket depth
  0). `let b = a` followed by `-1` is two statements, not a subtraction.
- **Ternary precedence inversion** (P0 #2): `?:` lowered to precedence 1
  (shared with `||`/`??`), restoring C/JS/Go/Python semantics:
  `a || b ? c : d` now parses as `(a || b) ? c : d`.
- **Named arguments vs assignment arguments** (P0 #3): reviewed and
  **lock-in** decision — `f(name = value)` remains the named-argument syntax;
  passing an assignment expression as a positional argument requires
  parentheses (`f((a = 5))`). Documented as a known sharp edge.
- **Silent import binding derivation** (P0 #4): paths that are not valid
  identifiers (e.g. containing `-`) now produce a clear compile-time error
  unless an explicit `as` alias or `from ... import` form is used.
- Build portability (GCC / MinGW):
  - `src/ast/binding_pattern.h`: explicit `#include <cstdint>`
    (`enum class BindingKind : uint8_t`).
  - `src/gc/gc_heap.cpp`: explicit `#include <algorithm>`
    (`std::remove_if` in compaction/collection passes).
  - Previously relied on transitive includes from MSVC `<vector>` /
    `<optional>`. GCC does not transitively re-expose `std::uint8_t`
    nor `std::remove_if`, causing compile errors under
    `build.sh -G "MinGW Makefiles"`.
- Removed leftover debug diagnostic in `Chunk::addConstant` that printed
  every integer constant ≥ 250 to stderr during normal script runs.

### Documentation
- `CHANGELOG.md` added — first formal changelog in the project, backfilled
  through v0.27 per `git log` and the actual code on `main`.
- `docs/00-roadmap.md` rewritten to reflect real shipped state, not aspirational.
- `docs/08-已实现功能总结.md` annotated with a header note that its body is a
  historical version-by-version record; current state is in `CHANGELOG.md`.
- `docs/16-v1.0-grammar-ebnf.md`: precedence tables (§2.4 / §6) aligned 1:1
  with `getPrecedence()` (bitwise levels added; `in` and shift rows corrected);
  §2.3 correction — `\$` interpolation escape marked **not implemented**
  (previously claimed working); §7 status rows updated for P1-D/E/F/G/H.

---

## [0.27.0] - 2026-07

### Added
- **NaN-boxed `Value`** (`src/runtime/value.h`): reduced from 16 to 8 bytes via
  IEEE-754 NaN-boxing. Direct `int64`/`double`/pointer; boxed object payloads in
  NaN payload. Microbenchmarks 2×–5× depending on workload.
- **Superinstructions** for property access: `OP_GET_LOCAL_PROP`,
  `OP_GET_GLOBAL_PROP` (`src/chunk.h`). Fuses "get receiver" + "get property".
- **VM public API expansion** for C++ embedding (`include/vora.hpp`):
  `setGlobal`, `getGlobal`, `hasGlobal`, `registerNativeFunction`.
- **Embedding documentation** (`USER_GUIDE.md` chapter 15) and `examples/embed/`.
- **Generational GC** (commit `992907d`, Jul 3 — initially released as v0.28
  but rebased onto v0.27 in `ed722dd`): dual-generation mark-and-sweep with
  minor (512 KiB threshold, young-only scan + remembered set) and major
  (4 MiB, full heap) collections, write barrier at array / dict / object
  property mutation sites, promotion after 3 minor GC survives. Pause time
  reduced 5–10× on long-running scripts.
- **`async` / `await` + event loop** (commit `22a70ed`, Jul 3 — same rebase
  situation): based on the existing generator extension. Adds `AwaitExpr` AST
  node, `OP_AWAIT` opcode, `Task` value type, `run(task)` builtin, and the
  `std/async` module.
- **Doxygen-driven API doc generation** (commit `8c0f1d6`, Jul 2): Doxygen
  comments on all 34 public headers plus `scripts/build-api-docs.py` and
  `scripts/build-website-docs.py`. 126 entries across 10 modules.

### Fixed (cumulative 0.26 → 0.27)
- `vora.hpp` regenerated for NaN-boxing; CMake Python fallback (`b9ed45d`).
- REPL global-definition seed bug (`1d9484f`) — `let x` after a failed bare
  assignment no longer errors spuriously.
- Variable reference detection inside `${var}` string interpolation
  (`6c302ad`).
- Header portability on Linux (`47d0471`).
- Direct-mapped inline property cache + `string_view` constant pool keys
  (`645dd5b`).
- CMake build header: `--jobs/-j` parallel flag (`a433f5f`, `88ea9b3`).

---

## [0.26.0] - 2026-06

> Note: backfilled from `git log`. Earlier in-line release notes have been
> reconstructed from commit messages below; commit-level detail omitted.

### Added
- `func f(a, b=1, :c, :d=2)` named-parameter syntax (`f550cce`, "命名参数").
- **List / Dict comprehensions** at parser level (`f550cce`).
  ⚠ **Known broken in v0.27**: parsing accepts the syntax, runtime
  iterator protocol throws `next() requires an iterator or generator`.
  Tracked as P0/P1 in `VORA_SYNTAX_REVIEW.md`.
- `defer` propagation across nested function throws (`f550cce`).
- `OP_CALL_KW` opcode deduplication (`f550cce`).

### Changed
- 4 项技术债清理 included in same commit.

---

## [0.25.0] - 2026-04

### Added
- Static / class methods via `this.func` dispatch in `Obj` bodies.
- Object rest destructuring: `let { x, ...rest } = obj`. *(Obj-method-only;
  top-level still parses to a Dict, not a binding pattern.)*
- Parameter destructuring: `func f({ x, y }) { ... }`.
- `defer` runs even when the function subsequently `throw`s (RAII semantics).
- Unified `for-in` iterator protocol — `iter()` / `next()`; works for arrays,
  strings, ranges, dicts, user objects, generators.
- Type annotations that accept strings: `let n :int = "42"`.

---

## [0.24.0] - 2026-03

### Added
- Null-coalescing `??`, optional-chaining `?.`, ternary `?:` operators.

---

## Versions ≤ 0.23

The full versions ≤ 0.23 (lexer, Pratt parser, `Obj` keyword, single-gen
mark-sweep GC, C3 MRO, `super`, `defer`, `try/catch/finally`, CMake build,
doctest unit tests) are not individually listed here. They are recoverable
from `git log --oneline` on `main` and from `docs/08-已实现功能总结.md`
(treated as a historical snapshot — see header note of that file).

---

## Known unimplemented / problematic features in `ed722dd`

> ⚠ **Historical snapshot (2026-09-05, baseline `ed722dd`).** The table below
> describes the state of `ed722dd`, **not** the current `main`. Since then the
> following rows have been fixed (see `[Unreleased]` for details):
> ternary precedence, match or-patterns, top-level `...rest` destructuring,
> named-argument handling (lock-in decision), silent `import` binding
> derivation, bitwise operators (`&` `|` `^` `~` `<<` `>>`), and
> list/dict comprehensions (verified working at Phase 1 P1-A).
> Still open at time of writing: `\$` interpolation escape, `not`
> keyword, labeled break/continue, `let` without initializer, object-literal
> shorthand, stdlib expansion, DAP server (verify against Vora-LSP repo).

These are real and verifiable in current code; do not write code that
depends on them. Confirmed via syntax review (2026-09) and inspection
of `ed722dd`.

| Feature | Status | Notes |
|---|---|---|
| **List / Dict comprehensions — runtime** | **半坏** | Parser accepts `for x in xs yield x*2` but runtime iterator protocol rejects them. Either re-introduce fully or strip parser. P0/P1. |
| `match` or-patterns `3\|4 =>` | BROKEN | Lexer does not produce `\|` token outside `\|\|`. Write chained `if` instead. |
| Top-level `...rest` in `func` | UNSUPPORTED | Works only inside `Obj` method bodies. |
| Named args with computed value `f(a = 1+2)` | BROKEN | Parser's 2-token lookahead eats the expression. |
| `import "foo-bar.V"` | BROKEN → silent subtraction | Binding name derived from path string; `-` is operator, not part of identifier. |
| `?:` precedence vs `\|\|` | WRONG ORDERING | `a \|\| b ? c : d` parses as `(a \|\| b) ? c : d`, opposite of C/JS/Python. |
| `^`, `\|`, `&`, `~` (bitwise) | NOT IMPLEMENTED | Token list comment in `parser.h` retains dead BITWISE_OR/XOR/AND levels. |
| `**`, `**=` | `**` works; `**=` added Phase 1 | Assignment form (`**=`) shipped 2026-09; `**` yields float. |
| Walrus `:=` | NOT IMPLEMENTED | (Listed as "non-goal" in roadmap.) |
| Char type | NOT IMPLEMENTED | (Listed as "non-goal".) |
| `std/http`, `std/net`, `std/path`, `std/process` | NOT IMPLEMENTED | Modules do not exist in `std/`. |
| `vpm` package manager | NOT IMPLEMENTED | (Skipped per user direction 2026-09.) |
| `vora test`, `vora bench`, `vora check` CLI subcommands | NOT IMPLEMENTED | Only `run` / `fmt` / `eval` exist. |
| DAP debugger server | NOT IMPLEMENTED | VM debug hooks exist; no DAP server either here or in Vora-LSP as of 2026-08. |

[0.27.0]: #0270---2026-07
[0.26.0]: #0260---2026-06
[0.25.0]: #0250---2026-04
[0.24.0]: #0240---2026-03
