#pragma once

// SHA-256 (FIPS 180-4) for the SMPL file record (PLUGIN-PLAN 3: "32-byte SHA-256 of the original bytes"). The
// plugin links no JUCE module that has one (juce_cryptography is not among the plan's modules), so it is
// written out here. Message side and worker thread only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sl::doc
{
    using Digest = std::array<std::uint8_t, 32>;

    class Sha256
    {
    public:
        Sha256() noexcept;

        /** Adds size bytes. */
        void update (const void* data, std::size_t size) noexcept;

        /** The digest of everything added. The object is spent afterwards (make a new one to hash again). */
        Digest finish() noexcept;

    private:
        void block (const std::uint8_t* p) noexcept;

        std::array<std::uint32_t, 8> h {};
        std::array<std::uint8_t, 64> buffer {};
        std::size_t used = 0;
        std::uint64_t totalBytes = 0;
    };

    /** The digest of one piece of memory. */
    Digest sha256 (const void* data, std::size_t size) noexcept;

    /** 64 lowercase hex digits. */
    std::string toHex (const Digest& d);
}
