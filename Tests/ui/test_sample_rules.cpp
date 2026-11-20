// The Sample tab's rules (PLUGIN-PLAN 4 M6 "fade rules" and "knobs", 7.8 T1 and decisions 1 and 2) against
// Tests/fixtures/ui/sample-rules.json and ui/readouts.json:
//
// - fade rules: ui/SampleTabRules' fades, contexts, grainKind, headsMulti, toneAllOpen, randomInUse, kindOf,
//   suggestEngine and pickPart equal SLSampleUI's exactly on every case (the count gated), and the plan's whole product
//   (every sound preset x recipe x clip preset, 864 pairs) equals the prototype's digests;
// - knobs, without a window: a drag lands where the prototype's q functions put it (the knobDrag cases, five moves and
//   the release, value and undo steps exactly), a double-click goes to the default, a typed value is kept as typed.
//
// The knobs in the real editor (each control changes exactly its own parameter, one undo step per finished gesture,
// typed Lock Grain 4.3 plays 4 taps) and the controls' faded state, tooltip and hint line are in test_sample_tab.cpp.

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "model/ClipEdit.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "params/ParamIds.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/SampleTabRules.h"
#include "model/ModelFixture.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    namespace rules = sl::ui::rules;

    const std::vector<std::string> kRuleFns { "fades", "contexts", "grainKind", "headsMulti", "toneAllOpen", "randomInUse", "kindOf", "suggestEngine",
                                              "pickPart" };

    bool isOneOf (const std::string& s, const std::vector<std::string>& list)
    {
        for (const auto& x : list)
            if (x == s)
                return true;
        return false;
    }

    std::optional<model::Clip> caseClip (const std::string& name, const core::JsonValue& in, const std::map<std::string, model::Clip>& clips)
    {
        std::optional<model::Clip> clip;
        if (name.rfind ("preset:", 0) == 0)
            clip = model::loadClipPreset (name.substr (7));
        else if (const auto it = clips.find (name); it != clips.end())
            clip = it->second;
        if (clip.has_value() && in.contains ("recipe"))
        {
            // the clip half of a recipe does not depend on the instrument (the fixture applies it with defaultInst)
            const auto r = model::applyRecipe (*clip, core::defaultInst(), (int) in["recipe"].asNumber(), model::Ids::all());
            clip = r.has_value() ? std::optional<model::Clip> (r->clip) : std::nullopt;
        }
        return clip;
    }

    const char* travelCode (model::TravelKind t) { return t == model::TravelKind::path ? "path" : "speed"; }

    bool fadesMatch (const rules::Fades& got, const core::JsonValue& want, std::string& why)
    {
        if (want.size() != (std::size_t) rules::kNumRules)
        {
            why = fmt ("%zu rules, the prototype %zu", (std::size_t) rules::kNumRules, want.size());
            return false;
        }
        for (int i = 0; i < rules::kNumRules; ++i)
        {
            const auto r = static_cast<rules::Rule> (i);
            const auto& g = rules::fadeOf (got, r);
            const auto& w = want.valueAt ((std::size_t) i);
            if (want.keyAt ((std::size_t) i) != rules::ruleId (r))
            {
                why = fmt ("rule %d is %s, the prototype's %s", i, rules::ruleId (r), want.keyAt ((std::size_t) i).c_str());
                return false;
            }
            if (g.faded != w["faded"].asBool() || g.notes != (int) w["notes"].asNumber() || g.reason != w["reason"].asString())
            {
                why = fmt ("%s: faded %d notes %d \"%s\", the prototype %s", rules::ruleId (r), (int) g.faded, g.notes, g.reason.c_str(),
                           core::toJson (w).c_str());
                return false;
            }
        }
        return true;
    }

    bool contextsMatch (const std::vector<rules::Context>& got, const core::JsonValue& want, std::string& why)
    {
        if (got.size() != want.size())
        {
            why = fmt ("%zu contexts, the prototype %zu", got.size(), want.size());
            return false;
        }
        for (std::size_t k = 0; k < got.size(); ++k)
        {
            const auto& g = got[k];
            const auto& w = want[k];
            const auto& f = w["from"];
            const bool ok = std::string (g.inst ? "inst" : "notes") == w["who"].asString() && g.count == (int) w["count"].asNumber()
                            && std::string (rules::code (g.kind)) == w["kind"].asString() && std::string (core::code (g.engine)) == w["engine"].asString()
                            && std::string (core::code (g.motion)) == w["motion"].asString() && std::string (travelCode (g.travel)) == w["travel"].asString()
                            && g.from.lock == f["lock"].asBool() && g.from.motion == f["motion"].asBool() && g.from.tape == f["tape"].asBool()
                            && g.from.rev == f["rev"].asBool() && g.from.jump == f["jump"].asBool();
            if (! ok)
            {
                why = fmt ("context %zu differs: the prototype %s", k, core::toJson (w).c_str());
                return false;
            }
        }
        return true;
    }

    std::optional<rules::Part> partFrom (const core::JsonValue& v)
    {
        if (! v.isObject())
            return std::nullopt;
        rules::Part p;
        p.kind = v["kind"].isString() ? v["kind"].asString() : std::string();
        p.label = v["label"].isString() ? v["label"].asString() : std::string();
        p.start = v.contains ("start") ? fixture::num (v["start"]) : std::nan ("");
        p.end = v.contains ("end") ? fixture::num (v["end"]) : std::nan ("");
        p.tempoFound = v["tempoFound"].asBool (false);
        return p;
    }

    std::vector<std::optional<rules::Part>> partsFrom (const core::JsonValue& v)
    {
        std::vector<std::optional<rules::Part>> out;
        for (std::size_t i = 0; i < v.size(); ++i)
            out.push_back (partFrom (v[i]));
        return out;
    }

    /** The text a product digest is taken over (tools/fixtures/ui.js productText). */
    std::string productText (const rules::Fades& F, const std::vector<rules::Context>& cx, const std::optional<rules::Kind>& gk)
    {
        std::string t;
        for (int i = 0; i < rules::kNumRules; ++i)
        {
            const auto& f = F[(std::size_t) i];
            t += std::string (rules::ruleId (static_cast<rules::Rule> (i))) + " " + (f.faded ? "1" : "0") + " " + std::to_string (f.notes) + " " + f.reason + "\n";
        }
        for (const auto& c : cx)
            t += std::string (c.inst ? "inst" : "notes") + " " + std::to_string (c.count) + " " + rules::code (c.kind) + " " + core::code (c.engine) + " "
                 + core::code (c.motion) + " " + travelCode (c.travel) + " " + (c.from.lock ? "1" : "0") + (c.from.motion ? "1" : "0")
                 + (c.from.tape ? "1" : "0") + (c.from.rev ? "1" : "0") + (c.from.jump ? "1" : "0") + "\n";
        return t + "grainKind " + (gk.has_value() ? rules::code (*gk) : "null") + "\n";
    }

    std::string fnvHex (const std::string& text)
    {
        std::uint32_t h = 0x811c9dc5u;
        for (const char ch : text)
        {
            h ^= (std::uint32_t) (unsigned char) ch;
            h *= 0x01000193u;
        }
        char buf[16];
        std::snprintf (buf, sizeof buf, "%08x", (unsigned) h);
        return buf;
    }

    //==============================================================================================
    // The knobs' cases (ui/readouts.json), shared with test_sample_tab.cpp's twin through the same rules.

    std::optional<rules::KnobId> knobNamed (const std::string& undoName)
    {
        for (const auto id : rules::allKnobs())
            if (undoName == rules::knobSpec (id).undo)
                return id;
        return std::nullopt;
    }

    std::optional<rules::Kind> grainOfContext (const std::string& ctx)
    {
        if (ctx == "cloud")
            return rules::Kind::cloud;
        if (ctx == "lock")
            return rules::Kind::lock;
        if (ctx == "tape")
            return std::nullopt;
        return rules::Kind::voice;
    }

    /** The inst value a knob's parameter holds. */
    double valueOf (const core::Inst& I, params::P p)
    {
        using P = params::P;
        switch (p)
        {
            case P::speed:         return I.speed;
            case P::heads:         return I.heads;
            case P::spread:        return I.spread;
            case P::grainVoice:    return I.grainVoice;
            case P::grainCloud:    return I.grainCloud;
            case P::grainLock:     return I.grainLock;
            case P::spray:         return I.spray;
            case P::drift:         return I.drift;
            case P::pitch:         return I.pitch;
            case P::refreshMs:     return I.refresh.ms;
            case P::refreshSkip:   return I.refresh.skip;
            case P::ampOffset:     return I.offsets.amp;
            case P::vibOffset:     return I.offsets.vib;
            case P::toneOffset:    return I.offsets.tone;
            case P::formantOffset: return I.offsets.formant;
            case P::hollowOffset:  return I.offsets.hollow;
            default:
            {
                const int m = params::index (p) - params::index (P::macro1);
                return m >= 0 && m < 8 ? I.macros[(std::size_t) m] : std::nan ("");
            }
        }
    }

    /** What a gesture did, as the cases write it: the value, its readout, the undo steps, the first step's label, a toast. */
    struct Outcome
    {
        double value = 0.0;
        std::string text;
        int steps = 0;
        std::string label;
        std::string toast;
        bool box = false;
    };

    std::string textOf (const rules::KnobSpec& s, double v) { return s.off ? std::string ("\xe2\x80\x94") : rules::knobText (s, v); }

    /** The test file's drag (dial, dy, shift) on the pure rules: pointer down at y 300, five moves, the release. */
    Outcome pureDrag (const rules::KnobSpec& s, double from, double dy, bool shift)
    {
        Outcome o;
        o.value = from;
        if (s.off)
        {
            o.toast = s.offWhy;   // a press on a knob with nothing to do says why, and changes nothing
            o.text = textOf (s, from);
            return o;
        }
        auto d = rules::beginDrag (s, 300.0, from);
        for (int k = 1; k <= 5; ++k)
            if (const auto v = rules::dragTo (s, d, 300.0 - dy * k / 5.0, shift))
                o.value = *v;
        o.steps = rules::dragCommits (d) ? 1 : 0;
        o.label = o.steps == 1 ? s.undo : "";
        o.text = textOf (s, o.value);
        return o;
    }

    Outcome pureReset (const rules::KnobSpec& s, double from)
    {
        Outcome o;
        o.value = from;
        if (! s.off && ! fixture::same (s.range.def, from))
        {
            o.value = s.range.def;
            o.steps = 1;
            o.label = std::string ("Reset ") + s.undo;
        }
        o.text = textOf (s, o.value);
        return o;
    }

    Outcome pureType (const rules::KnobSpec& s, double current, const std::string& text)
    {
        Outcome o;
        o.value = current;
        o.box = ! s.off && s.parse != rules::KnobParse::none;
        if (o.box)
        {
            if (const auto v = rules::knobParse (s, text); v.has_value() && std::isfinite (*v))
            {
                const auto& ps = params::spec (s.param);
                double t = rules::typedValue (s, *v);
                t = t < (double) ps.min ? (double) ps.min : t > (double) ps.max ? (double) ps.max : t;   // normalizeInst's clamps
                if (! fixture::same (t, current))
                {
                    o.value = t;
                    o.steps = 1;
                    o.label = s.undo;
                }
            }
            else
            {
                o.toast = rules::typingHelp (s);
            }
        }
        o.text = textOf (s, o.value);
        return o;
    }

    /** The case's output against an outcome: [value, readout, steps, label|null, toast?, box?]. */
    bool outcomeMatches (const Outcome& got, const core::JsonValue& out, bool typed, std::string& why)
    {
        const std::size_t n = out.size();
        const bool hasToast = typed ? n == 6 : n == 5;
        const std::string wantToast = hasToast ? out[4].asString() : std::string();
        const std::string wantLabel = out[3].isNull() ? std::string() : out[3].asString();
        const bool ok = fixture::same (got.value, fixture::num (out[0])) && got.text == out[1].asString() && got.steps == (int) out[2].asNumber()
                        && got.label == wantLabel && got.toast == wantToast && (! typed || got.box == out[n - 1].asBool());
        if (! ok)
            why = fmt ("got [%.17g, \"%s\", %d, \"%s\", \"%s\"%s], the prototype %s", got.value, got.text.c_str(), got.steps, got.label.c_str(),
                       got.toast.c_str(), typed ? (got.box ? ", box" : ", no box") : "", core::toJson (out).c_str());
        return ok;
    }
}

//==================================================================================================
SL_TEST ("ui", sampleRulesMatchFixture)
{
    section ("Fade rules: SampleTabRules equals SLSampleUI on every case (ui/sample-rules.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/sample-rules.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const auto& cases = doc["cases"];
    const auto& insts = doc["insts"];

    std::size_t mine = 0, other = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string fn = cases[i]["fn"].asString();
        if (isOneOf (fn, kRuleFns))
            ++mine;
        else if (fn != "product" && fn != "productClips")
            ++other;
    }
    check (other == 0, fmt ("every case names a function this test knows (%zu unknown)", other));

    modelfx::FileTally tally ("ui/sample-rules.json (fades, contexts, grainKind, headsMulti, toneAllOpen, randomInUse, kindOf, suggestEngine, pickPart)", mine);
    std::map<std::string, int> perFn;
    int fadedSeen = 0, notesSeen = 0, mixedSeen = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        if (! isOneOf (fn, kRuleFns))
            continue;
        ++perFn[fn];
        const auto& in = c["input"];
        const auto& want = c["output"];
        bool ok = false;
        std::string why;

        if (fn == "kindOf")
        {
            const auto engine = core::parseEngine (in[0].asString());
            const auto k = rules::kindOf (engine.value_or (core::Engine::voice), in[1].asBool());
            ok = engine.has_value() && std::string (rules::code (k)) == want.asString();
            why = rules::code (k);
        }
        else if (fn == "suggestEngine")
        {
            std::optional<rules::SoundInfo> info;
            if (in.isObject())
            {
                info.emplace();
                info->pitch = in["pitch"].asBool (false);
                info->seconds = in.contains ("seconds") ? fixture::num (in["seconds"]) : std::nan ("");
                if (in.contains ("parts"))
                    info->parts = partsFrom (in["parts"]);
            }
            const auto s = rules::suggestEngine (info ? &*info : nullptr);
            ok = std::string (core::code (s.engine)) == want["engine"].asString() && s.text == want["text"].asString() && s.why == want["why"].asString();
            why = s.text + ": " + s.why;
        }
        else if (fn == "pickPart")
        {
            std::optional<std::vector<std::optional<rules::Part>>> parts;
            if (in.isArray())
                parts = partsFrom (in);
            const auto p = rules::pickPart (parts ? &*parts : nullptr);
            ok = want.isNull() ? ! p.has_value()
                               : p.has_value() && p->kind == want["kind"].asString() && p->label == want["label"].asString()
                                     && fixture::same (p->start, fixture::num (want["start"])) && fixture::same (p->end, fixture::num (want["end"]));
            why = p ? p->kind + " " + core::jsNumberToString (p->start) : std::string ("null");
        }
        else
        {
            const std::string instName = in["inst"].asString(), clipName = in["clip"].asString();
            if (! insts.contains (instName))
            {
                tally.fail (fn + ": the file has no inst " + instName);
                continue;
            }
            const auto inst = core::instFromJson (insts[instName]);
            const auto clip = caseClip (clipName, in, clips);
            if (! clip)
            {
                tally.fail (fn + ": no clip " + clipName);
                continue;
            }
            if (fn == "fades")
            {
                const auto F = rules::fades (inst, &*clip);
                ok = fadesMatch (F, want, why);
                for (const auto& f : F)
                {
                    fadedSeen += f.faded ? 1 : 0;
                    notesSeen += f.notes > 0 ? 1 : 0;
                }
            }
            else if (fn == "contexts")
            {
                const auto cx = rules::contexts (inst, &*clip);
                ok = contextsMatch (cx, want, why);
                mixedSeen += cx.size() > 2 ? 1 : 0;
            }
            else if (fn == "grainKind")
            {
                const auto g = rules::grainKind (inst, &*clip);
                ok = want.isNull() ? ! g.has_value() : g.has_value() && want.asString() == rules::code (*g);
                why = g ? rules::code (*g) : "null";
            }
            else
            {
                const bool b = fn == "headsMulti" ? rules::headsMulti (inst, &*clip) : fn == "toneAllOpen" ? rules::toneAllOpen (inst, &*clip)
                                                                                                           : rules::randomInUse (inst, &*clip);
                ok = b == want.asBool();
                why = b ? "true" : "false";
            }
        }

        if (ok)
            tally.pass (true);
        else
            tally.fail (fmt ("%s %s: %s (the prototype %s)", fn.c_str(), core::toJson (in).c_str(), why.c_str(),
                             core::toJson (want).substr (0, 300).c_str()));
    }
    tally.finish();
    std::string counts;
    for (const auto& [fn, n] : perFn)
        counts += fmt ("%s%s %d", counts.empty() ? "" : ", ", fn.c_str(), n);
    note (counts);
    // a file whose cases never fade, never count notes or never mix settings would not test those branches
    check (perFn["fades"] >= 40 && fadedSeen > 100 && notesSeen > 0 && mixedSeen > 0,
           fmt ("the cases fade %d controls, count notes on %d and mix settings in %d contexts lists", fadedSeen, notesSeen, mixedSeen));
}

SL_TEST ("ui", sampleRulesWholeProduct)
{
    section ("Fade rules: every sound preset x recipe x clip preset (864 pairs) gives the prototype's fades, contexts and grain kind");

    core::JsonValue doc;
    if (! fixture::load ("ui/sample-rules.json", doc))
        return;
    const auto& cases = doc["cases"];
    const auto& insts = doc["insts"];

    std::vector<std::string> clipIds;
    std::size_t productCases = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        if (c["fn"].asString() == "productClips")
            for (std::size_t k = 0; k < c["output"].size(); ++k)
                clipIds.push_back (c["output"][k].asString());
        productCases += c["fn"].asString() == "product" ? 1 : 0;
    }
    check (clipIds.size() == model::clipPresets().size() && productCases == model::soundPresets().size() * (model::recipes().size() + 1),
           fmt ("the file lists the %zu clip presets and %zu sound preset x recipe cases (%zu x %zu)", clipIds.size(), productCases,
                model::soundPresets().size(), model::recipes().size() + 1));

    std::vector<model::Clip> presetClips;
    for (const auto& id : clipIds)
        if (auto c = model::loadClipPreset (id))
            presetClips.push_back (std::move (*c));
    check (presetClips.size() == clipIds.size(), "every listed clip preset loads");
    if (presetClips.size() != clipIds.size())
        return;

    int pairs = 0, same = 0, shown = 0, faded = 0, grainless = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        if (c["fn"].asString() != "product")
            continue;
        const std::string sound = c["input"][0].asString();
        const int n = (int) c["input"][1].asNumber();
        const std::string& wantAll = c["output"].asString();
        const auto inst0 = core::instFromJson (insts["sound:" + sound]);
        for (std::size_t k = 0; k < presetClips.size(); ++k)
        {
            core::Inst inst = inst0;
            model::Clip clip = presetClips[k];
            if (n > 0)
            {
                const auto r = model::applyRecipe (clip, inst, n, model::Ids::all());
                if (! r)
                    continue;
                inst = r->inst;
                clip = r->clip;
            }
            const auto F = rules::fades (inst, &clip);
            const auto cx = rules::contexts (inst, &clip);
            const auto gk = rules::grainKind (inst, &clip);
            const std::string got = fnvHex (productText (F, cx, gk));
            const std::string want = wantAll.size() >= k * 9 + 8 ? wantAll.substr (k * 9, 8) : std::string();
            ++pairs;
            if (got == want)
                ++same;
            else if (shown++ < 6)
                note (fmt ("sound %s, recipe %d, clip %s: digest %s, the prototype %s; the text:\n%s", sound.c_str(), n, clipIds[k].c_str(), got.c_str(),
                           want.c_str(), productText (F, cx, gk).c_str()));
            for (const auto& f : F)
                faded += f.faded ? 1 : 0;
            grainless += gk.has_value() ? 0 : 1;
        }
    }
    check (pairs == 864 && same == pairs, fmt ("%d of %d pairs give the prototype's digest", same, pairs));
    check (faded > 1000 && grainless > 0, fmt ("the product fades %d controls in all, and %d pairs play no grains", faded, grainless));
}

//==================================================================================================
SL_TEST ("ui", knobGesturesMatchFixture)
{
    section ("Knobs (rules): drags snap as the prototype's q functions do, double-clicks reset, typed values are kept (ui/readouts.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/readouts.json", doc))
        return;
    const auto& cases = doc["cases"];
    const auto& insts = doc["insts"];

    std::size_t mine = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string fn = cases[i]["fn"].asString();
        mine += fn == "knobDrag" || fn == "knobReset" || fn == "knobType" ? 1 : 0;
    }
    modelfx::FileTally tally ("ui/readouts.json (knobDrag, knobReset, knobType)", mine);

    int moved = 0, snapped = 0, fine = 0, kept = 0, refused = 0;
    std::set<std::string> knobsSeen;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        if (fn != "knobDrag" && fn != "knobReset" && fn != "knobType")
            continue;
        const auto& in = c["input"];
        const auto& out = c["output"];
        const auto id = knobNamed (in[0].asString());
        const std::string ctx = in[1].asString();
        if (! id || ! insts.contains (ctx))
        {
            tally.fail (fn + ": unknown knob or setting " + core::toJson (in));
            continue;
        }
        knobsSeen.insert (in[0].asString());
        const auto spec = rules::knobSpec (*id, grainOfContext (ctx));
        Outcome got;
        if (fn == "knobDrag")
        {
            got = pureDrag (spec, fixture::num (in[2]), fixture::num (in[3]), in[4].asBool());
            moved += got.steps;
            fine += in[4].asBool() && got.steps == 1 ? 1 : 0;
            snapped += got.steps == 1 && ! in[4].asBool() && fixture::same (got.value, rules::knobQuant (spec, got.value, false)) ? 1 : 0;
        }
        else if (fn == "knobReset")
        {
            got = pureReset (spec, fixture::num (in[2]));
        }
        else
        {
            const auto inst = core::instFromJson (insts[ctx]);
            got = pureType (spec, valueOf (inst, spec.param), in[2].asString());
            kept += got.steps;
            refused += got.toast.empty() ? 0 : 1;
        }
        std::string why;
        if (outcomeMatches (got, out, fn == "knobType", why))
            tally.pass (true);
        else
            tally.fail (fn + " " + core::toJson (in) + ": " + why);
    }
    tally.finish();
    check (knobsSeen.size() == (std::size_t) rules::kNumKnobs && moved > 150 && snapped > 100 && fine > 30 && kept > 30 && refused > 5,
           fmt ("all %zu knobs covered: %d drags moved a knob (%d of them coarse and on the q grid, %d fine), %d typed values kept, %d refused",
                knobsSeen.size(), moved, snapped, fine, kept, refused));

    // decision 2 by itself: Lock Grain typed 4.3 stays 4.3 (a drag would land on whole waves)
    const auto lock = rules::knobSpec (rules::KnobId::grain, rules::Kind::lock);
    const auto typed = rules::knobParse (lock, "4.3");
    auto drag = rules::beginDrag (lock, 300.0, 4.0);
    const auto dragged = rules::dragTo (lock, drag, 300.0 - 10.0, false);
    check (typed.has_value() && fixture::same (rules::typedValue (lock, *typed), 4.3) && dragged.has_value() && fixture::same (*dragged, 5.0)
               && lock.param == params::P::grainLock,
           fmt ("Lock Grain: typed \"4.3\" is %.17g; a 10 px drag from 4 lands on %.17g (whole waves)", typed ? rules::typedValue (lock, *typed) : -1.0,
                dragged ? *dragged : -1.0));
}
