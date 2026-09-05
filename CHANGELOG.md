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

### Fixed
- Build portability (GCC / MinGW):
  - `src/ast/binding_pattern.h`: explicit `#include <cstdint>`
    (`enum class BindingKind : uint8_t`).
  - `src/gc/gc_heap.cpp`: explicit `#include <algorithm>`
    (`std::remove_if` in compaction/collection passes).
  - Previously relied on transitive includes from MSVC `<vector>` /
    `<optional>`. GCC does not transitively re-expose `std::uint8_t`
    nor `std::remove_if`, causing compile errors under
    `build.sh -G "MinGW Makefiles"`.

### Documentation
- `CHANGELOG.md` added — first formal changelog in the project, backfilled
  through v0.27 per `git log` and the actual code on `main`.
- `docs/00-roadmap.md` rewritten to reflect real shipped state, not aspirational.
- `docs/08-已实现功能总结.md` annotated with a header note that its body is a
  historical version-by-version record; current state is in `CHANGELOG.md`.

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
| `**`, `**=` | NOT IMPLEMENTED |  |
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
