// tests/unit/test_bigint.cpp — Arbitrary-precision integer unit tests
//
// Tests three layers:
//   1. The BigInt arithmetic core (bigint.h) — value-level correctness.
//   2. Value integration — boxing/demotion, fitsInt64/toInt64Exact, hashing and
//      equality normalization across representations.
//   3. The failure paths that must NOT be silent: asInt() on a boxed integer,
//      an oversized literal at compile time, and GC reachability of a boxed
//      constant in a constant pool.

#include "doctest.h"
#include "gc/gc_heap.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "runtime/bigint.h"
#include "runtime/builtins.h"
#include "runtime/runtime_error.h"
#include "runtime/value.h"
#include "vm/chunk.h"
#include "vm/compiler.h"
#include "vm/value_ops.h"
#include "vm/vm.h"

#include <string>
#include <vector>

using namespace vora;

namespace {

/// @brief Parse a decimal string into a BigInt, requiring success.
///        Handles a leading '-' because fromChars takes digits only.
BigInt big(const std::string& dec) {
    const bool neg = !dec.empty() && dec[0] == '-';
    const std::string digits = neg ? dec.substr(1) : dec;
    BigInt b;
    REQUIRE(BigInt::fromChars(digits.c_str(), digits.size(), 10, b));
    if (neg && !b.limbs.empty()) b.negative = true;
    return b;
}

/// @brief Build a boxed (GcBigInt) Value from a decimal string.
Value boxed(const std::string& dec) {
    return Value(GcHeap::instance().alloc<GcBigInt>(big(dec)));
}

/// @brief Compile and interpret a source string, returning the VM for
///        inspection.  Mirrors the helper in test_vm.cpp.
std::pair<InterpretResult, VM> runSource(const std::string& src) {
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    Compiler compiler(reporter);
    Chunk chunk = compiler.compile(prog.get());
    REQUIRE_FALSE(compiler.hadError);
    VM vm;
    vm.errorReporter = &reporter;
    vm.initGlobals(compiler.getGlobalNames());
    registerBuiltins(vm);
    InterpretResult result = vm.interpret(chunk);
    return {result, std::move(vm)};
}

} // namespace

// ============================================================================
// 1. BigInt core
// ============================================================================

TEST_CASE("bigint_from_int64_roundtrip") {
    CHECK(BigInt::fromInt64(0).isZero());
    CHECK(BigInt::fromInt64(0).toDecimal() == "0");
    CHECK(BigInt::fromInt64(42).toDecimal() == "42");
    CHECK(BigInt::fromInt64(-42).toDecimal() == "-42");
    CHECK(BigInt::fromInt64(42).toInt64() == 42);
    CHECK(BigInt::fromInt64(-42).toInt64() == -42);
    // INT64_MIN cannot be negated, so it needs care.
    CHECK(BigInt::fromInt64(INT64_MIN).toInt64() == INT64_MIN);
    CHECK(BigInt::fromInt64(INT64_MIN).toDecimal() == "-9223372036854775808");
    CHECK(BigInt::fromInt64(INT64_MAX).toDecimal() == "9223372036854775807");
}

TEST_CASE("bigint_parse_bases") {
    CHECK(big("35184372088832").toDecimal() == "35184372088832");
    CHECK(big("123456789012345678901234567890").toDecimal() == "123456789012345678901234567890");
    BigInt hexVal;
    REQUIRE(BigInt::fromChars("ffffffffffffffffffffffffffffffff", 32, 16, hexVal));
    CHECK(hexVal.toDecimal() == "340282366920938463463374607431768211455");
    // Rejects digits out of range for the base, and empty input.
    BigInt tmp;
    CHECK_FALSE(BigInt::fromChars("", 0, 10, tmp));
    CHECK_FALSE(BigInt::fromChars("2", 1, 2, tmp));
    CHECK_FALSE(BigInt::fromChars("g", 1, 16, tmp));
}

TEST_CASE("bigint_overflow_flag_for_oversized_literals") {
    // A literal larger than the limb cap must be refused, not truncated.
    std::string huge(80000, '9');
    BigInt tmp;
    CHECK_FALSE(BigInt::fromChars(huge.c_str(), huge.size(), 10, tmp));
}

TEST_CASE("bigint_arithmetic_matches_known_values") {
    // 2^45 * 2^45 == 2^90
    CHECK(BigInt::mul(big("35184372088832"), big("35184372088832")).toDecimal()
          == "1237940039285380274899124224");
    CHECK(BigInt::add(big("9223372036854775807"), BigInt::fromInt64(1)).toDecimal()
          == "9223372036854775808");
    CHECK(BigInt::sub(BigInt::fromInt64(0), big("9223372036854775808")).toDecimal()
          == "-9223372036854775808");
    CHECK(BigInt::negate(big("18446744073709551616")).toDecimal()
          == "-18446744073709551616");
}

TEST_CASE("bigint_divmod_is_truncated_like_c") {
    BigInt q, r;
    // Python's // and % floor; Vora's must truncate, matching C and the
    // pre-existing inline `%` behaviour.
    REQUIRE(BigInt::divModTrunc(BigInt::fromInt64(-7), BigInt::fromInt64(3), q, r));
    CHECK(q.toDecimal() == "-2");
    CHECK(r.toDecimal() == "-1");

    REQUIRE(BigInt::divModTrunc(BigInt::fromInt64(7), BigInt::fromInt64(-3), q, r));
    CHECK(q.toDecimal() == "-2");
    CHECK(r.toDecimal() == "1");

    REQUIRE(BigInt::divModTrunc(BigInt::fromInt64(-7), BigInt::fromInt64(-3), q, r));
    CHECK(q.toDecimal() == "2");
    CHECK(r.toDecimal() == "-1");

    // Division by zero is reported, not returned as some default.
    CHECK_FALSE(BigInt::divModTrunc(big("5"), BigInt::fromInt64(0), q, r));
}

TEST_CASE("bigint_shifts_are_floor_and_exact") {
    CHECK(BigInt::shiftLeftBits(BigInt::fromInt64(1), 100).toDecimal()
          == "1267650600228229401496703205376");
    // Arithmetic (floor) right shift: -1 >> 100 stays -1, -7 >> 1 is -4.
    CHECK(BigInt::shiftRightBits(BigInt::fromInt64(-1), 100).toDecimal() == "-1");
    CHECK(BigInt::shiftRightBits(BigInt::fromInt64(-7), 1).toDecimal() == "-4");
    CHECK(BigInt::shiftRightBits(BigInt::fromInt64(-8), 1).toDecimal() == "-4");
    CHECK(BigInt::shiftRightBits(big("18446744073709551616"), 3).toDecimal()
          == "2305843009213693952");
}

TEST_CASE("bigint_fits_int64_boundaries") {
    CHECK(BigInt::fromInt64(INT64_MAX).fitsInt64());
    CHECK(BigInt::fromInt64(INT64_MIN).fitsInt64());
    CHECK(big("9223372036854775807").fitsInt64());
    // 2^63 is one past INT64_MAX, so as a *positive* value it does not fit.
    CHECK_FALSE(big("9223372036854775808").fitsInt64());
    CHECK_FALSE(big("9223372036854775809").fitsInt64());
    CHECK_FALSE(big("18446744073709551616").fitsInt64());
    CHECK_FALSE(big("-9223372036854775809").fitsInt64());
    // -2^63 is exactly INT64_MIN, so it does fit — the asymmetry of two's
    // complement means the negative side has one more representable value.
    CHECK(big("-9223372036854775808").fitsInt64());
    CHECK(big("-9223372036854775808").toInt64() == INT64_MIN);
}

TEST_CASE("bigint_from_double_is_exact") {
    // 2^70 is exactly representable as a double; the decomposition must be exact.
    CHECK(BigInt::fromDoubleTrunc(1180591620717411303424.0).toDecimal()
          == "1180591620717411303424");
    CHECK(BigInt::fromDoubleTrunc(-1180591620717411303424.0).toDecimal()
          == "-1180591620717411303424");
    CHECK(BigInt::fromDoubleTrunc(0.0).isZero());
    // Fractional parts truncate toward zero.
    CHECK(BigInt::fromDoubleTrunc(2.75).toDecimal() == "2");
    CHECK(BigInt::fromDoubleTrunc(-2.75).toDecimal() == "-2");
}

TEST_CASE("bigint_to_double_rounds") {
    CHECK(BigInt::fromInt64(42).toDouble() == 42.0);
    CHECK(big("1180591620717411303424").toDouble() == 1180591620717411303424.0);
}

// ============================================================================
// 2. Value integration: boxing, demotion, the single-representation invariant
// ============================================================================

TEST_CASE("value_inline_range_is_never_boxed") {
    // Invariant: everything representable inline must build an inline Int, or a
    // value would acquire two identities depending on how it was produced.
    CHECK(Value(static_cast<int64_t>(0)).isInt());
    CHECK(Value(INT47_MAX).isInt());
    CHECK(Value(INT47_MIN).isInt());
    CHECK(Value(static_cast<int64_t>(-1)).isInt());
    CHECK_FALSE(Value(INT47_MAX).isBigInt());
}

TEST_CASE("value_out_of_range_boxes_instead_of_clamping") {
    // These all used to clamp to INT47_MAX (or INT47_MIN) silently.
    const int64_t outOfRange[] = {
        35184372088832LL,                        // INT47_MAX + 1
        INT47_MIN - 1,
        9223372036854775807LL,                   // INT64_MAX
        INT64_MIN,
    };
    for (int64_t v : outOfRange) {
        Value box = Value(v);
        CHECK(box.isBigInt());
        CHECK_FALSE(box.isInt());
        // The stored value is the exact input, not a boundary.
        int64_t back = 0;
        CHECK(box.toInt64Exact(back));
        CHECK(back == v);
    }
    CHECK(Value(static_cast<int64_t>(35184372088832LL)).asBigInt()->value.toDecimal()
          == "35184372088832");
    CHECK(Value(INT64_MAX).asBigInt()->value.toDecimal() == "9223372036854775807");
}

TEST_CASE("value_fits_and_to_int64_exact") {
    CHECK(Value(static_cast<int64_t>(5)).fitsInt64());
    int64_t small = 0;
    CHECK(Value(static_cast<int64_t>(5)).toInt64Exact(small));
    CHECK(small == 5);
    CHECK_FALSE(Value(1.5).fitsInt64());          // doubles are not integers
    CHECK_FALSE(Value(nullptr).fitsInt64());
    CHECK_FALSE(Value(true).fitsInt64());

    Value big50 = Value(static_cast<int64_t>(35184372088832LL));
    CHECK(big50.fitsInt64());
    int64_t got = 0;
    CHECK(big50.toInt64Exact(got));
    CHECK(got == 35184372088832LL);

    Value huge = boxed("1267650600228229401496703205376");  // 2^100
    CHECK_FALSE(huge.fitsInt64());
    CHECK_FALSE(huge.toInt64Exact(got));
    CHECK(got == 35184372088832LL);  // untouched on failure
}

TEST_CASE("value_arithmetic_boxes_and_demotes") {
    // Overflow the inline range: the result boxes.
    Value over = intAddExact(Value(INT47_MAX), Value(static_cast<int64_t>(1)));
    CHECK(over.isBigInt());
    CHECK(over.asBigInt()->value.toDecimal() == "35184372088832");

    // Come back into range: the result demotes to an inline Int.
    Value back = intSubExact(over, Value(static_cast<int64_t>(1)));
    CHECK(back.isInt());
    CHECK_FALSE(back.isBigInt());
    CHECK(back.asInt() == INT47_MAX);

    // A product that overflows int64 entirely.
    Value prod = intMulExact(Value(static_cast<int64_t>(35184372088832LL)),
                             Value(static_cast<int64_t>(35184372088832LL)));
    CHECK(prod.isBigInt());
    CHECK(prod.asBigInt()->value.toDecimal() == "1237940039285380274899124224");

    // Negating the inline minimum leaves the range (2^45 > INT47_MAX).
    Value neg = intNegateExact(Value(INT47_MIN));
    CHECK(neg.isBigInt());
    CHECK(neg.asBigInt()->value.toDecimal() == "35184372088832");
}

TEST_CASE("value_modulo_is_exact_and_truncated") {
    Value a = boxed("18446744073709551616");  // 2^64
    Value r = intModExact(a, Value(static_cast<int64_t>(1000000000)));
    CHECK(r.isInt());
    CHECK(r.asInt() == 709551616);

    // Sign follows the dividend, as for inline ints.
    Value neg = intModExact(Value(static_cast<int64_t>(-7)), Value(static_cast<int64_t>(3)));
    CHECK(neg.asInt() == -1);
}

TEST_CASE("value_asInt_on_bigint_never_truncates") {
    Value huge = boxed("1267650600228229401496703205376");
#ifdef NDEBUG
    // Release: a catchable error, never a truncated value.
    CHECK_THROWS_AS(huge.asInt(), RuntimeError);
#else
    // Debug: the call is an assertion failure, which would abort the test
    // process, so only the safe accessors can be exercised here.
    CHECK(huge.isBigInt());
    CHECK_FALSE(huge.fitsInt64());
#endif
}

TEST_CASE("bigint_bitwise_is_infinite_twos_complement") {
    // Python semantics: a negative operand behaves as if it had infinitely many
    // leading 1 bits, so these hold at any magnitude.
    const BigInt minus6 = BigInt::fromInt64(-6);
    const BigInt three = BigInt::fromInt64(3);
    CHECK(BigInt::bitwise(minus6, three, BigInt::BitOp::And).toDecimal() == "2");
    CHECK(BigInt::bitwise(BigInt::fromInt64(-1), BigInt::fromInt64(255),
                          BigInt::BitOp::And).toDecimal() == "255");
    CHECK(BigInt::bitwise(BigInt::fromInt64(-1), big("18446744073709551616"),
                          BigInt::BitOp::Or).toDecimal() == "-1");
    CHECK(BigInt::bitwise(BigInt::fromInt64(0), BigInt::fromInt64(-1),
                          BigInt::BitOp::Xor).toDecimal() == "-1");

    // `~a == -a - 1`, exactly as in Python.
    CHECK(BigInt::invert(BigInt::fromInt64(0)).toDecimal() == "-1");
    CHECK(BigInt::invert(BigInt::fromInt64(-1)).toDecimal() == "0");
    CHECK(BigInt::invert(BigInt::fromInt64(5)).toDecimal() == "-6");
    CHECK(BigInt::invert(big("35184372088832")).toDecimal() == "-35184372088833");
    CHECK(BigInt::invert(big("-123456789012345678901234567890")).toDecimal()
          == "123456789012345678901234567889");

    // A result inside the inline range is still a correct BigInt value.
    CHECK(BigInt::bitwise(big("35184372088833"), big("35184372088833"),
                          BigInt::BitOp::Xor).isZero());
}

TEST_CASE("bigint_shift_beyond_64_bits_is_exact") {
    // The whole point of the big-integer path: no 64-bit truncation.
    CHECK(BigInt::shiftLeftBits(BigInt::fromInt64(35184372088832), 40).toDecimal()
          == "38685626227668133590597632");   // 2^85
    CHECK(BigInt::shiftRightBits(big("38685626227668133590597632"), 40).toDecimal()
          == "35184372088832");
    // Consistency: shifting back and forth is the identity.
    CHECK(BigInt::shiftRightBits(BigInt::shiftLeftBits(big("123456789012345678901234567890"), 137), 137)
              .toDecimal() == "123456789012345678901234567890");
}

// ============================================================================
// 3. Equality / hashing normalization across representations
// ============================================================================

TEST_CASE("numeric_equality_is_exact_not_double_rounded") {
    Value a = boxed("1180591620717411303424");  // 2^70
    Value b = boxed("1180591620717411303425");  // 2^70 + 1
    // Both round to the same double, but they are not the same number.
    CHECK(a.asDouble() == b.asDouble());
    CHECK_FALSE(valuesEqual(a, b));
    CHECK_FALSE(numericValuesEqual(a, b));
    CHECK(numericValuesCompare(a, b) < 0);
    CHECK(numericValuesCompare(b, a) > 0);
    CHECK(numericValuesCompare(a, a) == 0);
}

TEST_CASE("numeric_equality_across_representations") {
    // inline int vs boxed int with the same value (boxed value fits int64)
    Value boxedFit = boxed("35184372088832");
    Value arithFit = intAddExact(Value(INT47_MAX), Value(static_cast<int64_t>(1)));
    CHECK(valuesEqual(boxedFit, arithFit));

    // boxed int vs integral double
    CHECK(numericValuesEqual(boxedFit, Value(35184372088832.0)));
    CHECK(numericValuesEqual(Value(Value(static_cast<int64_t>(42))), Value(42.0)));
    CHECK(numericValuesEqual(Value(static_cast<int64_t>(0)), Value(0.0)));
    CHECK(numericValuesEqual(Value(static_cast<int64_t>(0)), Value(-0.0)));

    // Non-integral doubles are never equal to an integer.
    CHECK_FALSE(numericValuesEqual(Value(static_cast<int64_t>(2)), Value(2.5)));
    CHECK(numericValuesCompare(Value(static_cast<int64_t>(2)), Value(2.5)) < 0);
    CHECK(numericValuesCompare(Value(static_cast<int64_t>(3)), Value(2.5)) > 0);

    // NaN is equal to nothing, including itself.
    const double nan = 0.0 / 0.0;
    CHECK_FALSE(numericValuesEqual(Value(nan), Value(nan)));
}

TEST_CASE("numeric_compare_handles_infinities") {
    const double inf = 1.0 / 0.0;
    CHECK(numericValuesCompare(Value(static_cast<int64_t>(5)), Value(inf)) < 0);
    CHECK(numericValuesCompare(Value(static_cast<int64_t>(5)), Value(-inf)) > 0);
    CHECK(numericValuesCompare(Value(inf), Value(inf)) == 0);
}

TEST_CASE("hash_is_consistent_for_equal_values_across_representations") {
    ValueHash hash;
    // Same number, four constructions, one hash.
    Value viaLiteral = Value(static_cast<int64_t>(35184372088832LL));
    Value viaArith = intAddExact(Value(INT47_MAX), Value(static_cast<int64_t>(1)));
    Value viaDouble = Value(35184372088832.0);
    CHECK(hash(viaLiteral) == hash(viaArith));
    CHECK(hash(viaLiteral) == hash(viaDouble));

    // Demotion must land on the shared identity too.
    Value demoted = intSubExact(viaLiteral, Value(static_cast<int64_t>(1)));
    CHECK(hash(demoted) == hash(Value(INT47_MAX)));

    // Values that fit int64 hash as int64; larger ones hash as their double.
    Value bigFit = boxed("35184372088832");
    CHECK(hash(bigFit) == hash(Value(static_cast<int64_t>(35184372088832LL))));
    Value bigger = boxed("1180591620717411303424");  // 2^70
    CHECK(hash(bigger) == hash(Value(1180591620717411303424.0)));
}

TEST_CASE("valuesEqual_dispatches_boxed_integers") {
    Value a = boxed("18446744073709551616");
    Value b = boxed("18446744073709551616");
    Value c = boxed("18446744073709551617");
    CHECK(valuesEqual(a, b));
    CHECK_FALSE(valuesEqual(a, c));
    // Boxed vs an unrelated type is not equal.
    CHECK_FALSE(valuesEqual(a, Value(nullptr)));
    CHECK_FALSE(valuesEqual(a, Value(true)));
}

TEST_CASE("vm_bitwise_hybrid_boundary") {
    // Inline operands keep the documented 64-bit contract, including shift
    // clamping; a big operand switches to arbitrary precision.
    auto [result, vm] = runSource(
        "let clamped = 1 << 100;"          // inline: still clamps to 0
        "let exact = 35184372088832 << 40;" // big operand: 2^85, exact
        "let mask = -123456789012345678901234567890 & 255;"
        "let inv = ~35184372088832;"
        "let neg = -6 & 3;"
        "clamped;"
    );
    REQUIRE(result == InterpretResult::OK);
    CHECK(vm.getGlobal("clamped").asInt() == 0);
    CHECK(vm.getGlobal("exact").asBigInt()->value.toDecimal() == "38685626227668133590597632");
    CHECK(vm.getGlobal("mask").asInt() == 46);
    CHECK(vm.getGlobal("inv").asBigInt()->value.toDecimal() == "-35184372088833");
    CHECK(vm.getGlobal("neg").asInt() == 2);
}

TEST_CASE("vm_absurd_big_shift_is_a_catchable_error") {
    // Must be a reported error, not a std::bad_alloc from a terabyte request.
    auto [result, vm] = runSource(
        "let ok = 0;"
        "try { let x = 35184372088832 << 35184372088832; } catch (e) { ok = 1; }"
        "ok;"
    );
    REQUIRE(result == InterpretResult::OK);
    CHECK(vm.getGlobal("ok").asInt() == 1);
}

// ============================================================================
// 4. Non-silent failure paths
// ============================================================================

TEST_CASE("oversized_literal_is_a_compile_error") {
    // ~80000 digits exceeds the limb cap: this must be reported at compile time
    // rather than becoming a rounded double or a truncated value.
    std::string src = "let x = " + std::string(80000, '9') + ";";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    Compiler compiler(reporter);
    compiler.compile(prog.get());
    CHECK(reporter.hadError());
}

TEST_CASE("bigint_literal_parses_and_compiles_fine") {
    const std::string src =
        "let x = 123456789012345678901234567890;"
        "let y = 0xFFFFFFFFFFFFFFFFFFFF;"
        "let z = 0b1;";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    Compiler compiler(reporter);
    compiler.compile(prog.get());
    CHECK_FALSE(compiler.hadError);
}

TEST_CASE("chunk_constant_pool_traces_bigint") {
    // Regression: FunctionPrototype::trace() used to push nothing, so a boxed
    // value living only in a constant pool was invisible to the GC and could be
    // swept while the bytecode still referenced it.
    Chunk chunk;
    Value bigConst = boxed("1267650600228229401496703205376");
    chunk.constants.push_back(bigConst);
    REQUIRE(chunk.constants[0].isBigInt());

    // A FunctionPrototype owns its Chunk; tracing it must yield the constant.
    FunctionPrototype proto;
    proto.chunk.constants.push_back(bigConst);
    std::vector<GcObject*> worklist;
    proto.trace(worklist);
    REQUIRE(worklist.size() == 1);
    CHECK(worklist[0] == proto.chunk.constants[0].asObject());
}

TEST_CASE("bigint_survives_gc_while_referenced_from_a_constant_pool") {
    // End-to-end: a script that builds allocation pressure (forcing minor GCs)
    // and only then loads an oversized literal. If the literal's heap object
    // were not rooted, this would read freed memory.
    const std::string src =
        "let first = 1267650600228229401496703205376;"
        "let s = \"\";"
        "for (let i = 0; i < 40000; i += 1) { s = s + \"abcdefghij\"; }"
        "let second = 1267650600228229401496703205376;"
        "assert(first == second, \"boxed constant survived GC\");"
        "second - first;";
    StderrErrorReporter reporter(src);
    Lexer lexer(src, reporter);
    auto tokens = lexer.scanTokens();
    Parser parser(std::move(tokens), reporter);
    auto prog = parser.parse();
    REQUIRE(prog != nullptr);
    Compiler compiler(reporter);
    Chunk chunk = compiler.compile(prog.get());
    REQUIRE_FALSE(compiler.hadError);

    VM vm;
    vm.errorReporter = &reporter;
    vm.initGlobals(compiler.getGlobalNames());
    registerBuiltins(vm);
    const size_t gcBefore = GcHeap::instance().minorGCCount() +
                            GcHeap::instance().majorGCCount();
    InterpretResult result = vm.interpret(chunk);
    const size_t gcAfter = GcHeap::instance().minorGCCount() +
                           GcHeap::instance().majorGCCount();
    CHECK(result == InterpretResult::OK);
    // Guard against a vacuous test: the allocation pressure must actually have
    // driven at least one collection, or this proves nothing about rooting.
    CHECK(gcAfter > gcBefore);
}
