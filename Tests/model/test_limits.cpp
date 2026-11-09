// Edit caps (PLUGIN-PLAN 3 "one table of limits", 4 M1): a Chop that would pass 1024 notes, and Duplicate,
// Paste and addNote past 4096 notes, are refused with the count and leave the clip unchanged; one note fewer is
// accepted. Chop's own maxNotes never lets a clip pass 4096. Also the note paste and the prototype's Copy /
// Paste lanes, which no fixture covers. (The 32 MiB encoded-size cap needs the clip codec: M1 part 3.)

#include "ModelFixture.h"

#include "core/Lanes.h"
#include "core/Rates.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Tools.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** n notes c0 .. c(n-1) of len beats. */
    model::Clip clipOf (int n, double len)
    {
        model::Clip c = model::newClip (64);
        c.notes.reserve (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
            c.notes.push_back (std::make_shared<const model::Note> (
                model::mkNote (48 + i % 24, static_cast<double> (i % 200), len, "c" + std::to_string (i))));
        return c;
    }

    model::Ids firstIds (int n)
    {
        std::vector<std::string> ids;
        for (int i = 0; i < n; ++i)
            ids.push_back ("c" + std::to_string (i));
        return model::Ids (std::move (ids));
    }

    /** A refused edit: refused, the count, no new ids, and exactly the input clip's own notes. */
    bool refusedWith (const model::EditResult& r, const model::Clip& in, double count)
    {
        bool same = r.clip.notes.size() == in.notes.size();
        for (std::size_t i = 0; same && i < in.notes.size(); ++i)
            same = r.clip.notes[i] == in.notes[i];
        return r.refused && r.count == count && same && r.ids.empty();
    }

    /** An accepted edit that added `added` notes after the input's own, which stay shared. */
    bool addedTo (const model::EditResult& r, const model::Clip& in, std::size_t added)
    {
        bool same = r.clip.notes.size() == in.notes.size() + added && r.ids.size() == added;
        for (std::size_t i = 0; same && i < in.notes.size(); ++i)
            same = r.clip.notes[i] == in.notes[i];
        std::set<std::string> ids;
        for (const auto& n : r.clip.notes)
            ids.insert (n->id);
        return ! r.refused && same && ids.size() == r.clip.notes.size();
    }
}

SL_TEST ("model", editCaps)
{
    section ("Edit caps: Chop past 1024 notes, Duplicate, Paste and addNote past 4096 notes refused, the clip unchanged");

    {
        const model::Clip in20 = clipOf (20, 16), in16 = clipOf (16, 16);
        const auto over = model::chop (in20, model::Ids::all(), core::RateId::r1_16);
        check (refusedWith (over, in20, 1280),
               fmt ("Chop of 20 four-bar notes at 1/16 (1280 notes) is refused with the count (%g), the clip unchanged", over.count));
        const auto atCap = model::chop (in16, model::Ids::all(), core::RateId::r1_16);
        check (! atCap.refused && atCap.count == 1024 && atCap.clip.notes.size() == 1024, "Chop to exactly 1024 notes is done");
    }
    {
        // a maxNotes above the clip cap still stops at 4096 (rates are quarter-note beats: 1/16 = 0.25, 1/32 = 0.125)
        const model::Clip in = clipOf (4090, 1);
        const auto fits = model::chop (in, model::Ids ("c0"), core::RateId::r1_16, {}, 100000);
        check (! fits.refused && fits.count == 4093 && fits.clip.notes.size() == 4093, "Chop with maxNotes 100000 to 4093 notes is done");
        const auto over = model::chop (in, firstIds (2), core::RateId::r1_32, {}, 100000);
        check (refusedWith (over, in, 4104), fmt ("Chop with maxNotes 100000 to 4104 notes is refused at the 4096 cap (count %g)", over.count));
    }
    {
        const model::Clip in = clipOf (4000, 1);
        check (refusedWith (model::duplicateNotes (in, firstIds (97)), in, 4097), "Duplicate to 4097 notes is refused with the count, the clip unchanged");
        check (addedTo (model::duplicateNotes (in, firstIds (96)), in, 96), "Duplicate to exactly 4096 notes is done, every id unique");
    }
    {
        const model::Clip full = clipOf (4096, 1), oneShort = clipOf (4095, 1);
        check (refusedWith (model::addNote (full, 60, 0, 1), full, 4097), "addNote on a 4096-note clip is refused with the count, the clip unchanged");
        check (addedTo (model::addNote (oneShort, 60, 0, 1), oneShort, 1), "addNote on a 4095-note clip is done");
    }
    {
        const model::Clip in = clipOf (4090, 1);
        const model::Clip source = clipOf (7, 2);
        const std::vector<model::NotePtr> seven = source.notes;
        const std::vector<model::NotePtr> six (seven.begin(), seven.begin() + 6);
        check (refusedWith (model::pasteNotes (in, seven, 8), in, 4097), "Paste to 4097 notes is refused with the count, the clip unchanged");
        check (addedTo (model::pasteNotes (in, six, 8), in, 6), "Paste to exactly 4096 notes is done");
    }
}

SL_TEST ("model", pasteAndLanes)
{
    section ("Paste notes and the prototype's Copy / Paste lanes");

    // a -> b inside the group, c -> x outside it
    model::Clip clip = model::newClip (4);
    auto add = [&clip] (double p, double s, double l, const char* id, const char* to)
    {
        model::Note n = model::mkNote (p, s, l, id);
        n.glideTo = to;
        clip.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
    };
    add (60, 1, 1, "a", "b");
    add (64, 2, 1, "b", "");
    add (67, 1.5, 0.5, "c", "x");
    add (72, 6, 1, "x", "");

    const std::vector<model::NotePtr> group { clip.notes[2], clip.notes[1], clip.notes[0] };   // any order: pasted in time order
    const auto pasted = model::pasteNotes (clip, group, 8);
    bool placed = ! pasted.refused && pasted.ids.size() == 3 && pasted.clip.notes.size() == 7;
    const model::Note* a2 = placed ? pasted.clip.notes[4].get() : nullptr;
    const model::Note* c2 = placed ? pasted.clip.notes[5].get() : nullptr;
    const model::Note* b2 = placed ? pasted.clip.notes[6].get() : nullptr;
    placed = placed && a2->s == 8 && c2->s == 8.5 && b2->s == 9 && a2->p == 60 && c2->p == 67 && b2->p == 64;
    check (placed, "Paste puts copies in time order after the clip's notes, the earliest at the paste point, the rest at their distances");
    check (placed && a2->glideTo == b2->id && c2->glideTo.empty() && b2->glideTo.empty(),
           "a link inside the pasted group points at the copy; a link out of it is dropped");
    check (placed && a2->id != "a" && b2->id != "b" && c2->id != "c" && a2->id.size() > 1 && a2->id[0] == 'n'
               && pasted.clip.notes[0] == clip.notes[0] && pasted.clip.notes[3] == clip.notes[3],
           "the copies get fresh ids ('n' + base 36) and the clip's own notes are shared");
    const auto again = model::pasteNotes (clip, group, 8);
    check (again.ids == pasted.ids, "Paste ids are reproducible (hashed from the source id and the paste point, never the clock)");

    // Copy lanes from a note with a tone generator; paste onto two notes of other lengths
    model::Note src = model::mkNote (60, 0, 2, "src");
    src.lane (core::LaneId::amp) = { model::mkNode (0, 0.2), model::mkNode (0.3, 0.9, model::NodeShape::bend, 0.5), model::mkNode (1, 0.4) };
    src.lane (core::LaneId::tone) = { model::mkNode (0, 1), model::mkNode (1, 0.1) };
    src.genOf (core::LaneId::tone) = model::makeGen (model::GenKind::random, model::GenPatch {});
    src.lane (core::LaneId::vib) = { model::mkNode (0, 0.7), model::mkNode (1, 0.7) };
    std::array<bool, core::kNumLanes> mask {};
    mask[static_cast<std::size_t> (core::LaneId::amp)] = true;
    mask[static_cast<std::size_t> (core::LaneId::tone)] = true;
    const model::LaneClipboard cb = model::copyLanes (src, mask);

    const model::Clip onto = model::pasteLanes (clip, model::Ids { "a", "x" }, cb);
    const model::Note& pa = *onto.notes[0];
    const model::Note& px = *onto.notes[3];
    check (model::sameNodes (pa.lane (core::LaneId::amp), src.lane (core::LaneId::amp))
               && model::sameNodes (px.lane (core::LaneId::tone), src.lane (core::LaneId::tone))
               && px.genOf (core::LaneId::tone).has_value() && ! pa.genOf (core::LaneId::amp).has_value(),
           "Paste lanes gives each target the copied lanes' nodes (t values identical, so they stretch to its length) and generators");
    check (model::sameNodes (pa.lane (core::LaneId::vib), clip.notes[0]->lane (core::LaneId::vib)) && pa.l == 1 && px.l == 1
               && onto.notes.size() == clip.notes.size() && onto.notes[1] == clip.notes[1] && onto.notes[2] == clip.notes[2],
           "lanes outside the mask, lengths and the other notes are untouched; Paste lanes adds no note");
}
