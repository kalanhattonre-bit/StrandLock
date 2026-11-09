// Immutable versions (PLUGIN-PLAN 1.3, 4 M1): every operation of model/tools.json leaves its input clip
// unchanged and shares every note it did not make new; and the mode lock (PROMPT 14.5): 1,000 seeded
// setLaneNodes calls change that one lane of that one note and nothing else.
//
// Both compare the clip codec's encoding byte for byte, as the plan says, and also every field of every note as bit
// patterns (modelfx::bitIdentical: -0 is not 0) plus the clip settings and the extras pointer, which the encoding
// does not show (an object replaced by an equal one).

#include "ModelFixture.h"
#include "ToolOps.h"

#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Seeds.h"
#include "model/ClipCodec.h"
#include "model/Node.h"
#include "model/NoteSettings.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    bool sameSettings (const model::Clip& a, const model::Clip& b)
    {
        return a.bars == b.bars && a.beatsPerBar == b.beatsPerBar && std::memcmp (&a.offsetBeats, &b.offsetBeats, sizeof (double)) == 0
            && a.loop == b.loop && a.tieAcrossLoop == b.tieAcrossLoop && a.extras == b.extras;
    }

    /** A copy of every note's content, to tell later whether the clip was changed in place. */
    struct Snapshot
    {
        explicit Snapshot (const model::Clip& c) : clip (c), bytes (model::encodeClip (c))
        {
            for (const auto& n : c.notes)
                content.push_back (*n);
        }

        /** The clip still encodes to the same bytes, and holds the same note objects with the same content. */
        bool unchanged (const model::Clip& now, std::string& why) const
        {
            if (model::encodeClip (now) != bytes)
            {
                why = "the encoding changed";
                return false;
            }
            if (! sameSettings (now, clip))
            {
                why = "the clip settings changed";
                return false;
            }
            if (now.notes.size() != clip.notes.size())
            {
                why = "the note count changed";
                return false;
            }
            for (std::size_t i = 0; i < now.notes.size(); ++i)
            {
                if (now.notes[i] != clip.notes[i])
                {
                    why = "note " + std::to_string (i) + " was replaced";
                    return false;
                }
                if (! modelfx::bitIdentical (*now.notes[i], content[i], why))
                    return false;
            }
            return true;
        }

        model::Clip clip;                  // the settings and the note pointers as they were
        std::vector<std::uint8_t> bytes;   // the codec encoding as it was
        std::vector<model::Note> content;  // every note's content as it was
    };
}

SL_TEST ("model", immutabilityTools)
{
    section ("Immutability: every model/tools.json operation leaves its input unchanged and shares the notes it did not make new");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    const JsonValue& cases = doc["cases"];
    int ran = 0, unchanged = 0, sharing = 0, sharedNotes = 0;
    std::string firstProblem;
    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const auto it = clips.find (c["clip"].asString());
        if (it == clips.end())
            continue;
        const model::Clip& in = it->second;
        const Snapshot before (in);
        const toolops::OpResult r = toolops::run (c, in);
        if (! r.ran)
            continue;
        ++ran;

        std::string why;
        if (before.unchanged (in, why))
            ++unchanged;
        else if (firstProblem.empty())
            firstProblem = c["op"].asString() + ": the input " + why;

        // every result note the prototype did not make new is the input's own note with that id
        if (! r.clip.has_value())
        {
            ++sharing;
            continue;
        }
        std::set<std::string> made;
        const JsonValue& listed = c["result"]["notes"];
        for (std::size_t i = 0; i < listed.size(); ++i)
            made.insert (listed[i]["id"].asString());
        bool shares = true;
        for (const auto& n : r.clip->notes)
        {
            if (made.count (n->id) != 0)
                continue;
            const model::Note* own = modelfx::findNote (in, n->id);
            if (own == nullptr || own != n.get())
            {
                shares = false;
                if (firstProblem.empty())
                    firstProblem = c["op"].asString() + ": note " + n->id + " is not the input's own";
                break;
            }
            ++sharedNotes;
        }
        sharing += shares ? 1 : 0;
    }

    note (fmt ("%d operations run, %d result notes checked to be the input's own", ran, sharedNotes));
    if (! firstProblem.empty())
        note ("first problem: " + firstProblem);
    check (ran == static_cast<int> (cases.size()), fmt ("every case of tools.json ran (%d of %d)", ran, static_cast<int> (cases.size())));
    check (unchanged == ran, fmt ("the input clip is unchanged after every operation (%d of %d): its codec encoding byte-identical, and field by "
                                  "field as bit patterns", unchanged, ran));
    check (sharing == ran, fmt ("every note an operation did not make new is shared with the input (%d of %d)", sharing, ran));
}

SL_TEST ("model", modeLock)
{
    section ("Mode lock (PROMPT 14.5): 1,000 seeded setLaneNodes calls change only that lane of that note");

    // The clip presets, and a clip whose notes hold lanes normalizeNodes would change (one node, unsorted,
    // out of range, empty) and kept extras, so a setLaneNodes that cloned or normalized the whole note shows.
    std::vector<model::Clip> clips;
    for (const auto& p : model::clipPresets())
        clips.push_back (*model::loadClipPreset (p.id));
    {
        model::Clip odd = model::newClip (2);
        auto extras = std::make_shared<model::NoteExtras>();
        extras->noteTrailing = { 1, 2, 3 };
        for (int i = 0; i < 6; ++i)
        {
            model::Note n = model::mkNote (48 + i, i * 0.5, 0.75, "odd-" + std::to_string (i));
            n.lane (core::LaneId::amp) = { model::mkNode (0.5, 0.9) };                                        // one node
            n.lane (core::LaneId::tone) = { model::mkNode (1, 0.2), model::mkNode (0, 0.7) };                 // unsorted
            n.lane (core::LaneId::heads) = { model::mkNode (0, 1.5), model::mkNode (1, -0.25, model::NodeShape::bend, 3) };   // out of range
            n.lane (core::LaneId::grain).clear();                                                             // empty
            n.vel = -0.0;
            n.extras = extras;
            odd.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        clips.push_back (std::move (odd));
    }

    std::uint32_t counter = 0;
    auto rnd = [&counter] { return core::rand01 (core::hash32 (0x10c4, counter++)); };
    auto pickInt = [&rnd] (std::size_t n) { return static_cast<std::size_t> (std::floor (rnd() * static_cast<double> (n))) % n; };

    int calls = 0, locked = 0, misses = 0;
    std::string firstProblem;
    for (int iter = 0; iter < 1000; ++iter)
    {
        model::Clip& clip = clips[pickInt (clips.size())];
        const bool miss = iter % 50 == 49;   // now and then an id the clip does not hold
        const std::size_t at = pickInt (clip.notes.size());
        const std::string id = miss ? std::string ("no-such-note") : clip.notes[at]->id;
        const auto lane = static_cast<core::LaneId> (pickInt (core::kNumLanes));

        model::NodeList raw;
        const std::size_t count = pickInt (14);
        for (std::size_t k = 0; k < count; ++k)
        {
            const double t = rnd() * 1.4 - 0.2;
            const double v = rnd() < 0.05 ? std::numeric_limits<double>::quiet_NaN() : rnd() * 1.4 - 0.2;
            raw.push_back (model::mkNode (t, v, static_cast<model::NodeShape> (pickInt (4)), rnd() * 2.4 - 1.2));
        }

        const Snapshot before (clip);
        const model::Clip out = model::setLaneNodes (clip, id, lane, raw);
        ++calls;
        misses += miss ? 1 : 0;

        std::string why;
        bool ok = before.unchanged (clip, why) && sameSettings (out, clip) && out.notes.size() == clip.notes.size();
        model::Clip expectedClip = clip;   // the input with only that lane of that note replaced
        if (! ok && why.empty())
            why = "the result's settings or note count differ";
        for (std::size_t j = 0; ok && j < clip.notes.size(); ++j)
        {
            const model::NotePtr& was = clip.notes[j];
            if (was->id != id)
            {
                if (out.notes[j] != was)
                {
                    ok = false;
                    why = "note " + was->id + " is not shared";
                }
                continue;
            }
            model::Note expected = *was;
            expected.lane (lane) = model::normalizeNodes (raw, core::laneDef (lane));
            expectedClip.notes[j] = std::make_shared<const model::Note> (expected);
            if (out.notes[j] == was)
            {
                ok = false;
                why = "the edited note " + id + " is the input's own object";
            }
            else if (! modelfx::bitIdentical (*out.notes[j], expected, why))
            {
                ok = false;
                why = "lane " + std::string (core::kLanes[static_cast<int> (lane)].code) + ": " + why;
            }
        }
        if (ok && model::encodeClip (out) != model::encodeClip (expectedClip))
        {
            ok = false;
            why = "the codec encoding differs from the input's with only that lane changed";
        }
        if (ok)
            ++locked;
        else if (firstProblem.empty())
            firstProblem = why;
        clip = out;   // the next call edits this version
    }

    note (fmt ("%d calls (%d with an id the clip does not hold) on 12 presets and a clip of unnormalized lanes", calls, misses));
    if (! firstProblem.empty())
        note ("first problem: " + firstProblem);
    check (calls == 1000 && locked == calls,
           fmt ("every call changed only that lane of that note: start, length, pitch, the other 9 lanes, the generators, the settings and "
                "the extras byte-identical in the codec encoding and bit-identical field by field, every other note shared (%d of %d)", locked, calls));
}
