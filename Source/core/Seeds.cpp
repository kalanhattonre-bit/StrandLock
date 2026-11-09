#include "Seeds.h"

namespace sl::core
{
    std::uint32_t hash32Ints (const std::int32_t* args, std::size_t count) noexcept
    {
        std::uint32_t h = 0x9747b28cu;
        for (std::size_t i = 0; i < count; ++i)
        {
            std::uint32_t k = static_cast<std::uint32_t> (args[i]) * 0xcc9e2d51u;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593u;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5u + 0xe6546b64u;
        }
        h ^= static_cast<std::uint32_t> (count * 4u);
        return fmix32 (h);
    }

    std::uint32_t idHash (std::u16string_view id) noexcept
    {
        std::uint32_t h = 0x811c9dc5u;
        for (const char16_t unit : id)
        {
            h ^= static_cast<std::uint32_t> (unit);
            h *= 0x01000193u;
        }
        return h;
    }

    std::uint32_t idHash (std::string_view utf8) noexcept
    {
        std::uint32_t h = 0x811c9dc5u;
        forEachUtf16Unit (utf8, [&h] (std::uint32_t unit) noexcept
        {
            h ^= unit;
            h *= 0x01000193u;
        });
        return h;
    }
}
