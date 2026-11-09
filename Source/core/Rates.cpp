#include "Rates.h"

#include <cmath>
#include <limits>

namespace sl::core
{
    double rateBeats (RateId id) noexcept
    {
        return isValidRate (id) ? kRates[static_cast<int> (id)].beats : kUnknownRateBeats;
    }

    double rateBeats (std::string_view code) noexcept
    {
        const auto id = parseRate (code);
        return id ? rateBeats (*id) : kUnknownRateBeats;
    }

    double rateBeatsOr (double beats) noexcept
    {
        return std::isfinite (beats) && beats > 0.0 ? beats : kUnknownRateBeats;
    }

    std::optional<RateId> parseRate (std::string_view code) noexcept
    {
        for (const auto& rate : kRates)
            if (code == rate.code)
                return rate.id;
        return std::nullopt;
    }

    const char* rateCode (RateId id) noexcept
    {
        return isValidRate (id) ? kRates[static_cast<int> (id)].code : "";
    }

    const char* rateLabel (RateId id) noexcept
    {
        return isValidRate (id) ? kRates[static_cast<int> (id)].label : "";
    }

    std::string rateLabel (std::string_view code)
    {
        const auto id = parseRate (code);
        return id ? std::string (rateLabel (*id)) : std::string (code);
    }

    std::optional<RateId> rateFromBeats (double beats, double tol) noexcept
    {
        // SLModel.rateFromBeats, operation for operation: e < best keeps the first of equal errors.
        std::optional<RateId> best;
        double bestError = std::numeric_limits<double>::infinity();
        for (const auto& rate : kRates)
        {
            const double e = std::fabs (rate.beats / beats - 1.0);
            if (e < bestError)
            {
                bestError = e;
                best = rate.id;
            }
        }
        return bestError <= tol ? best : std::nullopt;
    }
}
