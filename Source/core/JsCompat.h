#pragma once

// JavaScript's number and string rules, so the C++ prints, rounds and hashes exactly like the
// prototype (PLUGIN-PLAN 1.6 rules 4-6). Every function here is checked against model/seeds.json.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sl::core
{
    /** ECMAScript ToInt32, what JavaScript's `x | 0` does: truncate toward zero, then wrap modulo 2^32
        into -2^31 .. 2^31 - 1. NaN and the infinities give 0. */
    std::int32_t toInt32 (double x) noexcept;

    /** ECMAScript ToUint32 (`x >>> 0`): the bits of toInt32, read as unsigned. */
    std::uint32_t toUint32 (double x) noexcept;

    /** Math.round: the nearest integer, ties toward +infinity (Math.round (-2.5) is -2). Computed as the
        plan says: r = floor (x), plus 1 when x - r >= 0.5. NaN and the infinities pass through. */
    double jsRound (double x) noexcept;

    /** Math.fround: x rounded to the nearest float32, returned as a double. Beyond the float range it
        gives an infinity, as JavaScript does. */
    double f32 (double x) noexcept;

    /** String (x), ECMAScript Number::toString: the shortest digits that read back as x; decimal form
        for 1e-7 <= |x| < 1e21 ("0.000001", "123.5"), exponent form otherwise ("1e+21", "1.5e-7");
        -0 prints "0"; "NaN", "Infinity", "-Infinity". */
    std::string jsNumberToString (double x);

    /** Number.prototype.toFixed (digits), digits 0..100 (clamped). Rounds the exact binary value, and a
        tie goes to the larger candidate: 0.125 -> "0.13" at 2 digits, 1.005 -> "1.00" (it is
        1.00499999...). |x| >= 1e21 prints as String (x). A negative value keeps its sign: -0.0001 -> "-0". */
    std::string jsToFixed (double x, int digits);

    /** Number.prototype.toPrecision (precision), precision 1..100 (clamped), rounding as jsToFixed.
        Exponent form when the exponent is below -6 or at least the precision ("1.2e+2"). */
    std::string jsToPrecision (double x, int precision);

    //==============================================================================================
    // Declared for M1, which completes this file (PLUGIN-PLAN 7.1, 7.2).

    /** uint32.toString (36): lowercase digits, no sign ("0", "z", "10", "zik0zj"). */
    std::string toBase36 (std::uint32_t u);

    /** a < b in JavaScript string order (UTF-16 code units), for ids stored as UTF-8. */
    bool jsStringLess (std::string_view a, std::string_view b) noexcept;

    //==============================================================================================
    /** Calls unit (std::uint32_t) for every UTF-16 code unit of a UTF-8 string, the way a JavaScript
        string holds it: a code point above U+FFFF becomes a surrogate pair. A 3-byte sequence for a
        surrogate (WTF-8, how JsonLite stores a lone "\ud800") gives that one unit back. A byte that
        starts no valid sequence gives U+FFFD. Never allocates. */
    template <class UnitFn>
    void forEachUtf16Unit (std::string_view utf8, UnitFn&& unit)
    {
        const std::size_t size = utf8.size();
        auto byteAt = [&utf8] (std::size_t i) { return static_cast<std::uint32_t> (static_cast<unsigned char> (utf8[i])); };
        auto isTail = [&] (std::size_t i) { return i < size && (byteAt (i) & 0xc0u) == 0x80u; };

        std::size_t i = 0;
        while (i < size)
        {
            const std::uint32_t b0 = byteAt (i);
            std::uint32_t cp = 0xfffdu;
            std::size_t length = 1;

            if (b0 < 0x80u)
            {
                cp = b0;
            }
            else if ((b0 & 0xe0u) == 0xc0u && isTail (i + 1))
            {
                cp = ((b0 & 0x1fu) << 6) | (byteAt (i + 1) & 0x3fu);
                length = 2;
            }
            else if ((b0 & 0xf0u) == 0xe0u && isTail (i + 1) && isTail (i + 2))
            {
                cp = ((b0 & 0x0fu) << 12) | ((byteAt (i + 1) & 0x3fu) << 6) | (byteAt (i + 2) & 0x3fu);
                length = 3;
            }
            else if ((b0 & 0xf8u) == 0xf0u && isTail (i + 1) && isTail (i + 2) && isTail (i + 3))
            {
                cp = ((b0 & 0x07u) << 18) | ((byteAt (i + 1) & 0x3fu) << 12) | ((byteAt (i + 2) & 0x3fu) << 6) | (byteAt (i + 3) & 0x3fu);
                length = 4;
            }

            if (cp >= 0x10000u && cp <= 0x10ffffu)
            {
                cp -= 0x10000u;
                unit (0xd800u + (cp >> 10));
                unit (0xdc00u + (cp & 0x3ffu));
            }
            else
            {
                unit (cp > 0x10ffffu ? 0xfffdu : cp);
            }

            i += length;
        }
    }
}
