// Undo (PLUGIN-PLAN 1.2 History, 4 M1 "undo"): VersionHistory against the prototype's History rules: push
// ignores a state equal to the current one, a push after undo drops the redo steps, the 201st push drops the
// oldest version. 1,000 seeded random edit sequences of 1-300 steps (with undos mid-way): undo to the start gives
// the start clip byte-identical in the codec encoding, redo to the end gives the final clip. 200 one-note edits on
// a 1024-note clip hold at most 8 MB (AllocCounter's live bytes), because unchanged notes are shared.

#include "core/Lanes.h"
#include "core/Seeds.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/History.h"
#include "model/NoteSettings.h"
#include "model/Presets.h"
#include "model/Tools.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using model::ClipPtr;

    ClipPtr ptr (model::Clip c) { return std::make_shared<const model::Clip> (std::move (c)); }

    /** The prototype's History rules, written out on a plain list: the model the test holds VersionHistory to. */
    struct Reference
    {
        int limit = model::kHistoryLimit;
        std::vector<ClipPtr> stack;
        int i = -1;

        bool push (const ClipPtr& s)
        {
            if (i >= 0 && model::SameClipPtr() (stack[static_cast<std::size_t> (i)], s))
                return false;
            stack.resize (static_cast<std::size_t> (i + 1));
            stack.push_back (s);
            while (static_cast<int> (stack.size()) > limit + 1)
                stack.erase (stack.begin());
            i = static_cast<int> (stack.size()) - 1;
            return true;
        }
    };

    class Rng
    {
    public:
        explicit Rng (std::uint32_t s) : seed (s) {}
        double next() { return core::rand01 (core::hash32 (0x4157, seed, counter++)); }
        int below (int n) { return n <= 0 ? 0 : static_cast<int> (std::floor (next() * n)) % n; }

    private:
        std::uint32_t seed;
        std::uint32_t counter = 0;
    };

    model::Clip startClip (int which)
    {
        const auto& presets = model::clipPresets();
        if (which % 3 != 2)
            return *model::loadClipPreset (presets[static_cast<std::size_t> (which) % presets.size()].id);
        model::Clip c = model::newClip (2);
        for (int k = 0; k < 3 + which % 5; ++k)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (55 + k, k * 0.5, 1, "h" + std::to_string (which) + "-" + std::to_string (k))));
        return c;
    }

    /** One random edit of cur; some leave the clip as it was (push must then refuse them). */
    ClipPtr randomEdit (const ClipPtr& cur, Rng& rnd, std::string& label)
    {
        const model::Clip& c = *cur;
        const int n = static_cast<int> (c.notes.size());
        const std::string id = n > 0 ? c.notes[static_cast<std::size_t> (rnd.below (n))]->id : std::string();
        const auto lane = static_cast<core::LaneId> (rnd.below (core::kNumLanes));
        switch (rnd.below (10))
        {
            case 0:
                label = "Add note";
                if (n < 40)
                    return ptr (model::addNote (c, 40 + rnd.below (40), rnd.below (32) * 0.25, 0.25 + rnd.below (8) * 0.25).clip);
                [[fallthrough]];
            case 1:
                label = "Delete";
                if (n > 1)
                    return ptr (model::removeNotes (c, model::Ids (id)));
                [[fallthrough]];
            case 2:
            {
                label = "Move";
                model::NotePatch p;
                p.p = 36 + rnd.below (60);
                p.s = rnd.below (64) * 0.125;
                return ptr (model::updateNotes (c, model::Ids (id), p));
            }
            case 3:
            {
                label = "Length";
                model::NotePatch p;
                p.l = 0.125 + rnd.below (16) * 0.125;
                p.vel = rnd.next();
                return ptr (model::updateNotes (c, model::Ids (id), p));
            }
            case 4:
            {
                label = "Draw";
                model::NodeList nodes;
                const int count = 2 + rnd.below (6);
                for (int k = 0; k < count; ++k)
                    nodes.push_back (model::mkNode (static_cast<double> (k) / (count - 1), rnd.next(), static_cast<model::NodeShape> (rnd.below (4))));
                return ptr (model::setLaneNodes (c, id, lane, nodes));
            }
            case 5:
                label = "Duplicate";
                if (n < 40)
                    return ptr (model::duplicateNotes (c, model::Ids (id), 1.0 + rnd.below (4)).clip);
                [[fallthrough]];
            case 6:
            {
                label = "Generator";
                model::GenPatch g;
                g.seed = rnd.below (5);
                return ptr (model::setGen (c, model::Ids (id), lane, model::makeGen (rnd.below (2) ? model::GenKind::random : model::GenKind::pattern, g)));
            }
            case 7:
                label = "Reverse";
                return ptr (model::reverseLane (c, model::Ids (id), lane));
            case 8:
                label = "Nothing";   // a copy of the note with nothing changed: the same state
                return ptr (model::updateNotes (c, model::Ids (id), model::NotePatch {}));
            default:
                label = "Again";     // the very same version
                return cur;
        }
    }
}

SL_TEST ("model", historyRules)
{
    section ("VersionHistory: the prototype's History rules");

    std::vector<ClipPtr> v;
    for (int k = 0; k < 205; ++k)
    {
        model::Clip c = model::newClip (1);
        c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60 + k % 60, 0, 1, "v" + std::to_string (k))));
        v.push_back (ptr (std::move (c)));
    }

    model::ClipHistory h (v[0]);
    check (h.size() == 1 && ! h.canUndo() && ! h.canRedo() && h.current() == v[0], "a history starts with its first version, nothing to undo or redo");
    check (! h.push (v[0], "same object") && ! h.push (ptr (*v[0]), "same content") && h.size() == 1,
           "push ignores the current state, as the same object and as an equal copy");
    for (int k = 1; k <= 200; ++k)
        h.push (v[static_cast<std::size_t> (k)], "edit " + std::to_string (k));
    check (h.size() == 201 && h.undoLabel() == std::optional<std::string> ("edit 200"), "200 pushes after the start: 201 versions, the undo label names the last step");
    int undos = 0;
    while (h.undo())
        ++undos;
    check (undos == 200 && h.current() == v[0] && h.redoLabel() == std::optional<std::string> ("edit 1"), "200 undos reach the start");
    while (h.redo()) {}
    check (h.push (v[201], "edit 201") && h.size() == 201, "the 201st push keeps 201 versions");
    undos = 0;
    while (h.undo())
        ++undos;
    check (undos == 200 && h.current() == v[1], "and drops the oldest: undo now stops at the first edit");

    h.redo();
    h.redo();
    check (h.canRedo() && h.push (v[202], "branch") && ! h.canRedo() && h.size() == 4 && h.current() == v[202],
           "a push after undo drops the redo steps");
    check (h.undo() == v[3] && h.redo() == v[202] && ! h.redo().has_value(), "undo and redo return the versions themselves");

    model::ClipHistory small (v[0], 3);
    for (int k = 1; k <= 5; ++k)
        small.push (v[static_cast<std::size_t> (k)]);
    int smallUndos = 0;
    while (small.undo())
        ++smallUndos;
    check (small.size() == 4 && smallUndos == 3 && small.current() == v[2], "a limit of 3 keeps 3 undo steps");
    model::ClipHistory one (v[0], 0);
    one.push (v[1]);
    check (one.limit() == 1 && one.size() == 2, "a limit below 1 reads 1");
    one.clear (v[3]);
    check (one.size() == 1 && one.current() == v[3] && ! one.canUndo(), "clear (state) starts again from that state");
}

SL_TEST ("model", historyRandomSequences)
{
    section ("Undo: 1,000 seeded random edit sequences of 1-300 steps, undo to the start and redo to the end");

    int sequences = 0, startOk = 0, endOk = 0, pushesOk = 0, stepsOk = 0, longOnes = 0, refused = 0;
    long long steps = 0;
    std::string firstProblem;
    for (int s = 0; s < 1000; ++s)
    {
        Rng rnd (static_cast<std::uint32_t> (s));
        const ClipPtr start = ptr (startClip (s));
        const std::vector<std::uint8_t> startBytes = model::encodeClip (*start);
        model::ClipHistory h (start);
        Reference ref;
        ref.push (start);
        ClipPtr cur = start;
        const int length = 1 + rnd.below (300);
        bool pushes = true, during = true;
        for (int k = 0; k < length; ++k)
        {
            ++steps;
            if (k > 0 && k + 1 < length && rnd.next() < 0.04)
            {
                // undo a few steps mid-way; the next accepted push drops what they undid
                const int back = 1 + rnd.below (3);
                for (int u = 0; u < back && ref.i > 0; ++u)
                {
                    --ref.i;
                    const auto got = h.undo();
                    during = during && got.has_value() && *got == ref.stack[static_cast<std::size_t> (ref.i)];
                }
                cur = ref.stack[static_cast<std::size_t> (ref.i)];
                continue;
            }
            std::string label;
            const ClipPtr next = randomEdit (cur, rnd, label);
            const bool want = ref.push (next);
            const bool got = h.push (next, label);
            refused += want ? 0 : 1;
            pushes = pushes && want == got && h.size() == ref.stack.size() && h.position() == ref.i;
            if (want)
                cur = next;
        }
        ++sequences;
        pushesOk += pushes ? 1 : 0;
        longOnes += ref.stack.front() != start ? 1 : 0;

        // undo to the start
        bool walk = true;
        int i = ref.i;
        while (const auto back = h.undo())
            walk = walk && --i >= 0 && *back == ref.stack[static_cast<std::size_t> (i)];
        const auto oldest = h.current();
        const bool startKept = ref.stack.front() == start;
        const bool atStart = walk && i == 0 && oldest.has_value() && model::encodeClip (**oldest) == model::encodeClip (*ref.stack.front())
                          && (! startKept || model::encodeClip (**oldest) == startBytes);
        // redo to the end
        while (const auto fwd = h.redo())
            walk = walk && ++i < static_cast<int> (ref.stack.size()) && *fwd == ref.stack[static_cast<std::size_t> (i)];
        const auto last = h.current();
        const bool atEnd = walk && last.has_value() && *last == ref.stack.back() && model::encodeClip (**last) == model::encodeClip (*ref.stack.back());
        startOk += atStart ? 1 : 0;
        endOk += atEnd ? 1 : 0;
        stepsOk += during && walk ? 1 : 0;
        if (firstProblem.empty() && ! (pushes && atStart && atEnd && during))
            firstProblem = fmt ("sequence %d (%d steps): pushes %d, start %d, end %d, mid-way undos %d", s, length, pushes, atStart, atEnd, during);
        // the start clip was never changed by anything that edited its versions
        startOk -= model::encodeClip (*start) == startBytes ? 0 : 1;
    }
    note (fmt ("%d sequences, %lld steps, %d pushes refused as the same state, %d sequences pushed past the 200-step limit", sequences, steps, refused, longOnes));
    if (! firstProblem.empty())
        note ("first problem: " + firstProblem);
    check (sequences == 1000 && pushesOk == sequences, fmt ("every push accepted or refused as the prototype's History would, same size and position (%d of %d)", pushesOk, sequences));
    check (startOk == sequences, fmt ("undo to the start gives the start clip byte-identical (the oldest kept version past 200 steps) (%d of %d)", startOk, sequences));
    check (endOk == sequences && stepsOk == sequences, fmt ("redo to the end gives the final clip, every step on the way the version pushed there (%d of %d)", endOk, sequences));
    check (refused > 0 && longOnes > 0, "the sequences include refused pushes and sequences longer than the limit");
}

SL_TEST ("model", historyMemory)
{
    section ("Undo memory: 200 one-note edits on a 1024-note clip, unchanged notes shared between versions");

    long long live = 0;
    std::size_t versions = 0;
    alloc::reset();
    {
        const alloc::Scope counting;
        model::Clip c = model::newClip (64);
        for (int i = 0; i < 1024; ++i)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (36 + i % 48, (i % 256) * 1.0, 1.0, "m" + std::to_string (i))));
        ClipPtr cur = ptr (std::move (c));
        model::ClipHistory h (cur);
        for (int i = 0; i < 200; ++i)
        {
            model::NotePatch p;
            p.p = 30 + i % 80;
            cur = ptr (model::updateNotes (*cur, model::Ids (cur->notes[static_cast<std::size_t> ((i * 37) % 1024)]->id), p));
            h.push (cur, "Edit");
        }
        cur.reset();
        versions = h.size();
        live = alloc::counts().liveBytes;
    }
    const double mb = static_cast<double> (live) / (1024.0 * 1024.0);
    note (fmt ("%d versions hold %.2f MB live (counting %s)", static_cast<int> (versions), mb, alloc::coverage()));
    check (versions == 201 && live > 1024 * 1024, "the 201 versions are held, and the count sees the clip itself (over 1 MB)");
    check (live <= 8LL * 1024 * 1024, fmt ("they hold at most 8 MB (%.2f MB; a copy of every note per version would be about 450 MB)", mb));
}
