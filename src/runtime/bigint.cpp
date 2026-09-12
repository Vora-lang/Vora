/**
 * @file bigint.cpp
 * @brief Implementation of the arbitrary-precision integer in bigint.h.
 *
 * The algorithms are deliberately simple rather than asymptotically optimal:
 * schoolbook multiplication and shift-subtract long division.  BigInt is not on
 * the inline fast path (that stays int47), and @ref kMaxBigIntLimbs bounds the
 * operand size, so clarity that can be verified by test is preferred over
 * Karatsuba-style complexity.
 */

#include "bigint.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vora {

namespace {
/// 2^64 as a double, used for magnitude conversion.
constexpr double kTwoPow64 = 18446744073709551616.0;
} // namespace

void BigInt::normalize() {
    while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
    if (limbs.empty()) negative = false;
}

BigInt BigInt::fromInt64(int64_t v) {
    BigInt r;
    if (v == 0) return r;
    uint64_t mag;
    if (v < 0) {
        // Compute the magnitude without negating INT64_MIN (which is UB).
        mag = static_cast<uint64_t>(-(v + 1)) + 1ULL;
        r.negative = true;
    } else {
        mag = static_cast<uint64_t>(v);
    }
    r.limbs.push_back(mag);
    return r;
}

bool BigInt::fitsInt64() const {
    if (limbs.empty()) return true;
    if (limbs.size() > 1) return false;
    if (negative) return limbs[0] <= 0x8000000000000000ULL;
    return limbs[0] <= 0x7FFFFFFFFFFFFFFFULL;
}

int64_t BigInt::toInt64() const {
    if (limbs.empty()) return 0;
    if (!negative) return static_cast<int64_t>(limbs[0]);
    if (limbs[0] == 0x8000000000000000ULL) return INT64_MIN;
    return -static_cast<int64_t>(limbs[0]);
}

double BigInt::toDouble() const {
    double d = 0.0;
    for (size_t i = limbs.size(); i-- > 0;) {
        d = d * kTwoPow64 + static_cast<double>(limbs[i]);
    }
    return negative ? -d : d;
}

size_t BigInt::bitLength() const {
    if (limbs.empty()) return 0;
    uint64_t top = limbs.back();
    size_t bits = 0;
    while (top != 0) {
        ++bits;
        top >>= 1;
    }
    return (limbs.size() - 1) * 64 + bits;
}

bool BigInt::bitAt(size_t i) const {
    const size_t li = i / 64;
    if (li >= limbs.size()) return false;
    return ((limbs[li] >> (i % 64)) & 1ULL) != 0;
}

void BigInt::setBit(size_t i) {
    const size_t li = i / 64;
    if (li >= limbs.size()) limbs.resize(li + 1, 0);
    limbs[li] |= (1ULL << (i % 64));
}

int BigInt::cmpMag(const BigInt& a, const BigInt& b) {
    if (a.limbs.size() != b.limbs.size()) {
        return a.limbs.size() < b.limbs.size() ? -1 : 1;
    }
    for (size_t i = a.limbs.size(); i-- > 0;) {
        if (a.limbs[i] != b.limbs[i]) return a.limbs[i] < b.limbs[i] ? -1 : 1;
    }
    return 0;
}

BigInt BigInt::addMag(const BigInt& a, const BigInt& b) {
    BigInt r;
    const size_t n = std::max(a.limbs.size(), b.limbs.size());
    r.limbs.assign(n, 0);
    unsigned __int128 carry = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned __int128 s = carry;
        if (i < a.limbs.size()) s += a.limbs[i];
        if (i < b.limbs.size()) s += b.limbs[i];
        r.limbs[i] = static_cast<uint64_t>(s);
        carry = s >> 64;
    }
    if (carry != 0) r.limbs.push_back(static_cast<uint64_t>(carry));
    return r;
}

BigInt BigInt::subMag(const BigInt& a, const BigInt& b) {
    // Requires |a| >= |b|.
    BigInt r;
    r.limbs.assign(a.limbs.size(), 0);
    unsigned __int128 borrow = 0;
    const unsigned __int128 radix = static_cast<unsigned __int128>(1) << 64;
    for (size_t i = 0; i < a.limbs.size(); ++i) {
        const unsigned __int128 av = a.limbs[i];
        const unsigned __int128 bv = (i < b.limbs.size()) ? b.limbs[i] : 0;
        const unsigned __int128 sub = bv + borrow;
        if (av >= sub) {
            r.limbs[i] = static_cast<uint64_t>(av - sub);
            borrow = 0;
        } else {
            r.limbs[i] = static_cast<uint64_t>(av + radix - sub);
            borrow = 1;
        }
    }
    r.normalize();
    return r;
}

int BigInt::compare(const BigInt& a, const BigInt& b) {
    const bool an = a.negative && !a.limbs.empty();
    const bool bn = b.negative && !b.limbs.empty();
    if (an != bn) return an ? -1 : 1;
    const int c = cmpMag(a, b);
    return an ? -c : c;
}

BigInt BigInt::negate(const BigInt& a) {
    BigInt r = a;
    if (!r.limbs.empty()) r.negative = !r.negative;
    return r;
}

BigInt BigInt::add(const BigInt& a, const BigInt& b) {
    if (a.limbs.empty()) return b;
    if (b.limbs.empty()) return a;
    if (a.negative == b.negative) {
        BigInt r = addMag(a, b);
        r.negative = a.negative;
        r.normalize();
        return r;
    }
    const int c = cmpMag(a, b);
    if (c == 0) return BigInt();
    if (c > 0) {
        BigInt r = subMag(a, b);
        r.negative = a.negative;
        r.normalize();
        return r;
    }
    BigInt r = subMag(b, a);
    r.negative = b.negative;
    r.normalize();
    return r;
}

BigInt BigInt::sub(const BigInt& a, const BigInt& b) {
    return add(a, negate(b));
}

BigInt BigInt::mul(const BigInt& a, const BigInt& b) {
    BigInt r;
    if (a.limbs.empty() || b.limbs.empty()) return r;
    r.limbs.assign(a.limbs.size() + b.limbs.size(), 0);
    for (size_t i = 0; i < a.limbs.size(); ++i) {
        const uint64_t av = a.limbs[i];
        if (av == 0) continue;
        unsigned __int128 carry = 0;
        for (size_t j = 0; j < b.limbs.size(); ++j) {
            const unsigned __int128 cur =
                static_cast<unsigned __int128>(av) * b.limbs[j] + r.limbs[i + j] + carry;
            r.limbs[i + j] = static_cast<uint64_t>(cur);
            carry = cur >> 64;
        }
        size_t k = i + b.limbs.size();
        while (carry != 0 && k < r.limbs.size()) {
            const unsigned __int128 cur =
                static_cast<unsigned __int128>(r.limbs[k]) + carry;
            r.limbs[k] = static_cast<uint64_t>(cur);
            carry = cur >> 64;
            ++k;
        }
    }
    r.negative = (a.negative != b.negative);
    r.normalize();
    return r;
}

void BigInt::magDivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r) {
    // Shift-subtract long division over the bits of |a|, most significant first.
    q = BigInt();
    r = BigInt();
    if (b.limbs.empty()) return;
    q.limbs.assign(a.limbs.size(), 0);
    const size_t bits = a.bitLength();
    for (size_t i = bits; i-- > 0;) {
        // r = r * 2
        uint64_t carry = 0;
        for (size_t k = 0; k < r.limbs.size(); ++k) {
            const uint64_t next = r.limbs[k] >> 63;
            r.limbs[k] = (r.limbs[k] << 1) | carry;
            carry = next;
        }
        if (carry != 0) r.limbs.push_back(carry);
        // r += bit i
        if (a.bitAt(i)) {
            if (r.limbs.empty()) r.limbs.push_back(1);
            else r.limbs[0] |= 1ULL;
        }
        if (cmpMag(r, b) >= 0) {
            r = subMag(r, b);
            q.setBit(i);
        }
    }
    q.normalize();
    r.normalize();
}

bool BigInt::divModTrunc(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r) {
    if (b.limbs.empty()) return false;
    BigInt mq, mr;
    magDivMod(a, b, mq, mr);
    mq.negative = !mq.limbs.empty() && (a.negative != b.negative);
    mr.negative = !mr.limbs.empty() && a.negative;
    mq.normalize();
    mr.normalize();
    q = mq;
    r = mr;
    return true;
}

BigInt BigInt::shiftLeftBits(const BigInt& a, size_t bits) {
    if (a.limbs.empty()) return BigInt();
    const size_t limbShift = bits / 64;
    const size_t bitShift = bits % 64;
    BigInt r;
    r.limbs.assign(a.limbs.size() + limbShift + 1, 0);
    for (size_t i = 0; i < a.limbs.size(); ++i) {
        const uint64_t v = a.limbs[i];
        if (bitShift == 0) {
            r.limbs[i + limbShift] |= v;
        } else {
            r.limbs[i + limbShift] |= (v << bitShift);
            r.limbs[i + limbShift + 1] |= (v >> (64 - bitShift));
        }
    }
    r.negative = a.negative;
    r.normalize();
    return r;
}

BigInt BigInt::shiftRightBits(const BigInt& a, size_t bits) {
    if (a.limbs.empty()) return BigInt();
    const size_t limbShift = bits / 64;
    const size_t bitShift = bits % 64;
    const size_t n = a.limbs.size();

    BigInt mag;
    if (limbShift < n) {
        const size_t m = n - limbShift;
        mag.limbs.assign(m, 0);
        for (size_t i = 0; i < m; ++i) {
            const uint64_t v = a.limbs[i + limbShift];
            uint64_t carryIn = 0;
            if (bitShift != 0 && (i + limbShift + 1) < n) {
                carryIn = a.limbs[i + limbShift + 1] << (64 - bitShift);
            }
            mag.limbs[i] = (bitShift == 0) ? v : ((v >> bitShift) | carryIn);
        }
        mag.normalize();
    }

    if (!a.negative) return mag;

    // Floor semantics: a negative value rounds away from zero when any bit was
    // shifted out, so the magnitude increases by one.
    bool lostBits = false;
    for (size_t i = 0; i < limbShift && i < n; ++i) {
        if (a.limbs[i] != 0) {
            lostBits = true;
            break;
        }
    }
    if (!lostBits && bitShift != 0 && limbShift < n) {
        if ((a.limbs[limbShift] & ((1ULL << bitShift) - 1)) != 0) lostBits = true;
    }
    if (lostBits) {
        uint64_t carry = 1;
        for (size_t i = 0; i < mag.limbs.size() && carry != 0; ++i) {
            mag.limbs[i] += 1;
            if (mag.limbs[i] != 0) carry = 0;
        }
        if (carry != 0) mag.limbs.push_back(1);
    }
    mag.negative = !mag.limbs.empty();
    return mag;
}

BigInt BigInt::bitwise(const BigInt& a, const BigInt& b, BitOp op) {
    // One extra limb beyond the wider operand gives room for the sign bit, so a
    // result can never be misread as the wrong sign.  Standard formulation.
    const size_t n = std::max(a.limbs.size(), b.limbs.size()) + 1;

    // Two's-complement images, sign-extended to n limbs.
    auto toTwos = [n](const BigInt& v) {
        std::vector<uint64_t> out(n, 0);
        for (size_t i = 0; i < v.limbs.size() && i < n; ++i) out[i] = v.limbs[i];
        if (v.negative && !v.limbs.empty()) {
            for (size_t i = 0; i < n; ++i) out[i] = ~out[i];
            uint64_t carry = 1;
            for (size_t i = 0; i < n && carry != 0; ++i) {
                out[i] += 1;
                if (out[i] != 0) carry = 0;
            }
        }
        return out;
    };

    std::vector<uint64_t> x = toTwos(a);
    const std::vector<uint64_t> y = toTwos(b);
    for (size_t i = 0; i < n; ++i) {
        switch (op) {
            case BitOp::And: x[i] &= y[i]; break;
            case BitOp::Or:  x[i] |= y[i]; break;
            case BitOp::Xor: x[i] ^= y[i]; break;
        }
    }

    BigInt out;
    // A set top bit means the infinite two's-complement result is negative.
    if (((x[n - 1] >> 63) & 1ULL) != 0) {
        for (size_t i = 0; i < n; ++i) x[i] = ~x[i];
        uint64_t carry = 1;
        for (size_t i = 0; i < n && carry != 0; ++i) {
            x[i] += 1;
            if (x[i] != 0) carry = 0;
        }
        out.negative = true;
    }
    out.limbs = std::move(x);
    out.normalize();
    return out;
}

BigInt BigInt::invert(const BigInt& a) {
    return sub(negate(a), fromInt64(1));
}

BigInt BigInt::fromDoubleTrunc(double d) {
    BigInt r;
    if (!(d != 0.0)) return r;  // also catches NaN
    const bool neg = d < 0;
    const double a = neg ? -d : d;
    if (a < 1.0) {
        r.negative = false;
        return r;
    }
    // Exact decomposition: a == mant * 2^e with mant a 53-bit integer.
    int exp2 = 0;
    const double m = std::frexp(a, &exp2);          // a = m * 2^exp2, 0.5 <= m < 1
    const uint64_t mant = static_cast<uint64_t>(std::ldexp(m, 53));
    const int e = exp2 - 53;
    r = fromInt64(static_cast<int64_t>(mant));
    if (e > 0) {
        r = shiftLeftBits(r, static_cast<size_t>(e));
    } else if (e < 0) {
        // a is integral, so mant is divisible by 2^(-e): this shift is exact.
        r = shiftRightBits(r, static_cast<size_t>(-e));
    }
    r.negative = neg && !r.limbs.empty();
    r.normalize();
    return r;
}

void BigInt::mulAddSmall(uint32_t m, uint32_t d) {
    unsigned __int128 carry = d;
    for (size_t i = 0; i < limbs.size(); ++i) {
        const unsigned __int128 cur =
            static_cast<unsigned __int128>(limbs[i]) * m + carry;
        limbs[i] = static_cast<uint64_t>(cur);
        carry = cur >> 64;
    }
    while (carry != 0) {
        limbs.push_back(static_cast<uint64_t>(carry));
        carry >>= 64;
    }
}

bool BigInt::fromChars(const char* s, size_t n, int base, BigInt& out) {
    if (s == nullptr || n == 0 || base < 2 || base > 16) return false;
    BigInt r;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (d >= base) return false;
        r.mulAddSmall(static_cast<uint32_t>(base), static_cast<uint32_t>(d));
        if (r.exceedsLimit()) return false;
    }
    r.normalize();
    out = r;
    return true;
}

std::string BigInt::toDecimal() const {
    if (limbs.empty()) return "0";
    const BigInt base = [] {
        BigInt b;
        b.limbs.push_back(10000000000000000000ULL);  // 10^19, largest power of 10 in uint64
        return b;
    }();

    std::vector<uint64_t> chunks;
    BigInt cur = *this;
    cur.negative = false;
    while (!cur.limbs.empty()) {
        BigInt q, r;
        magDivMod(cur, base, q, r);
        chunks.push_back(r.limbs.empty() ? 0 : r.limbs[0]);
        cur = q;
    }

    std::string out;
    if (negative) out += '-';
    out += std::to_string(chunks.back());
    for (size_t i = chunks.size() - 1; i-- > 0;) {
        const std::string part = std::to_string(chunks[i]);
        out.append(19 - part.size(), '0');
        out += part;
    }
    return out;
}

} // namespace vora
