#pragma once

// CRC-32 for the plugin state's header and chunks (PLUGIN-PLAN 3): the IEEE 802.3 one that zlib and PNG
// use (reflected polynomial 0xEDB88320, initial value and final xor 0xFFFFFFFF), so crc32 of the nine
// bytes "123456789" is 0xCBF43926.

#include <array>
#include <cstddef>
#include <cstdint>

namespace sl::state
{
    namespace detail
    {
        constexpr std::array<std::uint32_t, 256> makeCrc32Table() noexcept
        {
            std::array<std::uint32_t, 256> table {};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int bit = 0; bit < 8; ++bit)
                    c = (c & 1u) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            return table;
        }

        inline constexpr std::array<std::uint32_t, 256> kCrc32Table = makeCrc32Table();
    }

    /** The CRC-32 of size bytes. Pass the result of an earlier call as previous to continue over more
        bytes: crc32 (b, nb, crc32 (a, na)) equals the CRC of a followed by b. */
    inline std::uint32_t crc32 (const void* data, std::size_t size, std::uint32_t previous = 0) noexcept
    {
        const auto* bytes = static_cast<const std::uint8_t*> (data);
        std::uint32_t c = ~previous;
        for (std::size_t i = 0; i < size; ++i)
            c = detail::kCrc32Table[(c ^ static_cast<std::uint32_t> (bytes[i])) & 0xffu] ^ (c >> 8);
        return ~c;
    }
}
