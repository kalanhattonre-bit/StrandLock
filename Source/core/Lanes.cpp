#include "Lanes.h"

namespace sl::core
{
    int laneIndex (std::string_view code) noexcept
    {
        for (int i = 0; i < kNumLanes; ++i)
            if (code == kLanes[i].code)
                return i;
        return -1;
    }

    std::optional<LaneId> findLane (std::string_view code) noexcept
    {
        const int i = laneIndex (code);
        if (i < 0)
            return std::nullopt;
        return kLanes[i].id;
    }

    double laneDef (std::string_view code) noexcept
    {
        const int i = laneIndex (code);
        return i < 0 ? 0.5 : kLanes[i].def;
    }
}
