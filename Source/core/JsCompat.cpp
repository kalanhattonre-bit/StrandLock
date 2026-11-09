#include "JsCompat.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace sl::core
{
    std::int32_t toInt32 (double x) noexcept
    {
        if (! std::isfinite (x))
            return 0;

        constexpr double two32 = 4294967296.0;
        double m = std::fmod (std::trunc (x), two32);   // exact, and it keeps the sign of x
        if (m < 0.0)
            m += two32;                                  // exact: an integer below 2^33

        // C++20 defines the unsigned-to-signed conversion as modular, as ToInt32 needs.
        return static_cast<std::int32_t> (static_cast<std::uint32_t> (m));
    }

    std::uint32_t toUint32 (double x) noexcept
    {
        return static_cast<std::uint32_t> (toInt32 (x));
    }

    double jsRound (double x) noexcept
    {
        double r = std::floor (x);
        if (x - r >= 0.5)
            r += 1.0;
        return r;
    }

    double f32 (double x) noexcept
    {
        // A double beyond the float range has no defined conversion in C++, so the overflow is done here:
        // from 2^128 - 2^103 (half way between FLT_MAX and 2^128) on, round-to-nearest gives infinity.
        constexpr double overflow = 3.4028235677973366e38;
        if (x >= overflow)
            return std::numeric_limits<double>::infinity();
        if (x <= -overflow)
            return -std::numeric_limits<double>::infinity();
        return static_cast<double> (static_cast<float> (x));
    }

    //==============================================================================================
    namespace
    {
        /** The shortest round-trip digits of a positive finite x (what std::to_chars gives, which is
            also what V8 prints), and its decimal exponent: x = 0.d1 d2 ... dk * 10^n. */
        struct ShortestDigits
        {
            std::array<char, 32> d {};
            int k = 0;
            int n = 0;
        };

        ShortestDigits shortestDigits (double x)
        {
            std::array<char, 64> buffer {};
            const auto result = std::to_chars (buffer.data(), buffer.data() + buffer.size(), x, std::chars_format::scientific);

            ShortestDigits out;
            const char* p = buffer.data();
            const char* end = result.ptr;

            for (; p < end && *p != 'e'; ++p)
                if (*p >= '0' && *p <= '9' && out.k < (int) out.d.size())
                    out.d[(std::size_t) out.k++] = *p;

            int exponent = 0;
            if (p < end && *p == 'e')
            {
                ++p;
                bool negative = false;
                if (p < end && (*p == '+' || *p == '-'))
                    negative = *p++ == '-';
                for (; p < end; ++p)
                    exponent = exponent * 10 + (*p - '0');
                if (negative)
                    exponent = -exponent;
            }

            while (out.k > 1 && out.d[(std::size_t) out.k - 1] == '0')
                --out.k;

            out.n = exponent + 1;
            return out;
        }

        /** The exact decimal expansion of a finite x >= 0: its digits with no sign and no point, and how
            many of them stand before the point (at least 1: 0.25 is "025" with 1). A double is
            m * 2^e with m odd, so its expansion ends max (0, -e) digits after the point, and
            std::to_chars prints exactly that many when asked to: nothing is rounded. */
        struct ExactDecimal
        {
            std::string digits;
            int intDigits = 1;
        };

        int exactFractionDigits (double x) noexcept
        {
            int e = 0;
            const double m = std::frexp (x, &e);                                // x = m * 2^e, 0.5 <= m < 1
            auto mantissa = static_cast<std::uint64_t> (std::ldexp (m, 53));     // exact: at most 53 bits
            int exponent = e - 53;
            while (mantissa != 0 && (mantissa & 1u) == 0)
            {
                mantissa >>= 1;
                ++exponent;
            }
            return exponent < 0 ? -exponent : 0;
        }

        ExactDecimal exactDecimal (double x)
        {
            ExactDecimal out;
            if (! (x > 0.0))
            {
                out.digits = "0";
                return out;
            }

            // At most 309 integer digits, the point and 1074 fraction digits.
            std::array<char, 1500> buffer {};
            const auto result = std::to_chars (buffer.data(), buffer.data() + buffer.size(), x,
                                               std::chars_format::fixed, exactFractionDigits (x));

            out.digits.reserve ((std::size_t) (result.ptr - buffer.data()));
            bool seenPoint = false;
            for (const char* p = buffer.data(); p < result.ptr; ++p)
            {
                if (*p == '.')
                {
                    out.intDigits = (int) out.digits.size();
                    seenPoint = true;
                }
                else
                {
                    out.digits += *p;
                }
            }

            if (! seenPoint)
                out.intDigits = (int) out.digits.size();
            return out;
        }

        /** Adds one to a string of decimal digits; true when it carried out of the first digit. */
        bool incrementDigits (std::string& digits)
        {
            for (std::size_t i = digits.size(); i-- > 0;)
            {
                if (digits[i] != '9')
                {
                    ++digits[i];
                    return false;
                }
                digits[i] = '0';
            }
            return true;
        }

        std::string exponentText (int e)
        {
            std::string s (1, e >= 0 ? '+' : '-');
            s += std::to_string (e >= 0 ? e : -e);
            return s;
        }
    }

    std::string jsNumberToString (double x)
    {
        if (std::isnan (x))
            return "NaN";
        if (x == 0.0)
            return "0";
        if (x < 0.0)
            return "-" + jsNumberToString (-x);
        if (std::isinf (x))
            return "Infinity";

        const ShortestDigits g = shortestDigits (x);
        const int k = g.k, n = g.n;
        const char* d = g.d.data();
        std::string s;

        if (k <= n && n <= 21)
        {
            s.assign (d, (std::size_t) k);
            s.append ((std::size_t) (n - k), '0');
        }
        else if (0 < n && n <= 21)
        {
            s.assign (d, (std::size_t) n);
            s += '.';
            s.append (d + n, (std::size_t) (k - n));
        }
        else if (-6 < n && n <= 0)
        {
            s = "0.";
            s.append ((std::size_t) -n, '0');
            s.append (d, (std::size_t) k);
        }
        else
        {
            s.assign (d, 1);
            if (k > 1)
            {
                s += '.';
                s.append (d + 1, (std::size_t) (k - 1));
            }
            s += 'e';
            s += exponentText (n - 1);
        }
        return s;
    }

    std::string jsToFixed (double x, int digits)
    {
        const int f = std::clamp (digits, 0, 100);
        if (std::isnan (x))
            return "NaN";

        std::string sign;
        if (x < 0.0)
        {
            sign = "-";
            x = -x;
        }
        if (x >= 1e21)
            return sign + jsNumberToString (x);

        // n = round (x * 10^f), ties to the larger n, read off the exact digits.
        const ExactDecimal exact = exactDecimal (x);
        const auto keep = (std::size_t) (exact.intDigits + f);
        std::string n;
        n.reserve (keep + 2);
        for (std::size_t i = 0; i < keep; ++i)
            n += i < exact.digits.size() ? exact.digits[i] : '0';
        if (keep < exact.digits.size() && exact.digits[keep] >= '5')
            if (incrementDigits (n))
                n.insert (n.begin(), '1');

        std::size_t zeros = 0;
        while (zeros + 1 < n.size() && n[zeros] == '0')
            ++zeros;
        n.erase (0, zeros);

        if (f == 0)
            return sign + n;
        if (n.size() <= (std::size_t) f)
            n.insert (0, (std::size_t) f + 1 - n.size(), '0');
        n.insert (n.size() - (std::size_t) f, 1, '.');
        return sign + n;
    }

    std::string jsToPrecision (double x, int precision)
    {
        const int p = std::clamp (precision, 1, 100);
        if (std::isnan (x))
            return "NaN";

        std::string sign;
        if (x < 0.0)
        {
            sign = "-";
            x = -x;
        }
        if (std::isinf (x))
            return sign + "Infinity";

        // m = the p digits of n, x ~ n * 10^(e - p + 1).
        std::string m;
        int e = 0;
        if (x == 0.0)
        {
            m.assign ((std::size_t) p, '0');
        }
        else
        {
            const ExactDecimal exact = exactDecimal (x);
            std::size_t first = 0;
            while (first < exact.digits.size() && exact.digits[first] == '0')
                ++first;
            e = exact.intDigits - 1 - (int) first;

            for (std::size_t i = 0; i < (std::size_t) p; ++i)
                m += first + i < exact.digits.size() ? exact.digits[first + i] : '0';

            const std::size_t next = first + (std::size_t) p;
            if (next < exact.digits.size() && exact.digits[next] >= '5')
            {
                if (incrementDigits (m))
                {
                    m.insert (m.begin(), '1');   // 999 -> 1000: keep p digits, one more power of ten
                    m.pop_back();
                    ++e;
                }
            }
        }

        if (e < -6 || e >= p)
        {
            std::string out = sign;
            out += m[0];
            if (p > 1)
            {
                out += '.';
                out.append (m, 1, std::string::npos);
            }
            out += 'e';
            out += exponentText (e);
            return out;
        }

        if (e == p - 1)
            return sign + m;
        if (e >= 0)
        {
            m.insert ((std::size_t) e + 1, 1, '.');
            return sign + m;
        }
        return sign + "0." + std::string ((std::size_t) -(e + 1), '0') + m;
    }
}
