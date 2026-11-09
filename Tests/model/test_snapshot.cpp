// The snapshot compiler (PLUGIN-PLAN 1.4, 4 M1, 5.3): model/snapshots.json's 24 snapshots (Strand Demo at res
// 256, the other 11 presets at 64, Strand Demo with Auto Slide off and always and recipes 1-8 at 32, the
// mixed-settings clip at 64 and 32) compiled by the C++ and resolved with each case's instrument
// (core::resolve), field by field exact, tables within 2 Float32 ulps, table lengths exact. Then determinism,
// the SnapshotCache, and the compile times (targets printed; timing gates per plan 4).

#include "ModelFixture.h"

#include "core/Inst.h"
#include "core/JsonLite.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "core/Snapshot.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "model/SnapshotCompiler.h"
#include "model/Tools.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    /** A lane table as the prototype holds it: the stored values, or a flat table's one value fullLength times. */
    std::vector<float> fullTable (const core::NoteTables& t, std::size_t lane)
    {
        const std::vector<float>& v = t.lanes[lane];
        if (v.size() == 2 && t.fullLength[lane] > 2 && std::memcmp (&v[0], &v[1], sizeof (float)) == 0)
            return std::vector<float> (static_cast<std::size_t> (t.fullLength[lane]), v[0]);
        return v;
    }

    std::string idOr (const core::Snapshot& s, std::int32_t index)
    {
        return index >= 0 && static_cast<std::size_t> (index) < s.ids.size() ? s.ids[static_cast<std::size_t> (index)] : std::string ("(none)");
    }

    bool sameId (const core::Snapshot& s, std::int32_t index, const JsonValue& want)
    {
        if (want.isNull())
            return index < 0;
        return index >= 0 && want.isString() && idOr (s, index) == want.asString();
    }

    /** One snapshot note against the fixture's, resolved with inst. */
    bool noteMatches (const core::Snapshot& snap, std::size_t k, const core::Inst& inst, const JsonValue& w, bool& exact, std::string& why)
    {
        const core::NoteSnap& x = snap.notes[k];
        const core::ResolvedNote r = core::resolve (x, inst);
        const std::string id = snap.ids[k];
        auto fail = [&why, &id] (const std::string& what) { why = id + ": " + what; return false; };
        auto num = [] (double got, const JsonValue& want) { return fixture::same (got, fixture::num (want)); };

        if (id != w["id"].asString() || x.idIndex != static_cast<std::int32_t> (k))
            return fail ("id (want " + w["id"].asString() + ")");
        if (static_cast<double> (x.idHash) != w["idHash"].asNumber() || core::idHash (std::string_view (id)) != x.idHash)
            return fail ("idHash");
        if (! num (x.p, w["p"]) || ! num (x.s, w["s"]) || ! num (x.l, w["l"]) || ! num (x.vel, w["vel"]))
            return fail ("p, s, l or vel");
        if (std::string (core::code (r.engine)) != w["engine"].asString() || r.lock != w["lock"].asBool()
            || std::string (core::code (r.motion)) != w["motion"].asString())
            return fail ("engine, lock or motion");
        if ((x.travel == 1 ? "path" : "speed") != w["travel"].asString())
            return fail ("travel");
        const JsonValue& st = w["start"];
        if (std::string (model::kStartModeCodes[x.start.mode]) != st["mode"].asString() || ! num (x.start.pos, st["pos"])
            || ! num (x.start.range, st["range"]) || static_cast<double> (x.start.seed) != st["seed"].asNumber())
            return fail ("start");
        if (r.tape != w["tape"].asBool() || r.rev != w["rev"].asBool() || ! num (r.jumpBeats, w["jump"]) || x.restart != w["restart"].asBool())
            return fail ("tape, rev, jump or restart");

        const JsonValue& g = w["glide"];
        if (g.isNull() != ! r.hasGlide)
            return fail (g.isNull() ? "a glide the prototype does not make" : "no glide");
        if (r.hasGlide && (! num (r.glideD, g["d"]) || ! num (r.glideLen, g["len"]) || ! num (r.glideCurve, g["curve"]) || ! sameId (snap, r.glideTo, g["toId"])))
            return fail ("the glide (d, len, curve or toId " + idOr (snap, r.glideTo) + ")");

        const JsonValue& ch = w["chain"];
        if (! sameId (snap, r.chain.firstIndex, ch["id"]) || static_cast<double> (r.chain.index) != ch["index"].asNumber()
            || ! sameId (snap, r.chain.prevIndex, ch["prevId"]) || ! sameId (snap, r.chain.nextIndex, ch["nextId"]))
            return fail ("the chain (first " + idOr (snap, r.chain.firstIndex) + ", prev " + idOr (snap, r.chain.prevIndex) + ", next "
                         + idOr (snap, r.chain.nextIndex) + ")");

        const core::NoteTables& tables = *snap.tables[k];
        for (int lane = 0; lane < core::kNumLanes; ++lane)
        {
            const auto li = static_cast<std::size_t> (lane);
            const JsonValue& want = w["lanes"][core::kLanes[lane].code];
            const bool flatWanted = want.isObject() && want.contains ("flat");
            const std::size_t length = flatWanted ? static_cast<std::size_t> (want["n"].asNumber()) : 0;
            if (x.lanes[li].data != tables.lanes[li].data() || x.lanes[li].len != static_cast<std::int32_t> (tables.lanes[li].size()) || x.lanes[li].len < 2)
                return fail (std::string ("lane ") + core::kLanes[lane].code + ": the LaneTab does not point at its table");
            if (flatWanted && (tables.lanes[li].size() != 2 || static_cast<std::size_t> (tables.fullLength[li]) != length))
                return fail (std::string ("lane ") + core::kLanes[lane].code + ": a flat table is not stored as 2 values of its full length");
            if (! flatWanted && tables.lanes[li].size() != static_cast<std::size_t> (tables.fullLength[li]))
                return fail (std::string ("lane ") + core::kLanes[lane].code + ": a table that is not flat is stored short");
            std::string tw;
            if (! modelfx::tableMatch (fullTable (tables, li), want, 2, exact, tw))
                return fail (std::string ("lane ") + core::kLanes[lane].code + ": " + tw);
        }
        return true;
    }

    std::optional<model::Clip> caseClip (const JsonValue& c, const std::map<std::string, model::Clip>& clips, const core::Inst& inst)
    {
        const std::string name = c["clip"].asString();
        std::optional<model::Clip> clip;
        if (name.rfind ("preset:", 0) == 0)
            clip = model::loadClipPreset (name.substr (7));
        else if (const auto it = clips.find (name); it != clips.end())
            clip = it->second;
        if (clip.has_value() && c.contains ("recipe"))
        {
            const auto r = model::applyRecipe (*clip, inst, static_cast<int> (c["recipe"].asNumber()), model::Ids::all());
            clip = r.has_value() ? std::optional<model::Clip> (r->clip) : std::nullopt;
        }
        return clip;
    }

    /** Compile clip `runs` times, the fastest time in ms. */
    template <class Fn>
    double fastestMs (int runs, Fn&& fn)
    {
        double best = 1e300;
        for (int i = 0; i < runs; ++i)
        {
            const auto t0 = std::chrono::steady_clock::now();
            fn();
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min (best, std::chrono::duration<double, std::milli> (t1 - t0).count());
        }
        return best;
    }

    /** Two compiles of one clip hold the same snapshot: every note field and every table value bit for bit. */
    bool sameSnapshot (const core::Snapshot& a, const core::Snapshot& b, std::string& why)
    {
        if (a.notes.size() != b.notes.size() || a.ids != b.ids || a.byHash != b.byHash || a.bars != b.bars || a.res != b.res)
        {
            why = "the note lists differ";
            return false;
        }
        auto bits = [] (double p, double q) { return std::memcmp (&p, &q, sizeof p) == 0; };
        auto sameNote = [&bits] (const core::NoteSnap& x, const core::NoteSnap& y)
        {
            bool same = x.idHash == y.idHash && x.idIndex == y.idIndex && bits (x.p, y.p) && bits (x.s, y.s) && bits (x.l, y.l) && bits (x.vel, y.vel)
                     && x.engine == y.engine && x.lock == y.lock && x.motion == y.motion && x.tape == y.tape && x.rev == y.rev && x.travel == y.travel
                     && bits (x.jumpBeats, y.jumpBeats) && x.restart == y.restart && x.start.mode == y.start.mode && bits (x.start.pos, y.start.pos)
                     && bits (x.start.range, y.start.range) && x.start.seed == y.start.seed && bits (x.glide, y.glide);
            for (int m = 0; same && m < 3; ++m)
                same = x.glides[m].present == y.glides[m].present && bits (x.glides[m].d, y.glides[m].d) && bits (x.glides[m].curve, y.glides[m].curve)
                    && x.glides[m].toIndex == y.glides[m].toIndex && x.chains[m].firstIndex == y.chains[m].firstIndex
                    && x.chains[m].index == y.chains[m].index && x.chains[m].prevIndex == y.chains[m].prevIndex
                    && x.chains[m].nextIndex == y.chains[m].nextIndex;
            for (int lane = 0; same && lane < core::kNumLanes; ++lane)
                same = x.lanes[lane].len == y.lanes[lane].len;
            return same;
        };
        for (std::size_t k = 0; k < a.notes.size(); ++k)
        {
            if (! sameNote (a.notes[k], b.notes[k]))
            {
                why = a.ids[k] + ": the note fields differ";
                return false;
            }
            for (std::size_t lane = 0; lane < static_cast<std::size_t> (core::kNumLanes); ++lane)
            {
                const auto& ta = a.tables[k]->lanes[lane];
                const auto& tb = b.tables[k]->lanes[lane];
                if (ta.size() != tb.size() || (! ta.empty() && std::memcmp (ta.data(), tb.data(), ta.size() * sizeof (float)) != 0))
                {
                    why = a.ids[k] + ": lane " + core::kLanes[lane].code + " differs";
                    return false;
                }
            }
        }
        return true;
    }

    /** Plan 4's timing gate: fail only past 5 times the target, and only in an optimized build. */
    void timing (const std::string& what, double ms, double targetMs)
    {
#if defined (NDEBUG) && ! defined (STRANDLOCK_SANITIZE)
        const bool gated = true;
#else
        const bool gated = false;
#endif
        note (fmt ("%s: %.2f ms (target %.0f ms, gate %.0f ms%s)", what.c_str(), ms, targetMs, 5 * targetMs, gated ? "" : ", not gated in this build"));
        check (! gated || ms <= 5 * targetMs, fmt ("%s within 5 x its target (plan 4 timing gates)", what.c_str()));
    }
}

SL_TEST ("model", snapshotsFixture)
{
    section ("model/snapshots.json: compileSnapshot, resolved with each case's instrument, field by field");

    JsonValue doc;
    if (! fixture::load ("model/snapshots.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("model/snapshots.json", cases.size());
    int notes = 0, glides = 0, flats = 0;
    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const core::Inst inst = core::normalizeInst (core::instFromJson (c["inst"]));
        const std::string label = c["clip"].asString() + (c.contains ("recipe") ? " recipe " + core::toJson (c["recipe"]) : std::string())
                                + " res " + core::toJson (c["res"]) + " " + std::string (core::code (inst.glideMode));
        const auto clip = caseClip (c, clips, inst);
        if (! clip.has_value())
        {
            tally.fail (label + ": no clip");
            continue;
        }
        const auto snap = model::compileSnapshot (*clip, static_cast<int> (c["res"].asNumber()));
        const JsonValue& want = c["snapshot"];
        if (snap->version != static_cast<std::uint32_t> (want["version"].asNumber()) || snap->bars != static_cast<int> (want["bars"].asNumber())
            || snap->beatsPerBar != static_cast<int> (want["beatsPerBar"].asNumber()) || snap->res != static_cast<int> (want["res"].asNumber())
            || snap->notes.size() != want["notes"].size() || snap->ids.size() != snap->notes.size() || snap->tables.size() != snap->notes.size())
        {
            tally.fail (label + ": version, bars, beatsPerBar, res or the note count");
            continue;
        }
        bool ok = true, exact = true;
        std::string why;
        for (std::size_t k = 0; ok && k < snap->notes.size(); ++k)
        {
            ok = noteMatches (*snap, k, inst, want["notes"][k], exact, why);
            ++notes;
            glides += want["notes"][k]["glide"].isNull() ? 0 : 1;
            for (int lane = 0; lane < core::kNumLanes; ++lane)
                flats += snap->tables[k]->lanes[static_cast<std::size_t> (lane)].size() == 2 ? 1 : 0;
        }
        bool sorted = std::is_sorted (snap->byHash.begin(), snap->byHash.end()) && snap->byHash.size() == snap->notes.size();
        for (const auto& [h, index] : snap->byHash)
            sorted = sorted && snap->notes[static_cast<std::size_t> (index)].idHash == h;
        if (ok && ! sorted)
        {
            ok = false;
            why = "byHash is not the sorted (idHash, index) list";
        }
        if (ok)
            tally.pass (exact);
        else
            tally.fail (label + ": " + why);
    }
    note (fmt ("%d notes compared, %d with a glide, %d lane tables stored flat as 2 values", notes, glides, flats));
    tally.finish();
}

SL_TEST ("model", snapshotResolveFollows)
{
    section ("core::resolve: the follow markers take the instrument of the moment, one compile serves every inst");

    const auto clip = model::loadClipPreset ("strand-demo");
    if (! clip.has_value())
    {
        check (false, "Strand Demo loads");
        return;
    }
    const auto snap = model::compileSnapshot (*clip, 32);
    // Strand Demo's notes follow the instrument's engine, motion, tape, rev and jump: the resolved values move with it
    core::Inst a = core::defaultInst(), b = core::defaultInst();
    b.engine = core::Engine::cloud;
    b.lock = true;
    b.motion = core::Motion::scatter;
    b.tape = true;
    b.rev = true;
    b.jump = core::RateId::r1_4;
    b.glideTime = 3.0;
    int follows = 0, moved = 0;
    for (const auto& x : snap->notes)
    {
        if (x.engine >= 0 || x.motion >= 0 || x.tape >= 0 || x.rev >= 0 || ! std::isnan (x.jumpBeats))
            continue;
        ++follows;
        const auto ra = core::resolve (x, a), rb = core::resolve (x, b);
        moved += ra.engine == core::Engine::voice && rb.engine == core::Engine::cloud && rb.lock && ! ra.lock && rb.motion == core::Motion::scatter
                         && rb.tape && rb.rev && ra.jumpBeats == 0.5 && rb.jumpBeats == 1.0
                         && (! rb.hasGlide || rb.glideLen == std::min (std::max (0.0, std::isnan (x.glide) ? 3.0 : x.glide), x.l * 0.95))
                     ? 1 : 0;
    }
    check (follows > 0 && moved == follows, fmt ("%d of %d following notes resolve to each instrument's engine, Cloud's lock, motion, tape, rev, "
                                                 "jump and glide time", moved, follows));
    int differ = 0;
    for (const auto& x : snap->notes)
    {
        core::Inst off = a, always = a;
        off.glideMode = core::GlideMode::off;
        always.glideMode = core::GlideMode::always;
        differ += core::resolve (x, off).chain.firstIndex != core::resolve (x, always).chain.firstIndex ? 1 : 0;
    }
    check (differ > 0, fmt ("the chains of Auto Slide off and always differ for %d notes: the glide mode picks the compiled variant", differ));
}

SL_TEST ("model", snapshotDeterminismAndSpeed)
{
    section ("compileSnapshot: the same clip twice gives the same tables; the SnapshotCache; compile times");

    const model::Clip demo = *model::loadClipPreset ("strand-demo");
    {
        const auto s1 = model::compileSnapshot (demo, 256);
        const auto s2 = model::compileSnapshot (demo, 256);
        std::string why;
        check (sameSnapshot (*s1, *s2, why) && s1->tables[0] != s2->tables[0], "compiling Strand Demo twice gives identical notes and tables " + why);

        // the compiler reuses the tables of untouched default lanes within one compile: the same values as computing each
        int same = 0, total = 0;
        for (std::size_t k = 0; k < s1->notes.size(); ++k)
        {
            const model::Note* n = modelfx::findNote (demo, s1->ids[k]);
            if (n == nullptr)
                continue;
            const auto own = model::compileTables (*n, 256);
            for (std::size_t lane = 0; lane < static_cast<std::size_t> (core::kNumLanes); ++lane)
            {
                ++total;
                same += own->lanes[lane] == s1->tables[k]->lanes[lane] && own->fullLength[lane] == s1->tables[k]->fullLength[lane] ? 1 : 0;
            }
        }
        check (total == 260 && same == total, fmt ("each table equals compileTables of its note alone, value for value (%d of %d)", same, total));
    }

    // the cache: an edit of one note recompiles that note only
    {
        model::SnapshotCache cache;
        const auto first = model::compileSnapshot (demo, 256, nullptr, &cache);
        model::NotePatch patch;
        patch.vel = 0.5;
        const std::string edited = demo.notes[3]->id;
        const model::Clip next = model::updateNotes (demo, model::Ids (edited), patch);
        const auto second = model::compileSnapshot (next, 256, &cache, &cache);
        const auto fresh = model::compileSnapshot (next, 256);
        int shared = 0, recompiled = 0;
        for (std::size_t k = 0; k < second->notes.size(); ++k)
        {
            if (second->ids[k] == edited)
                recompiled += second->tables[k] != first->tables[k] ? 1 : 0;
            else
                shared += second->tables[k] == first->tables[k] ? 1 : 0;
        }
        std::string why;
        check (shared == static_cast<int> (demo.notes.size()) - 1 && recompiled == 1 && cache.size() == demo.notes.size(),
               fmt ("after a one-note edit %d tables are shared with the last snapshot and 1 is new; the cache holds the last clip's %d notes", shared,
                    static_cast<int> (cache.size())));
        check (sameSnapshot (*second, *fresh, why), "a snapshot built with the cache equals one built without it " + why);
        const auto res64 = model::compileSnapshot (next, 64, &cache);
        check (res64->tables[0] != second->tables[0] && res64->tables[0]->fullLength[0] != second->tables[0]->fullLength[0],
               "a cached table is not reused at another res");
    }

    // timings (Release targets, plan 4): Strand Demo 2 ms; 256 chopped notes with a 1/32 pattern 10 ms;
    // 1024 four-beat notes with a random generator 120 ms
    const double demoMs = fastestMs (5, [&demo] { (void) model::compileSnapshot (demo, 256); });
    timing ("Strand Demo at res 256", demoMs, 2.0);

    model::Clip chopSrc = model::newClip (16);
    for (int i = 0; i < 16; ++i)
    {
        model::Note n = model::mkNote (48 + i, i * 4.0, 4.0, "ch" + std::to_string (i));
        model::GenPatch gp;
        gp.rate = core::RateId::r1_32;
        n.genOf (core::LaneId::tone) = model::makeGen (model::GenKind::pattern, gp);
        n.genOf (core::LaneId::amp) = model::makeGen (model::GenKind::pattern, gp);
        chopSrc.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
    }
    const auto chopped = model::chop (chopSrc, model::Ids::all(), core::RateId::r1_16);
    check (! chopped.refused && chopped.clip.notes.size() == 256, fmt ("(16 four-beat notes with a 1/32 pattern chopped at 1/16: %d notes)",
                                                                       static_cast<int> (chopped.clip.notes.size())));
    const double chopMs = fastestMs (5, [&chopped] { (void) model::compileSnapshot (chopped.clip, 256); });
    timing ("256 chopped notes with a 1/32 pattern", chopMs, 10.0);

    model::Clip big = model::newClip (64);
    for (int i = 0; i < 1024; ++i)
    {
        model::Note n = model::mkNote (36 + i % 48, (i % 64) * 4.0, 4.0, "rg" + std::to_string (i));
        model::GenPatch gp;
        gp.rate = core::RateId::r1_16;
        gp.perNote = model::PerNote::different;
        n.genOf (core::LaneId::tone) = model::makeGen (model::GenKind::random, gp);
        big.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
    }
    const double bigMs = fastestMs (3, [&big] { (void) model::compileSnapshot (big, 256); });
    timing ("1024 four-beat notes with a random generator", bigMs, 120.0);
    const auto bigSnap = model::compileSnapshot (big, 256);
    std::size_t floats = 0;
    for (const auto& t : bigSnap->tables)
        for (const auto& lane : t->lanes)
            floats += lane.size();
    check (bigSnap->notes.size() == 1024 && floats == std::size_t (1024) * (9 * 2 + 1025),
           fmt ("its snapshot keeps 9 flat lanes as 2 values and the generator lane at full length per note (%d floats)", static_cast<int> (floats)));
}
