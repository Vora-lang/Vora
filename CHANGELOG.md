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
> snapshot commit (`3f4f71e`, an initial commit with no parent), so the
> original per-fix commit hashes cited in older roadmap revisions
> (`970caa4`, `61366f9`, `830a2f2`, `c3bc77a`) are no longer resolvable from
> `main` — `main` is a fresh 14-commit history unrelated to the original one.
> This section is the authoritative record.
>
> **Pre-rewrite history archive:** the original history survives in two
> remote branches that share no common ancestor with `main` and must be kept:
> `origin/Bytecode-VM` (131 commits, tip 2026-06-27) and
> `origin/AST-interpreter` (24 commits, tip 2026-05-31). Deleting them
> orphans the last reachable copies of old commits (e.g. `f550cce`, the v0.26
> commit cited below). All other hashes cited in this file were never pushed
> to those branches and are unrecoverable.
>
> **Historical spelling:** entries describing releases up to v0.27 and the
> `ed722dd` snapshot below spell the class keyword as `Obj`, which was its
> name at the time. Those entries are kept verbatim; the keyword is `class`
> from the next release onward.

### Breaking changes
- **The class-declaration keyword is `class`, not `Obj`** (syntax-review
  #3.6). Migration is a mechanical rename: `Obj Name(...) { }` becomes
  `class Name(...) { }`, including the inheritance form
  (`Obj Dog : Animal(name)` → `class Dog : Animal(name)`).
  Why `class` rather than simply lowercasing to `obj`: measured over
  `std/`, `examples/` and `tests/`, `obj` is already used as an identifier
  28 times — including the natural `let obj = ...` instance-variable name —
  so reserving it would break real code, whereas `class` appears only
  inside comments and string literals and so breaks nothing. `class` is
  also how both JS and Wren (the language's stated OOP influences) spell
  this keyword, and a lowercase `obj` reads like a variable rather than a
  declaration keyword. `std/` does not use the keyword at all.
  Note: the `Obj` spelling throughout the historical entries above is
  intentional — see the historical-spelling note.
- **A `match` with no matching arm now raises instead of returning `null`**
  (syntax-review #3.8). `match 99 { 1 => "one" }` used to evaluate to
  `null`, which then propagated far from its origin before anything failed —
  a silent-wrong-meaning defect of the kind design principle 7 forbids.
  It now throws a catchable runtime error naming the unmatched value:
  `no match arm matched the value: 99`, reported at the line of the `match`.
  Migration: add a `_` arm (recommended — a wildcard always matched
  before, so this is the idiomatic spelling of the old behaviour), or
  handle the error with `try`/`catch` if the fallthrough is intentional.
  Matches that already end in `_`, or whose arms cover the value, are
  unaffected. Defers in the enclosing function still run, as with `throw`.

### Added
- **Labeled `break` / `continue`** (Phase 1 closing item, syntax-review #2.8):
  `name: <loop>` names a loop, and `break name` / `continue name` target it from
  any nesting depth, so nested loops no longer need boolean flags to exit:

      outer: for i in [1, 2, 3] {
          for j in [1, 2, 3] {
              if (j == 2) { continue outer }
              if (i == 3) { break outer }
          }
      }

  All four loop forms take a label. No new AST node was needed — the loop
  statements gained an optional `label` and break/continue an optional
  `targetLabel` — so the visitor interface and its four implementations are
  untouched; only the formatter had to learn about labels. Unlabeled
  break/continue is unchanged, byte for byte.

  Rejected at compile time: a label pointing at no enclosing loop, the same
  label twice on one nesting chain, and a label on anything that is not a loop
  (`x: if (...) { }` — with no `goto` such a label could never be a target).
  A label after `break` / `continue` counts only when it is on the same line as
  the keyword, matching the Go-style ASI rule, so `break` followed by `foo()` on
  the next line is still two statements. Loops that are not nested may reuse a
  name.

  ⚠ **Breaking change for one previously-accepted form:** `break <identifier>`
  on a single line used to be legal and parsed as two statements with the second
  one unreachable (`break` transfers control unconditionally). It now parses as
  a label reference, so such code becomes a compile error. A full scan of
  `tests/`, `examples/` and `std/` found no occurrence.

  Design: `docs/17-labeled-break-continue-design.md`. Tests:
  `tests/runtime/test_labeled_break_continue.va`, parser/compiler units for all
  four negative cases, and label cases in the formatter round-trip test.
  Editor syntax updated in all three front-ends (VS Code tmLanguage, Zed
  tree-sitter incl. the rebuilt WASM, and the website playground, which needed
  no change).

- **`\$` interpolation escape** (Phase 1, syntax-review #2.10): `"\${x}"`
  now yields the literal text `${x}` instead of interpolating. The lexer
  stores an escaped dollar as an in-band sentinel (`kEscapedDollar`,
  `src/lexer/token.h`); the compiler treats a real `$` as an interpolation
  start and resolves the sentinel back to `$` at `emitConstant` — the single
  choke point, so compile-time constant folding is covered too. The
  formatter emits `\$` for the sentinel, so `vora fmt` round-trips without
  changing meaning. `"\\${x}"` (escaped backslash + interpolation → `\5`)
  remains distinct from `"\${x}"` (literal).
  Tests: `tests/runtime/test_string_escape.va` + formatter round-trip cases.
- **`not` keyword** (Phase 1, syntax-review #2.9): `not` is an alias for `!`
  — the same `TokenType::NOT`, so precedence and semantics are identical to
  the symbol operator, completing the `and`/`or`/`not` keyword set. Like `!`
  (and unlike Python), it binds tighter than comparison, so `not x > 0`
  parses as `(not x) > 0` — documented explicitly in EBNF §5.1 and
  USER_GUIDE. Editor highlighting for `and`/`or`/`not` added to all three
  front-ends (the Vora-LSP tmLanguage and Zed tree-sitter grammar also
  gained `and`/`or`, which were previously parsed as plain identifiers —
  a silent mis-parse in the editor grammar).
  Tests: `tests/runtime/test_not_operator.va` + lexer units.
- **Object literal shorthand `{x}`** (Phase 1, syntax-review #2.11): a bare
  identifier entry expands to `{x: x}`, matching the shorthand already
  accepted in destructuring patterns. Recognized only when the identifier is
  followed by `,` or `}`, so dict comprehensions and other forms are
  unaffected (`{a: 1, b}` mixes explicit and shorthand entries). Previously
  an error ("Expected ':' after dict key").
  Tests: `tests/runtime/test_dict_shorthand.va` + 4 parser units including a
  negative case for `{a + b}` in expression position.
- **`let x` without an initializer** (Phase 1, syntax-review #2.7): the
  binding defaults to `null`, enabling declare-then-assign in branches
  (`let x; if (c) { x = 1 }`). With a type annotation the declaration is
  equivalent to `let x:T = null`, so the annotation's coercion still applies
  (`let a:int` → `0`). `const` still requires an initializer. Previously a
  parse error ("Expected '=' after variable name").
  Tests: `tests/runtime/test_declarations.va` + parser units (positive and
  the `const` negative case).
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
- **A `finally` could not see the locals the exit had just discarded**
  (`c8211c6`, pre-existing, **syntactically valid but semantically invisible**):
  break/continue popped the locals they abandoned and *then* jumped to the
  finally replay, so a finally that read one of them evaluated its own
  expressions into the recycled slots. The finally's pushes start at the
  abandoned region's base, so it clobbered the *lowest* abandoned local first —
  which is why the loop variable was the visible symptom:

      let trace = ""
      for i in [1, 2, 3] {
          try { trace = trace + "b" + toString(i); if (i == 2) { break } }
          finally { trace = trace + "f" + toString(i) }
      }
      print(trace)   // b1f1b2f<native fn toString>, expected b1f1b2f2

  The same applied to the C-style `for` initializer local and to the first local
  of a `while` / `do-while` body, and the corrupted value fed back into the loop,
  so those loops also ran the wrong number of iterations (`b1f1b2fb1f1b2f`
  instead of `b1f1b2f2`). The exit is now split — a pre-jump enters the finally
  chain while the locals are still live, the chain falls back to a cleanup pad,
  and only then does the real exit jump run — which is also what makes labeled
  exits through a finally correct.
  Tests: `tests/runtime/test_finally_locals_visibility.va`.
- **Non-local exits written in a `catch` block skipped the enclosing `finally`**
  (`53f63e0`, pre-existing): `visitTryStmt` only collected exits emitted while
  compiling the *try body*, and it released this try's finally-nesting count
  before compiling the catch — so `break`, `continue` and `return` written in a
  catch block behaved as if no finally were pending:

      let log = ""
      let i = 0
      while (i < 3) {
          i = i + 1
          try { if (i == 1) { throw "x" } }
          catch (e) { log = log + "c"; break }
          finally { log = log + "f" }
      }
      print(log)   // "c", expected "cf"

  Both the try body and the catch fall through to the finally, so the count now
  stays up until the catch is finished (released before the finally block
  itself, so an exit written in a finally is never routed back into it), and
  pending exits are collected again after the catch body.
  Tests: `tests/runtime/test_finally_from_catch.va`.
- **`return` inside a try with no `finally` of its own was dropped**
  (`c8211c6`, pre-existing): such a return was removed from the pending-jump
  list and then discarded, leaving its `OP_JUMP` operand at the 0xFF
  placeholder. Nested in an outer try that had a finally this aborted with
  "Unknown opcode":

      func f() {
          try { try { return 1 } catch (e) { } } finally { print("outer finally") }
          return 2
      }

  Return jumps are now captured only by a try that has a finally of its own to
  replay them through; otherwise they stay pending for an enclosing finally.
  Tests: covered by `tests/runtime/test_finally_locals_visibility.va` and
  `tests/runtime/test_finally_from_catch.va`.
- **Which finallys a non-local exit owes was computed too coarsely**
  (`53f63e0`, introduced and fixed within the same unreleased work): an exit was
  handed to an enclosing finally whenever one merely existed lexically, but a
  finally that *encloses* the target loop is not owed by the exit — the loop
  ends normally inside its try. Two consequences: a finally-less try between the
  exit and a finally swallowed the pending exit so the finally never ran ("b1"
  instead of "b1f"), and a break could fall through into a replay continuation
  instead of exiting (`i1i2i3o` for a loop that should stop at the first
  iteration). LoopContext now records the finally depth at loop entry, and an
  exit owes exactly the finallys between it and its target.
  Tests: `tests/runtime/test_finally_handoff.va`.
- **`break` / `continue` did not close upvalues** (`8696cb5`, pre-existing):
  both statements discard the loop body's locals with `OP_POPN` but, unlike
  `endScope()` (`src/vm/compiler.cpp:310`), never emitted `OP_CLOSE_UPVALUE`. A
  closure that captured a loop-body local therefore kept pointing at the raw
  stack slot, and the next iteration — or any later statement in the same frame
  — overwrote it. The closure silently returned the wrong value: with
  `for i in [1, 2] { let v = i * 10; fns += [func() { return v }]; if (i == 1) { continue } }`,
  `fns[0]()` returned `20` instead of `10`. `break` was affected the same way
  (breaking out of an inner loop while the outer loop keeps running returned
  `21` for both closures instead of `11` and `21`), and so were two cases the
  design doc had not predicted: the initializer local of a C-style `for`
  (`for (let k = ...)` → `777` instead of `0`) and the loop variable of a
  `for ... in` (`888` instead of `5`) — both are loop infrastructure that sits at
  the loop's own scope depth, invisible to the body-local scan. Fixed by the new
  `emitLoopExitCleanup()`, which emits `OP_CLOSE_UPVALUE` for every captured
  local a jumping loop exit discards — body locals and the target loop's
  infrastructure locals alike — before the `OP_POPN`.
  Tests: `tests/runtime/test_loop_closure.va` (every case confirmed failing
  before the fix).
- **`break` / `continue` popped an enclosing `try`'s catch handler**
  (`ad57950`, pre-existing, **syntactically valid but semantically invisible**):
  both emitted one `OP_POP_CATCH` per lexically enclosing `try` block, but a jump
  out of a loop only leaves the `try` blocks registered *inside* that loop.
  Every `try` enclosing the loop had its handler popped, so a later `throw` in
  the same `try` block escaped uncaught — or was silently caught by an outer
  handler instead of the intended one. This was not a corner case: a
  `for x in [...]` loop's synthetic `StopIteration` break runs on **every normal
  loop exit**, so *any* `for-in` inside a `try` destroyed that `try`'s handler;
  list and dict comprehensions desugar the same way, and with two nested `try`
  blocks around one loop the outer `catch` swallowed exceptions meant for the
  inner one. Observed:

      func f() {
          let n = 0
          try {
              let i = 0
              while (i < 5) { n = n + 1; break }
              throw "boom"          // uncaught: the try's handler was already gone
          } catch (e) { n = n + 1000 }
          return n
      }
      print(f())    // expected 1001, got an uncaught exception

  Fixed by snapshotting `tryNesting` into `LoopContext::tryDepthAtEntry` at loop
  entry and popping `tryNesting - tryDepthAtEntry` handlers, i.e. exactly the
  handlers registered inside the loops being exited.
  Tests: `tests/runtime/test_loop_try_interaction.va`.
- **`break` / `continue` jumps dropped by a `try` with no `finally`**
  (`2a4668e`, pre-existing): `visitTryStmt` collects the break/continue jumps
  emitted inside its try block so it can re-route them through the `finally`.
  With no `finally` there was nothing to re-route, and the collected jumps were
  simply discarded — left in the chunk with their `0xFF` placeholder operands
  and never back-patched. A `break` inside a `try` (no `finally`) therefore
  jumped into the middle of an unrelated instruction and the VM aborted with
  `Unknown opcode`. Affected every `break` written inside such a `try`, for all
  loop kinds, plus `continue` in the loops whose continue target is a forward
  jump (do-while, C-style `for`). Fixed by putting the captured jumps back into
  their loop context when there is no `finally`.
  Tests: `tests/runtime/test_try_loop_jump_routing.va`.
- **`continue` silently skipped an enclosing `finally`** (`77aa5ad`,
  pre-existing): for the loop kinds whose continue target was the loop start
  (`while`, `for ... in`), `continue` was compiled as a direct **backward**
  `OP_LOOP` to the condition. That jump leaves the continue site entirely, so
  `visitTryStmt` never saw it and the `finally` was skipped for that iteration:

      let n = 0
      let i = 0
      while (i < 3) {
          i = i + 1
          try { n = n + 1; continue } finally { n = n + 10 }
      }
      print(n)   // 3, expected 33

  `continue` is now a forward jump for every loop kind, with `while` and
  `for ... in` given a real continue target at their back edge (do-while and the
  C-style `for` already worked this way). This also makes it possible to
  intercept and re-route a `continue` at all, which labeled `continue` across a
  `finally` will need.
  Tests: `tests/runtime/test_continue_finally.va`.
- **`finally` replay blocks were reachable from the normal path** (`59dd146`,
  pre-existing, **syntactically valid but semantically invisible**): when a `try`
  block contained a `break`, `continue` or `return`, `visitTryStmt` emitted a
  replay of the `finally` bytecode for each such exit — inline, immediately after
  the `finally` compiled for the normal path, with nothing jumping over them. A
  try body that completed normally therefore fell straight into the replay
  blocks. The `finally` ran twice on iterations that did not exit non-locally,
  and on iterations that did, the fall-through reached the `break` replay and
  jumped out of the loop early:

      let n = 0
      let i = 0
      while (i < 3) {
          i = i + 1
          try { n = n + 1; if (i == 2) { break } }
          finally { n = n + 10 }
      }
      print(n)   // 21, expected 22 — and only one iteration ran

  The normal path now jumps over the replay blocks, so the `finally` runs exactly
  once per exit route.
  Tests: `tests/runtime/test_finally_replay_isolation.va`.

> **Note on the five fixes above.** All five are pre-existing defects found while
> preparing the labeled `break`/`continue` work (`docs/VORA_SYNTAX_REVIEW.md` §2.8),
> and all five silently changed program meaning rather than failing loudly — the
> class the project principles call unacceptable. They were fixed before any
> label work, one commit each. Notably, the existing
> `tests/interpreter/test_edge_cases.va` "break in try-finally" and "continue in
> try-finally" cases passed *both before and after* these fixes: they only assert
> that the `finally` ran, never that it ran exactly once or that the loop exited
> on the right iteration, which is why nothing caught them. The remaining known
> defect in the same area — a `finally` that reads a loop-body local sees a
> recycled slot, because the locals are popped before the replay runs — is
> recorded in `docs/17-labeled-break-continue-design.md` §2.6 / §4.3-F and is not
> fixed yet.
- **Compile-time string folding swallowed interpolation** (pre-existing,
  found while implementing `\$`): `"a" + "${x}"` folded both string operands
  by concatenating their raw values, bypassing the `${...}` handling in
  `visitLiteralExpr`, so it produced the literal text `${x}` instead of the
  interpolated value. Folding now defers when either operand contains `${`.
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
  Tracked as P0/P1 in `docs/VORA_SYNTAX_REVIEW.md`.
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
> Still open at time of writing: labeled break/continue, trailing commas,
> stdlib expansion, DAP server (verify against Vora-LSP repo).

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
