// The 50 v1 parameters (PLUGIN-PLAN 2, 4 M0): the layout against docs/parameters.json field by field, the
// defaults against core::defaultInst(), range round trips, readout text that reads back as itself, and the
// readouts against the prototype's own (ui/readouts.json).

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Inst.h"
#include "params/ParamText.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    const char* typeOf (juce::AudioProcessorParameter* p)
    {
        if (dynamic_cast<params::FloatParameter*> (p) != nullptr)
            return "float";
        if (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr)
            return "choice";
        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
            return "bool";
        if (dynamic_cast<juce::AudioParameterInt*> (p) != nullptr)
            return "int";
        return "other";
    }

    bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

    /** (double)(float) x: what a parameter holds for x (PLUGIN-PLAN 1.6 #12). */
    double f (double x) { return (double) (float) x; }

    std::string text (const juce::RangedAudioParameter& p, float normalised)
    {
        return p.getText (normalised, 1024).toStdString();
    }
}

SL_TEST ("params", parametersMatchJson)
{
    section ("Parameters: the layout is docs/parameters.json, field for field (50 in order)");

    std::string bytes;
    core::JsonValue doc;
    const std::string path = options().repoRoot + "/docs/parameters.json";
    core::JsonError error;
    const bool read = fixture::readText (path, bytes) && core::parseJson (bytes, doc, &error);
    check (read && doc.isArray(), fmt ("docs/parameters.json reads (%s)", read ? "an array" : error.message.c_str()));
    if (! read || ! doc.isArray())
        return;

    StrandLockProcessor proc;
    const auto& list = proc.getParameters();
    check (doc.size() == 50 && list.size() == 50 && params::kNumParams == 50,
           fmt ("50 parameters: the JSON has %d, the processor %d", (int) doc.size(), list.size()));

    int good = 0;
    const int n = std::min ((int) doc.size(), list.size());
    for (int i = 0; i < n; ++i)
    {
        const auto& j = doc[(std::size_t) i];
        auto* raw = list[i];
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (raw);
        std::vector<std::string> wrong;
        if (p == nullptr)
        {
            check (false, fmt ("#%d is a RangedAudioParameter", i));
            continue;
        }

        const auto& range = p->getNormalisableRange();
        const std::string type = j["type"].asString();
        const double lo = j["min"].asNumber(), hi = j["max"].asNumber(), def = j["default"].asNumber();
        const double tol = 1e-6 * std::max (1.0, std::abs (hi - lo));

        if ((int) j["index"].asNumber() != i || p->getParameterIndex() != i)
            wrong.push_back ("index");
        if (p->getParameterID().toStdString() != j["id"].asString())
            wrong.push_back ("id " + p->getParameterID().toStdString());
        if (p->getVersionHint() != 1)
            wrong.push_back ("version hint");
        if (p->getName (1024).toStdString() != j["name"].asString())
            wrong.push_back ("name " + p->getName (1024).toStdString());
        if (type != typeOf (raw))
            wrong.push_back (std::string ("type ") + typeOf (raw));
        if (! near (range.start, lo, tol) || ! near (range.end, hi, tol))
            wrong.push_back (fmt ("range %g..%g", (double) range.start, (double) range.end));
        if (! near (p->convertFrom0to1 (p->getDefaultValue()), def, tol))
            wrong.push_back (fmt ("default %g", (double) p->convertFrom0to1 (p->getDefaultValue())));
        if (! raw->isAutomatable() || ! j["automatable"].asBool (false))
            wrong.push_back ("automatable");

        if (type == "float")
        {
            // Continuous (interval 0), and skewed so that the middle of the knob is skewCentre (or linear).
            if (! juce::exactlyEqual (range.interval, 0.0f) || ! juce::exactlyEqual (j["step"].asNumber (-1.0), 0.0))
                wrong.push_back (fmt ("step %g", (double) range.interval));
            const double centre = j["skewCentre"].isNull() ? lo + 0.5 * (hi - lo) : j["skewCentre"].asNumber();
            if (! near (range.convertFrom0to1 (0.5f), centre, 1e-5 * (hi - lo)))
                wrong.push_back (fmt ("centre %g (%g)", (double) range.convertFrom0to1 (0.5f), centre));
        }
        else if (type == "int")
        {
            if (! juce::exactlyEqual (range.interval, 1.0f) || ! juce::exactlyEqual (j["step"].asNumber (-1.0), 1.0))
                wrong.push_back (fmt ("step %g", (double) range.interval));
        }
        else if (type == "choice")
        {
            const auto* c = dynamic_cast<juce::AudioParameterChoice*> (raw);
            const auto& want = j["choices"];
            bool same = c != nullptr && c->choices.size() == (int) want.size();
            for (int k = 0; same && k < c->choices.size(); ++k)
                same = c->choices[k].toStdString() == want[(std::size_t) k].asString()
                    && text (*p, p->convertTo0to1 ((float) k)) == want[(std::size_t) k].asString();
            if (! same)
                wrong.push_back ("choices");
            if (c != nullptr && want[(std::size_t) (int) def].asString() != j["defaultLabel"].asString())
                wrong.push_back ("defaultLabel");
        }

        std::string why;
        for (const auto& w : wrong)
            why += " " + w + ";";
        check (wrong.empty(), fmt ("#%d %s (%s, %s)%s", i, j["id"].asString().c_str(), j["name"].asString().c_str(), type.c_str(),
                                   wrong.empty() ? "" : (" wrong:" + why).c_str()));
        good += wrong.empty() ? 1 : 0;
    }
    note (fmt ("%d of %d parameters match", good, n));

    check (proc.getBypassParameter() == list[49], "the host bypass is parameter 49 (bypass)");
    bool specs = true;
    for (int i = 0; i < params::kNumParams; ++i)
        specs = specs && params::findParam (params::kParams[i].id) == i;
    check (specs && params::findParam ("nope") == -1, "findParam finds every id at its index, and nothing for an unknown id");
}

SL_TEST ("params", defaultsAreDefaultInst)
{
    section ("Parameters: readInst at the defaults is defaultInst(), each parameter-backed field as (double)(float) of it");

    StrandLockProcessor proc;
    const core::Inst got = proc.readInst();
    const core::Inst d = core::defaultInst();

    struct Field { const char* name; double got, want; };
    const std::vector<Field> fields = {
        { "macros[0]", got.macros[0], f (d.macros[0]) }, { "macros[1]", got.macros[1], f (d.macros[1]) },
        { "macros[2]", got.macros[2], f (d.macros[2]) }, { "macros[3]", got.macros[3], f (d.macros[3]) },
        { "macros[4]", got.macros[4], f (d.macros[4]) }, { "macros[5]", got.macros[5], f (d.macros[5]) },
        { "macros[6]", got.macros[6], f (d.macros[6]) }, { "macros[7]", got.macros[7], f (d.macros[7]) },
        { "offsets.amp", got.offsets.amp, f (d.offsets.amp) }, { "offsets.vib", got.offsets.vib, f (d.offsets.vib) },
        { "offsets.tone", got.offsets.tone, f (d.offsets.tone) }, { "offsets.formant", got.offsets.formant, f (d.offsets.formant) },
        { "offsets.hollow", got.offsets.hollow, f (d.offsets.hollow) }, { "outGain", got.outGain, f (d.outGain) },
        { "engine", (double) (int) got.engine, (double) (int) d.engine }, { "lock", got.lock ? 1.0 : 0.0, d.lock ? 1.0 : 0.0 },
        { "motion", (double) (int) got.motion, (double) (int) d.motion }, { "speed", got.speed, f (d.speed) },
        { "heads", got.heads, f (d.heads) }, { "spread", got.spread, f (d.spread) },
        { "grainVoice", got.grainVoice, f (d.grainVoice) }, { "grainCloud", got.grainCloud, f (d.grainCloud) },
        { "grainLock", got.grainLock, f (d.grainLock) }, { "spray", got.spray, f (d.spray) },
        { "drift", got.drift, f (d.drift) }, { "pitch", got.pitch, f (d.pitch) },
        { "tape", got.tape ? 1.0 : 0.0, d.tape ? 1.0 : 0.0 }, { "rev", got.rev ? 1.0 : 0.0, d.rev ? 1.0 : 0.0 },
        { "headTune", (double) (int) got.headTune, (double) (int) d.headTune }, { "jump", (double) (int) got.jump, (double) (int) d.jump },
        { "refresh.mode", (double) (int) got.refresh.mode, (double) (int) d.refresh.mode }, { "refresh.ms", got.refresh.ms, f (d.refresh.ms) },
        { "refresh.rate", (double) (int) got.refresh.rate, (double) (int) d.refresh.rate },
        { "refresh.lockToBeat", got.refresh.lockToBeat ? 1.0 : 0.0, d.refresh.lockToBeat ? 1.0 : 0.0 },
        { "refresh.skip", got.refresh.skip, f (d.refresh.skip) },
        { "region.start", got.region.start, f (d.region.start) }, { "region.end", got.region.end, f (d.region.end) },
        { "region.atEnd", (double) (int) got.region.atEnd, (double) (int) d.region.atEnd }, { "region.xfadeMs", got.region.xfadeMs, f (d.region.xfadeMs) },
        { "glideMode", (double) (int) got.glideMode, (double) (int) d.glideMode }, { "glideTime", got.glideTime, f (d.glideTime) },
        { "vibRate", got.vibRate, f (d.vibRate) }, { "vibSync", (double) got.vibSync, (double) d.vibSync },
        { "bendRange", got.bendRange, f (d.bendRange) }, { "velSens", got.velSens, f (d.velSens) },
        { "polyphony", got.polyphony, f (d.polyphony) }, { "liveLegato", got.liveLegato ? 1.0 : 0.0, d.liveLegato ? 1.0 : 0.0 },
        { "root", got.root, f (d.root) }, { "cents", got.cents, f (d.cents) },
        // The state fields of PLUGIN-PLAN 2.2, exactly.
        { "seed", (double) got.seed, (double) d.seed },
        { "mod1.target", (double) got.mod1.target, (double) d.mod1.target }, { "mod1.cc", (double) got.mod1.cc, (double) d.mod1.cc },
        { "mod2.target", (double) got.mod2.target, (double) d.mod2.target }, { "mod2.cc", (double) got.mod2.cc, (double) d.mod2.cc },
        { "key", (double) got.key, (double) d.key }, { "scale", (double) got.scale, (double) d.scale },
        { "scaleLock", got.scaleLock ? 1.0 : 0.0, d.scaleLock ? 1.0 : 0.0 },
    };

    int same = 0;
    for (const auto& x : fields)
    {
        const bool ok = juce::exactlyEqual (x.got, x.want);
        same += ok ? 1 : 0;
        if (! ok)
            check (false, fmt ("%s reads %.17g, defaultInst says %.17g", x.name, x.got, x.want));
    }
    check (same == (int) fields.size(), fmt ("%d of %d fields equal (velSens reads %.9f = (float) 0.7)", same, (int) fields.size(), got.velSens));
    check (! juce::exactlyEqual (got.velSens, 0.7) && juce::exactlyEqual (got.velSens, f (0.7)),
           "velSens 0.7 reads back as 0.699999988, the float the parameter holds, not 0.7");
    check (! params::readBypass (params::makeParameterRefs (proc.getApvts())), "bypass is off");

    // A parameter's value reaches the instrument, and the exact Region edge plays while the parameter holds (float) of it.
    params::setPlainValue (proc.getParam (params::P::speed), params::index (params::P::speed), 3.25f);
    params::setPlainValue (proc.getParam (params::P::engine), params::index (params::P::engine), 2.0f);
    check (juce::exactlyEqual (proc.readInst().speed, 3.25) && proc.readInst().engine == core::Engine::cloud,
           "Sample Speed 3.25 and Engine Cloud reach the instrument");

    const double exact = 0.123456789012345;
    auto s = proc.getInstState();
    s.regionStartExact = exact;
    proc.setInstState (s);
    params::setPlainValue (proc.getParam (params::P::regionStart), params::index (params::P::regionStart), (float) exact);
    check (juce::exactlyEqual (proc.readInst().region.start, exact),
           fmt ("Region Start plays the exact edge %.15f while the parameter holds (float) of it", proc.readInst().region.start));
    params::setPlainValue (proc.getParam (params::P::regionStart), params::index (params::P::regionStart), 0.25f);
    check (juce::exactlyEqual (proc.readInst().region.start, 0.25),
           fmt ("once automation moves it, the parameter's own value plays (%.15f)", proc.readInst().region.start));
}

SL_TEST ("params", rangesAndText)
{
    section ("Parameters: range round trips and readout text at 101 points each");

    StrandLockProcessor proc;
    int rangeBad = 0, textBad = 0, readBad = 0, points = 0, oneToOne = 0;
    std::vector<std::string> examples;
    auto example = [&examples] (const std::string& s)
    {
        if (examples.size() < 40)
            examples.push_back (s);
    };

    for (int i = 0; i < params::kNumParams; ++i)
    {
        auto& p = proc.getParam (i);
        const auto& s = params::kParams[i];
        const double span = (double) s.max - (double) s.min;
        for (int k = 0; k <= 100; ++k)
        {
            ++points;
            const float v = p.convertFrom0to1 ((float) k / 100.0f);

            const float back = p.convertFrom0to1 (p.convertTo0to1 (v));
            if (! near (back, v, 1e-5 * span))
            {
                ++rangeBad;
                example (fmt ("%s: %.9g -> %.9g", s.id, (double) v, (double) back));
            }

            const std::string t = text (p, p.convertTo0to1 (v));
            const float read = p.getValueForText (juce::String::fromUTF8 (t.c_str()));
            const std::string t2 = text (p, read);
            if (t2 != t)
            {
                ++textBad;
                example (fmt ("%s at %.9g: \"%s\" reads back as \"%s\"", s.id, (double) v, t.c_str(), t2.c_str()));
            }

            const double half = params::readoutHalfStep (i, v);
            if (std::isfinite (half))
            {
                ++oneToOne;
                const double got = p.convertFrom0to1 (read);
                if (! near (got, v, half + 1e-6 * span))
                {
                    ++readBad;
                    example (fmt ("%s at %.9g: \"%s\" reads as %.9g (within %.3g?)", s.id, (double) v, t.c_str(), got, half));
                }
            }
        }
    }
    for (const auto& e : examples)
        note (e);

    check (rangeBad == 0, fmt ("convertFrom0to1 (convertTo0to1 (v)) within 1e-5 x range: %d of %d points off", rangeBad, points));
    check (textBad == 0, fmt ("getText (getValueForText (getText (v))) == getText (v): %d of %d points differ", textBad, points));
    check (oneToOne > 0 && readBad == 0,
           fmt ("one-to-one readouts read back within their printed step: %d of %d points off (macros and offsets excluded)", readBad, oneToOne));

    // A few typed entries, as the readouts print them and as a person types them.
    struct Typed { params::P p; const char* typed; double want; };
    const Typed typed[] = {
        { params::P::speed, "stopped", 0.0 }, { params::P::speed, "2.5x", 2.5 }, { params::P::outGain, "-6 dB", -6.0 },
        { params::P::grainCloud, "1.5 s", 1500.0 }, { params::P::refreshMs, "250 ms", 250.0 }, { params::P::spread, "40%", 0.4 },
        { params::P::rootNote, "C3", 60.0 }, { params::P::rootNote, "A3", 69.0 }, { params::P::rootNote, "Db3", 61.0 },
        { params::P::engine, "Cloud", 2.0 }, { params::P::engine, "tape", 0.0 }, { params::P::jump, "1/16 triplet", 13.0 },
        { params::P::jump, "1/16T", 13.0 }, { params::P::vibSync, "Free", 0.0 }, { params::P::lock, "On", 1.0 },
        { params::P::glideTime, "1/4", 1.0 }, { params::P::polyphony, "8", 8.0 }, { params::P::pitch, "+7 semitones", 7.0 },
        { params::P::macro1, "no change", 0.5 }, { params::P::macro2, "+20% spread", 0.6 },
    };
    int typedGood = 0;
    for (const auto& t : typed)
    {
        auto& p = proc.getParam (t.p);
        const double got = p.convertFrom0to1 (p.getValueForText (t.typed));
        const bool ok = near (got, t.want, 1e-4 * std::max (1.0, std::abs (t.want)));
        typedGood += ok ? 1 : 0;
        if (! ok)
            check (false, fmt ("%s: typing \"%s\" gives %.9g (%.9g)", params::spec (t.p).id, t.typed, got, t.want));
    }
    check (typedGood == (int) std::size (typed), fmt ("%d of %d typed entries mean what they say", typedGood, (int) std::size (typed)));
}

SL_TEST ("params", readoutsMatchThePrototype)
{
    section ("Readouts: SLSampleUI's text, character for character (ui/readouts.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/readouts.json", doc))
        return;

    const auto offsetKind = [] (const std::string& k, params::text::Offset& out)
    {
        static const char* const names[] = { "amp", "vib", "tone", "formant", "hollow" };
        for (int i = 0; i < 5; ++i)
            if (k == names[i])
            {
                out = static_cast<params::text::Offset> (i);
                return true;
            }
        return false;
    };

    int tried = 0, same = 0, shown = 0;
    const auto& cases = doc["cases"];
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const auto& k = cases[c];
        const std::string fn = k["fn"].asString();
        const auto& in = k["input"];
        bool known = true, ok = false;
        std::string got;

        if (fn == "macroText")
            ok = (got = params::text::macroText ((int) in[0].asNumber(), fixture::num (in[1]))) == k["output"].asString();
        else if (fn == "offsetText")
        {
            params::text::Offset kind {};
            known = offsetKind (in[0].asString(), kind);
            ok = known && (got = params::text::offsetText (kind, fixture::num (in[1]))) == k["output"].asString();
        }
        else if (fn == "fmtX")      ok = (got = params::text::fmtX (fixture::num (in))) == k["output"].asString();
        else if (fn == "fmtPct")    ok = (got = params::text::fmtPct (fixture::num (in))) == k["output"].asString();
        else if (fn == "fmtSt")     ok = (got = params::text::fmtSt (fixture::num (in))) == k["output"].asString();
        else if (fn == "fmtCycles") ok = (got = params::text::fmtCycles (fixture::num (in))) == k["output"].asString();
        else if (fn == "fmtMs")     ok = (got = params::text::fmtMs (fixture::num (in))) == k["output"].asString();
        else if (fn == "fmtHeads")  ok = (got = params::text::fmtHeads (fixture::num (in))) == k["output"].asString();
        else if (fn == "signed")    ok = (got = params::text::signedText (fixture::num (in[0]), (int) in[1].asNumber())) == k["output"].asString();
        else if (fn == "parseNum" || fn == "parseMs" || fn == "parsePct")
        {
            const std::string typed = in.isString() ? in.asString() : std::string();
            const auto v = fn == "parseNum" ? params::text::parseNum (typed) : fn == "parseMs" ? params::text::parseMs (typed) : params::text::parsePct (typed);
            const auto& want = k["output"];
            ok = want.isNull() ? ! v.has_value() : (v.has_value() && fixture::same (*v, want.asNumber()));
            got = v ? fmt ("%.17g", *v) : std::string ("null");
        }
        else
            known = false;   // the knob maths (toNorm, qLog, ...) belong to the Sample tab (the UI part)

        if (! known)
            continue;
        ++tried;
        same += ok ? 1 : 0;
        if (! ok && shown++ < 25)
            note (fmt ("%s %s: \"%s\", the prototype \"%s\"", fn.c_str(), core::toJson (in).c_str(), got.c_str(),
                       k["output"].isString() ? k["output"].asString().c_str() : core::toJson (k["output"]).c_str()));
    }
    check (tried >= 700 && same == tried, fmt ("%d of %d readout and parse cases match exactly", same, tried));
}
