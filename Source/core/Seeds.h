#pragma once

// The seed contract (BLUEPRINT 1.6, PLUGIN-PLAN 1.7): every random value in StrandLock is one of these
// hashes, so the C++ makes the prototype's random choices bit for bit. Never change them.

#include "JsCompat.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace sl::core
{
    /** MurmurHash3's 32-bit finaliser. */
    constexpr std::uint32_t fmix32 (std::uint32_t h) noexcept
    {
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }

    /** hash32 over a list of int32 values: MurmurHash3 x86_32, seed 0x9747b28c, 4 bytes per value,
        h ^= 4 * count, then fmix32. */
    std::uint32_t hash32Ints (const std::int32_t* args, std::size_t count) noexcept;

    namespace detail
    {
        /** Each hash32 argument goes through JavaScript's `x | 0` (PLUGIN-PLAN 1.6 rule 4): a double is
            truncated and wrapped by toInt32, an integer of any width wraps modulo 2^32. */
        template <class T>
        std::int32_t hashArg (T x) noexcept
        {
            static_assert (std::is_arithmetic_v<T>, "hash32 takes numbers");
            if constexpr (std::is_floating_point_v<T>)
                return toInt32 (static_cast<double> (x));
            else
                return static_cast<std::int32_t> (static_cast<std::uint32_t> (x));
        }
    }

    /** hash32 (a, b, ...) exactly as SLModel.hash32 and the engine's copy. */
    template <class... A>
    std::uint32_t hash32 (A... args) noexcept
    {
        if constexpr (sizeof... (A) == 0)
        {
            return hash32Ints (nullptr, 0);
        }
        else
        {
            const std::int32_t ints[] = { detail::hashArg (args)... };
            return hash32Ints (ints, sizeof... (A));
        }
    }

    /** h / 2^32, in [0, 1). */
    constexpr double rand01 (std::uint32_t h) noexcept
    {
        return static_cast<double> (h) / 4294967296.0;
    }

    /** FNV-1a 32 over the UTF-16 code units of an id (offset 0x811c9dc5, prime 0x01000193). */
    std::uint32_t idHash (std::u16string_view id) noexcept;

    /** The same hash for an id stored as UTF-8: it is read as the UTF-16 units JavaScript would hold
        (forEachUtf16Unit), so an id with accents or emoji hashes as in the prototype. */
    std::uint32_t idHash (std::string_view utf8) noexcept;
}
