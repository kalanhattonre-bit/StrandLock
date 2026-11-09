// model/glides.json (PLUGIN-PLAN 4 M1, 5.3): the automatic slides and the chains (one voice per chain) for
// the 12 clip presets and the voice-leading clip, in all three Auto Slide modes and two glide times. The
// glides are compared in computeGlides' Map order (buildChains breaks ties by it): ids, order, curve and
// linked exact, d and len within 1e-12; the chains exactly.

#include "ModelFixture.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "model/Glides.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    constexpr double kTol = 1e-12;

    std::string chainText (const model::Chains& chains)
    {
        std::string s;
        for (const auto& ch : chains)
        {
            s += "[";
            for (std::size_t i = 0; i < ch.size(); ++i)
                s += (i ? "," : "") + ch[i];
            s += "]";
        }
        return s;
    }
}

SL_TEST ("model", glidesFixture)
{
    section ("model/glides.json: computeGlides in Map order and buildChains, 12 presets and voice-lead, off / legato / always");

    JsonValue doc;
    if (! fixture::load ("model/glides.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("model/glides.json", cases.size());
    std::map<std::string, int> perMode;
    int linkedGlides = 0, autoGlides = 0, joined = 0;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const std::string name = c["clip"].asString();
        const std::string label = name + " " + core::toJson (c["inst"]);

        std::optional<model::Clip> clip;
        if (name.rfind ("preset:", 0) == 0)
            clip = model::loadClipPreset (name.substr (7));
        else if (const auto it = clips.find (name); it != clips.end())
            clip = it->second;
        if (! clip.has_value())
        {
            tally.fail (label + ": no clip " + name);
            continue;
        }
        ++perMode[c["inst"]["glideMode"].asString()];

        const core::Inst inst = core::instFromJson (c["inst"]);
        const model::GlideMap glides = model::computeGlides (*clip, inst);
        const JsonValue& want = c["glides"];
        bool ok = true, exact = true;
        std::string why;
        if (glides.size() != want.size())
        {
            ok = false;
            why = "got " + std::to_string (glides.size()) + " glides, want " + std::to_string (want.size());
        }
        for (std::size_t i = 0; ok && i < glides.size(); ++i)
        {
            const auto& [sid, g] = glides[i];
            const JsonValue& w = want[i];
            const std::string at = "glide " + std::to_string (i) + " (" + sid + ")";
            if (sid != w[0].asString() || g.toId != w[4].asString() || g.linked != w[5].asBool() || ! fixture::same (g.curve, w[3].asNumber()))
            {
                ok = false;
                why = at + ": source, target, curve or linked differ (want " + core::toJson (w) + ", got " + g.toId + ")";
                break;
            }
            if (! fixture::same (g.d, w[1].asNumber()) || ! fixture::same (g.len, w[2].asNumber()))
                exact = false;
            if (! fixture::withinAbs (g.d, w[1].asNumber(), kTol) || ! fixture::withinAbs (g.len, w[2].asNumber(), kTol))
            {
                ok = false;
                why = at + ": d " + core::jsNumberToString (g.d) + " len " + core::jsNumberToString (g.len) + ", want " + core::toJson (w);
            }
            (g.linked ? linkedGlides : autoGlides) += 1;
        }

        const model::Chains chains = model::buildChains (*clip, glides);
        model::Chains wantChains;
        for (std::size_t i = 0; i < c["chains"].size(); ++i)
            wantChains.push_back (modelfx::stringsFrom (c["chains"][i]));
        if (ok && chains != wantChains)
        {
            ok = false;
            why = "chains " + chainText (chains) + ", want " + chainText (wantChains);
        }
        for (const auto& ch : chains)
            joined += ch.size() > 1 ? static_cast<int> (ch.size()) - 1 : 0;

        if (ok)
            tally.pass (exact);
        else
            tally.fail (label + ": " + why);
    }

    note (fmt ("cases: off %d, legato %d, always %d; %d explicit and %d automatic glides; %d joins in the chains",
               perMode["off"], perMode["legato"], perMode["always"], linkedGlides, autoGlides, joined));
    check (perMode["off"] > 0 && perMode["legato"] > 0 && perMode["always"] > 0 && autoGlides > 0 && linkedGlides > 0,
           "all three Auto Slide modes, explicit links and automatic glides are covered");
    tally.finish();
}

SL_TEST ("model", glideMapOrder)
{
    section ("GlideMap keeps JavaScript Map order: a second set () of an id keeps its place");

    model::GlideMap m;
    m.set ("b", model::Glide { 1, 0.5, 0, "x", false });
    m.set ("a", model::Glide { 2, 0.5, 0, "y", false });
    m.set ("b", model::Glide { 3, 0.5, 0, "z", true });
    check (m.size() == 2 && m[0].first == "b" && m[0].second.d == 3 && m[0].second.toId == "z" && m[1].first == "a"
               && m.find ("a") != nullptr && m.find ("c") == nullptr,
           "set () replaces the value in place and find () reads it");
}
