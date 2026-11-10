// The plugin state (PLUGIN-PLAN 3, M0, and M2's full state): the binary container, the PARM, INST, CLIP, SMPL
// and UIST chunks, unknown chunks and unreadable blobs written back unchanged, and loads that never crash,
// never read past the data and never leave a parameter out of range, whatever the bytes are. The clip round
// trips over every preset and fixture clip, forward compatibility through the processor and the SMPL file
// record are in test_state_clip.cpp.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "core/Version.h"
#include "model/Clip.h"
#include "model/ClipCodec.h"
#include "model/Node.h"
#include "model/Presets.h"
#include "params/ParameterLayout.h"
#include "sounds/StarterSounds.h"
#include "state/Crc32.h"
#include "state/StateChunks.h"
#include "state/StateFormat.h"
#include "state/UserSettings.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using state::Bytes;

    constexpr int kBypass = params::index (params::P::bypass);

    /** splitmix64: the tests' own seeded numbers. */
    struct Rng
    {
        std::uint64_t s;
        std::uint64_t next() noexcept
        {
            std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }
        double unit() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
        int range (int lo, int hi) noexcept { return lo + (int) (next() % (std::uint64_t) (hi - lo + 1)); }
        std::uint8_t byte() noexcept { return (std::uint8_t) (next() & 0xffu); }
    };

    std::uint32_t le32 (const Bytes& b, std::size_t at)
    {
        return (std::uint32_t) b[at] | ((std::uint32_t) b[at + 1] << 8) | ((std::uint32_t) b[at + 2] << 16) | ((std::uint32_t) b[at + 3] << 24);
    }

    void put32 (Bytes& b, std::size_t at, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b[at + (std::size_t) i] = (std::uint8_t) ((v >> (8 * i)) & 0xffu);
    }

    Bytes saveOf (StrandLockProcessor& p)
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        const auto* d = static_cast<const std::uint8_t*> (m.getData());
        return Bytes (d, d + m.getSize());
    }

    /** Loads exactly these bytes from a heap block of exactly their size (so a read past the end is a read past
        the allocation). Returns false when an exception escaped. */
    bool loadInto (StrandLockProcessor& p, const Bytes& b)
    {
        try
        {
            if (b.empty())
            {
                p.setStateInformation (nullptr, 0);
                return true;
            }
            auto exact = std::make_unique<std::uint8_t[]> (b.size());
            std::memcpy (exact.get(), b.data(), b.size());
            p.setStateInformation (exact.get(), (int) b.size());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    float plainOf (StrandLockProcessor& p, int i) { return params::plainValueOf (p.getParam (i)); }

    /** Every parameter finite and inside its range. */
    bool parametersLegal (StrandLockProcessor& p)
    {
        for (int i = 0; i < params::kNumParams; ++i)
        {
            const float v = plainOf (p, i);
            const auto& s = params::kParams[i];
            if (! std::isfinite (v) || v < s.min || v > s.max)
                return false;
        }
        return true;
    }

    bool parametersAre (StrandLockProcessor& p, const std::array<float, params::kNumParams>& values)
    {
        for (int i = 0; i < params::kNumParams; ++i)
            if (i != kBypass && ! juce::exactlyEqual (plainOf (p, i), values[(std::size_t) i]))
                return false;
        return true;
    }

    std::array<float, params::kNumParams> defaults()
    {
        std::array<float, params::kNumParams> v {};
        for (int i = 0; i < params::kNumParams; ++i)
            v[(std::size_t) i] = params::defaultPlain (i);
        return v;
    }

    std::array<float, params::kNumParams> randomValues (Rng& rng)
    {
        std::array<float, params::kNumParams> v {};
        for (int i = 0; i < params::kNumParams; ++i)
        {
            const auto& s = params::kParams[i];
            if (s.kind == params::Kind::real)
                v[(std::size_t) i] = std::clamp ((float) (s.min + rng.unit() * (s.max - s.min)), s.min, s.max);
            else
                v[(std::size_t) i] = (float) rng.range ((int) s.min, (int) s.max);
        }
        v[(std::size_t) kBypass] = 0.0f;
        return v;
    }

    state::InstState randomInst (Rng& rng)
    {
        auto target = [&rng]
        {
            const int k = rng.range (0, core::kNumLanes);
            return k == core::kNumLanes ? std::string ("off") : std::string (core::kLanes[k].code);
        };
        state::InstState s;
        s.seed = (std::int32_t) (std::uint32_t) rng.next();
        s.mod1Target = target();
        s.mod1Cc = (std::uint8_t) rng.range (0, 127);
        s.mod2Target = target();
        s.mod2Cc = (std::uint8_t) rng.range (0, 127);
        s.key = (std::uint8_t) rng.range (0, 11);
        s.scale = core::kScales[rng.range (0, core::kNumScales - 1)].name;
        s.scaleLock = (std::uint8_t) rng.range (0, 1);
        s.regionStartExact = rng.range (0, 1) == 0 ? std::numeric_limits<double>::quiet_NaN() : rng.unit() * 0.5;
        s.regionEndExact = rng.range (0, 1) == 0 ? std::numeric_limits<double>::quiet_NaN() : 0.5 + rng.unit() * 0.5;
        return s;
    }

    state::UiState randomUi (Rng& rng)
    {
        static const char* const tabs[] = { "sample", "macros", "notes", "slides", "copy", "wheels", "sound", "diag" };
        static const float grids[] = { 0.0f, 0.25f, 0.5f, 1.0f, 1.0f / 3.0f };
        state::UiState u;
        u.theme = (std::uint8_t) rng.range (0, 1);
        u.uiScalePercent = (std::uint16_t) rng.range (75, 200);
        u.logicalWidth = (std::uint16_t) rng.range (900, 2400);
        u.logicalHeight = (std::uint16_t) rng.range (560, 1600);
        u.mode = (std::uint8_t) rng.range (1, 8);
        u.subLane = core::kLanes[rng.range (6, 9)].code;
        u.tab = tabs[rng.range (0, 7)];
        u.folded = (std::uint8_t) rng.range (0, 1);
        u.zoomH = (float) (0.25 + rng.unit() * 8.0);
        u.zoomV = (float) (0.25 + rng.unit() * 8.0);
        u.scrollBeat = rng.unit() * 512.0;
        u.scrollRow = (float) (rng.unit() * 127.0);
        u.gridBeats = grids[rng.range (0, 4)];
        u.scope = (std::uint8_t) rng.range (0, 1);
        u.copyMask = (std::uint16_t) rng.range (0, 0x3ff);
        u.stripViewA = rng.unit() * 0.5;
        u.stripViewB = 0.5 + rng.unit() * 0.5;
        return u;
    }

    Bytes randomBytes (Rng& rng, std::size_t n)
    {
        Bytes b (n);
        for (auto& x : b)
            x = rng.byte();
        return b;
    }

    struct Chunk
    {
        std::uint32_t tag;
        Bytes payload;
        std::uint16_t version = 1;
    };

    Bytes containerOf (const std::vector<Chunk>& chunks)
    {
        Bytes framed;
        for (const auto& c : chunks)
            state::appendChunk (framed, c.tag, c.version, 0, c.payload);
        return state::makeContainer (framed);
    }

    /** The CLIP payload of an empty 4-bar clip (what a state with no CLIP chunk loads). */
    const Bytes& emptyClipBytes()
    {
        static const Bytes b = model::encodeClip (model::newClip (4.0));
        return b;
    }

    /** A small clip (two notes, one drawn lane) for the reference blobs: about 1.5 KB encoded. */
    const Bytes& smallClipBytes()
    {
        static const Bytes b = []
        {
            model::Clip c = model::newClip (2.0);
            model::Note n1 = model::mkNote (60.0, 0.0, 1.0, "ref-1");
            n1.lane (core::LaneId::amp) = { model::mkNode (0.0, 0.2), model::mkNode (1.0, 0.9, model::NodeShape::linear) };
            c.notes.push_back (std::make_shared<const model::Note> (n1));
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (64.0, 1.0, 1.0, "ref-2")));
            return model::encodeClip (c);
        }();
        return b;
    }

    /** The five chunks this version writes, for these values (as the processor would encode them). */
    std::vector<Chunk> ownChunks (const std::array<float, params::kNumParams>& values, const state::InstState& inst,
                                  const state::SampleRef& smpl, const state::UiState& ui, const Bytes& clip = emptyClipBytes())
    {
        return { { state::kTagParm, state::encodeParm (values, {}, {}) },
                 { state::kTagInst, state::encodeInst (inst) },
                 { state::kTagClip, clip },
                 { state::kTagSmpl, state::encodeSmpl (smpl) },
                 { state::kTagUist, state::encodeUist (ui) } };
    }

    state::SampleRef starterRef (sounds::StarterId id, std::int32_t seed)
    {
        state::SampleRef r;
        r.kind = 0;
        r.starterId = sounds::starterInfo (id).code;
        r.seed = seed;
        return r;
    }

    /** Recomputes the header CRC and every chunk CRC that the (possibly damaged) lengths still frame, so a
        mutation reaches the chunk decoders instead of failing a CRC. */
    void refreshCrcs (Bytes& b)
    {
        if (b.size() < state::kHeaderSize || std::memcmp (b.data(), state::kMagic, 4) != 0)
            return;
        const std::size_t total = std::min<std::size_t> (le32 (b, 8), b.size());
        std::size_t pos = state::kHeaderSize;
        while (pos + state::kChunkHeaderSize <= total)
        {
            const std::size_t length = le32 (b, pos + 8);
            if (length > total - pos - state::kChunkHeaderSize)
                break;
            put32 (b, pos + 12, state::crc32 (b.data() + pos + state::kChunkHeaderSize, length));
            pos += state::kChunkHeaderSize + length + (4 - length % 4) % 4;
        }
        put32 (b, 12, state::crc32 (b.data(), 12));
    }

    /** A blob of exactly totalSize bytes: the given parameters, a small clip, the default starter, and an unknown
        chunk of seeded bytes (kept verbatim) after PARM filling the rest. */
    Bytes referenceBlob (const std::array<float, params::kNumParams>& values, std::size_t totalSize, std::uint64_t seed)
    {
        auto chunks = ownChunks (values, state::InstState {}, starterRef (sounds::StarterId::sawopen, 1), state::UiState {}, smallClipBytes());
        const std::size_t base = containerOf (chunks).size();
        Rng rng { seed };
        chunks.insert (chunks.begin() + 1, Chunk { state::fourCC ("FILL"), randomBytes (rng, totalSize - base - state::kChunkHeaderSize) });
        return containerOf (chunks);
    }

    /** After any load, the next save is either the loaded blob itself (kept because it could not be read) or a
        container this version reads. */
    bool saveIsSound (StrandLockProcessor& p, const Bytes& loaded)
    {
        const Bytes s = saveOf (p);
        return s == loaded || state::parseContainer (s.data(), s.size()).status == state::HeaderStatus::ok;
    }

    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

//==================================================================================================
SL_TEST ("state", crc32Reference)
{
    section ("CRC-32 is the zlib/PNG one");

    const char* digits = "123456789";
    const auto whole = state::crc32 (digits, 9);
    const auto chained = state::crc32 (digits + 4, 5, state::crc32 (digits, 4));
    check (whole == 0xcbf43926u, fmt ("crc32 (\"123456789\") = 0x%08x (0xcbf43926)", (unsigned) whole));
    check (chained == whole, fmt ("continuing over a second piece gives the same CRC (0x%08x)", (unsigned) chained));
    check (state::crc32 (digits, 0) == 0u, "the CRC of no bytes is 0");
}

SL_TEST ("state", containerLayout)
{
    section ("State: a new instance saves the header, then PARM, INST, CLIP, SMPL, UIST (PLUGIN-PLAN 3)");

    StrandLockProcessor p;
    const Bytes b = saveOf (p);
    const auto parsed = state::parseContainer (b.data(), b.size());
    check (parsed.status == state::HeaderStatus::ok, fmt ("it parses as a container (%s)", state::describe (parsed.status)));
    if (parsed.status != state::HeaderStatus::ok || b.size() < 16)
        return;

    check (std::memcmp (b.data(), "STLK", 4) == 0, "it starts with the magic 'STLK'");
    check (b[4] == sl::core::kStateFormatVersion && b[5] == 0 && b[6] == 0 && b[7] == 0, "formatVersion 1, flags 0");
    check (le32 (b, 8) == b.size(), fmt ("totalLength %u is the blob's size (%d)", le32 (b, 8), (int) b.size()));
    check (le32 (b, 12) == state::crc32 (b.data(), 12), "headerCrc32 is the CRC-32 of the 12 bytes before it");

    std::string tags;
    bool framesGood = true;
    for (const auto& c : parsed.chunks)
    {
        tags += state::tagName (c.tag) + " ";
        framesGood = framesGood && c.crcOk && c.withinLimits && ! c.truncated && c.version == 1 && c.flags == 0
                  && c.rawLength % 4 == 0;
    }
    check (tags == "PARM INST CLIP SMPL UIST ", fmt ("chunks in order: %s", tags.c_str()));
    check (framesGood, "every chunk is version 1, flags 0, CRC good, padded to 4");
    if (parsed.chunks.size() != 5)
        return;

    state::ParmChunk parm;
    const bool parmOk = state::decodeParm (parsed.chunks[0].payload, parsed.chunks[0].payloadLength, parm);
    bool idsInOrder = parmOk && parm.entries.size() == (std::size_t) params::kNumParams - 1 && parm.trailing.empty();
    bool valuesAreDefaults = idsInOrder;
    for (std::size_t i = 0; idsInOrder && i < parm.entries.size(); ++i)
    {
        idsInOrder = parm.entries[i].id == params::kParams[i].id;
        valuesAreDefaults = valuesAreDefaults && juce::exactlyEqual (parm.entries[i].value, params::kParams[i].def);
    }
    check (idsInOrder, fmt ("PARM holds the 49 parameters by id in order, bypass left out (%d entries)", (int) parm.entries.size()));
    check (valuesAreDefaults, "PARM holds every default");

    state::InstState inst;
    check (state::decodeInst (parsed.chunks[1].payload, parsed.chunks[1].payloadLength, inst)
               && inst.seed == 1 && inst.mod1Target == "vib" && inst.mod1Cc == 1 && inst.mod2Target == "tone" && inst.mod2Cc == 2
               && inst.key == 0 && inst.scale == "Chromatic" && inst.scaleLock == 0
               && std::isnan (inst.regionStartExact) && std::isnan (inst.regionEndExact) && inst.liveTemplate.empty() && inst.trailing.empty(),
           "INST holds defaultInst's state fields: seed 1, wheels vib/CC1 and tone/CC2, C Chromatic, no exact Region edges");

    // A new instance opens on Strand Demo (BLUEPRINT 6) and the Saw that opens up (D9).
    const auto clip = model::decodeClip (parsed.chunks[2].payload, parsed.chunks[2].payloadLength);
    const auto demo = model::loadClipPreset ("strand-demo");
    check (clip.clip.has_value() && demo.has_value() && clip.clip->notes.size() == 26
               && Bytes (parsed.chunks[2].payload, parsed.chunks[2].payload + parsed.chunks[2].payloadLength) == model::encodeClip (*demo),
           fmt ("CLIP holds Strand Demo through the clip codec (%d notes)", clip.clip.has_value() ? (int) clip.clip->notes.size() : -1));

    state::SampleRef smpl;
    check (state::decodeSmpl (parsed.chunks[3].payload, parsed.chunks[3].payloadLength, smpl) && smpl.kind == 0
               && smpl.starterId == "sawopen" && smpl.seed == 1,
           fmt ("SMPL names the default starter: \"%s\" seed %d", smpl.starterId.c_str(), (int) smpl.seed));

    state::UiState ui;
    check (state::decodeUist (parsed.chunks[4].payload, parsed.chunks[4].payloadLength, ui) && ui.theme == 0
               && ui.uiScalePercent == 100 && ui.logicalWidth == 1100 && ui.logicalHeight == 680 && ui.mode == 1,
           fmt ("UIST: dark, 100 %%, 1100 x 680, mode 1 (%d, %d %%, %d x %d, mode %d)", ui.theme, ui.uiScalePercent,
                ui.logicalWidth, ui.logicalHeight, ui.mode));

    StrandLockProcessor fresh;
    loadInto (fresh, b);
    check (saveOf (fresh) == b, "a fresh instance that loads it saves the same bytes");
    check (fresh.getStateBanner().isEmpty() && fresh.getUnknownChunkCount() == 0, "no banner, no unknown chunks");
}

SL_TEST ("state", roundTripWithStarters)
{
    section ("State: random parameters x clips x starters: get -> set -> get byte-identical, the starter made and playing");

    // Hold every starter the cases use, so each is made once.
    std::vector<std::shared_ptr<const sounds::StarterSound>> held;
    for (const auto& s : sounds::kStarterList)
        held.push_back (sounds::sharedStarterSound (s.id, 1));
    held.push_back (sounds::sharedStarterSound (sounds::StarterId::pad, 2));
    held.push_back (sounds::sharedStarterSound (sounds::StarterId::beat, 2));

    Rng rng { 20261109 };
    const int cases = 32;
    int sameBytes = 0, sameInFresh = 0, sameParams = 0, sameInst = 0, sameUi = 0, sameSample = 0, quiet = 0, soundsMatch = 0, sameClip = 0;
    for (int c = 0; c < cases; ++c)
    {
        const auto values = randomValues (rng);
        const auto inst = randomInst (rng);
        const auto ui = randomUi (rng);
        const auto& starter = sounds::kStarterList[rng.range (0, sounds::kNumStarters - 1)];
        const std::int32_t seed = (starter.id == sounds::StarterId::pad || starter.id == sounds::StarterId::beat) ? rng.range (1, 2) : 1;
        const auto smpl = starterRef (starter.id, seed);

        // A clip preset (or the empty clip), with the CLIP chunk moved to a random place in the container.
        const auto& presets = model::clipPresets();
        const int pick = rng.range (-1, (int) presets.size() - 1);
        const Bytes clip = pick < 0 ? emptyClipBytes() : model::encodeClip (*model::loadClipPreset (presets[(std::size_t) pick].id));
        auto chunks = ownChunks (values, inst, smpl, ui, clip);
        const Chunk clipChunk = chunks[2];
        chunks.erase (chunks.begin() + 2);
        chunks.insert (chunks.begin() + rng.range (0, 4), clipChunk);
        const Bytes blob = containerOf (chunks);

        StrandLockProcessor a;
        loadInto (a, blob);
        const Bytes saved = saveOf (a);
        sameBytes += saved == blob ? 1 : 0;
        sameParams += parametersAre (a, values) ? 1 : 0;

        const auto gotInst = a.getInstState();
        sameInst += state::encodeInst (gotInst) == state::encodeInst (inst) && gotInst.seed == inst.seed ? 1 : 0;
        sameUi += state::encodeUist (a.getUiState()) == state::encodeUist (ui) ? 1 : 0;
        const auto gotSample = a.getSampleRef();
        sameSample += gotSample.starterId == smpl.starterId && gotSample.seed == smpl.seed ? 1 : 0;
        const auto sound = a.getCurrentSound();   // the load waited for it (decision 3)
        soundsMatch += sound != nullptr && sound->builtIn && sound->starter == starter.id && sound->seed == seed ? 1 : 0;
        sameClip += model::encodeClip (*a.getDocument().current()) == clip ? 1 : 0;
        quiet += a.getStateBanner().isEmpty() && a.getUnknownChunkCount() == 0 ? 1 : 0;

        StrandLockProcessor b;
        loadInto (b, saved);
        sameInFresh += saveOf (b) == blob ? 1 : 0;
    }

    check (sameBytes == cases, fmt ("the save after a load is the loaded blob, byte for byte: %d of %d", sameBytes, cases));
    check (sameInFresh == cases, fmt ("a second instance loading that save saves the same bytes: %d of %d", sameInFresh, cases));
    check (sameParams == cases, fmt ("every parameter holds exactly the float it was saved with: %d of %d", sameParams, cases));
    check (sameInst == cases, fmt ("INST read back field for field: %d of %d", sameInst, cases));
    check (sameUi == cases, fmt ("UIST read back field for field: %d of %d", sameUi, cases));
    check (sameSample == cases && soundsMatch == cases, fmt ("SMPL read back, and that starter loaded: %d and %d of %d", sameSample, soundsMatch, cases));
    check (sameClip == cases, fmt ("the document holds the clip the CLIP chunk carried (it encodes to the same bytes): %d of %d", sameClip, cases));
    check (quiet == cases, fmt ("no banner and no unknown chunk: %d of %d", quiet, cases));
}

SL_TEST ("state", parametersByIdAndDefaults)
{
    section ("State: parameters are matched by id; one the state does not name gets its default, never the current value");

    // A PARM naming only Speed and Pitch (in the other order), plus an id this version does not have, and bypass.
    Bytes parm;
    {
        state::ByteWriter w (parm);
        w.u16 (4);
        w.str8 ("pitch");
        w.f32 (-7.25f);
        w.str8 ("futureKnob");
        w.f32 (0.125f);
        w.str8 ("macro1");
        w.f32 (0.75f);
        w.str8 ("bypass");
        w.f32 (1.0f);
    }
    const Bytes blob = containerOf ({ { state::kTagParm, parm } });

    StrandLockProcessor p;
    params::setPlainValue (p.getParam (params::P::drift), params::index (params::P::drift), 0.9f);
    params::setPlainValue (p.getParam (params::P::engine), params::index (params::P::engine), 2.0f);
    loadInto (p, blob);

    auto expected = defaults();
    expected[(std::size_t) params::index (params::P::pitch)] = -7.25f;
    expected[(std::size_t) params::index (params::P::macro1)] = 0.75f;
    check (parametersAre (p, expected), "Pitch and Speed load by id; Drift and Engine go back to their defaults (they were 90 % and Cloud)");
    check (juce::exactlyEqual (plainOf (p, kBypass), 0.0f), "bypass is the host's: a saved bypass is never loaded");

    // The blob had PARM only: the save writes that PARM back as it was (the unknown id kept), then the chunks
    // the blob did not have, from the defaults.
    const Bytes unedited = saveOf (p);
    const auto first = state::parseContainer (unedited.data(), unedited.size());
    std::string tags;
    for (const auto& c : first.chunks)
        tags += state::tagName (c.tag) + " ";
    check (first.status == state::HeaderStatus::ok && first.chunks.size() == 5
               && std::equal (blob.begin() + (std::ptrdiff_t) state::kHeaderSize, blob.end(), first.chunks[0].raw)
               && first.chunks[0].rawLength == blob.size() - state::kHeaderSize,
           fmt ("while nothing changed the PARM chunk is written back byte for byte, then the missing chunks (%s)", tags.c_str()));

    params::setPlainValue (p.getParam (params::P::drift), params::index (params::P::drift), 0.5f);
    const Bytes edited = saveOf (p);
    const auto parsed = state::parseContainer (edited.data(), edited.size());
    state::ParmChunk out;
    const bool decoded = parsed.status == state::HeaderStatus::ok && ! parsed.chunks.empty()
                      && state::decodeParm (parsed.chunks[0].payload, parsed.chunks[0].payloadLength, out);
    bool futureKept = false;
    float drift = -1.0f;
    for (const auto& e : out.entries)
    {
        futureKept = futureKept || (e.id == "futureKnob" && juce::exactlyEqual (e.value, 0.125f));
        if (e.id == "drift")
            drift = e.value;
    }
    check (decoded && out.entries.size() == (std::size_t) params::kNumParams && futureKept,
           fmt ("after an edit PARM is written anew: the 49 parameters plus the unknown id, kept (%d entries)", (int) out.entries.size()));
    check (juce::exactlyEqual (drift, 0.5f), fmt ("and it holds the edit (drift %g)", (double) drift));

    // Out-of-range and non-finite saved values are made legal.
    Bytes wild;
    {
        state::ByteWriter w (wild);
        w.u16 (4);
        w.str8 ("pitch");
        w.f32 (1000.0f);
        w.str8 ("drift");
        w.f32 (std::numeric_limits<float>::quiet_NaN());
        w.str8 ("engine");
        w.f32 (1.6f);
        w.str8 ("polyphony");
        w.f32 (-5.0f);
    }
    StrandLockProcessor q;
    loadInto (q, containerOf ({ { state::kTagParm, wild } }));
    check (juce::exactlyEqual (plainOf (q, params::index (params::P::pitch)), 48.0f)
               && juce::exactlyEqual (plainOf (q, params::index (params::P::drift)), 0.0f)
               && juce::exactlyEqual (plainOf (q, params::index (params::P::engine)), 2.0f)
               && juce::exactlyEqual (plainOf (q, params::index (params::P::polyphony)), 1.0f),
           fmt ("pitch 1000 -> 48, drift NaN -> its default 0, engine 1.6 -> 2 (Cloud), polyphony -5 -> 1 (%g, %g, %g, %g)",
                (double) plainOf (q, params::index (params::P::pitch)), (double) plainOf (q, params::index (params::P::drift)),
                (double) plainOf (q, params::index (params::P::engine)), (double) plainOf (q, params::index (params::P::polyphony))));
}

SL_TEST ("state", unknownChunkKept)
{
    section ("State: an unknown chunk (a later version's) is skipped, kept and written back where it was (PLUGIN-PLAN 3 (a))");

    Rng rng { 7 };
    auto values = defaults();
    values[(std::size_t) params::index (params::P::speed)] = 3.5f;
    auto chunks = ownChunks (values, state::InstState {}, starterRef (sounds::StarterId::saw, 1), state::UiState {});
    const Bytes unknown = randomBytes (rng, 37);
    chunks.insert (chunks.begin() + 2, Chunk { state::fourCC ("CLPL"), unknown, 3 });
    const Bytes blob = containerOf (chunks);

    StrandLockProcessor p;
    loadInto (p, blob);
    check (juce::exactlyEqual (plainOf (p, params::index (params::P::speed)), 3.5f), "the chunks around it still load (Sample Speed 3.5)");
    check (p.getUnknownChunkCount() == 1, fmt ("one unknown chunk is kept (%d)", p.getUnknownChunkCount()));
    check (p.getStateBanner().isEmpty(), "an intact unknown chunk shows no banner");
    check (saveOf (p) == blob, "re-saved byte-identically");

    params::setPlainValue (p.getParam (params::P::speed), params::index (params::P::speed), 2.0f);
    const Bytes edited = saveOf (p);
    const auto parsed = state::parseContainer (edited.data(), edited.size());
    bool keptInPlace = parsed.status == state::HeaderStatus::ok && parsed.chunks.size() == 6
                    && parsed.chunks[2].tag == state::fourCC ("CLPL") && parsed.chunks[2].version == 3
                    && parsed.chunks[2].payloadLength == unknown.size()
                    && std::equal (unknown.begin(), unknown.end(), parsed.chunks[2].payload);
    check (keptInPlace, "after an edit the unknown chunk is still written, unchanged, in its place (third)");
    check (edited != blob && edited.size() == blob.size(), "and the edit is in the save (PARM rewritten, same size)");
}

SL_TEST ("state", newerChunkFieldsKept)
{
    section ("State: a newer chunk version's extra trailing fields are kept and written back after the known ones");

    state::InstState inst;
    inst.seed = 42;
    inst.trailing = { 1, 2, 3, 4, 5, 6, 7, 8 };   // what a later version appended
    auto chunks = ownChunks (defaults(), inst, starterRef (sounds::StarterId::saw, 1), state::UiState {});
    chunks[1].version = 2;
    const Bytes blob = containerOf (chunks);

    StrandLockProcessor p;
    loadInto (p, blob);
    const auto got = p.getInstState();
    check (got.seed == 42 && got.trailing == inst.trailing, fmt ("INST v2 reads its v1 fields (seed %d) and keeps 8 trailing bytes (%d)",
                                                                 (int) got.seed, (int) got.trailing.size()));
    check (saveOf (p) == blob, "re-saved byte-identically while unedited");

    auto edited = got;
    edited.seed = 43;
    p.setInstState (edited);
    const Bytes after = saveOf (p);
    const auto parsed = state::parseContainer (after.data(), after.size());
    state::InstState back;
    const bool ok = parsed.status == state::HeaderStatus::ok && parsed.chunks.size() == 5
                 && state::decodeInst (parsed.chunks[1].payload, parsed.chunks[1].payloadLength, back);
    check (ok && back.seed == 43 && back.trailing == inst.trailing && parsed.chunks[1].version == 2,
           "after editing the seed: INST is re-encoded with seed 43, still version 2, the 8 bytes still after it");
    check (p.readInst().seed == 43, "and the audio thread's instrument plays seed 43");
}

SL_TEST ("state", unreadableBlobsKept)
{
    section ("State: what cannot be read loads nothing, is written back unchanged until an edit (a UIST change is none), and shows the banner (PLUGIN-PLAN 3 (e))");

    auto values = defaults();
    values[(std::size_t) params::index (params::P::drift)] = 0.4f;
    const Bytes good = containerOf (ownChunks (values, state::InstState {}, starterRef (sounds::StarterId::saw, 1), state::UiState {}));

    struct Case { const char* what; Bytes data; };
    std::vector<Case> cases;
    {
        auto b = good;
        b[4] = (std::uint8_t) (sl::core::kStateFormatVersion + 1);
        refreshCrcs (b);
        cases.push_back ({ "formatVersion 2 (a newer StrandLock)", b });
    }
    {
        auto b = good;
        b[0] = 'G';
        cases.push_back ({ "another plugin's magic", b });
    }
    {
        auto b = good;
        b[12] ^= 0x01u;
        cases.push_back ({ "a wrong header CRC", b });
    }
    {
        auto b = good;
        put32 (b, 8, (std::uint32_t) b.size() + 4);
        refreshCrcs (b);
        cases.push_back ({ "a totalLength past the end", b });
    }
    {
        auto b = good;
        b[4] = 0;
        refreshCrcs (b);
        cases.push_back ({ "formatVersion 0", b });
    }
    cases.push_back ({ "15 bytes", Bytes (good.begin(), good.begin() + 15) });

    for (const auto& c : cases)
    {
        StrandLockProcessor p;
        loadInto (p, c.data);
        const bool nothing = parametersAre (p, defaults());
        const bool kept = saveOf (p) == c.data;
        const bool banner = p.getStateBanner().isNotEmpty();

        // What the window writes (UIST: tab, mode, fold, size, scale, theme) is not an edit of the project.
        auto ui = p.getUiState();
        ui.tab = "diag";
        ui.mode = 3;
        ui.folded = 1;
        ui.theme = 1;
        ui.uiScalePercent = 150;
        ui.logicalWidth = 1300;
        p.setUiState (ui);
        const bool keptThroughUi = saveOf (p) == c.data && p.getStateBanner().isNotEmpty();

        params::setPlainValue (p.getParam (params::P::pitch), params::index (params::P::pitch), 5.0f);
        const Bytes after = saveOf (p);
        const bool replaced = after != c.data && state::parseContainer (after.data(), after.size()).status == state::HeaderStatus::ok;
        check (nothing && kept && banner && keptThroughUi && replaced && p.getStateBanner().isEmpty(),
               fmt ("%s: loads nothing %s, re-saved unchanged %s, banner %s; still kept after UIST changes %s; after an edit replaced %s, banner gone %s",
                    c.what, nothing ? "yes" : "NO", kept ? "yes" : "NO", banner ? "yes" : "NO", keptThroughUi ? "yes" : "NO",
                    replaced ? "yes" : "NO", p.getStateBanner().isEmpty() ? "yes" : "NO"));
    }

    // A known chunk with a bad CRC is skipped on its own; the others load.
    {
        auto chunks = ownChunks (values, state::InstState {}, starterRef (sounds::StarterId::saw, 1), state::UiState {});
        state::UiState light;
        light.theme = 1;
        chunks[3].payload = state::encodeUist (light);
        auto b = containerOf (chunks);
        b[16 + 12] ^= 0x40u;   // PARM's payload CRC
        StrandLockProcessor p;
        loadInto (p, b);
        check (parametersAre (p, defaults()) && p.getUiState().theme == 1,
               "a PARM with a bad CRC is skipped (the parameters keep their defaults) while UIST still loads (light)");
        check (saveOf (p) == b && p.getStateBanner().isNotEmpty(), "it is written back unchanged, with the banner");
        params::setPlainValue (p.getParam (params::P::pitch), params::index (params::P::pitch), 5.0f);
        const Bytes after = saveOf (p);
        const auto parsed = state::parseContainer (after.data(), after.size());
        check (parsed.status == state::HeaderStatus::ok && ! parsed.chunks.empty() && parsed.chunks[0].crcOk
                   && p.getStateBanner().isEmpty(),
               "after a parameter edit PARM is written anew (CRC good) and the banner goes");
    }

    // A CLIP with a bad CRC: kept (and the banner stays) through edits of other parts; replaced by a clip edit
    // (test_state_clip.cpp, case (e)).
    {
        auto chunks = ownChunks (values, state::InstState {}, starterRef (sounds::StarterId::saw, 1), state::UiState {}, smallClipBytes());
        auto b = containerOf (chunks);
        const auto parsed = state::parseContainer (b.data(), b.size());
        const std::size_t clipAt = (std::size_t) (parsed.chunks[2].raw - b.data());
        b[clipAt + 12] ^= 0x01u;
        StrandLockProcessor p;
        loadInto (p, b);
        check (parametersAre (p, values) && p.getDocument().current()->notes.empty(),
               "with a bad CLIP CRC the other chunks load (Drift 40 %) and the clip keeps its default (empty)");
        check (saveOf (p) == b && p.getStateBanner().isNotEmpty(), "the blob is re-saved byte-identically, with the banner");
        params::setPlainValue (p.getParam (params::P::pitch), params::index (params::P::pitch), 5.0f);
        const Bytes after = saveOf (p);
        const auto again = state::parseContainer (after.data(), after.size());
        const std::size_t clipRaw = parsed.chunks[2].rawLength;
        const bool clipKept = again.status == state::HeaderStatus::ok && again.chunks.size() == 5 && again.chunks[2].tag == state::kTagClip
                           && again.chunks[2].rawLength == clipRaw
                           && std::equal (b.begin() + (std::ptrdiff_t) clipAt, b.begin() + (std::ptrdiff_t) (clipAt + clipRaw), again.chunks[2].raw);
        check (clipKept && after != b && p.getStateBanner().isNotEmpty(),
               "after a parameter edit the damaged CLIP is still written byte for byte (its part is unedited) and the banner stays");
    }
}

SL_TEST ("state", truncation)
{
    section ("State: every prefix of a 4 KB blob and 200 cuts of a 2 MB blob: no crash, no exception, parameters legal");

    auto values = defaults();
    values[(std::size_t) params::index (params::P::pitch)] = 12.5f;
    values[(std::size_t) params::index (params::P::engine)] = 0.0f;
    values[(std::size_t) params::index (params::P::outGain)] = -6.0f;
    const Bytes small = referenceBlob (values, 4096, 11);
    check (small.size() == 4096, fmt ("the reference blob is 4096 bytes (%d)", (int) small.size()));

    const auto parsedSmall = state::parseContainer (small.data(), small.size());
    const std::size_t parmEnd = parsedSmall.chunks.empty() ? 0 : state::kHeaderSize + parsedSmall.chunks[0].rawLength;

    StrandLockProcessor p, q;
    int escaped = 0, illegal = 0, unsound = 0, unstable = 0;
    int fixedWithParm = 0, fixedWithParmLoaded = 0, fixedWithoutParm = 0, fixedWithoutParmDefault = 0;
    double worstMs = 0.0;
    for (std::size_t n = 0; n <= small.size(); ++n)
    {
        // As cut: the header's totalLength now runs past the end.
        const Bytes cut (small.begin(), small.begin() + (std::ptrdiff_t) n);
        const double t0 = nowMs();
        escaped += loadInto (p, cut) ? 0 : 1;
        worstMs = std::max (worstMs, nowMs() - t0);
        illegal += parametersLegal (p) ? 0 : 1;
        unsound += saveIsSound (p, cut) ? 0 : 1;

        // As a host that cut the data and wrote a matching header would: the chunk walk meets a cut chunk.
        if (n >= state::kHeaderSize)
        {
            Bytes fixed = cut;
            put32 (fixed, 8, (std::uint32_t) n);
            put32 (fixed, 12, state::crc32 (fixed.data(), 12));
            escaped += loadInto (p, fixed) ? 0 : 1;
            illegal += parametersLegal (p) ? 0 : 1;
            unsound += saveIsSound (p, fixed) ? 0 : 1;

            // What it saves loads to the same thing and saves the same bytes again (the cut end is kept last).
            const Bytes resaved = saveOf (p);
            loadInto (q, resaved);
            unstable += saveOf (q) == resaved && parametersAre (q, values) == parametersAre (p, values) ? 0 : 1;

            if (n >= parmEnd)
            {
                ++fixedWithParm;
                fixedWithParmLoaded += parametersAre (p, values) ? 1 : 0;
            }
            else
            {
                ++fixedWithoutParm;
                fixedWithoutParmDefault += parametersAre (p, defaults()) ? 1 : 0;
            }
        }
    }
    check (escaped == 0 && illegal == 0, fmt ("4097 prefixes (and 4081 with their header fixed up): %d exceptions, %d loads leaving a parameter out of range",
                                              escaped, illegal));
    check (unsound == 0, fmt ("after every load the next save is the blob kept as it was, or a container that reads (%d not)", unsound));
    check (unstable == 0, fmt ("a fixed-up prefix's save loads to the same parameters and saves the same bytes again (%d not)", unstable));
    check (fixedWithParm > 0 && fixedWithParmLoaded == fixedWithParm,
           fmt ("a fixed-up prefix holding the whole PARM chunk loads its parameters: %d of %d", fixedWithParmLoaded, fixedWithParm));
    check (fixedWithoutParm > 0 && fixedWithoutParmDefault == fixedWithoutParm,
           fmt ("one that cuts PARM loads the defaults (the cut chunk is kept, not read): %d of %d", fixedWithoutParmDefault, fixedWithoutParm));

    const Bytes big = referenceBlob (values, 2u * 1024u * 1024u, 12);
    Rng rng { 99 };
    std::vector<std::size_t> cuts = { 0, 1, 15, 16, 17, 31, 32, parmEnd - 1, parmEnd, parmEnd + 16, big.size() - 1, big.size() };
    while (cuts.size() < 200)
        cuts.push_back ((std::size_t) rng.range (0, (int) big.size()));
    int bigEscaped = 0, bigIllegal = 0, bigUnsound = 0;
    double bigWorstMs = 0.0;
    for (const auto n : cuts)
    {
        Bytes cut (big.begin(), big.begin() + (std::ptrdiff_t) n);
        if (n >= state::kHeaderSize && (n % 2) == 0)
        {
            put32 (cut, 8, (std::uint32_t) n);
            put32 (cut, 12, state::crc32 (cut.data(), 12));
        }
        const double t0 = nowMs();
        bigEscaped += loadInto (p, cut) ? 0 : 1;
        bigWorstMs = std::max (bigWorstMs, nowMs() - t0);
        bigIllegal += parametersLegal (p) ? 0 : 1;
        bigUnsound += saveIsSound (p, cut) ? 0 : 1;
    }
    check (bigEscaped == 0 && bigIllegal == 0 && bigUnsound == 0,
           fmt ("200 cuts of a %d-byte blob (half with the header fixed up): %d exceptions, %d illegal parameters, %d unsound saves",
                (int) big.size(), bigEscaped, bigIllegal, bigUnsound));
    const bool bigLoaded = loadInto (p, big);
    check (bigLoaded && parametersAre (p, values) && saveOf (p) == big, "the whole 2 MB blob loads its parameters and re-saves byte-identically");

    const double worst = std::max (worstMs, bigWorstMs);
    note (fmt ("slowest load: %.2f ms on the 4 KB prefixes, %.2f ms on the 2 MB cuts (target 50 ms, gate 250 ms)", worstMs, bigWorstMs));
    check (worst <= 250.0, fmt ("no load slower than 250 ms (%.2f ms)", worst));
}

SL_TEST ("state", corruptionFuzz)
{
    section ("State: 10,000 single-byte mutations (and 10,000 with their CRCs fixed up) and 1,000 random blobs");

    auto values = defaults();
    values[(std::size_t) params::index (params::P::heads)] = 4.5f;
    const Bytes ref = referenceBlob (values, 4096, 21);
    StrandLockProcessor p;
    Rng rng { 424242 };

    struct Tally
    {
        int loads = 0, escaped = 0, illegal = 0, unsound = 0, overAlloc = 0, overLive = 0;
        std::size_t largest = 0;
        long long peakLive = 0;
        double worstMs = 0.0;
    };

    auto run = [&p] (const Bytes& b, Tally& t)
    {
        alloc::reset();
        const double t0 = nowMs();
        bool ok = false;
        {
            const alloc::Scope counting;
            ok = loadInto (p, b);
        }
        t.worstMs = std::max (t.worstMs, nowMs() - t0);
        const auto c = alloc::counts();
        ++t.loads;
        t.escaped += ok ? 0 : 1;
        t.illegal += parametersLegal (p) ? 0 : 1;
        t.unsound += saveIsSound (p, b) ? 0 : 1;
        t.largest = std::max (t.largest, c.largest);
        t.peakLive = std::max (t.peakLive, c.peakLiveBytes);
        t.overAlloc += c.largest > 51u * 1024u * 1024u ? 1 : 0;
        t.overLive += c.peakLiveBytes > 2 * (long long) b.size() + 64ll * 1024 * 1024 ? 1 : 0;
    };

    Tally plain, fixed, random;
    int fixedReadable = 0;
    for (int i = 0; i < 10000; ++i)
    {
        Bytes b = ref;
        const auto at = (std::size_t) rng.range (0, (int) b.size() - 1);
        b[at] = (std::uint8_t) (b[at] ^ (std::uint8_t) rng.range (1, 255));
        run (b, plain);

        Bytes f = ref;
        const auto at2 = (std::size_t) rng.range (0, (int) f.size() - 1);
        f[at2] = (std::uint8_t) (f[at2] ^ (std::uint8_t) rng.range (1, 255));
        refreshCrcs (f);
        run (f, fixed);
        fixedReadable += state::parseContainer (f.data(), f.size()).status == state::HeaderStatus::ok ? 1 : 0;
    }
    for (int i = 0; i < 1000; ++i)
    {
        Bytes b = randomBytes (rng, (std::size_t) rng.range (0, 65536));
        if ((i % 2) == 1 && b.size() >= state::kHeaderSize)
        {
            // Half of them start with a valid header, so the chunk walk runs over random bytes.
            std::memcpy (b.data(), state::kMagic, 4);
            b[4] = 1;
            b[5] = 0;
            put32 (b, 8, (std::uint32_t) b.size());
            put32 (b, 12, state::crc32 (b.data(), 12));
        }
        run (b, random);
    }

    for (const auto* t : { &plain, &fixed, &random })
    {
        const char* what = t == &plain ? "single-byte mutations" : t == &fixed ? "mutations with CRCs fixed up" : "random blobs (0..64 KB)";
        check (t->escaped == 0 && t->illegal == 0 && t->unsound == 0,
               fmt ("%d %s: %d exceptions, %d leaving a parameter out of range, %d unsound saves", t->loads, what, t->escaped, t->illegal, t->unsound));
        check (t->overAlloc == 0 && t->overLive == 0,
               fmt ("%s: largest single allocation %zu bytes (limit 51 MiB), most live %lld bytes (limit 2 x blob + 64 MiB)",
                    what, t->largest, t->peakLive));
        // A load that names another starter waits for the worker to make it (decision 3): a missed target goes to NOTES-M2.
        note (fmt ("%s: slowest load %.2f ms (target 50 ms, gate 250 ms)", what, t->worstMs));
        check (t->worstMs <= 250.0, fmt ("%s: no load slower than 250 ms (%.2f ms)", what, t->worstMs));
    }
    check (fixedReadable > 9000, fmt ("most fixed-up mutations still read as a container, so the chunk decoders saw them (%d of 10000)", fixedReadable));
    note (fmt ("allocation sizes are counted %s", alloc::coverage()));
}

SL_TEST ("state", userSettingsPersist)
{
    section ("UserSettings: theme, UI scale and the Keys default persist in a per-user file, across instances");

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("StrandLockTests")
                          .getNonexistentChildFile ("user-settings-test", ".settings", false);
    {
        state::UserSettings fresh (file);
        check (! fresh.theme().has_value() && ! fresh.uiScalePercent().has_value() && ! fresh.keysDefault(),
               "a new file has no theme, no UI scale and the Keys switch off");
        fresh.setTheme (state::Theme::light);
        fresh.setUiScalePercent (150);
        fresh.setKeysDefault (true);
        check (file.existsAsFile(), fmt ("every set is written at once (%s exists)", file.getFileName().toRawUTF8()));
    }
    {
        state::UserSettings again (file);
        check (again.theme() == state::Theme::light && again.uiScalePercent() == 150 && again.keysDefault(),
               "another instance reads them back: light, 150 %, Keys on");
        again.setUiScalePercent (300);
        check (again.uiScalePercent() == 200, fmt ("the UI scale is kept inside 75..200 %% (300 -> %d)", again.uiScalePercent().value_or (-1)));
        again.setTheme (state::Theme::dark);
    }
    {
        state::UserSettings third (file);
        check (third.theme() == state::Theme::dark && third.uiScalePercent() == 200, "and the next one sees the changes");
    }
    file.deleteFile();

    check (state::UserSettings::shared() == state::UserSettings::shared(), "every instance in a process shares one UserSettings");
    const auto real = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("StrandLock");
    check (! state::UserSettings::defaultFile().isAChildOf (real),
           fmt ("the test runner never uses the real per-user file (%s)", state::UserSettings::defaultFile().getFullPathName().toRawUTF8()));

    // With no per-user theme, a project's UIST copy decides; the processor's setTheme writes both.
    StrandLockProcessor p;
    if (! state::UserSettings::shared()->theme().has_value())
    {
        auto ui = p.getUiState();
        ui.theme = 1;
        p.setUiState (ui);
        check (p.getTheme() == state::Theme::light, "no per-user theme: the project's UIST copy (light) is used");
    }
    else
    {
        note ("a per-user theme is already set in this run: the UIST fallback is not checked");
    }
}
