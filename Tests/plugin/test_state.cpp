// The scaffold's state: the empty binary container (PLUGIN-PLAN 3), a 16-byte header and no chunks.
// (Replaces the skeleton's 8-byte header test; M0 owns this file and adds the chunk tests.)

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Version.h"
#include "state/Crc32.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <cstring>
#include <vector>

namespace
{
    using namespace sl::test;

    std::vector<std::uint8_t> bytesOf (const juce::MemoryBlock& block)
    {
        const auto* p = static_cast<const std::uint8_t*> (block.getData());
        return std::vector<std::uint8_t> (p, p + block.getSize());
    }

    std::vector<std::uint8_t> withCrc (std::vector<std::uint8_t> header)
    {
        const std::uint32_t crc = sl::state::crc32 (header.data(), 12);
        for (int i = 0; i < 4; ++i)
            header[(std::size_t) (12 + i)] = static_cast<std::uint8_t> ((crc >> (8 * i)) & 0xffu);
        return header;
    }

    bool loads (const std::vector<std::uint8_t>& data)
    {
        StrandLockProcessor p;
        p.setStateInformation (data.data(), (int) data.size());
        return p.getLoadedStateVersion() != 0;
    }
}

SL_TEST ("state", crc32Reference)
{
    section ("CRC-32 is the zlib/PNG one");

    const char* digits = "123456789";
    const auto whole = sl::state::crc32 (digits, 9);
    const auto chained = sl::state::crc32 (digits + 4, 5, sl::state::crc32 (digits, 4));
    check (whole == 0xcbf43926u, fmt ("crc32 (\"123456789\") = 0x%08x (0xcbf43926)", (unsigned) whole));
    check (chained == whole, fmt ("continuing over a second piece gives the same CRC (0x%08x)", (unsigned) chained));
    check (sl::state::crc32 (digits, 0) == 0u, "the CRC of no bytes is 0");
}

SL_TEST ("state", emptyContainerRoundTrip)
{
    section ("State: the empty container's 16-byte header round-trips");

    StrandLockProcessor a;
    juce::MemoryBlock saved;
    a.getStateInformation (saved);
    const auto b = bytesOf (saved);

    check (b.size() == 16, fmt ("the empty container is the header alone (%d bytes)", (int) b.size()));
    if (b.size() != 16)
        return;

    const auto le16 = [&b] (int i) { return (unsigned) (b[(std::size_t) i] | (b[(std::size_t) i + 1] << 8)); };
    const auto le32 = [&b] (int i) { return (unsigned) b[(std::size_t) i] | ((unsigned) b[(std::size_t) i + 1] << 8)
                                            | ((unsigned) b[(std::size_t) i + 2] << 16) | ((unsigned) b[(std::size_t) i + 3] << 24); };

    check (std::memcmp (b.data(), "STLK", 4) == 0, "it starts with the magic 'STLK'");
    check (le16 (4) == (unsigned) sl::core::kStateFormatVersion, fmt ("formatVersion %u (Version.h says %d)", le16 (4), sl::core::kStateFormatVersion));
    check (le16 (6) == 0, fmt ("flags 0 (%u)", le16 (6)));
    check (le32 (8) == 16, fmt ("totalLength 16 (%u)", le32 (8)));
    check (le32 (12) == sl::state::crc32 (b.data(), 12),
           fmt ("headerCrc32 0x%08x is the CRC-32 of the 12 bytes before it (0x%08x)", le32 (12), (unsigned) sl::state::crc32 (b.data(), 12)));

    StrandLockProcessor fresh;
    check (fresh.getLoadedStateVersion() == 0, "a new instance has loaded no state");
    fresh.setStateInformation (saved.getData(), (int) saved.getSize());
    check (fresh.getLoadedStateVersion() == 1, fmt ("a fresh instance accepts it as format version %u", (unsigned) fresh.getLoadedStateVersion()));

    juce::MemoryBlock again;
    fresh.getStateInformation (again);
    check (again == saved, "saving the restored instance gives the same bytes");

    // A longer blob whose totalLength still says 16 is a valid container followed by bytes it does not cover.
    auto longer = b;
    longer.resize (40, 0xab);
    check (loads (longer), "a valid header followed by more bytes loads");
}

SL_TEST ("state", brokenHeadersAreIgnored)
{
    section ("State: anything that is not an intact header loads nothing");

    StrandLockProcessor a;
    juce::MemoryBlock saved;
    a.getStateInformation (saved);
    const auto good = bytesOf (saved);
    check (loads (good), "the processor's own header loads (the cases below differ from it in one thing)");

    struct Case { const char* what; std::vector<std::uint8_t> data; };
    std::vector<Case> cases;
    cases.push_back ({ "an empty block", {} });
    cases.push_back ({ "the header cut to 15 bytes", { good.begin(), good.begin() + 15 } });

    auto magic = good;
    magic[0] = 'G';
    cases.push_back ({ "another plugin's magic (CRC fixed up)", withCrc (magic) });

    auto badCrc = good;
    badCrc[12] = static_cast<std::uint8_t> (badCrc[12] ^ 0x01u);
    cases.push_back ({ "a wrong header CRC", badCrc });

    auto version0 = good;
    version0[4] = 0;
    version0[5] = 0;
    cases.push_back ({ "formatVersion 0 (CRC fixed up)", withCrc (version0) });

    auto newer = good;
    newer[4] = (std::uint8_t) (sl::core::kStateFormatVersion + 1);
    cases.push_back ({ "a newer formatVersion (CRC fixed up)", withCrc (newer) });

    auto tooLong = good;
    tooLong[8] = 17;
    cases.push_back ({ "a totalLength past the end of the data (CRC fixed up)", withCrc (tooLong) });

    auto tooShort = good;
    tooShort[8] = 15;
    cases.push_back ({ "a totalLength shorter than the header (CRC fixed up)", withCrc (tooShort) });

    for (const auto& c : cases)
        check (! loads (c.data), fmt ("ignored: %s", c.what));
}
