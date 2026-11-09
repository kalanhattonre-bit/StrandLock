#include "JsMath.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace sl::core
{
    namespace
    {
        constexpr double kHalF[2] = { 0.5, -0.5 };
        constexpr double kHuge = 1.0e+300;
        constexpr double kOThreshold = 7.09782712893383973096e+02;    // 0x40862E42 FEFA39EF
        constexpr double kUThreshold = -7.45133219101941108420e+02;   // 0xC0874910 D52D3051
        constexpr double kLn2Hi[2] = { 6.93147180369123816490e-01, -6.93147180369123816490e-01 };   // 0x3FE62E42 FEE00000
        constexpr double kLn2Lo[2] = { 1.90821492927058770002e-10, -1.90821492927058770002e-10 };   // 0x3DEA39EF 35793C76
        constexpr double kInvLn2 = 1.44269504088896338700e+00;        // 0x3FF71547 652B82FE
        constexpr double kP1 = 1.66666666666666019037e-01;            // 0x3FC55555 5555553E
        constexpr double kP2 = -2.77777777770155933842e-03;           // 0xBF66C16C 16BEBD93
        constexpr double kP3 = 6.61375632143793436117e-05;            // 0x3F11566A AF25DE2C
        constexpr double kP4 = -1.65339022054652515390e-06;           // 0xBEBBBD41 C5D26BF1
        constexpr double kP5 = 4.13813679705723846039e-08;            // 0x3E663769 72BEA4D0
        constexpr double kE = 2.718281828459045;                      // 0x4005BF0A 8B145769
        constexpr double kTwoM1000 = 9.33263618503218878990e-302;     // 2^-1000
        constexpr double kTwo1023 = 8.988465674311579539e307;         // 2^1023

        std::uint32_t highWord (double x) noexcept { return static_cast<std::uint32_t> (std::bit_cast<std::uint64_t> (x) >> 32); }
        std::uint32_t lowWord (double x) noexcept  { return static_cast<std::uint32_t> (std::bit_cast<std::uint64_t> (x) & 0xffffffffu); }

        double fromHighWord (std::uint32_t hi) noexcept { return std::bit_cast<double> (static_cast<std::uint64_t> (hi) << 32); }
    }

    double jsExp (double x) noexcept
    {
        double hi = 0.0, lo = 0.0;
        int k = 0;

        std::uint32_t hx = highWord (x);
        const int xsb = static_cast<int> ((hx >> 31) & 1u);   // the sign bit of x
        hx &= 0x7fffffffu;                                    // the high word of |x|

        // Non-finite arguments, overflow and underflow.
        if (hx >= 0x40862E42u)
        {
            if (hx >= 0x7ff00000u)
            {
                if (((hx & 0xfffffu) | lowWord (x)) != 0)
                    return x + x;                                 // NaN
                return xsb == 0 ? x : 0.0;                        // exp (+inf) = inf, exp (-inf) = 0
            }
            if (x > kOThreshold)
                return std::numeric_limits<double>::infinity();   // huge * huge
            if (x < kUThreshold)
                return 0.0;                                       // twom1000 * twom1000
        }

        // Argument reduction.
        if (hx > 0x3fd62e42u)                // |x| > 0.5 ln2
        {
            if (hx < 0x3FF0A2B2u)            // and |x| < 1.5 ln2
            {
                if (x == 1.0)
                    return kE;               // V8's special case: the reduction below gets the last bit of e wrong
                hi = x - kLn2Hi[xsb];
                lo = kLn2Lo[xsb];
                k = 1 - xsb - xsb;
            }
            else
            {
                k = static_cast<int> (kInvLn2 * x + kHalF[xsb]);   // truncation, as fdlibm's (int) cast
                const double t = static_cast<double> (k);
                hi = x - t * kLn2Hi[0];                            // t * ln2HI is exact here
                lo = t * kLn2Lo[0];
            }
            x = hi - lo;
        }
        else if (hx < 0x3e300000u)           // |x| < 2^-28
        {
            if (kHuge + x > 1.0)
                return 1.0 + x;
        }
        else
        {
            k = 0;
        }

        // x is now in the primary range.
        const double t = x * x;
        const double twopk = k >= -1021 ? fromHighWord (static_cast<std::uint32_t> (0x3ff00000 + k * 1048576))
                                        : fromHighWord (static_cast<std::uint32_t> (0x3ff00000 + (k + 1000) * 1048576));
        const double c = x - t * (kP1 + t * (kP2 + t * (kP3 + t * (kP4 + t * kP5))));
        if (k == 0)
            return 1.0 - ((x * c) / (c - 2.0) - x);

        const double y = 1.0 - ((lo - (x * c) / (2.0 - c)) - hi);
        if (k >= -1021)
        {
            if (k == 1024)
                return y * 2.0 * kTwo1023;
            return y * twopk;
        }
        return y * twopk * kTwoM1000;
    }
}
