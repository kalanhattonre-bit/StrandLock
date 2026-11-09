// Forward compatibility of the clip codec (PLUGIN-PLAN 3, cases (b)-(d), on the codec; M2 runs them again
// through the processor): a CLIP a newer StrandLock might write, made by the independent layout writer
// (CodecSpec), loads and re-saves byte-identically; after a pitch edit only that note's p changed and the
// content this version does not know is still written. (b) every record carries 8 extra trailing bytes; (c) a
// note holds a lane record of an id this version does not know; (d) a note holds an unknown motion string and a
// node shape code 9.

#include "CodecSpec.h"
#include "ModelFixture.h"

#include "core/Lanes.h"
#include "core/Rates.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/NoteSettings.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using Bytes = std::vector<std::uint8_t>;

    /** The clip the cases start from: Strand Demo (26 notes, links), generators on two notes (it has none of its
        own), two notes with Bounce, and note 6 following the instrument's motion with a tone lane that starts with a smooth node (what case (d)'s
        unknown values read as). */
    model::Clip baseClip()
    {
        model::Clip clip = *model::loadClipPreset ("strand-demo");
        model::SamplePatch patch;
        patch.motion = std::optional<core::Motion> (core::Motion::bounce);
        clip = model::setNoteSample (clip, model::Ids { clip.notes[2]->id, clip.notes[5]->id }, patch);
        model::GenPatch g;
        g.rate = core::RateId::r1_8;
        g.key = "sd-other";
        clip = model::setGen (clip, model::Ids { clip.notes[0]->id, clip.notes[3]->id }, core::LaneId::amp, model::makeGen (model::GenKind::random, g));
        clip = model::setGen (clip, model::Ids (clip.notes[3]->id), core::LaneId::heads, model::makeGen (model::GenKind::pattern, model::GenPatch {}));
        model::SamplePatch follow;
        follow.motion.emplace();   // null: follow the instrument
        clip = model::setNoteSample (clip, model::Ids (clip.notes[6]->id), follow);
        clip = model::setLaneNodes (clip, clip.notes[6]->id, core::LaneId::tone,
                                    { model::mkNode (0, 0.9), model::mkNode (0.6, 0.4, model::NodeShape::linear), model::mkNode (1, 0.7) });
        return clip;
    }

    /** a and b hold the same notes (everything but the extras). */
    bool sameValues (const model::Clip& a, const model::Clip& b, std::string& why)
    {
        if (a.notes.size() != b.notes.size())
        {
            why = "the note count differs";
            return false;
        }
        for (std::size_t i = 0; i < a.notes.size(); ++i)
        {
            model::Note x = *a.notes[i], y = *b.notes[i];
            x.extras = nullptr;
            y.extras = nullptr;
            if (! modelfx::bitIdentical (x, y, why))
                return false;
        }
        return true;
    }

    /** The p field (8 bytes at pAt) of a payload, read as the little-endian double it is. */
    double pAt (const Bytes& b, std::size_t at)
    {
        double v = 0.0;
        if (at + 8 <= b.size())
            std::memcpy (&v, &b[at], 8);   // the runners are little-endian (x64)
        return v;
    }

    struct Case
    {
        const char* name;
        codecspec::Hooks hooks;
        std::size_t note = 0;   // the note whose pitch is edited
    };

    void runCase (const Case& c)
    {
        const model::Clip base = baseClip();
        std::vector<std::size_t> pOffsets;
        const Bytes newer = codecspec::write (base, c.hooks, &pOffsets);

        const model::DecodeResult d = model::decodeClip (newer);
        check (d.clip.has_value(), std::string (c.name) + ": the newer CLIP loads " + d.error);
        if (! d.clip.has_value())
            return;
        std::string why;
        check (sameValues (*d.clip, base, why), std::string (c.name) + ": it plays as this version reads it, every known value bit for bit " + why);
        const Bytes resaved = model::encodeClip (*d.clip);
        check (resaved == newer, std::string (c.name) + fmt (": re-saved byte-identically (%d bytes)", static_cast<int> (newer.size())));

        const std::string& id = d.clip->notes[c.note]->id;
        const double p = d.clip->notes[c.note]->p + 7;
        model::NotePatch patch;
        patch.p = p;
        const model::Clip edited = model::updateNotes (*d.clip, model::Ids (id), patch);
        const Bytes after = model::encodeClip (edited);
        const std::size_t at = pOffsets[c.note];
        check (codecspec::differOnlyIn (after, newer, at, 8) && pAt (after, at) == p && after != newer,
               std::string (c.name) + ": after a pitch edit only that note's p changed; the unknown content is still written");
        const auto again = model::decodeClip (after);
        check (again.clip.has_value() && model::encodeClip (*again.clip) == after,
               std::string (c.name) + ": and the edited clip loads and re-saves byte-identically too");
    }
}

SL_TEST ("model", codecCompatTrailing)
{
    section ("Forward compatibility (b): every record carries 8 extra trailing bytes");

    Case c { "(b) trailing bytes", {}, 3 };
    c.hooks.trailing = { 0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04 };
    runCase (c);

    // where the bytes were kept, and that an edited record is written from its value with them again
    const model::Clip base = baseClip();
    const Bytes newer = codecspec::write (base, c.hooks);
    const auto d = model::decodeClip (newer);
    const model::Note* n = d.clip.has_value() ? d.clip->notes[0].get() : nullptr;
    const auto* x = n != nullptr ? n->extras.get() : nullptr;
    const auto amp = static_cast<std::size_t> (core::LaneId::amp);
    check (x != nullptr && x->noteTrailing == c.hooks.trailing && x->sample.trailing == c.hooks.trailing && x->sample.original.empty()
               && x->lanes[amp].lane.trailing == c.hooks.trailing && d.clip->extras != nullptr && d.clip->extras->settingsTrailing == c.hooks.trailing,
           "the trailing bytes are kept per record in Note::extras (note, sample settings, lanes) and the clip extras (settings)");
    bool gens = false;
    for (std::size_t i = 0; d.clip.has_value() && i < d.clip->notes.size() && ! gens; ++i)
        for (const auto& lane : d.clip->notes[i]->extras->lanes)
            gens = gens || lane.gen.trailing == c.hooks.trailing;
    check (gens, "and the generator records' trailing bytes too");

    if (d.clip.has_value())
    {
        model::NodeList drawn { model::mkNode (0, 0.1, model::NodeShape::linear), model::mkNode (0.5, 0.95), model::mkNode (1, 0.3) };
        const model::Clip edited = model::setLaneNodes (*d.clip, d.clip->notes[0]->id, core::LaneId::amp, drawn);
        const model::Clip expected = model::setLaneNodes (base, base.notes[0]->id, core::LaneId::amp, drawn);
        check (model::encodeClip (edited) == codecspec::write (expected, c.hooks),
               "after a lane edit that lane is written from its new nodes, followed by its kept trailing bytes");
    }
}

SL_TEST ("model", codecCompatUnknownLane)
{
    section ("Forward compatibility (c): a lane record of an id this version does not know");

    Case c { "(c) unknown lane", {}, 4 };
    c.hooks.unknownLaneNote = 4;
    c.hooks.unknownLanes = { codecspec::laneRecord ("sparkle", 3, true), codecspec::laneRecord ("chance", 5, false) };
    runCase (c);

    const model::Clip base = baseClip();
    const auto d = model::decodeClip (codecspec::write (base, c.hooks));
    const auto* x = d.clip.has_value() ? d.clip->notes[4]->extras.get() : nullptr;
    check (x != nullptr && x->unknownLanes.size() == 2 && x->unknownLanes[0] == c.hooks.unknownLanes[0] && x->unknownLanes[1] == c.hooks.unknownLanes[1],
           "both unknown lane records are kept verbatim, in order");
    bool othersClean = d.clip.has_value();
    for (std::size_t i = 0; othersClean && i < d.clip->notes.size(); ++i)
        othersClean = i == 4 || d.clip->notes[i]->extras == nullptr;
    check (othersClean, "the other notes carry no extras");

    // written before the known lanes, they are written back after them (plan 3)
    codecspec::Hooks first = c.hooks;
    first.unknownLaneFirst = true;
    const auto e = model::decodeClip (codecspec::write (base, first));
    check (e.clip.has_value() && model::encodeClip (*e.clip) == codecspec::write (base, c.hooks),
           "unknown lane records found before the known lanes are written back after them");

    // a second record of a known lane is kept the same way, and the first one is the lane
    codecspec::Hooks dup;
    dup.unknownLaneNote = 1;
    dup.unknownLanes = { codecspec::laneRecord ("amp", 4, false) };
    const auto f = model::decodeClip (codecspec::write (base, dup));
    std::string why;
    check (f.clip.has_value() && model::encodeClip (*f.clip) == codecspec::write (base, dup) && sameValues (*f.clip, base, why),
           "a second record of a known lane is kept verbatim and does not replace the first " + why);

    // Duplicate copies a note's extras (plan 3: operations that copy a note copy them)
    if (d.clip.has_value())
    {
        const auto dupd = model::duplicateNotes (*d.clip, model::Ids (d.clip->notes[4]->id), 16.0);
        check (! dupd.refused && dupd.clip.notes.back()->extras == d.clip->notes[4]->extras,
               "Duplicate's copy carries the same extras, so the unknown lanes are written for it too");
    }
}

SL_TEST ("model", codecCompatUnknownValues)
{
    section ("Forward compatibility (d): an unknown motion string and a node shape code 9");

    Case c { "(d) unknown values", {}, 6 };
    c.hooks.oddNote = 6;
    c.hooks.motion = "warble";
    c.hooks.oddLane = static_cast<int> (core::LaneId::tone);
    c.hooks.shapeCode = 9;
    runCase (c);

    const model::Clip base = baseClip();
    const Bytes newer = codecspec::write (base, c.hooks);
    const auto d = model::decodeClip (newer);
    if (! d.clip.has_value())
        return;
    const model::Note& n = *d.clip->notes[6];
    const auto tone = static_cast<std::size_t> (core::LaneId::tone);
    check (! n.sample.motion.has_value() && n.lane (core::LaneId::tone)[0].shape == model::NodeShape::smooth,
           "the note plays as well as this version can: the unknown motion follows the instrument, shape 9 reads as smooth");
    check (n.extras != nullptr && ! n.extras->sample.original.empty() && ! n.extras->lanes[tone].lane.original.empty()
               && n.extras->lanes[tone].lane.trailing.empty(),
           "the two records holding unknown values keep their original bytes");
    check (codecspec::find (newer, "warble") != std::string::npos, "(the newer CLIP holds the word warble)");

    // editing the tone lane writes it from its value (shape 9 is gone); the sample record keeps "warble"
    model::NodeList drawn { model::mkNode (0, 0.4), model::mkNode (1, 0.6) };
    const model::Clip laneEdited = model::setLaneNodes (*d.clip, n.id, core::LaneId::tone, drawn);
    codecspec::Hooks motionOnly = c.hooks;
    motionOnly.oddLane = -1;
    check (model::encodeClip (laneEdited) == codecspec::write (model::setLaneNodes (base, n.id, core::LaneId::tone, drawn), motionOnly),
           "after a tone-lane edit that lane is written from its value and the unknown motion is still written");

    // choosing a motion replaces the unknown one
    model::SamplePatch patch;
    patch.motion = std::optional<core::Motion> (core::Motion::freeze);
    const model::Clip motionSet = model::setNoteSample (*d.clip, model::Ids (n.id), patch);
    const Bytes after = model::encodeClip (motionSet);
    codecspec::Hooks shapeOnly = c.hooks;
    shapeOnly.oddNote = 6;
    shapeOnly.motion = "freeze";
    check (codecspec::find (after, "warble") == std::string::npos && after == codecspec::write (model::setNoteSample (base, model::Ids (n.id), patch), shapeOnly),
           "after choosing Freeze the sample record is written from its value; the lane with shape 9 is still written as it was");
}
