/**
 * @file bigint.h
 * @brief Arbitrary-precision signed integer for Vora's boxed big-integer support.
 *
 * Implements a sign + magnitude integer with little-endian 64-bit limbs.  This
 * type is deliberately independent of Value, GcObject and the GC so that it can
 * be unit-tested as pure arithmetic; the runtime wraps it in `GcBigInt`
 * (see value.h).
 *
 * ## Representation
 *
 *   - @ref limbs holds the magnitude in base 2^64, least-significant limb first.
 *   - @ref negative is the sign.  Zero is canonical: empty @ref limbs and
 *     `negative == false`, so there is exactly one representation of zero.
 *   - @ref limbs is always normalized — no most-significant zero limbs.
 *
 * ## Range limiting
 *
 * Every operation that can grow a value must keep it within
 * @ref kMaxBigIntLimbs.  This bounds both memory and the cost of the
 * shift-subtract division, and is the mechanism behind the documented rule that
 * an oversized literal is a compile-time error rather than an allocation.
 *
 * ## Division semantics
 *
 * @ref divModTrunc follows C/C++ truncated division: the quotient rounds toward
 * zero and the remainder takes the sign of the dividend.  This matches Vora's
 * existing `%` on inline integers (which is C++ `%`), so extending `%` to
 * BigInt does not change the sign convention that scripts already observe.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vora {

/// Maximum magnitude, expressed in 64-bit limbs.  4096 limbs is 262,144 bits
/// (~78,900 decimal digits) — far beyond any legitimate program, but still
/// small enough that a runaway `x = x * x` loop is caught at a bounded cost.
constexpr size_t kMaxBigIntLimbs = 4096;

/**
 * @brief Arbitrary-precision signed integer (sign + base-2^64 magnitude).
 */
class BigInt {
public:
    /// @brief Sign of the value.  Always `false` when the value is zero.
    bool negative = false;

    /// @brief Magnitude limbs, least-significant first, normalized
    ///        (no trailing zero limbs).  Empty means zero.
    std::vector<uint64_t> limbs;

    BigInt() = default;

    // --- Factories ---

    /// @brief Build a BigInt from a 64-bit signed integer.
    static BigInt fromInt64(int64_t v);

    /**
     * @brief Parse a digit string in the given base.
     *
     * Accepts ASCII digits valid for @p base (0-9a-fA-F as appropriate); the
     * caller is responsible for stripping any `0x`/`0o`/`0b` prefix.  Rejects
     * the empty string, any out-of-base digit, and any value that would exceed
     * @ref kMaxBigIntLimbs.
     *
     * @param s    Digit characters (not NUL-terminated; length is @p n).
     * @param n    Number of characters.
     * @param base Radix, 2..16.
     * @param out  Receives the parsed value on success.
     * @return `true` on success; `false` if invalid or out of range.
     */
    static bool fromChars(const char* s, size_t n, int base, BigInt& out);

    /**
     * @brief Build a BigInt from a finite double, truncating toward zero.
     * @param d A finite double (the caller must reject inf/NaN).
     */
    static BigInt fromDoubleTrunc(double d);

    // --- Predicates and accessors ---

    bool isZero() const { return limbs.empty(); }
    bool isNegative() const { return negative; }
    bool isOdd() const { return !limbs.empty() && (limbs[0] & 1ULL) != 0; }

    /// @brief Drop most-significant zero limbs and canonicalize the sign of zero.
    void normalize();

    /// @brief True when the magnitude would exceed @ref kMaxBigIntLimbs.
    bool exceedsLimit() const { return limbs.size() > kMaxBigIntLimbs; }

    /// @brief True when the value is exactly representable as int64_t.
    bool fitsInt64() const;

    /// @brief Exact conversion to int64_t.  Only valid when fitsInt64() is true.
    int64_t toInt64() const;

    /// @brief Convert to the nearest double (may round; may produce infinity).
    double toDouble() const;

    /// @brief Decimal string, with a leading '-' for negative values.
    std::string toDecimal() const;

    // --- Arithmetic ---

    /// @brief Three-way comparison: -1, 0 or 1.
    static int compare(const BigInt& a, const BigInt& b);

    /// @brief Signed addition.
    static BigInt add(const BigInt& a, const BigInt& b);
    /// @brief Signed subtraction.
    static BigInt sub(const BigInt& a, const BigInt& b);
    /// @brief Signed multiplication.
    static BigInt mul(const BigInt& a, const BigInt& b);
    /// @brief Arithmetic negation.
    static BigInt negate(const BigInt& a);

    /**
     * @brief Truncated division with remainder (C semantics).
     *
     * Computes @p q and @p r such that `a == q * b + r`, @p q rounds toward
     * zero, and @p r has the sign of @p a (and `|r| < |b|`).
     *
     * @return `false` when @p b is zero (no result is written).
     */
    static bool divModTrunc(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);

    // --- Bit helpers ---

    /// @brief Number of significant bits in the magnitude (0 for zero).
    size_t bitLength() const;

    /// @brief Test bit @p i of the magnitude.
    bool bitAt(size_t i) const;

    /// @brief Multiply the magnitude by 2^@p bits, preserving the sign.
    static BigInt shiftLeftBits(const BigInt& a, size_t bits);

    /// @brief Floor-divide by 2^@p bits, preserving the sign (arithmetic shift).
    ///
    /// Floor semantics — not truncation — so that `-1 >> 1` is `-1`, matching
    /// the sign-fill behavior of Vora's inline-integer `>>` and of Python.
    static BigInt shiftRightBits(const BigInt& a, size_t bits);

    // --- Bitwise operations (infinite two's-complement, Python semantics) ---

    /// @brief Which bitwise combination to apply.
    enum class BitOp { And, Or, Xor };

    /**
     * @brief Combine two values bitwise under infinite two's-complement rules.
     *
     * There is no fixed width: a negative operand behaves as though it had an
     * infinite run of leading 1 bits, so `-1 & x == x`, `-1 | x == -1`, and
     * `-6 & 3 == 2` all hold for any magnitude.  This is Python's model, and it
     * is why a shift by 100 bits is exact rather than clamped.
     *
     * @param a  Left operand.
     * @param b  Right operand.
     * @param op Which operation to apply.
     * @return The exact result.
     */
    static BigInt bitwise(const BigInt& a, const BigInt& b, BitOp op);

    /// @brief Bitwise complement: `~a == -a - 1` (Python semantics).
    static BigInt invert(const BigInt& a);

private:
    /// @brief Compare magnitudes only (ignores signs).
    static int cmpMag(const BigInt& a, const BigInt& b);
    /// @brief Add magnitudes (signs ignored).
    static BigInt addMag(const BigInt& a, const BigInt& b);
    /// @brief Subtract magnitudes, requiring `|a| >= |b|` (signs ignored).
    static BigInt subMag(const BigInt& a, const BigInt& b);
    /// @brief Unsigned division of magnitudes producing truncated quotient and remainder.
    static void magDivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);

    /// @brief `this = this * m + d` for a small multiplier/adder (used by parsing).
    void mulAddSmall(uint32_t m, uint32_t d);

    /// @brief Set bit @p i of the magnitude to 1.
    void setBit(size_t i);
};

} // namespace vora
