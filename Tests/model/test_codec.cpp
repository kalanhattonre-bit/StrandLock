// The clip codec (PLUGIN-PLAN 3 CLIP records, 4 M1 "clip codec"): every fixture clip encodes, decodes and encodes
// again to the same bytes, with every double bit for bit, and a decoded clip equals normalizeClip of the same
// notes. Also: the bytes follow plan 3's record table (an independent writer gives the same payload), the
// decoder refuses what the limits table refuses, and cut, mutated and random payloads never crash it and
// always decode to a valid clip or to nothing.

#include "CodecSpec.h"
#include "ModelFixture.h"
#include "ToolOps.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Normalize.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;
    using Bytes = std::vector<std::uint8_t>;

    struct Named
    {
        std::string name;
        model::Clip clip;
    };

    bool sameSettings (const model::Clip& a, const model::Clip& b)
    {
        return a.bars == b.bars && a.beatsPerBar == b.beatsPerBar && fixture::same (a.offsetBeats, b.offsetBeats) && a.loop == b.loop
            && a.tieAcrossLoop == b.tieAcrossLoop;
    }

    /** Every note of a bit-identical to b's, in order, and the same settings. */
    bool clipsBitIdentical (const model::Clip& a, const model::Clip& b, std::string& why)
    {
        if (! sameSettings (a, b))
        {
            why = "the clip settings differ";
            return false;
        }
        if (a.notes.size() != b.notes.size())
        {
            why = "the note count differs";
            return false;
        }
        for (std::size_t i = 0; i < a.notes.size(); ++i)
            if (! modelfx::bitIdentical (*a.notes[i], *b.notes[i], why))
                return false;
        return true;
    }

    /** The fixture clips: every named clip of the model fixture files, the 12 clip presets, recipes 1-8 on Strand
        Demo and normalize.json's clip results. */
    std::vector<Named> fixtureClips()
    {
        std::vector<Named> out;
        for (const char* file : { "model/tools.json", "model/glides.json", "model/generators.json", "model/shapes.json", "model/snapshots.json",
                                  "model/presets.json" })
        {
            JsonValue doc;
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
        JsonValue doc;
        if (fixture::load ("model/normalize.json", doc))
        {
            const JsonValue& cases = doc["cases"];
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

    /** A clip that sets every field away from its default. */
    model::Clip everyField()
    {
        model::Clip clip = model::newClip (7);
        clip.beatsPerBar = 3;
        clip.offsetBeats = 1.25;
        clip.loop = false;
        clip.tieAcrossLoop = false;
        // the second id is n, e acute, -, a CJK character, written as its UTF-8 bytes
        const std::string ids[] = { "a", "n\xc3\xa9-\xe4\xb8\xad", "x~1", std::string (255, 'z') };
        for (int k = 0; k < 4; ++k)
        {
            model::Note n = model::mkNote (40.5 + k * 7.25, 0.1 + k * 1.37, 0.3 + k * 0.61, ids[k]);
            n.vel = 0.123456789;
            n.gc = -0.75 + 0.5 * k;
            if (k == 1)
                n.glide = 0.0;
            if (k == 2)
                n.glide = 0.3333333333333333;
            if (k < 3)
                n.glideTo = ids[k + 1];
            model::NoteSample& sm = n.sample;
            sm.motion = static_cast<core::Motion> (k + 1);
            sm.travel = k % 2 ? model::TravelKind::path : model::TravelKind::speed;
            sm.start.mode = static_cast<model::StartMode> (k);
            sm.start.pos = 0.1 * k;
            sm.start.range = 0.9 - 0.2 * k;
            sm.start.seed = k == 3 ? std::numeric_limits<std::int32_t>::min() : 7 * k - 3;
            sm.tape = k == 0 ? std::optional<bool> () : std::optional<bool> (k % 2 == 1);
            sm.rev = k == 1 ? std::optional<bool> () : std::optional<bool> (k % 2 == 0);
            sm.jump = static_cast<core::RateId> (k * 5);
            sm.engine = static_cast<core::Engine> (k % 3);
            sm.lock = k == 2 ? std::optional<bool> () : std::optional<bool> (k > 1);
            sm.restart = k == 3;
            for (int lane = 0; lane < core::kNumLanes; ++lane)
            {
                const auto li = static_cast<core::LaneId> (lane);
                model::NodeList nodes;
                const int count = k == 3 ? model::kMaxNodes : 2 + (lane + k) % 5;
                for (int j = 0; j < count; ++j)
                    nodes.push_back (model::mkNode (static_cast<double> (j) / (count - 1), std::fmod (0.137 * (j + lane + k), 1.0),
                                                    static_cast<model::NodeShape> ((j + lane) % 4), j % 3 == 0 ? 0.0 : -0.6 + 0.01 * j));
                n.lane (li) = model::normalizeNodes (nodes, core::laneDef (li));
                if ((lane + k) % 3 == 0)
                {
                    model::Gen g;
                    g.kind = (lane % 2) ? model::GenKind::random : model::GenKind::pattern;
                    g.shape = static_cast<model::GenShape> (lane % 7);
                    g.rate = static_cast<core::RateId> ((lane * 3 + k) % core::kNumRates);
                    g.d0 = 0.1 * lane / 9.0;
                    g.d1 = 0.95;
                    g.warp = -0.5 + 0.3 * k;
                    g.lockToBeat = lane % 2 == 0;
                    g.amount = 0.77;
                    g.smooth = 0.4;
                    g.perNote = k % 2 ? model::PerNote::different : model::PerNote::same;
                    g.seed = -12345 + lane;
                    g.invert = lane == 3;
                    g.key = lane == 6 ? ids[0] : std::string();
                    n.genOf (li) = model::normalizeGen (g);
                }
            }
            clip.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        return clip;
    }
}

SL_TEST ("model", codecFixtureClips)
{
    section ("Clip codec: every fixture clip encodes, decodes and encodes again byte-identical, doubles bit for bit");

    const std::vector<Named> clips = fixtureClips();
    int roundTrips = 0, equalsNormalize = 0;
    std::size_t notes = 0, bytes = 0;
    std::string firstProblem;
    for (const Named& c : clips)
    {
        const Bytes once = model::encodeClip (c.clip);
        const model::DecodeResult d = model::decodeClip (once);
        if (! d.clip.has_value())
        {
            if (firstProblem.empty())
                firstProblem = c.name + ": not decoded (" + d.error + ")";
            continue;
        }
        notes += c.clip.notes.size();
        bytes += once.size();
        const Bytes twice = model::encodeClip (*d.clip);
        if (twice == once && once.size() == model::encodedClipSize (c.clip))
            ++roundTrips;
        else if (firstProblem.empty())
            firstProblem = c.name + ": the second encoding differs";

        std::string why;
        if (clipsBitIdentical (*d.clip, model::normalizeClip (c.clip), why) && clipsBitIdentical (*d.clip, c.clip, why))
            ++equalsNormalize;
        else if (firstProblem.empty())
            firstProblem = c.name + ": " + why;
    }
    note (fmt ("%d clips, %d notes, %d bytes encoded", static_cast<int> (clips.size()), static_cast<int> (notes), static_cast<int> (bytes)));
    if (! firstProblem.empty())
        note ("first problem: " + firstProblem);
    check (clips.size() >= 40, fmt ("the fixture clips were all read (%d: 8 named clips of tools.json and 4 more files' tables, 12 presets, "
                                    "8 recipes, normalize.json's results)", static_cast<int> (clips.size())));
    check (roundTrips == static_cast<int> (clips.size()), fmt ("encode -> decode -> encode is byte-identical, and encodedClipSize is the size (%d of %d)",
                                                             roundTrips, static_cast<int> (clips.size())));
    check (equalsNormalize == static_cast<int> (clips.size()),
           fmt ("each decoded clip equals normalizeClip of the same notes and the clip itself, every double bit for bit (%d of %d)", equalsNormalize,
                static_cast<int> (clips.size())));
}

SL_TEST ("model", codecToolResults)
{
    section ("Clip codec: the clips the tools make from model/tools.json decode to normalizeClip of themselves");

    JsonValue doc;
    std::map<std::string, model::Clip> clips;
    if (! fixture::load ("model/tools.json", doc) || ! modelfx::clipsFrom (doc, clips))
        return;
    const JsonValue& cases = doc["cases"];
    int expected = 0, made = 0, decoded = 0, stable = 0, already = 0;
    std::string firstProblem;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string op = cases[i]["op"].asString();
        expected += op != "selectSimilar" && op != "getMixed" ? 1 : 0;   // the two that make no clip
        const auto it = clips.find (cases[i]["clip"].asString());
        if (it == clips.end())
            continue;
        const toolops::OpResult r = toolops::run (cases[i], it->second);
        if (! r.clip.has_value())
            continue;
        ++made;
        const model::Clip normal = model::normalizeClip (*r.clip);
        const Bytes once = model::encodeClip (*r.clip);
        const model::DecodeResult d = model::decodeClip (once);
        std::string why;
        if (d.clip.has_value() && clipsBitIdentical (*d.clip, normal, why))
            ++decoded;
        else if (firstProblem.empty())
            firstProblem = cases[i]["op"].asString() + ": " + (d.clip.has_value() ? why : d.error);
        if (d.clip.has_value() && model::encodeClip (*d.clip) == model::encodeClip (normal))
            ++stable;
        already += clipsBitIdentical (normal, *r.clip, why) ? 1 : 0;
    }
    note (fmt ("%d result clips, %d of them already normalized", made, already));
    if (! firstProblem.empty())
        note ("first problem: " + firstProblem);
    check (made == expected && expected > 100, fmt ("every case that makes a clip made one (%d of %d)", made, expected));
    check (decoded == made, fmt ("each decodes to normalizeClip of itself, bit for bit (%d of %d)", decoded, made));
    check (stable == made, fmt ("and encodes again to normalizeClip's bytes (%d of %d)", stable, made));
}

SL_TEST ("model", codecLayout)
{
    section ("Clip codec: the bytes follow plan 3's record table; every field round-trips");

    const model::Clip clip = everyField();
    const Bytes ours = model::encodeClip (clip);
    const Bytes spec = codecspec::write (clip);
    check (ours == spec, fmt ("an independent writer of plan 3's records gives the same %d bytes (settings, notes, sample settings, lanes, generators)",
                              static_cast<int> (ours.size())));
    std::vector<std::size_t> pAt;
    codecspec::write (clip, {}, &pAt);
    double p0 = 0.0;
    if (pAt.size() == clip.notes.size() && pAt[0] + 8 <= ours.size())
        std::memcpy (&p0, &ours[pAt[0]], 8);   // the test runners are little-endian (x64)
    check (p0 == clip.notes[0]->p, "the first note's p sits where the table puts it, little-endian");

    const model::DecodeResult d = model::decodeClip (ours);
    std::string why;
    check (d.clip.has_value() && clipsBitIdentical (*d.clip, clip, why), "every field of every note comes back bit for bit (a 255-byte id, "
                                                                        "non-ASCII, null and 0 glides, links, every sample setting, generators, "
                                                                        "128-node lanes, an int32 minimum seed) " + why);
    check (d.clip.has_value() && model::encodeClip (*d.clip) == ours, "and encodes to the same bytes");
    check (d.clip.has_value() && d.clip->beatsPerBar == 3 && d.clip->offsetBeats == 1.25 && ! d.clip->loop && ! d.clip->tieAcrossLoop && d.clip->bars == 7,
           "the clip settings come back");
    bool noExtras = d.clip.has_value() && d.clip->extras == nullptr;
    for (std::size_t i = 0; noExtras && i < d.clip->notes.size(); ++i)
        noExtras = d.clip->notes[i]->extras == nullptr;
    check (noExtras, "a payload with nothing unknown gives no extras");

    const model::Clip empty = model::newClip (1);
    const Bytes e = model::encodeClip (empty);
    const auto de = model::decodeClip (e);
    check (e.size() == 26 && de.clip.has_value() && de.clip->notes.empty() && model::encodeClip (*de.clip) == e,
           fmt ("an empty clip is the 22-byte settings record and a note count of 0 (%d bytes), and round-trips", static_cast<int> (e.size())));
}

SL_TEST ("model", codecRebuilds)
{
    section ("Clip codec: the decoder rebuilds notes through normalizeClip and refuses what the limits table refuses");

    // a valid-looking but odd payload: NaN and unsorted nodes, an out-of-range pitch, a dangling link, two notes
    // with one id, a 300-byte id (an imported prototype save could hold one: 8.2 #27; str8 cuts it at 255 here)
    model::Clip odd = model::newClip (2);
    {
        model::Note a = model::mkNote (200, -1, 0, "dup");
        a.lane (core::LaneId::amp) = { model::mkNode (1, 0.2), model::mkNode (0.5, std::nan ("")), model::mkNode (0, 0.9) };
        a.glideTo = "nobody";
        odd.notes.push_back (std::make_shared<const model::Note> (a));
        odd.notes.push_back (std::make_shared<const model::Note> (model::mkNote (61, 1, 1, "dup")));
    }
    const auto d = model::decodeClip (model::encodeClip (odd));
    std::string why;
    check (d.clip.has_value() && clipsBitIdentical (*d.clip, model::normalizeClip (odd), why),
           "an odd payload decodes to exactly what normalizeClip makes of its notes " + why);
    check (d.clip.has_value() && d.clip->notes[0]->p == 127 && d.clip->notes[0]->glideTo.empty() && d.clip->notes[1]->id == "dup~1"
               && d.clip->notes[0]->lane (core::LaneId::amp).size() == 3 && d.clip->notes[0]->lane (core::LaneId::amp)[0].t == 0,
           "(pitch clamped, the dangling link dropped, the repeated id renamed, the lane sorted)");

    {
        // plan 8.2 #27: an imported id over 255 bytes is renamed by the missing-id rule; a link that long is dropped
        const std::string longId (300, 'q');
        model::Clip imported = model::newClip (1);
        model::Note n = model::mkNote (60, 0, 1, longId);
        n.glideTo = longId;
        imported.notes.push_back (std::make_shared<const model::Note> (n));
        imported.notes.push_back (std::make_shared<const model::Note> (model::mkNote (62, 1, 1, "b")));
        const model::Clip fixed = model::normalizeClip (imported);
        const std::string expected = "n" + core::toBase36 (core::hash32 (0, 6000, 0, 1000000));
        check (fixed.notes[0]->id == expected && fixed.notes[0]->glideTo.empty(),
               "an id over 255 bytes becomes the missing-id rule's id ('n' + base 36 of hash32 (0, 6000, 0, 1000000)), and a link that long is dropped");
        model::Clip dupLong = model::newClip (1);
        dupLong.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 1, std::string (254, 'w'))));
        dupLong.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 1, std::string (254, 'w'))));
        const model::Clip renamed = model::normalizeClip (dupLong);
        check (renamed.notes[1]->id.size() <= model::kMaxIdBytes && renamed.notes[1]->id != renamed.notes[0]->id,
               "a repeated 254-byte id is renamed to an id the CLIP chunk can hold (" + std::to_string (renamed.notes[1]->id.size()) + " bytes)");
    }

    // the limits the decoder shares with every edit
    Bytes header;
    {
        const Bytes base = model::encodeClip (model::newClip (4));
        header.assign (base.begin(), base.end() - 4);
    }
    auto withCount = [&header] (std::uint32_t count)
    {
        Bytes b = header;
        for (int i = 0; i < 4; ++i)
            b.push_back (static_cast<std::uint8_t> (count >> (8 * i)));
        return b;
    };
    {
        // 4097 empty note records: each u32 length 0 (a note of defaults)
        Bytes over = withCount (4097);
        over.resize (over.size() + 4097 * 4, 0);
        Bytes atCap = withCount (4096);
        atCap.resize (atCap.size() + 4096 * 4, 0);
        const auto r1 = model::decodeClip (over);
        const auto r2 = model::decodeClip (atCap);
        check (! r1.clip.has_value() && ! r1.error.empty(), "a payload of 4097 notes is refused (" + r1.error + ")");
        check (r2.clip.has_value() && r2.clip->notes.size() == 4096, "a payload of 4096 notes of empty records loads, each a note of defaults");
        bool defaults = r2.clip.has_value();
        for (std::size_t i = 0; defaults && i < r2.clip->notes.size(); ++i)
        {
            const model::Note& n = *r2.clip->notes[i];
            defaults = n.p == 60 && n.s == 0 && n.l == 1 && n.vel == 0.8 && ! n.id.empty() && n.lane (core::LaneId::amp).size() == 2;
        }
        check (defaults, "(missing trailing fields take their defaults: p 60, s 0, l 1, vel 0.8, a made id, flat lanes)");
    }
    {
        Bytes lie = withCount (100);   // 100 notes promised, none there
        const auto r = model::decodeClip (lie);
        check (! r.clip.has_value(), "a note count larger than the bytes left is refused before anything is allocated (" + r.error + ")");
    }
    {
        // a note with 65 lane records
        Bytes note;
        auto put32 = [] (Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back (static_cast<std::uint8_t> (v >> (8 * i))); };
        Bytes body;
        body.push_back (1);
        body.push_back ('a');
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 8; ++j)
                body.push_back (0);
        body.push_back (0);   // glideTo
        body.push_back (0);   // sample record length 0 (u16)
        body.push_back (0);
        body.push_back (65);  // lane count
        for (int i = 0; i < 65; ++i)
            put32 (body, 0);
        put32 (note, static_cast<std::uint32_t> (body.size()));
        note.insert (note.end(), body.begin(), body.end());
        Bytes b = withCount (1);
        b.insert (b.end(), note.begin(), note.end());
        const auto r = model::decodeClip (b);
        check (! r.clip.has_value(), "a note of 65 lane records is refused (64 is the cap) (" + r.error + ")");
        b[header.size() + 4 + 4 + 2 + 48 + 1 + 2] = 64;   // the same note with 64 (empty, unknown) lane records
        const std::uint32_t shorter = static_cast<std::uint32_t> (body.size() - 4);
        for (int i = 0; i < 4; ++i)
            b[header.size() + 4 + static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (shorter >> (8 * i));
        b.erase (b.end() - 4, b.end());
        const auto ok = model::decodeClip (b);
        check (ok.clip.has_value() && ok.clip->notes.size() == 1 && ok.clip->notes[0]->extras != nullptr
                   && ok.clip->notes[0]->extras->unknownLanes.size() == static_cast<std::size_t> (model::kMaxUnknownLanes),
               "64 lane records load; the 64 records of no known id keep the first 54 (10 known + 54 = 64 written back)");
    }
    {
        const Bytes big (model::kMaxClipBytes + 1, 0);
        const auto r = model::decodeClip (big);
        check (! r.clip.has_value(), "a payload above 32 MiB is refused (" + r.error + ")");
    }
}

SL_TEST ("model", codecRobust)
{
    section ("Clip codec: cut payloads, 2,000 single-byte changes and 500 random blobs: no crash, a valid clip or nothing");

    const model::Clip clip = everyField();
    const Bytes good = model::encodeClip (clip);

    // a decoded clip is valid: normalizing it changes nothing, and its own encoding decodes to the same bytes
    auto valid = [] (const model::DecodeResult& r)
    {
        if (! r.clip.has_value())
            return ! r.error.empty();
        std::string why;
        const Bytes b = model::encodeClip (*r.clip);
        const auto again = model::decodeClip (b);
        return again.clip.has_value() && model::encodeClip (*again.clip) == b && clipsBitIdentical (model::normalizeClip (*r.clip), *r.clip, why)
            && r.clip->notes.size() <= static_cast<std::size_t> (model::kMaxClipNotes);
    };

    int cuts = 0, cutsValid = 0, cutsLoaded = 0;
    for (std::size_t n = 0; n < good.size(); n += (n < 2000 ? 1 : 7))
    {
        const Bytes cut (good.begin(), good.begin() + static_cast<std::ptrdiff_t> (n));
        const auto r = model::decodeClip (cut);
        ++cuts;
        cutsValid += valid (r) ? 1 : 0;
        cutsLoaded += r.clip.has_value() ? 1 : 0;
    }
    check (cuts > 2000 && cutsValid == cuts, fmt ("%d cut payloads (of a %d-byte clip): each a valid clip or a refusal with a reason (%d loaded)", cuts,
                                                  static_cast<int> (good.size()), cutsLoaded));

    std::uint32_t counter = 0;
    auto rnd = [&counter] { return core::rand01 (core::hash32 (0xc0dec, counter++)); };
    int mutations = 0, mutationsValid = 0, mutationsLoaded = 0;
    for (int i = 0; i < 2000; ++i)
    {
        Bytes b = good;
        const auto at = static_cast<std::size_t> (rnd() * static_cast<double> (b.size())) % b.size();
        b[at] = static_cast<std::uint8_t> (static_cast<int> (rnd() * 256.0) & 0xff);
        const auto r = model::decodeClip (b);
        ++mutations;
        mutationsValid += valid (r) ? 1 : 0;
        mutationsLoaded += r.clip.has_value() ? 1 : 0;
    }
    check (mutationsValid == mutations, fmt ("%d single-byte changes: each a valid clip or a refusal (%d loaded)", mutations, mutationsLoaded));

    int blobs = 0, blobsValid = 0;
    for (int i = 0; i < 500; ++i)
    {
        Bytes b (static_cast<std::size_t> (rnd() * 4096.0));
        for (auto& x : b)
            x = static_cast<std::uint8_t> (static_cast<int> (rnd() * 256.0) & 0xff);
        if (b.size() >= 26 && i % 2 == 0)
        {
            // a plausible start: a real settings record, then noise
            std::copy (good.begin(), good.begin() + 26, b.begin());
            b[22] = static_cast<std::uint8_t> (i % 7);
            b[23] = b[24] = b[25] = 0;
        }
        const auto r = model::decodeClip (b);
        ++blobs;
        blobsValid += valid (r) ? 1 : 0;
    }
    check (blobsValid == blobs, fmt ("%d random blobs (0..4 KB): each a valid clip or a refusal", blobs));
}
