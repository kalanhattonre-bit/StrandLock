#include "Curves.h"

#include "JsMath.h"

#include <cmath>

namespace sl::core
{
    double bendShape (double u, double bend) noexcept
    {
        if (std::fabs (bend) < 1.0e-3)
            return u;
        const double k = bend * 8.0;
        return (jsExp (k * u) - 1.0) / (jsExp (k) - 1.0);
    }

    double gShape (double u, double c) noexcept
    {
        u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
        const double sm = u * u * (3.0 - 2.0 * u);
        if (c > 0.0)
            return sm * (1.0 - c) + (1.0 - std::pow (1.0 - u, 1.0 + 4.0 * c)) * c;
        if (c < 0.0)
            return sm * (1.0 + c) + std::pow (u, 1.0 - 4.0 * c) * (-c);
        return sm;
    }
}
