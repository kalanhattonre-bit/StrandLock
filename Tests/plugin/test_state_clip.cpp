// M2's full state through the processor (PLUGIN-PLAN 3 "State tests", 4 M2 "state with a clip", 7.4 decisions 3
// and 4): every clip preset and fixture clip x random parameters through getStateInformation and
// setStateInformation, byte-identical with the clip bit for bit; a pluginval-style restore; the forward
// compatibility cases (a)-(e); the limits case; the SMPL file record (a TestSignals WAV in a temp folder, a song's
// default part, a file cut to 10 minutes); a missing or changed file that keeps its record and plays the default
// starter; and a state load naming another starter that plays it on the very first block.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "doc/Sha256.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/CodecSpec.h"
#include "model/Generators.h"
#include "model/ModelFixture.h"
#include "model/Normalize.h"
#include "model/NoteSettings.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "params/ParameterLayout.h"
#include "sounds/StarterSounds.h"
#include "state/Crc32.h"
#include "state/StateChunks.h"
#include "state/StateFormat.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using state::Bytes;

    constexpr int kBypass = params::index (params::P::bypass);

    /** splitmix64. */
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

    Bytes saveOf (StrandLockProcessor& p)
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        const auto* d = static_cast<const std::uint8_t*> (m.getData());
        return Bytes (d, d + m.getSize());
    }

    void loadInto (StrandLockProcessor& p, const Bytes& b)
    {
        auto exact = std::make_unique<std::uint8_t[]> (std::max<std::size_t> (1, b.size()));
        if (! b.empty())
            std::memcpy (exact.get(), b.data(), b.size());
        p.setStateInformation (b.empty() ? nullptr : exact.get(), (int) b.size());
    }

    float plainOf (StrandLockProcessor& p, int i) { return params::plainValueOf (p.getParam (i)); }
    float plainOf (StrandLockProcessor& p, params::P which) { return plainOf (p, params::index (which)); }

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

    void setAll (StrandLockProcessor& p, const std::array<float, params::kNumParams>& values)
    {
        for (int i = 0; i < params::kNumParams; ++i)
            if (i != kBypass)
                params::setPlainValue (p.getParam (i), i, values[(std::size_t) i]);
    }

    bool parametersAre (StrandLockProcessor& p, const std::array<float, params::kNumParams>& values)
    {
        for (int i = 0; i < params::kNumParams; ++i)
            if (i != kBypass && ! juce::exactlyEqual (plainOf (p, i), values[(std::size_t) i]))
                return false;
        return true;
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

    state::SampleRef starterRef (sounds::StarterId id, std::int32_t seed)
    {
        state::SampleRef r;
        r.kind = state::kSampleStarter;
        r.starterId = sounds::starterInfo (id).code;
        r.seed = seed;
        return r;
    }

    /** The five chunks this version writes. */
    std::vector<Chunk> ownChunks (const Bytes& clip, const state::SampleRef& smpl = starterRef (sounds::kDefaultStarter, 1),
                                  const std::array<float, params::kNumParams>& values = defaults())
    {
        return { { state::kTagParm, state::encodeParm (values, {}, {}) },
                 { state::kTagInst, state::encodeInst (state::InstState {}) },
                 { state::kTagClip, clip },
                 { state::kTagSmpl, state::encodeSmpl (smpl) },
                 { state::kTagUist, state::encodeUist (state::UiState {}) } };
    }

    Bytes payloadOf (const Bytes& blob, std::uint32_t tag)
    {
        const auto parsed = state::parseContainer (blob.data(), blob.size());
        for (const auto& c : parsed.chunks)
            if (c.tag == tag && c.payload != nullptr)
                return Bytes (c.payload, c.payload + c.payloadLength);
        return {};
    }

    std::vector<std::uint32_t> tagsOf (const Bytes& blob)
    {
        std::vector<std::uint32_t> tags;
        for (const auto& c : state::parseContainer (blob.data(), blob.size()).chunks)
            tags.push_back (c.tag);
        return tags;
    }

    /** Two clips with the same settings and every note bit for bit the same (ids, links, every node). */
    bool clipsBitIdentical (const model::Clip& a, const model::Clip& b, std::string& why)
    {
        if (a.bars != b.bars || a.beatsPerBar != b.beatsPerBar || ! fixture::same (a.offsetBeats, b.offsetBeats) || a.loop != b.loop
            || a.tieAcrossLoop != b.tieAcrossLoop)
        {
            why = "the clip settings differ";
            return false;
        }
        if (a.notes.size() != b.notes.size())
        {
            why = fmt ("%d notes, not %d", (int) a.notes.size(), (int) b.notes.size());
            return false;
        }
        for (std::size_t i = 0; i < a.notes.size(); ++i)
            if (! modelfx::bitIdentical (*a.notes[i], *b.notes[i], why))
                return false;
        return true;
    }

    struct Named
    {
        std::string name;
        model::Clip clip;
    };

    /** The fixture clips (as the codec test gathers them): every named clip of the model fixture files, the 12
        clip presets, recipes 1-8 on Strand Demo, and normalize.json's clip results. */
    std::vector<Named> fixtureClips()
    {
        std::vector<Named> out;
        for (const char* file : { "model/tools.json", "model/glides.json", "model/generators.json", "model/shapes.json", "model/snapshots.json",
                                  "model/presets.json" })
        {
            core::JsonValue doc;
            std::map<std::string, model::Clip> clips;
            if (! fixture::load (file, doc) || ! modelfx::clipsFrom (doc, clips))
                continue;
            for (auto& [name, clip] : clips)
                out.push_back ({ std::string (file) + " " + name, std::move (clip) });
        }
        for (const auto& p : model::clipPresets())
            if (auto clip = model::loadClipPreset (p.id))
                out.push_back ({ "preset:" + p.id, std::move (*clip) });
        if (const auto demo = model::loadClipPreset ("strand-demo"))
            for (int n = 1; n <= 8; ++n)
                if (auto r = model::applyRecipe (*demo, core::defaultInst(), n, model::Ids::all()))
                    out.push_back ({ "recipe " + std::to_string (n), std::move (r->clip) });
        core::JsonValue doc;
        if (fixture::load ("model/normalize.json", doc))
        {
            const core::JsonValue& cases = doc["cases"];
            for (std::size_t i = 0; i < cases.size(); ++i)
                if (cases[i]["kind"].asString() == "clip")
                {
                    model::Clip clip;
                    std::string why;
                    if (modelfx::clipFrom (cases[i]["result"], clip, why))
                        out.push_back ({ "normalize: " + cases[i]["label"].asString(), std::move (clip) });
                }
        }
        return out;
    }

    model::Clip strandDemo() { return *model::loadClipPreset ("strand-demo"); }

    /** Strand Demo with generators on two notes (it has none of its own), so the generator records exist. */
    model::Clip demoWithGens()
    {
        model::Clip clip = strandDemo();
        model::GenPatch g;
        g.rate = core::RateId::r1_8;
        clip = model::setGen (clip, model::Ids { clip.notes[0]->id, clip.notes[3]->id }, core::LaneId::amp, model::makeGen (model::GenKind::random, g));
        return clip;
    }

    /** One note's pitch moved by 7 semitones, as one undo step through the processor. */
    doc::EditOutcome movePitch (StrandLockProcessor& p, std::size_t note, double& newPitch)
    {
        const auto clip = p.getDocument().current();
        model::NotePatch patch;
        newPitch = clip->notes[note]->p + 7.0;
        patch.p = newPitch;
        return p.editClip ("Move a note", model::updateNotes (*clip, model::Ids (clip->notes[note]->id), patch));
    }

    double doubleAt (const Bytes& b, std::size_t at)
    {
        double v = 0.0;
        if (at + 8 <= b.size())
            std::memcpy (&v, &b[at], 8);
        return v;
    }

    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    //==============================================================================================
    juce::File tempFolder (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLockTests").getNonexistentChildFile (name, "", false);
        dir.createDirectory();
        return dir;
    }

    /** Writes channels as a WAV: 32-bit float (bits 32) or 16-bit PCM (bits 16). */
    bool writeWav (const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate, int bits)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto options = juce::AudioFormatWriterOptions {}
                           .withSampleRate (sampleRate)
                           .withNumChannels ((int) channels.size())
                           .withBitsPerSample (bits)
                           .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                         : juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;
        std::vector<const float*> ptrs;
        for (const auto& c : channels)
            ptrs.push_back (c.data());
        const bool ok = writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) channels[0].size());
        writer.reset();
        return ok && file.existsAsFile();
    }

    doc::Digest digestOfFile (const juce::File& file)
    {
        juce::MemoryBlock m;
        file.loadFileAsData (m);
        return doc::sha256 (m.getData(), m.getSize());
    }

    std::vector<float> sine (double hz, double seconds, double sr, float amp)
    {
        std::vector<float> x ((std::size_t) std::llround (seconds * sr));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = amp * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / sr);
        return x;
    }

    bool samplesAre (const float* got, const std::vector<float>& want)
    {
        return got != nullptr && std::memcmp (got, want.data(), want.size() * sizeof (float)) == 0;
    }

    /** One block of n samples with a note on at sample 0. */
    std::vector<float> firstBlock (StrandLockProcessor& p, int n)
    {
        juce::AudioBuffer<float> buffer (2, n);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        p.processBlock (buffer, midi);
        return std::vector<float> (buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
    }
}

//==================================================================================================
SL_TEST ("state", clipRoundTripEveryFixtureClip)
{
    section ("State with a clip: every clip preset and fixture clip x random parameters, through the processor: byte-identical, the clip bit for bit");

    const auto clips = fixtureClips();
    check (clips.size() >= 40, fmt ("the fixture clips: %d (the model fixture files, 12 presets, 8 recipes, normalize.json)", (int) clips.size()));

    StrandLockProcessor a, b;
    Rng rng { 20261109 };
    int sameBytes = 0, sameParams = 0, sameClip = 0, sameEncoding = 0, notes = 0;
    std::string firstWhy;
    for (const auto& c : clips)
    {
        const auto values = randomValues (rng);
        setAll (a, values);
        a.getDocument().replace (std::make_shared<const model::Clip> (c.clip));
        const Bytes saved = saveOf (a);

        loadInto (b, saved);
        sameBytes += saveOf (b) == saved ? 1 : 0;
        // The document's own encoding, not just the kept chunk (a save writes a chunk unchanged since the load as it was).
        sameEncoding += model::encodeClip (*b.getDocument().current()) == payloadOf (saved, state::kTagClip) ? 1 : 0;
        sameParams += parametersAre (b, values) ? 1 : 0;

        // The decoder rebuilds every note through normalizeClip (plan 3), so B holds normalizeClip of A's clip.
        std::string why;
        const model::Clip expected = model::normalizeClip (c.clip);
        if (clipsBitIdentical (*b.getDocument().current(), expected, why))
            ++sameClip;
        else if (firstWhy.empty())
            firstWhy = c.name + ": " + why;
        notes += (int) c.clip.notes.size();
    }
    const int n = (int) clips.size();
    check (sameBytes == n, fmt ("get -> set -> get is byte-identical: %d of %d clips", sameBytes, n));
    check (sameEncoding == n, fmt ("and the loaded clip encodes to the CLIP payload it came from: %d of %d", sameEncoding, n));
    check (sameParams == n, fmt ("every parameter holds exactly the float it was saved with: %d of %d", sameParams, n));
    check (sameClip == n, fmt ("ids, links, every node and every double bit for bit: %d of %d clips (%d notes) %s", sameClip, n, notes, firstWhy.c_str()));
}

SL_TEST ("state", pluginvalStyleRestore)
{
    section ("State: a pluginval-style restore with a non-empty clip (random normalised values, get, set into a new instance)");

    juce::Random rng (4711);
    int same = 0, clipSame = 0;
    const int rounds = 10;
    for (int r = 0; r < rounds; ++r)
    {
        StrandLockProcessor p;   // Strand Demo, a non-empty clip
        for (int i = 0; i < params::kNumParams; ++i)
            if (i != kBypass)
                p.getParam (i).setValueNotifyingHost (rng.nextFloat());
        const Bytes saved = saveOf (p);
        StrandLockProcessor q;
        q.getDocument().replace (std::make_shared<const model::Clip> (model::newClip (7)));
        loadInto (q, saved);
        bool all = true;
        for (int i = 0; i < params::kNumParams; ++i)
            all = all && (i == kBypass || juce::exactlyEqual (plainOf (q, i), plainOf (p, i)));
        same += all ? 1 : 0;
        clipSame += model::encodeClip (*q.getDocument().current()) == model::encodeClip (*p.getDocument().current())
                     && q.getDocument().current()->notes.size() == 26 ? 1 : 0;
    }
    check (same == rounds, fmt ("every parameter restored exactly: %d of %d rounds", same, rounds));
    check (clipSame == rounds, fmt ("and the 26-note clip with them: %d of %d", clipSame, rounds));
}

SL_TEST ("state", forwardCompatThroughProcessor)
{
    section ("Forward compatibility through the processor (plan 3 (a)-(d)): load and re-save byte-identically; after a pitch edit only p changed");

    // (a) an extra unknown chunk, kept in its place through a clip edit
    {
        Rng rng { 31 };
        auto chunks = ownChunks (model::encodeClip (strandDemo()));
        Bytes unknown (61);
        for (auto& x : unknown)
            x = rng.byte();
        chunks.insert (chunks.begin() + 3, Chunk { state::fourCC ("CLPL"), unknown, 2 });
        const Bytes blob = containerOf (chunks);
        StrandLockProcessor p;
        loadInto (p, blob);
        check (saveOf (p) == blob && p.getUnknownChunkCount() == 1 && p.getStateBanner().isEmpty(),
               "(a) an unknown chunk: loaded, kept (1), no banner, re-saved byte-identically");
        double newP = 0.0;
        const auto edit = movePitch (p, 4, newP);
        const Bytes after = saveOf (p);
        const auto parsed = state::parseContainer (after.data(), after.size());
        const bool inPlace = parsed.chunks.size() == 6 && parsed.chunks[3].tag == state::fourCC ("CLPL") && parsed.chunks[3].version == 2
                          && Bytes (parsed.chunks[3].payload, parsed.chunks[3].payload + parsed.chunks[3].payloadLength) == unknown;
        const auto clip = model::decodeClip (payloadOf (after, state::kTagClip));
        check (edit.accepted && inPlace && clip.clip.has_value() && clip.clip->notes[4]->p == newP && payloadOf (after, state::kTagParm) == payloadOf (blob, state::kTagParm),
               "(a) after a pitch edit the unknown chunk is still written, unchanged, in its place; CLIP holds the edit; PARM is untouched");
    }

    // (b) (c) (d) through CodecSpec, the independent writer of plan 3's records
    struct Case { const char* name; codecspec::Hooks hooks; std::size_t note; };
    std::vector<Case> cases;
    {
        Case b { "(b) every record with 8 trailing bytes", {}, 3 };
        b.hooks.trailing = { 0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04 };
        cases.push_back (b);
        Case c { "(c) a note with an unknown lane id", {}, 4 };
        c.hooks.unknownLaneNote = 4;
        c.hooks.unknownLanes = { codecspec::laneRecord ("sparkle", 3, true), codecspec::laneRecord ("chance", 5, false) };
        cases.push_back (c);
        Case d { "(d) an unknown motion string and node shape code 9", {}, 6 };
        d.hooks.oddNote = 6;
        d.hooks.motion = "spiral";
        d.hooks.oddLane = static_cast<int> (core::LaneId::tone);
        d.hooks.shapeCode = 9;
        cases.push_back (d);
    }
    const model::Clip base = demoWithGens();
    for (const auto& c : cases)
    {
        std::vector<std::size_t> pOffsets;
        const Bytes newer = codecspec::write (base, c.hooks, &pOffsets);
        const Bytes blob = containerOf (ownChunks (newer));
        StrandLockProcessor p;
        loadInto (p, blob);
        const bool loaded = p.getDocument().current()->notes.size() == base.notes.size()
                         && model::encodeClip (*p.getDocument().current()) == newer;
        check (loaded && saveOf (p) == blob && p.getStateBanner().isEmpty(),
               std::string (c.name) + fmt (": loads (%d notes), no banner, re-saved byte-identically", (int) p.getDocument().current()->notes.size()));
        double newP = 0.0;
        const auto edit = movePitch (p, c.note, newP);
        const Bytes after = payloadOf (saveOf (p), state::kTagClip);
        const std::size_t at = c.note < pOffsets.size() ? pOffsets[c.note] : 0;
        check (edit.accepted && codecspec::differOnlyIn (after, newer, at, 8) && doubleAt (after, at) == newP && after != newer,
               std::string (c.name) + ": after a pitch edit only that note's p changed; the content this version does not know is still written");
    }
}

SL_TEST ("state", unreadableThroughProcessor)
{
    section ("State (plan 3 (e)): a newer formatVersion and a bad CLIP CRC: re-saved unchanged with the banner, replaced after a clip edit");

    auto values = defaults();
    values[(std::size_t) params::index (params::P::drift)] = 0.4f;
    const Bytes good = containerOf (ownChunks (model::encodeClip (strandDemo()), starterRef (sounds::kDefaultStarter, 1), values));

    // formatVersion 2
    {
        Bytes b = good;
        b[4] = 2;
        const auto crc = state::crc32 (b.data(), 12);
        for (int i = 0; i < 4; ++i)
            b[12 + (std::size_t) i] = (std::uint8_t) ((crc >> (8 * i)) & 0xffu);
        StrandLockProcessor p;
        p.getDocument().replace (std::make_shared<const model::Clip> (model::newClip (2)));
        loadInto (p, b);
        check (p.getDocument().current()->notes.empty() && juce::exactlyEqual (plainOf (p, params::P::drift), 0.0f) && saveOf (p) == b
                   && p.getStateBanner().isNotEmpty(),
               "formatVersion 2: loads nothing (the clip and Drift stay as they were), re-saved byte-identically, banner shown");
        const auto edit = p.editClip ("Add a note", model::addNote (*p.getDocument().current(), 60, 0, 1));
        const Bytes after = saveOf (p);
        const auto afterClip = model::decodeClip (payloadOf (after, state::kTagClip));
        check (edit.accepted && after != b && state::parseContainer (after.data(), after.size()).status == state::HeaderStatus::ok
                   && p.getStateBanner().isEmpty() && afterClip.clip.has_value() && afterClip.clip->notes.size() == 1,
               "after a clip edit the save is this version's container (the new note in it) and the banner is gone");
    }

    // a bad CLIP CRC
    {
        Bytes b = good;
        const auto parsed = state::parseContainer (b.data(), b.size());
        const std::size_t clipAt = (std::size_t) (parsed.chunks[2].raw - b.data());
        b[clipAt + 12] ^= 0x01u;
        StrandLockProcessor p;
        loadInto (p, b);
        check (juce::exactlyEqual (plainOf (p, params::P::drift), 0.4f) && p.getDocument().current()->notes.empty(),
               "a bad CLIP CRC: the other chunks load (Drift 40 %), the clip keeps its default (empty)");
        check (saveOf (p) == b && p.getStateBanner().isNotEmpty(), "re-saved byte-identically, banner shown");
        params::setPlainValue (p.getParam (params::P::pitch), params::index (params::P::pitch), 3.0f);
        const Bytes afterParam = saveOf (p);
        check (afterParam != b && payloadOf (afterParam, state::kTagClip) == payloadOf (b, state::kTagClip) && p.getStateBanner().isNotEmpty(),
               "after a parameter edit the damaged CLIP is still written as it was, and the banner stays");
        const auto edit = p.editClip ("Add a note", model::addNote (*p.getDocument().current(), 62, 1, 1));
        const Bytes afterClip = saveOf (p);
        const auto again = state::parseContainer (afterClip.data(), afterClip.size());
        const auto clip = model::decodeClip (payloadOf (afterClip, state::kTagClip));
        check (edit.accepted && again.chunks.size() == 5 && again.chunks[2].crcOk && clip.clip.has_value() && clip.clip->notes.size() == 1
                   && p.getStateBanner().isEmpty(),
               "after a clip edit CLIP is written anew (CRC good, the one new note) and the banner is gone");
    }
}

namespace
{
    /** A note whose lanes hold `extra` nodes more than flat, filled lane by lane up to 128 (test_limits.cpp's). */
    model::Note heavyNote (int i, int extra, const std::string& id)
    {
        model::Note n = model::mkNote (36 + i % 48, (i % 256) * 1.0, 1.0, id);
        for (int lane = 0; lane < core::kNumLanes && extra > 0; ++lane)
        {
            const int more = std::min (extra, model::kMaxNodes - 2);
            const int count = 2 + more;
            model::NodeList nodes;
            for (int j = 0; j < count; ++j)
                nodes.push_back (model::mkNode (static_cast<double> (j) / (count - 1), static_cast<double> ((j + i) % 10) / 10.0, model::NodeShape::linear));
            n.lane (static_cast<core::LaneId> (lane)) = nodes;
            extra -= more;
        }
        return n;
    }

    std::string heavyId (int i) { return "L" + std::to_string (10000 + i); }

    /** count notes whose encoded sizes add up to total bytes (test_limits.cpp's). */
    model::Clip clipOfSize (int count, std::size_t total)
    {
        model::Clip c = model::newClip (64);
        const std::size_t header = model::encodedHeaderSize (c);
        const std::size_t plain = model::encodedNoteSize (heavyNote (0, 0, heavyId (0)));
        const std::size_t perNote = (total - header) / static_cast<std::size_t> (count);
        const std::size_t extra = (perNote - plain) / 25;
        std::size_t left = total - header - static_cast<std::size_t> (count) * (plain + 25 * extra);
        const std::size_t moreNodes = left / 25;
        left -= moreNodes * 25;
        c.notes.reserve (static_cast<std::size_t> (count));
        for (int i = 0; i < count; ++i)
        {
            std::string id = heavyId (i);
            if (i == 0)
                id += std::string (left, 'x');
            const int nodes = static_cast<int> (extra) + (static_cast<std::size_t> (i) < moreNodes ? 1 : 0);
            c.notes.push_back (std::make_shared<const model::Note> (heavyNote (i, nodes, id)));
        }
        return c;
    }
}

SL_TEST ("state", clipLimitsThroughProcessor)
{
    section ("State limits (plan 3): the largest editable clip saves and loads through the processor; one more note or node is refused at edit time");

    const std::size_t cap = model::kMaxClipBytes;
    StrandLockProcessor p;
    const double t0 = nowMs();
    const auto made = p.editClip ("The largest clip", clipOfSize (model::kMaxClipNotes, cap - 1));
    check (made.accepted && p.getDocument().current()->notes.size() == 4096 && p.getDocument().encodedBytes() == cap - 1,
           fmt ("the largest editable clip is accepted: 4096 notes, %d bytes (one under 32 MiB)", (int) p.getDocument().encodedBytes()));

    const Bytes saved = saveOf (p);
    const double t1 = nowMs();
    StrandLockProcessor q;
    loadInto (q, saved);
    const double t2 = nowMs();
    std::string why;
    check (clipsBitIdentical (*q.getDocument().current(), *p.getDocument().current(), why) && saveOf (q) == saved,
           fmt ("it saves (%d bytes in all) and loads in another instance, every note bit for bit, re-saved byte-identically %s", (int) saved.size(), why.c_str()));
    note (fmt ("the 32 MiB clip: built and accepted %.0f ms, loaded %.0f ms (no target)", t1 - t0, t2 - t1));

    const auto before = q.getDocument().current();
    const auto beforeVersion = q.getDocument().version();
    const model::NodeList three { model::mkNode (0, 0.2), model::mkNode (0.5, 0.7), model::mkNode (1, 0.4) };
    const auto oneNode = q.editClip ("One more node", model::setLaneNodes (*before, before->notes[5]->id, core::LaneId::refresh, three));
    check (oneNode.refused && ! oneNode.accepted && q.getDocument().current() == before && q.getDocument().version() == beforeVersion
               && oneNode.message.find ("32 MB") != std::string::npos,
           fmt ("one more node is refused, the clip unchanged, the toast names the cap (\"%s\")", oneNode.message.c_str()));
    const auto oneNote = q.editClip ("One more note", model::addNote (*before, 60, 0, 1));
    check (oneNote.refused && q.getDocument().current() == before && oneNote.message.find ("4096") != std::string::npos,
           fmt ("one more note is refused, the clip unchanged (\"%s\")", oneNote.message.c_str()));
    check (saveOf (q) == saved, "and the save is still the same bytes");
}

SL_TEST ("state", sampleFileRecord)
{
    section ("SMPL file record (decision 4): a TestSignals WAV loads, its record holds path, size, SHA-256, rate, channels, frames, the part; it round-trips");

    const auto dir = tempFolder ("smpl");
    const auto wavFile = dir.getChildFile ("two tones.wav");
    const double sr = 48000.0;
    const auto left = signals::tone (220.0, 2.0, sr);
    signals::ToneOpts opts;
    opts.seed = 9;
    const auto right = signals::tone (330.0, 2.0, sr, opts);
    check (writeWav (wavFile, { left, right }, sr, 32), fmt ("a 2 s stereo 32-bit float WAV written to %s", wavFile.getFullPathName().toRawUTF8()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (wavFile);
    p.waitForSounds();
    const auto ref = p.getSampleRef();
    const auto& f = ref.file;
    check (ref.kind == state::kSampleFile && f.path == wavFile.getFullPathName().toStdString() && f.displayName == "two tones.wav"
               && f.fileSize == (std::uint64_t) wavFile.getSize() && f.sha256 == digestOfFile (wavFile),
           fmt ("SMPL kind 1: the path, the name, %d bytes and the SHA-256 of those bytes (%s)", (int) f.fileSize, doc::toHex (f.sha256).substr (0, 16).c_str()));
    check (juce::exactlyEqual (f.fileSampleRate, sr) && f.channels == 2 && f.fileFrames == left.size() && f.loadedStartFrame == 0
               && f.loadedFrames == left.size() && f.keepInProject == 1 && f.embedAnswer == 0 && f.cache.analysed == 0 && f.cache.parts.empty(),
           fmt ("48 kHz, 2 channels, %d frames, the whole file loaded, keep in project on, the analysis cache empty", (int) f.fileFrames));

    const auto sound = p.getCurrentSound();
    check (sound != nullptr && ! sound->builtIn && ! sound->missing && sound->frames == (std::int64_t) left.size() && sound->stereo
               && samplesAre (sound->left, left) && samplesAre (sound->right, right),
           "the sound that plays is the file, decoded bit for bit (both channels)");
    const auto inst = p.getInstState();
    check (juce::exactlyEqual (plainOf (p, params::P::rootNote), 60.0f) && juce::exactlyEqual (plainOf (p, params::P::rootCents), 0.0f)
               && juce::exactlyEqual (plainOf (p, params::P::regionStart), 0.0f) && juce::exactlyEqual (plainOf (p, params::P::regionEnd), 1.0f)
               && inst.regionStartExact == 0.0 && inst.regionEndExact == 1.0,
           "the load set root C3, cents 0 and the whole file as the Region (the exact edges too)");
    check (p.undoLabel().value_or ("") == "Load two tones.wav", fmt ("one undo step: \"%s\"", p.undoLabel().value_or ("").c_str()));

    const Bytes saved = saveOf (p);
    StrandLockProcessor q;
    loadInto (q, saved);
    const auto qs = q.getCurrentSound();
    check (qs != nullptr && ! qs->builtIn && ! qs->missing && samplesAre (qs->left, left) && samplesAre (qs->right, right) && saveOf (q) == saved,
           "a project reload decodes the path again: the same samples, and the state re-saves byte-identically");

    // A song: longer than 20 s, so its Region starts as 4 s from 35 % in.
    {
        const auto song = dir.getChildFile ("song.wav");
        const double rate = 8000.0;
        check (writeWav (song, { sine (110.0, 30.0, rate, 0.4f) }, rate, 16), "a 30 s mono 16-bit WAV");
        StrandLockProcessor s;
        s.loadSoundFile (song);
        s.waitForSounds();
        const double a = 0.35, b = 0.35 + 4.0 / 30.0;
        const auto si = s.getInstState();
        const auto reports = s.takeSoundReports();
        bool said = false;
        for (const auto& r : reports)
            said = said || r.message.find ("song") != std::string::npos;
        check (juce::exactlyEqual (plainOf (s, params::P::regionStart), (float) a) && juce::exactlyEqual (plainOf (s, params::P::regionEnd), (float) b)
                   && si.regionStartExact == a && si.regionEndExact == b && said,
               fmt ("the Region is %.6f .. %.6f (4 s from 35 %% in), the exact edges kept in INST, and the toast says so", si.regionStartExact, si.regionEndExact));
    }

    // Longer than 10 minutes: its first 10 minutes (question 9.3).
    {
        const auto longFile = dir.getChildFile ("long.wav");
        const double rate = 8000.0;
        check (writeWav (longFile, { sine (55.0, 601.0, rate, 0.3f) }, rate, 16), "a 601 s mono 16-bit WAV");
        StrandLockProcessor s;
        s.loadSoundFile (longFile);
        s.waitForSounds();
        const auto r = s.getSampleRef();
        const auto reports = s.takeSoundReports();
        bool said = false;
        for (const auto& rep : reports)
            said = said || rep.message.find ("10 minutes") != std::string::npos;
        const auto cur = s.getCurrentSound();
        check (r.kind == state::kSampleFile && r.file.fileFrames == 4808000u && r.file.loadedStartFrame == 0 && r.file.loadedFrames == 4800000u
                   && cur != nullptr && cur->frames == 4800000 && cur->cut && said,
               fmt ("it loads its first 10 minutes (%d of %d frames) and the toast says so", (int) r.file.loadedFrames, (int) r.file.fileFrames));
        StrandLockProcessor t;
        loadInto (t, saveOf (s));
        check (t.getCurrentSound() != nullptr && t.getCurrentSound()->frames == 4800000 && ! t.getCurrentSound()->missing,
               "a reload decodes the same part again");
    }

    // A missing file keeps its SMPL record verbatim and plays the default starter (decision 4).
    {
        StrandLockProcessor m;
        wavFile.deleteFile();
        loadInto (m, saved);
        const auto ms = m.getCurrentSound();
        const auto reports = m.takeSoundReports();
        bool named = false;
        for (const auto& r : reports)
            named = named || (! r.ok && r.message.find ("two tones.wav") != std::string::npos);
        check (ms != nullptr && ms->builtIn && ms->starter == sounds::kDefaultStarter && ms->missing && named,
               "the file is missing: the Saw that opens up plays and the toast names the file");
        check (m.getSampleRef().kind == state::kSampleFile && saveOf (m) == saved && m.getStateBanner().isEmpty(),
               "the SMPL record is kept: the save is the loaded state byte for byte (no banner)");
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        m.prepareToPlay (48000.0, 64);
        m.processBlock (buffer, midi);
        check (m.getAudioSample() != nullptr && m.getAudioSample()->name == sounds::starterInfo (sounds::kDefaultStarter).name,
               "and the audio thread plays the default starter");
        m.releaseResources();
    }

    // The same path holding another file: it has changed, so it is missing too.
    {
        check (writeWav (wavFile, { right, left }, sr, 32), "the path written again with other samples");
        StrandLockProcessor c;
        loadInto (c, saved);
        const auto reports = c.takeSoundReports();
        bool changed = false;
        for (const auto& r : reports)
            changed = changed || r.message.find ("changed") != std::string::npos;
        check (c.getCurrentSound() != nullptr && c.getCurrentSound()->missing && changed && saveOf (c) == saved,
               "a file at the path with another SHA-256 is not used: the default plays, the toast says it changed, the record is kept");
    }
    dir.deleteRecursively();
}

SL_TEST ("state", loadPlaysNamedStarterOnFirstBlock)
{
    section ("State (decision 3): a load naming another starter waits for it, so the first block plays it");

    const Bytes blob = containerOf (ownChunks (model::encodeClip (model::newClip (4)), starterRef (sounds::StarterId::pad, 2)));
    constexpr int n = 512;

    StrandLockProcessor p;
    p.prepareToPlay (48000.0, n);   // plays the default sound (sawopen) until the load
    const double t0 = nowMs();
    loadInto (p, blob);
    const double loadMs = nowMs() - t0;
    const auto out = firstBlock (p, n);
    const auto* played = p.getAudioSample();
    check (played != nullptr && played->name == sounds::starterInfo (sounds::StarterId::pad).name,
           fmt ("the first block after the load plays the Evolving pad (\"%s\")", played != nullptr ? played->name.c_str() : "none"));

    // The same block from an instance that had the pad (seed 2) long before, and from one still on sawopen.
    StrandLockProcessor ref;
    ref.loadStarterSound (sounds::StarterId::pad, 2);
    ref.waitForSounds();
    ref.prepareToPlay (48000.0, n);
    const auto want = firstBlock (ref, n);
    StrandLockProcessor other;
    other.prepareToPlay (48000.0, n);
    const auto sawopen = firstBlock (other, n);
    double rms = 0.0;
    for (const float x : out)
        rms += (double) x * x;
    rms = std::sqrt (rms / n);
    check (out == want && out != sawopen && rms > 1e-4,
           fmt ("it equals the block of an instance that had the pad all along, bit for bit, and differs from sawopen's (rms %.4f)", rms));

    const double t1 = nowMs();
    loadInto (p, containerOf (ownChunks (model::encodeClip (model::newClip (4)), starterRef (sounds::kDefaultStarter, 1))));
    const double cachedMs = nowMs() - t1;
    note (fmt ("a state load naming a starter the worker makes: %.2f ms; naming one in its cache of 4: %.2f ms (target 50 ms)", loadMs, cachedMs));
    check (cachedMs <= 250.0, fmt ("a load naming a cached starter is under 5 x the 50 ms target (%.2f ms)", cachedMs));
    p.releaseResources();
    ref.releaseResources();
    other.releaseResources();
}
