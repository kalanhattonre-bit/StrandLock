// model/format.json (PLUGIN-PLAN 4 M1, 5.3): every lane value in real units (mapLane, within 1e-12 relative)
// and as readout text (formatLane, exact), against the settings each lane depends on. And the lane
// mapping table row (PROMPT 14.4, BLUEPRINT 1.2): mapLane at 41 values per lane equals the table's formula
// within 1e-12 relative.

#include "ModelFixture.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Travel.h"
#include "model/LaneFormat.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <map>
#include <string>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    constexpr double kValueRel = 1e-12;

    std::string str (double x) { return core::jsNumberToString (x); }
}

SL_TEST ("model", formatFixture)
{
    section ("model/format.json: lane values in real units and as readout text");

    JsonValue doc;
    if (! fixture::load ("model/format.json", doc))
        return;

    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("model/format.json", cases.size());
    std::map<std::string, int> perLane;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const auto lane = core::findLane (c["lane"].asString());
        if (! lane.has_value())
        {
            tally.fail ("unknown lane " + c["lane"].asString());
            continue;
        }
        ++perLane[c["lane"].asString()];

        // ctx.inst is a partial instrument: instFromJson normalizes it, as mapLane's normalizeInst does
        model::LaneCtx ctx;
        ctx.inst = core::instFromJson (c["ctx"]["inst"]);
        ctx.bpm = c["ctx"].contains ("bpm") ? fixture::num (c["ctx"]["bpm"]) : std::nan ("");
        ctx.travelKind = c["ctx"]["travelKind"].isString() && c["ctx"]["travelKind"].asString() == "path" ? model::TravelKind::path
                                                                                                          : model::TravelKind::speed;
        const double v = fixture::num (c["v"]);
        const double value = model::mapLane (*lane, v, ctx), wantValue = fixture::num (c["value"]);
        const std::string text = model::formatLane (*lane, v, ctx), wantText = c["text"].asString();
        const std::string label = c["lane"].asString() + " " + str (v) + " " + core::toJson (c["ctx"]);

        if (! fixture::withinRel (value, wantValue, kValueRel))
            tally.fail (label + ": value " + str (value) + ", want " + str (wantValue));
        else if (text != wantText)
            tally.fail (label + ": \"" + text + "\", want \"" + wantText + "\"");
        else
            tally.pass (fixture::same (value, wantValue));
    }

    std::string lanes;
    for (const auto& [lane, n] : perLane)
        lanes += lane + " " + std::to_string (n) + "  ";
    note ("cases per lane: " + lanes);
    check (perLane.size() == 10, fmt ("all 10 lanes are in the file (%d)", static_cast<int> (perLane.size())));
    tally.finish();
}

SL_TEST ("model", laneMappingTable)
{
    section ("The lane mapping table (BLUEPRINT 1.2, 1.3): mapLane at 41 values per lane against the table's formulas");

    // The formulas as BLUEPRINT 1.2 writes them, in other but equal forms, evaluated here and not in the model.
    // Heads, Grain and Refresh are relative to the instrument: heads 3, Voice grain 2 cycles, Refresh free 120 ms.
    core::Inst inst = core::defaultInst();
    inst.heads = 3.0;
    inst.refresh.mode = core::RefreshMode::free;
    inst.refresh.ms = 120.0;
    model::LaneCtx ctx;
    ctx.inst = inst;
    model::LaneCtx pathCtx = ctx;
    pathCtx.travelKind = model::TravelKind::path;

    auto speed = [] (double v)   // BLUEPRINT 1.3 s (v) on the Float32 value, the band inclusive at Float32 precision
    {
        const double f = static_cast<double> (static_cast<float> (v));
        if (f >= 0.48 - 2e-8 && f <= 0.52 + 2e-8)
            return 0.0;
        if (f > 0.52)
            return std::fmin (8.0, std::exp2 (3.0 * (f - 0.75) / 0.23));
        return -std::fmin (8.0, std::exp2 (3.0 * (0.25 - f) / 0.23));
    };

    struct Row
    {
        core::LaneId lane;
        const model::LaneCtx* ctx;
        const char* name;
    };
    const Row rows[] = {
        { core::LaneId::pitch, &ctx, "pitch (v-0.5)*24 st" },        { core::LaneId::amp, &ctx, "amp gain v*v*1.3" },
        { core::LaneId::vib, &ctx, "vib v st" },                     { core::LaneId::tone, &ctx, "tone 60*2^(v*8.3) Hz" },
        { core::LaneId::formant, &ctx, "formant (v-0.5)*24 st" },    { core::LaneId::hollow, &ctx, "hollow -v*28 dB" },
        { core::LaneId::travel, &ctx, "travel speed s(v)" },         { core::LaneId::travel, &pathCtx, "travel path v" },
        { core::LaneId::heads, &ctx, "heads 3+(v-0.5)*14 in 1..8" }, { core::LaneId::grain, &ctx, "grain 2*2^((v-0.5)*4) cycles" },
        { core::LaneId::refresh, &ctx, "refresh 120/2^((v-0.5)*6) ms, Smooth below 0.06" },
    };

    for (const Row& row : rows)
    {
        int bad = 0;
        double worst = 0.0;
        for (int i = 0; i <= 40; ++i)
        {
            const double v = static_cast<double> (i) / 40.0;
            double want = 0.0;
            switch (row.lane)
            {
                case core::LaneId::pitch:
                case core::LaneId::formant: want = 24.0 * v - 12.0; break;
                case core::LaneId::amp:     want = 1.3 * std::pow (v, 2.0); break;
                case core::LaneId::vib:     want = v; break;
                case core::LaneId::tone:    want = 60.0 * std::exp2 (8.3 * v); break;
                case core::LaneId::hollow:  want = -28.0 * v; break;
                case core::LaneId::travel:  want = row.ctx->travelKind == model::TravelKind::path ? v : speed (v); break;
                case core::LaneId::heads:   want = std::fmax (1.0, std::fmin (8.0, 3.0 + 14.0 * v - 7.0)); break;
                case core::LaneId::grain:   want = std::exp2 (4.0 * v - 1.0); break;
                case core::LaneId::refresh:
                {
                    const double f = static_cast<double> (static_cast<float> (v));
                    want = f < 0.06 - 2e-8 ? 0.0 : 120.0 * std::exp2 (3.0 - 6.0 * f);
                    break;
                }
            }
            const double got = model::mapLane (row.lane, v, *row.ctx);
            if (! fixture::withinRel (got, want, 1e-12))
            {
                if (bad < 3)
                    note (fmt ("%s at v = %s: %s, want %s", row.name, str (v).c_str(), str (got).c_str(), str (want).c_str()));
                ++bad;
            }
            if (std::isfinite (got) && std::isfinite (want) && std::fmax (std::fabs (got), std::fabs (want)) > 0.0)
                worst = std::fmax (worst, std::fabs (got - want) / std::fmax (std::fabs (got), std::fabs (want)));
        }
        check (bad == 0, fmt ("%s: 41 values within 1e-12 relative (largest %.2g)", row.name, worst));
    }

    // A NaN lane value reads as the lane default, and formatCtx takes the note's Travel kind.
    model::Note pathNote = model::mkNote (60, 0, 1, "p");
    pathNote.sample.travel = model::TravelKind::path;
    check (fixture::same (model::mapLane (core::LaneId::tone, std::nan (""), ctx), model::mapLane (core::LaneId::tone, 1.0, ctx))
               && model::formatCtx (inst, 87.0, &pathNote).travelKind == model::TravelKind::path
               && model::formatCtx (inst, 87.0, nullptr).travelKind == model::TravelKind::speed
               && fixture::same (model::formatCtx (inst, 87.0, nullptr).bpm, 87.0),
           "a NaN value reads as the lane default; formatCtx follows the note's Travel kind and the tempo");
    check (model::formatLane (core::LaneId::pitch, 0.625, ctx) == "+3 st" && model::formatLane (core::LaneId::travel, 0.75, ctx) == "1x forwards"
               && model::formatLane (core::LaneId::travel, 0.5, ctx) == "stopped" && model::formatLane (core::LaneId::refresh, 0.5, ctx) == "Refresh 120 ms",
           "readouts: '+3 st', '1x forwards', 'stopped', 'Refresh 120 ms'");
}
