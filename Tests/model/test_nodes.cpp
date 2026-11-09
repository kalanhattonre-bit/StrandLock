// model/nodes.json (PLUGIN-PLAN 4 M1, 5.3): every node list cleaned, sampled, evaluated on both sides of an
// edge, sliced, reversed, flipped and scaled. Node lists: same length and shapes, t / v / bend within 1e-12;
// tables within 2 Float32 ulps (the share bit-identical is printed); eval within 1e-12. And the monotone
// smooth segments row: 10,000 seeded random smooth lists at 1025 points overshoot their two nodes by at most 1e-9.

#include "ModelFixture.h"

#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Seeds.h"
#include "model/Node.h"
#include "model/Normalize.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    constexpr double kNodeTol = 1e-12;
    constexpr std::uint64_t kTableUlps = 2;
    constexpr double kEvalTol = 1e-12;
}

SL_TEST ("model", nodesFixture)
{
    section ("model/nodes.json: normalize, sample, evaluate, slice, reverse, flip and scale every node list");

    JsonValue doc;
    if (! fixture::load ("model/nodes.json", doc))
        return;

    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("model/nodes.json", cases.size());
    long long tableValues = 0, tableSame = 0;
    int cappedCases = 0;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const std::string name = c["name"].asString();
        const double def = fixture::num (c["def"]);
        bool exact = true;
        std::string why;
        auto fail = [&] (const std::string& what) { tally.fail (name + ": " + what + ": " + why); };

        // 1. the raw input, cleaned
        const model::NodeList norm = model::normalizeNodes (model::nodesFromJson (c["input"]), def);
        if (! modelfx::nodesMatch (norm, c["normalized"], kNodeTol, exact, why))
        {
            fail ("normalizeNodes");
            continue;
        }
        if (c["capped"].isBool())
        {
            ++cappedCases;
            if (norm.size() > static_cast<std::size_t> (model::kMaxNodes))
            {
                why = std::to_string (norm.size()) + " nodes";
                fail ("capNodes");
                continue;
            }
        }

        // Every operation reads the normalized list as the fixture holds it.
        model::NodeList nd;
        if (! modelfx::nodesFrom (c["normalized"], nd, why))
        {
            fail ("reading normalized");
            continue;
        }

        // 2. 257 Float32 samples
        const auto table = model::sampleNodes (nd, 257);
        {
            std::vector<float> want;
            if (fixture::table (c["sample257"], want) && want.size() == table.size())
                for (std::size_t j = 0; j < table.size(); ++j, ++tableValues)
                    tableSame += fixture::ulps (table[j], want[j]) == 0 ? 1 : 0;
        }
        if (! modelfx::tableMatch (table, c["sample257"], kTableUlps, exact, why))
        {
            fail ("sampleNodes (257)");
            continue;
        }

        // 3. both sides of every vertical edge
        bool evalOk = true;
        for (int side = 0; side < 2 && evalOk; ++side)
        {
            const JsonValue& want = c[side == 0 ? "evalRight" : "evalLeft"];
            if (want.size() != 33)
            {
                why = "33 values expected";
                evalOk = false;
                break;
            }
            for (int j = 0; j <= 32; ++j)
            {
                const double t = static_cast<double> (j) / 32.0;
                const double got = model::evalNodes (nd, t, side == 1);
                const double w = fixture::num (want[static_cast<std::size_t> (j)]);
                if (! fixture::same (got, w))
                    exact = false;
                if (! fixture::withinAbs (got, w, kEvalTol))
                {
                    why = fmt ("t %s: %s, want %s", core::jsNumberToString (t).c_str(), core::jsNumberToString (got).c_str(), core::jsNumberToString (w).c_str());
                    evalOk = false;
                    break;
                }
            }
            if (! evalOk)
                why = std::string (side == 0 ? "evalRight " : "evalLeft ") + why;
        }
        if (! evalOk)
        {
            fail ("evalNodes");
            continue;
        }

        // 4. slices
        bool sliceOk = true;
        const JsonValue& slices = c["slices"];
        for (std::size_t s = 0; s < slices.size() && sliceOk; ++s)
        {
            const double u0 = fixture::num (slices[s]["u0"]), u1 = fixture::num (slices[s]["u1"]);
            if (! modelfx::nodesMatch (model::sliceNodes (nd, u0, u1), slices[s]["nodes"], kNodeTol, exact, why))
            {
                why = "u0 " + core::jsNumberToString (u0) + " u1 " + core::jsNumberToString (u1) + ": " + why;
                sliceOk = false;
            }
        }
        if (! sliceOk || slices.size() != 7)
        {
            fail ("sliceNodes");
            continue;
        }

        // 5. reverse, flip, scale (flipLane / scaleLane on the lanes of this def: flipNodes, scaleNodes (nd, def, f))
        if (! modelfx::nodesMatch (model::reverseNodes (nd), c["reversed"], kNodeTol, exact, why))
        {
            fail ("reverseNodes");
            continue;
        }
        if (! modelfx::nodesMatch (model::flipNodes (nd), c["flipped"], kNodeTol, exact, why))
        {
            fail ("flipNodes");
            continue;
        }
        bool scaleOk = c["scaled"].size() == 2;
        for (std::size_t s = 0; s < c["scaled"].size() && scaleOk; ++s)
        {
            const JsonValue& sc = c["scaled"][s];
            const double f = fixture::num (sc["f"]);
            if (! modelfx::nodesMatch (model::scaleNodes (nd, def, f), sc["nodes"], kNodeTol, exact, why))
            {
                why = "f " + core::jsNumberToString (f) + ": " + why;
                scaleOk = false;
            }
        }
        if (! scaleOk)
        {
            fail ("scaleNodes");
            continue;
        }

        tally.pass (exact);
    }

    note (fmt ("sampleNodes tables: %lld of %lld Float32 values bit for bit (%.2f %%)", tableSame, tableValues,
               tableValues > 0 ? 100.0 * static_cast<double> (tableSame) / static_cast<double> (tableValues) : 0.0));
    check (cappedCases == 3, fmt ("the three lists over 128 nodes were capped (%d)", cappedCases));
    tally.finish();
}

SL_TEST ("model", nodesMonotoneSmooth)
{
    section ("Monotone smooth segments: 10,000 seeded random smooth node lists at 1025 points (PROMPT 14.4)");

    // Random lists through the seed hash: 2..16 nodes, sorted t (some pairs very close, some equal: steep
    // segments and vertical edges), v anywhere in 0..1, every segment smooth.
    std::uint32_t counter = 0;
    auto rnd = [&counter] (std::uint32_t list) { return core::rand01 (core::hash32 (0x6d6f6e, list, counter++)); };

    double worst = 0.0;
    int worstList = -1;
    long long floatOutside = 0;
    for (std::uint32_t list = 0; list < 10000; ++list)
    {
        counter = 0;
        const int n = 2 + static_cast<int> (rnd (list) * 15.0);
        std::vector<double> ts { 0.0 };
        for (int i = 1; i < n - 1; ++i)
            ts.push_back (rnd (list));
        ts.push_back (1.0);
        std::sort (ts.begin() + 1, ts.end() - 1);
        for (std::size_t i = 1; i + 1 < ts.size(); ++i)
        {
            const double r = rnd (list);
            if (r < 0.08)
                ts[i] = ts[i - 1];                                  // a vertical edge
            else if (r < 0.16)
                ts[i] = std::min (ts[i + 1], ts[i - 1] + 1e-7);     // a very steep segment
        }

        model::NodeList nd;
        for (const double t : ts)
            nd.push_back (model::mkNode (t, rnd (list) < 0.1 ? (rnd (list) < 0.5 ? 0.0 : 1.0) : rnd (list)));

        const auto m = model::tangents (nd);
        const auto table = model::sampleNodes (nd, 1025);
        for (int j = 0; j <= 1024; ++j)
        {
            const double t = static_cast<double> (j) / 1024.0;
            const std::size_t k = model::segIndex (nd, t, true);
            const double y = model::evalSeg (nd, m, k, t);   // unclamped
            const double lo = std::min (nd[k].v, nd[k + 1].v), hi = std::max (nd[k].v, nd[k + 1].v);
            const double over = std::max ({ lo - y, y - hi, 0.0 });
            if (over > worst)
            {
                worst = over;
                worstList = static_cast<int> (list);
            }
            // the Float32 table holds clamp (y) rounded, so it lies within the two nodes rounded to Float32
            const float s = table[static_cast<std::size_t> (j)];
            if (s < static_cast<float> (lo) || s > static_cast<float> (hi))
                ++floatOutside;
        }
    }
    note (fmt ("largest overshoot %.3g (list %d); target <= 1e-9", worst, worstList));
    check (worst <= 1e-9, fmt ("smooth segments never pass their two nodes by more than 1e-9 (largest %.3g)", worst));
    check (floatOutside == 0, fmt ("sampleNodes' Float32 values lie within their two nodes (Float32) at all 10,250,000 points (%lld outside)", floatOutside));
}
