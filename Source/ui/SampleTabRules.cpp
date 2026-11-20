#include "SampleTabRules.h"

#include "core/JsCompat.h"
#include "core/JsMath.h"
#include "params/ParamText.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

namespace sl::ui::rules
{
    namespace
    {
        using core::Engine;
        using core::LaneId;
        using core::Motion;

        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        /** SLSampleUI's num (x, d): a finite number, else d. */
        double num (double x, double d) noexcept { return std::isfinite (x) ? x : d; }

        /** Number (text) for the digits jsToPrecision prints (always a plain number for a finite value). */
        double numberOf (const std::string& s) noexcept
        {
            double v = 0.0;
            const char* first = s.data();
            if (! s.empty() && s.front() == '+')
                ++first;
            const auto r = std::from_chars (first, s.data() + s.size(), v);
            return r.ec == std::errc() ? v : std::numeric_limits<double>::quiet_NaN();
        }

        /** sig (v, d): v to d significant digits (Number (v.toPrecision (d))); 0 stays 0. */
        double sig (double v, int d)
        {
            if (v == 0.0)
                return 0.0;   // -0 too: the prototype returns 0
            if (! std::isfinite (v))
                return v;
            return numberOf (core::jsToPrecision (v, d));
        }

        /** Math.log, bit for bit as V8 computes it: fdlibm's e_log.c (V8 src/base/ieee754.cc log). The knobs' toNorm and
            fromNorm compare exactly with the prototype's (ui/readouts.json), and the C libraries' log rounds a few inputs the
            other way. A JavaScript copy of this routine gave Math.log's bits on 2.7 million inputs (node 24). (core/JsMath
            holds jsExp; this one stays here because core is M1's, PLUGIN-PLAN 7.8.) */
        double jsLog (double x) noexcept
        {
            constexpr double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10, two54 = 1.80143985094819840000e+16;
            constexpr double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01, Lg3 = 2.857142874366239149e-01,
                             Lg4 = 2.222219843214978396e-01, Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
                             Lg7 = 1.479819860511658591e-01;
            const auto bits = [] (double v) { return std::bit_cast<std::uint64_t> (v); };
            const auto highOf = [&bits] (double v) { return static_cast<std::int32_t> (static_cast<std::uint32_t> (bits (v) >> 32)); };
            const auto withHigh = [&bits] (double v, std::int32_t h)
            {
                return std::bit_cast<double> ((static_cast<std::uint64_t> (static_cast<std::uint32_t> (h)) << 32) | (bits (v) & 0xffffffffu));
            };

            std::int32_t hx = highOf (x);
            const auto lx = static_cast<std::uint32_t> (bits (x) & 0xffffffffu);
            std::int32_t k = 0;
            if (hx < 0x00100000)
            {
                if (((static_cast<std::uint32_t> (hx) & 0x7fffffffu) | lx) == 0)
                    return -std::numeric_limits<double>::infinity();   // log (+-0)
                if (hx < 0)
                    return std::numeric_limits<double>::quiet_NaN();   // log (negative)
                k -= 54;
                x *= two54;   // a subnormal: scale it up
                hx = highOf (x);
            }
            if (hx >= 0x7ff00000)
                return x + x;
            k += (hx >> 20) - 1023;
            hx &= 0x000fffff;
            std::int32_t i = (hx + 0x95f64) & 0x100000;
            x = withHigh (x, hx | (i ^ 0x3ff00000));   // x or x / 2, normalised
            k += i >> 20;
            const double f = x - 1.0;
            if ((0x000fffff & (2 + hx)) < 3)
            {
                // |f| < 2^-20
                if (f == 0.0)
                {
                    if (k == 0)
                        return 0.0;
                    const double dk = static_cast<double> (k);
                    return dk * ln2_hi + dk * ln2_lo;
                }
                const double R = f * f * (0.5 - 0.33333333333333333 * f);
                if (k == 0)
                    return f - R;
                const double dk = static_cast<double> (k);
                return dk * ln2_hi - ((R - dk * ln2_lo) - f);
            }
            const double s = f / (2.0 + f);
            const double dk = static_cast<double> (k);
            const double z = s * s;
            i = hx - 0x6147a;
            const double w = z * z;
            const std::int32_t j = 0x6b851 - hx;
            const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
            const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
            i |= j;
            const double R = t2 + t1;
            if (i > 0)
            {
                const double hfsq = 0.5 * f * f;
                if (k == 0)
                    return f - (hfsq - s * (hfsq + R));
                return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
            }
            if (k == 0)
                return f - s * (f - R);
            return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
        }

        /** Tape plays Freeze as Forward. */
        Motion effMotion (Kind kind, Motion motion) noexcept
        {
            return kind == Kind::tape && motion == Motion::freeze ? Motion::forward : motion;
        }

        /** moves (c): the heads travel (Tape always; otherwise a Speed note that is not frozen). */
        bool moves (const Context& c) noexcept
        {
            return c.kind == Kind::tape || (c.travel == model::TravelKind::speed && c.motion != Motion::freeze);
        }

        bool sameContext (const Context& a, const Context& b) noexcept
        {
            return a.kind == b.kind && a.engine == b.engine && a.motion == b.motion && a.travel == b.travel
                   && a.from.lock == b.from.lock && a.from.motion == b.from.motion && a.from.tape == b.from.tape
                   && a.from.rev == b.from.rev && a.from.jump == b.from.jump;
        }

        const model::NodeList& laneNodes (const model::Note& n, LaneId lane) noexcept { return n.lane (lane); }

        bool modOn (const core::ModWheel& m, LaneId lane) noexcept
        {
            return m.target == static_cast<std::int8_t> (core::laneIndex (lane));
        }

        // The rules' sentences (sl-ui-sample.js RULES).
        constexpr const char* kSpeedWhy = "Freeze holds the heads still, so Speed does nothing.";
        constexpr const char* kSpreadWhy = "Spread spaces out several heads. With 1 head it does nothing: raise Heads.";

        std::string sprayWhy (const Context& c)
        {
            return c.kind == Kind::tape ? "Spray scatters grains, and Tape has none. Pick Cloud to use Spray."
                                        : "Voice keeps its grains in step with the pitch, so Spray does nothing. Pick Cloud to use Spray.";
        }

        enum class RuleKind : std::uint8_t { uses, inst, both, multi };

        RuleKind kindOfRule (Rule r) noexcept
        {
            switch (r)
            {
                case Rule::spread: case Rule::headTune: case Rule::mSpread:            return RuleKind::multi;
                case Rule::refreshMs: case Rule::refreshRate: case Rule::skip:
                case Rule::mRefresh:                                                    return RuleKind::inst;
                case Rule::mTone: case Rule::reroll:                                    return RuleKind::both;
                default:                                                                return RuleKind::uses;
            }
        }

        /** RULES[id].uses (c). */
        bool uses (Rule r, const Context& c) noexcept
        {
            switch (r)
            {
                case Rule::lock:    return c.engine == Engine::cloud && c.from.lock;
                case Rule::freeze:  return c.from.motion && c.kind != Kind::tape;
                case Rule::speed:
                case Rule::drift:
                case Rule::mSpeed:  return moves (c);
                case Rule::grain:
                case Rule::mGrain:  return c.kind != Kind::tape;
                case Rule::spray:
                case Rule::mSpray:  return c.kind == Kind::cloud || c.kind == Kind::lock;
                case Rule::tape:
                case Rule::rev:     return (r == Rule::tape ? c.from.tape : c.from.rev) && (c.kind == Kind::voice || c.kind == Kind::cloud);
                case Rule::atEnd:   return c.motion != Motion::bounce;
                case Rule::jump:    return c.from.jump && c.motion == Motion::scatter;
                default:            return false;
            }
        }

        /** RULES[id].inst (I). */
        bool instUses (Rule r, const core::Inst& I) noexcept
        {
            switch (r)
            {
                case Rule::refreshMs:   return I.refresh.mode == core::RefreshMode::free;
                case Rule::refreshRate: return I.refresh.mode == core::RefreshMode::sync;
                case Rule::skip:
                case Rule::mRefresh:    return I.refresh.mode != core::RefreshMode::smooth;
                default:                return false;
            }
        }
    }

    //==============================================================================================
    const char* code (Kind k) noexcept
    {
        switch (k)
        {
            case Kind::tape:  return "tape";
            case Kind::voice: return "voice";
            case Kind::cloud: return "cloud";
            case Kind::lock:  return "lock";
        }
        return "voice";
    }

    Kind kindOf (Engine engine, bool lock) noexcept
    {
        return engine == Engine::tape ? Kind::tape : engine == Engine::voice ? Kind::voice : lock ? Kind::lock : Kind::cloud;
    }

    std::vector<Context> contexts (const core::Inst& I, const model::Clip* clip)
    {
        std::vector<Context> out;
        Context own;
        own.inst = true;
        own.kind = kindOf (I.engine, I.engine == Engine::cloud && I.lock);
        own.engine = I.engine;
        own.motion = effMotion (own.kind, I.motion);
        own.travel = model::TravelKind::speed;
        own.from = { I.engine == Engine::cloud, true, true, true, true };
        out.push_back (own);

        if (clip == nullptr)
            return out;
        for (const auto& np : clip->notes)
        {
            if (np == nullptr)
                continue;
            const model::NoteSample& s = np->sample;
            Context c;
            c.engine = s.engine.value_or (I.engine);
            const bool lock = c.engine == Engine::cloud && (s.lock.has_value() ? *s.lock : I.lock);
            c.kind = kindOf (c.engine, lock);
            c.motion = effMotion (c.kind, s.motion.value_or (I.motion));
            c.travel = s.travel == model::TravelKind::path ? model::TravelKind::path : model::TravelKind::speed;
            c.from = { c.engine == Engine::cloud && ! s.lock.has_value(), ! s.motion.has_value(), ! s.tape.has_value(),
                       ! s.rev.has_value(), ! s.jump.has_value() };
            // a Map keyed by the mix: the first note of a mix makes its entry, the rest count
            bool found = false;
            for (std::size_t k = 1; k < out.size(); ++k)
                if (sameContext (out[k], c))
                {
                    ++out[k].count;
                    found = true;
                    break;
                }
            if (! found)
            {
                c.count = 1;
                out.push_back (c);
            }
        }
        return out;
    }

    bool headsMulti (const core::Inst& I, const model::Clip* clip)
    {
        const double mac = (num (I.macros[2], 0.5) - 0.5) * 14.0;
        if (I.heads + mac > 1.0 + 1e-4)
            return true;
        if (modOn (I.mod1, LaneId::heads) || modOn (I.mod2, LaneId::heads))
            return true;
        if (clip == nullptr)
            return false;
        for (const auto& np : clip->notes)
        {
            if (np == nullptr)
                continue;
            if (np->genOf (LaneId::heads).has_value())
                return true;
            for (const auto& q : laneNodes (*np, LaneId::heads))
                if (I.heads + (q.v - 0.5) * 14.0 + mac > 1.0 + 1e-4)
                    return true;
        }
        return false;
    }

    bool toneAllOpen (const core::Inst& I, const model::Clip* clip)
    {
        const double o = num (I.offsets.tone, 0.0);
        if (1.0 + o < 0.999)
            return false;   // the live keys play the default line (fully open) plus the offset
        if (clip == nullptr)
            return true;
        for (const auto& np : clip->notes)
        {
            if (np == nullptr)
                continue;
            if (np->genOf (LaneId::tone).has_value())
                return false;
            for (const auto& q : laneNodes (*np, LaneId::tone))
                if (q.v + o < 0.999)
                    return false;
        }
        return true;
    }

    bool randomInUse (const core::Inst& I, const model::Clip* clip)
    {
        const auto cx = contexts (I, clip);
        const auto any = [&cx] (auto&& pred) { return std::any_of (cx.begin(), cx.end(), pred); };
        if (any ([] (const Context& c) { return c.motion == Motion::scatter; }))
            return true;
        if (I.spray > 0.0 && any ([] (const Context& c) { return c.kind == Kind::cloud || c.kind == Kind::lock; }))
            return true;
        if (I.drift > 0.0 && any ([] (const Context& c) { return moves (c); }))
            return true;
        if (I.refresh.mode != core::RefreshMode::smooth && I.refresh.skip > 0.0)
            return true;
        if (clip != nullptr)
            for (const auto& np : clip->notes)
                if (np != nullptr && np->sample.start.mode == model::StartMode::random)
                    return true;
        return false;
    }

    //==============================================================================================
    const char* ruleId (Rule r) noexcept
    {
        static constexpr const char* ids[kNumRules] = {
            "lock", "freeze", "speed", "drift", "spread", "headTune", "grain", "spray", "tape", "rev", "atEnd", "jump", "refreshMs",
            "refreshRate", "skip", "mSpeed", "mSpread", "mRefresh", "mGrain", "mSpray", "mTone", "reroll"
        };
        const auto i = static_cast<std::size_t> (r);
        return i < static_cast<std::size_t> (kNumRules) ? ids[i] : "";
    }

    std::string ruleWhy (Rule r, const Context& ic)
    {
        switch (r)
        {
            case Rule::lock:        return "Lock works with Cloud only. Clicking it switches to Cloud with Lock.";
            case Rule::freeze:      return "Freeze does nothing in Tape: Tape plays forwards instead. Pick Voice or Cloud to freeze.";
            case Rule::speed:
            case Rule::mSpeed:      return kSpeedWhy;
            case Rule::drift:       return "Freeze holds the heads still, so Drift does nothing.";
            case Rule::spread:
            case Rule::mSpread:     return kSpreadWhy;
            case Rule::headTune:    return "Head tune tunes heads 2 to 8. With 1 head it does nothing: raise Heads.";
            case Rule::grain:
            case Rule::mGrain:      return kGrainWhy;
            case Rule::spray:
            case Rule::mSpray:      return sprayWhy (ic);
            case Rule::tape:        return ic.kind == Kind::lock ? "Lock always plays in tune, so the Tape switch does nothing. Switch Lock off to use it."
                                                                 : "The Tape engine already ties pitch to speed, so this switch does nothing.";
            case Rule::rev:         return ic.kind == Kind::lock ? "Lock repeats single waves, so turning grains round does nothing. Switch Lock off to use it."
                                                                 : "Tape has no grains to turn round. Pick Voice or Cloud.";
            case Rule::atEnd:       return "Bounce turns round at the edges of the Region, so At the end does nothing.";
            case Rule::jump:        return "Jump every sets how often Scatter jumps. Pick Scatter to use it.";
            case Rule::refreshMs:   return "This time is used only when Refresh is Free.";
            case Rule::refreshRate: return "The note value and Lock to the beat are used only when Refresh is Synced.";
            case Rule::skip:        return "Skip leaves out some fresh grabs, so it needs Refresh set to Free or Synced.";
            case Rule::mRefresh:    return "Refresh is Smooth, so the Refresh macro does nothing. Pick Free or Synced in the Sample tab.";
            case Rule::mTone:       return "Every note's Tone line is fully open already, so turning Tone up changes nothing. Turn it down to darken, or draw the Tone lines lower first.";
            case Rule::reroll:      return "Re-roll deals new Scatter jumps, Spray, Drift, Skip and random Starts. None of them is in use now.";
        }
        return {};
    }

    Fades fades (const core::Inst& I, const model::Clip* clip)
    {
        const auto cx = contexts (I, clip);
        const Context& ic = cx.front();
        std::optional<bool> multi;
        Fades out;
        for (int i = 0; i < kNumRules; ++i)
        {
            const auto r = static_cast<Rule> (i);
            bool instUsesIt = false;
            int notes = 0;
            switch (kindOfRule (r))
            {
                case RuleKind::inst:
                    instUsesIt = instUses (r, I);
                    break;
                case RuleKind::both:
                    instUsesIt = r == Rule::mTone ? ! (num (I.macros[6], 0.5) > 0.5025 && toneAllOpen (I, clip)) : randomInUse (I, clip);
                    break;
                case RuleKind::multi:
                    if (! multi.has_value())
                        multi = headsMulti (I, clip);
                    instUsesIt = *multi;
                    break;
                case RuleKind::uses:
                    instUsesIt = uses (r, ic);
                    if (! instUsesIt)
                        for (std::size_t k = 1; k < cx.size(); ++k)
                            if (uses (r, cx[k]))
                                notes += cx[k].count;
                    break;
            }
            Fade& f = out[static_cast<std::size_t> (i)];
            f.faded = ! instUsesIt && notes == 0;
            f.notes = instUsesIt ? 0 : notes;
            f.reason = instUsesIt ? std::string() : ruleWhy (r, ic);
        }
        return out;
    }

    std::optional<Kind> grainKind (const core::Inst& I, const model::Clip* clip)
    {
        const auto cx = contexts (I, clip);
        if (cx.front().kind != Kind::tape)
            return cx.front().kind;
        // n = { voice, cloud, lock }; the most used, ties in that order (a stable sort on the counts)
        int counts[3] = { 0, 0, 0 };
        for (std::size_t k = 1; k < cx.size(); ++k)
        {
            switch (cx[k].kind)
            {
                case Kind::voice: counts[0] += cx[k].count; break;
                case Kind::cloud: counts[1] += cx[k].count; break;
                case Kind::lock:  counts[2] += cx[k].count; break;
                case Kind::tape:  break;
            }
        }
        static constexpr Kind order[3] = { Kind::voice, Kind::cloud, Kind::lock };
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (counts[i] > counts[best])
                best = i;
        if (counts[best] > 0)
            return order[best];
        return std::nullopt;
    }

    //==============================================================================================
    EngineSuggestion suggestEngine (const SoundInfo* info)
    {
        const double secs = info != nullptr ? num (info->seconds, 0.0) : 0.0;
        bool beat = false;
        if (info != nullptr)
            for (const auto& p : info->parts)
                if (p.has_value() && p->kind == "loop" && p->tempoFound)
                    beat = true;
        if (info == nullptr || ! info->pitch)
        {
            if (secs <= 20.0 && beat)
                return { Engine::tape, "Try Tape", "A beat or loop with no single pitch: Tape keeps its punch." };
            return { Engine::cloud, "Try Cloud", "No single pitch (a chord, a song or a texture): Cloud plays it best. Switch Lock on to play it in tune." };
        }
        if (secs > 20.0)
            return { Engine::cloud, "Try Cloud", "A long recording: Cloud copes with whatever is in it." };
        return { Engine::voice, "Try Voice", "One clear pitch: Voice keeps its character as you play it up and down." };
    }

    std::optional<Part> pickPart (const std::vector<std::optional<Part>>* parts)
    {
        std::vector<const Part*> list;
        if (parts != nullptr)
            for (const auto& p : *parts)
                if (p.has_value() && p->end > p->start)
                    list.push_back (&*p);
        for (const char* k : { "steady", "loop", "quiet", "hit" })
            for (const Part* p : list)
                if (p->kind == k)
                    return *p;
        if (! list.empty())
            return *list.front();
        return std::nullopt;
    }

    //==============================================================================================
    std::string macroText (int i, double m) { return params::text::macroText (i, m); }

    std::string offsetText (int k, double o)
    {
        if (k >= 0 && k <= 4)
            return params::text::offsetText (static_cast<params::text::Offset> (k), o);
        // an offset the prototype does not know: its number (String (o))
        o = num (o, 0.0);
        return std::fabs (o) < 0.0025 ? std::string ("no change") : core::jsNumberToString (o);
    }

    std::optional<double> parseNum (std::string_view t) { return params::text::parseNum (t); }
    std::optional<double> parseMs (std::string_view t)  { return params::text::parseMs (t); }
    std::optional<double> parsePct (std::string_view t) { return params::text::parsePct (t); }

    //==============================================================================================
    double toNorm (const KnobRange& s, double v) noexcept
    {
        v = num (v, s.def);
        if (s.log)
        {
            const double a = jsLog (s.min), b = jsLog (s.max);
            return clampD ((jsLog (std::max (v, s.min)) - a) / (b - a), 0.0, 1.0);
        }
        return clampD ((v - s.min) / (s.max - s.min), 0.0, 1.0);
    }

    double fromNorm (const KnobRange& s, double n) noexcept
    {
        n = clampD (n, 0.0, 1.0);
        return s.log ? core::jsExp (jsLog (s.min) + n * (jsLog (s.max) - jsLog (s.min))) : s.min + n * (s.max - s.min);
    }

    double magnetLog (double v, std::span<const double> marks, double tol) noexcept
    {
        for (const double m : marks)
            if (std::fabs (std::log2 (v / m)) < tol)
                return m;
        return v;
    }

    double qLog (std::span<const double> marks, int digits, double v, bool fine)
    {
        if (fine)
            return sig (v, 4);
        return magnetLog (sig (v, digits != 0 ? digits : 3), marks, 0.06);
    }

    double r6 (double v) noexcept
    {
        return core::jsRound (v * 1e6) / 1e6;
    }

    double qLin (double step, double fineStep, std::span<const double> marks, double tol, double v, bool fine) noexcept
    {
        if (fine)
            return r6 (core::jsRound (v / fineStep) * fineStep);
        double x = r6 (core::jsRound (v / step) * step);
        for (const double m : marks)
            if (std::fabs (v - m) < tol)
                x = m;
        return x;
    }

    //==============================================================================================
    namespace
    {
        constexpr double kSpeedMarks[] = { 0.25, 0.5, 1, 2, 4 };
        constexpr double kHeadsMarks[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        constexpr double kVoiceGrainMarks[] = { 0.5, 1, 2, 4, 8 };
        constexpr double kCloudGrainMarks[] = { 20, 50, 100, 140, 200, 500, 1000 };
        constexpr double kRefreshMarks[] = { 10, 20, 50, 100, 120, 250, 500, 1000 };

        constexpr const char* kMacroNames[8] = { "Speed", "Spread", "Heads", "Refresh", "Grain", "Spray", "Tone", "Formant" };
        constexpr const char* kMacroUndo[8] = { "Macro Speed", "Macro Spread", "Macro Heads", "Macro Refresh", "Macro Grain", "Macro Spray",
                                                "Macro Tone", "Macro Formant" };
        constexpr const char* kMacroTitles[8] = {
            "Speed macro: Every note plays 1/4x to 4x faster. Centre = no change; double-click = centre. Moves every note at once (Quick Control 1).",
            "Spread macro: Spreads the heads further apart, or closer together. Centre = no change; double-click = centre. Moves every note at once (Quick Control 2).",
            "Heads macro: Adds or removes heads on every note (up to 8). Centre = no change; double-click = centre. Moves every note at once (Quick Control 3).",
            "Refresh macro: Fresh grabs come faster or slower (works when Refresh is Free or Synced). Centre = no change; double-click = centre. Moves every note at once (Quick Control 4).",
            "Grain macro: Longer or shorter grains on every note. Centre = no change; double-click = centre. Moves every note at once (Quick Control 5).",
            "Spray macro: More or less Spray on every note. Centre = no change; double-click = centre. Moves every note at once (Quick Control 6).",
            "Tone macro: Brighter or darker: moves every note's Tone line. Centre = no change; double-click = centre. Moves every note at once (Quick Control 7).",
            "Formant macro: Moves every note's Formant line up or down. Centre = no change; double-click = centre. Moves every note at once (Quick Control 8)."
        };
        constexpr std::optional<Rule> kMacroRules[8] = { Rule::mSpeed, Rule::mSpread, std::nullopt, Rule::mRefresh, Rule::mGrain, Rule::mSpray, Rule::mTone,
                                                         std::nullopt };

        constexpr const char* kOffsetNames[5] = { "Volume", "Vibrato", "Tone", "Formant", "Hollow" };
        constexpr const char* kOffsetUndo[5] = { "Offset Volume", "Offset Vibrato", "Offset Tone", "Offset Formant", "Offset Hollow" };
        constexpr const char* kOffsetTitles[5] = {
            "Whole-clip Volume: Added to every note's Volume line (the dB shown is for a note at the default volume). Centre = no change; double-click = centre.",
            "Whole-clip Vibrato: Added to every note's Vibrato line (in semitones of depth). Centre = no change; double-click = centre.",
            "Whole-clip Tone: Added to every note's Tone line. Centre = no change; double-click = centre.",
            "Whole-clip Formant: Added to every note's Formant line. Centre = no change; double-click = centre.",
            "Whole-clip Hollow: Added to every note's Hollow line. Centre = no change; double-click = centre."
        };
        constexpr KnobColour kOffsetColours[5] = { KnobColour::amp, KnobColour::vib, KnobColour::tone, KnobColour::formant, KnobColour::hollow };

        KnobSpec make (KnobId id, const char* label, const char* undo, const char* title, params::P param, KnobRange range, KnobFormat format,
                       KnobQuant quant, KnobParse parse, std::optional<Rule> rule)
        {
            KnobSpec s;
            s.id = id;
            s.label = label;
            s.undo = undo;
            s.title = title;
            s.param = param;
            s.range = range;
            s.tmin = range.min;
            s.tmax = range.max;
            s.format = format;
            s.quant = quant;
            s.parse = parse;
            s.rule = rule;
            return s;
        }
    }

    const std::array<KnobId, kNumKnobs>& allKnobs() noexcept
    {
        static const std::array<KnobId, kNumKnobs> all = [] {
            std::array<KnobId, kNumKnobs> a {};
            for (int i = 0; i < kNumKnobs; ++i)
                a[static_cast<std::size_t> (i)] = static_cast<KnobId> (i);
            return a;
        }();
        return all;
    }

    KnobSpec knobSpec (KnobId id, std::optional<Kind> grain)
    {
        using P = params::P;
        switch (id)
        {
            case KnobId::speed:
            {
                auto s = make (id, "Speed", "Speed", "How fast the heads move through the sound: 1x = as recorded, 1/8x to 8x. Double-click = 1x.", P::speed,
                               { 0.125, 8.0, true, 1.0 }, KnobFormat::x, KnobQuant::speed, KnobParse::num, Rule::speed);
                s.tmin = 0.0;
                s.tmax = 16.0;
                s.example = "0.5";
                s.pin = "speed";
                return s;
            }
            case KnobId::heads:
            {
                auto s = make (id, "Heads", "Heads",
                               "How many playheads read the sound at once, 1 to 8. 2.5 = two heads plus a third at half level. More heads don't get louder.",
                               P::heads, { 1.0, 8.0, false, 1.0 }, KnobFormat::heads, KnobQuant::heads, KnobParse::num, std::nullopt);
                s.example = "2.5";
                s.pin = "heads";
                return s;
            }
            case KnobId::spread:
            {
                auto s = make (id, "Spread", "Spread", "Spaces the heads out through the Region and across left and right.", P::spread,
                               { 0.0, 1.0, false, 0.25 }, KnobFormat::pct, KnobQuant::pct, KnobParse::pct, Rule::spread);
                s.example = "25";
                s.pin = "spread";
                return s;
            }
            case KnobId::grain:
            {
                // the Voice spec is the knob's own; the engine's grain setting replaces it (dyn); none: off
                auto s = make (id, "Grain", "Grain", "How long each grain is.", P::grainVoice, { 0.25, 16.0, true, 2.0 }, KnobFormat::cycles,
                               KnobQuant::voiceGrain, KnobParse::num, Rule::grain);
                s.pin = "grain";
                s.offWhy = kGrainWhy;
                if (! grain.has_value() || *grain == Kind::tape)
                {
                    s.off = true;
                    return s;
                }
                if (*grain == Kind::voice)
                {
                    s.title = "Voice: each grain is this many cycles (waves) of the sound's root note long. Shorter = buzzier, longer = smoother.";
                    s.example = "2";
                    return s;
                }
                if (*grain == Kind::cloud)
                {
                    s.title = "Cloud: each grain is this long. Short grains buzz, long grains smear.";
                    s.param = P::grainCloud;
                    s.range = { 5.0, 2000.0, true, 140.0 };
                    s.tmin = 5.0;
                    s.tmax = 2000.0;
                    s.format = KnobFormat::cloudMs;
                    s.quant = KnobQuant::cloudGrain;
                    s.parse = KnobParse::ms;
                    s.example = "140";
                    return s;
                }
                s.title = "Cloud with Lock: each grab is this many waves (wavelengths) of the note you play. More waves = smoother, fewer = more detail.";
                s.param = P::grainLock;
                s.range = { 1.0, 12.0, false, 4.0 };
                s.tmin = 1.0;
                s.tmax = 16.0;
                s.format = KnobFormat::lockWaves;
                s.quant = KnobQuant::lockGrain;
                s.parse = KnobParse::num;
                s.example = "4";
                return s;
            }
            case KnobId::spray:
            {
                auto s = make (id, "Spray", "Spray", "Scatters each grain a little around its head (Cloud).", P::spray, { 0.0, 1.0, false, 0.0 },
                               KnobFormat::pct, KnobQuant::pct, KnobParse::pct, Rule::spray);
                s.example = "20";
                s.pin = "spray";
                return s;
            }
            case KnobId::drift:
            {
                auto s = make (id, "Drift", "Drift",
                               "Gives each head a slightly different speed, so they wander apart (and with Bounce, come back together).", P::drift,
                               { 0.0, 1.0, false, 0.0 }, KnobFormat::pct, KnobQuant::pct, KnobParse::pct, Rule::drift);
                s.example = "30";
                s.pin = "drift";
                return s;
            }
            case KnobId::pitch:
            {
                auto s = make (id, "Pitch", "Pitch", "Transposes the whole instrument, in semitones (Shift = fine).", P::pitch, { -24.0, 24.0, false, 0.0 },
                               KnobFormat::st, KnobQuant::pitch, KnobParse::num, std::nullopt);
                s.tmin = -48.0;
                s.tmax = 48.0;
                s.bipolar = true;
                s.colour = KnobColour::pitch;
                s.example = "-12";
                return s;
            }
            case KnobId::refreshMs:
            {
                auto s = make (id, "Refresh time", "Refresh time", "Free Refresh: the time between fresh grabs, 5 ms to 2 s.", P::refreshMs,
                               { 5.0, 2000.0, true, 120.0 }, KnobFormat::ms, KnobQuant::refreshMs, KnobParse::ms, Rule::refreshMs);
                s.example = "120";
                return s;
            }
            case KnobId::skip:
            {
                auto s = make (id, "Skip", "Skip",
                               "Refresh: the chance that a fresh grab is skipped, as in GrainLock. It skips the same grabs on every loop until you Re-roll.",
                               P::refreshSkip, { 0.0, 1.0, false, 0.0 }, KnobFormat::pct, KnobQuant::pct, KnobParse::pct, Rule::skip);
                s.example = "25";
                return s;
            }
            default:
                break;
        }

        const int k = static_cast<int> (id);
        if (k >= static_cast<int> (KnobId::macro1) && k <= static_cast<int> (KnobId::macro8))
        {
            const int i = k - static_cast<int> (KnobId::macro1);
            const auto ui = static_cast<std::size_t> (i);
            auto s = make (id, kMacroNames[ui], kMacroUndo[ui], kMacroTitles[ui], static_cast<P> (params::index (P::macro1) + i),
                           { 0.0, 1.0, false, 0.5 }, KnobFormat::macro, KnobQuant::macro, KnobParse::none, kMacroRules[ui]);
            s.index = i;
            s.bipolar = true;
            s.colour = i == 6 ? KnobColour::tone : i == 7 ? KnobColour::formant : KnobColour::sample;
            s.macroRow = true;
            return s;
        }
        const int j = std::clamp (k - static_cast<int> (KnobId::ampOffset), 0, 4);
        const auto uj = static_cast<std::size_t> (j);
        auto s = make (id, kOffsetNames[uj], kOffsetUndo[uj], kOffsetTitles[uj], static_cast<P> (params::index (P::ampOffset) + j),
                       { -1.0, 1.0, false, 0.0 }, KnobFormat::offset, KnobQuant::offset, KnobParse::none, std::nullopt);
        s.index = j;
        s.bipolar = true;
        s.colour = kOffsetColours[uj];
        s.macroRow = true;
        return s;
    }

    std::vector<params::P> knobParams (KnobId id)
    {
        if (id == KnobId::grain)
            return { params::P::grainVoice, params::P::grainCloud, params::P::grainLock };
        return { knobSpec (id).param };
    }

    std::string knobText (const KnobSpec& s, double v)
    {
        namespace t = params::text;
        switch (s.format)
        {
            case KnobFormat::x:         return t::fmtX (v);
            case KnobFormat::heads:     return t::fmtHeads (v);
            case KnobFormat::pct:       return t::fmtPct (v);
            case KnobFormat::cycles:    return t::fmtCycles (v);
            case KnobFormat::cloudMs:   return core::jsNumberToString (core::jsRound (v)) + " ms";
            case KnobFormat::lockWaves:
            {
                const std::string w = t::trimZ (core::jsToFixed (v, 1));
                return w + (w == "1" ? " wave" : " waves");
            }
            case KnobFormat::st:        return t::fmtSt (v);
            case KnobFormat::ms:        return t::fmtMs (v);
            case KnobFormat::macro:     return macroText (s.index, v);
            case KnobFormat::offset:    return offsetText (s.index, v);
        }
        return core::jsNumberToString (v);
    }

    double knobQuant (const KnobSpec& s, double v, bool fine)
    {
        switch (s.quant)
        {
            case KnobQuant::speed:      return qLog (kSpeedMarks, 0, v, fine);
            case KnobQuant::heads:      return qLin (0.05, 0.01, kHeadsMarks, 0.08, v, fine);
            case KnobQuant::pct:        return qLin (0.01, 0.001, {}, 0.0, v, fine);
            case KnobQuant::voiceGrain: return qLog (kVoiceGrainMarks, 0, v, fine);
            case KnobQuant::cloudGrain: return fine ? core::jsRound (v * 10.0) / 10.0 : magnetLog (core::jsRound (v), kCloudGrainMarks, 0.04);
            case KnobQuant::lockGrain:  return qLin (1.0, 0.5, {}, 0.0, v, fine);
            case KnobQuant::pitch:      return qLin (1.0, 0.01, {}, 0.0, v, fine);
            case KnobQuant::refreshMs:  return fine ? core::jsRound (v * 10.0) / 10.0 : magnetLog (core::jsRound (v), kRefreshMarks, 0.04);
            case KnobQuant::macro:
                return fine ? r6 (core::jsRound (v * 1000.0) / 1000.0) : std::fabs (v - 0.5) < 0.02 ? 0.5 : r6 (core::jsRound (v * 200.0) / 200.0);
            case KnobQuant::offset:
                return fine ? r6 (core::jsRound (v * 1000.0) / 1000.0) : std::fabs (v) < 0.03 ? 0.0 : r6 (core::jsRound (v * 100.0) / 100.0);
        }
        return v;
    }

    std::optional<double> knobParse (const KnobSpec& s, std::string_view text)
    {
        switch (s.parse)
        {
            case KnobParse::none: return std::nullopt;
            case KnobParse::num:  return parseNum (text);
            case KnobParse::ms:   return parseMs (text);
            case KnobParse::pct:  return parsePct (text);
        }
        return std::nullopt;
    }

    double typedValue (const KnobSpec& s, double parsed) noexcept
    {
        return clampD (r6 (parsed), s.tmin, s.tmax);
    }

    std::string typingHelp (const KnobSpec& s)
    {
        return std::string ("Type a number, for example ") + (s.example[0] != '\0' ? std::string (s.example) : knobText (s, s.range.def));
    }

    std::string fadeTitle (const std::string& base, const Fade* st)
    {
        if (st == nullptr)
            return base;
        if (st->faded)
            return "Does nothing now: " + st->reason + "\n\n" + base;
        if (st->notes != 0)
            return base + "\n\n" + std::to_string (st->notes) + (st->notes == 1 ? " note uses" : " notes use")
                   + " it through its own settings (Note settings).";
        return base;
    }

    std::string knobTitle (const KnobSpec& s, const Fade* st)
    {
        if (s.off)
            return std::string ("Does nothing now: ") + s.offWhy + "\n\n" + s.title;
        return fadeTitle (s.title, st);
    }

    std::string knobHint (const KnobSpec& s, const Fade* st)
    {
        const std::string label = s.label;
        if (s.off)
            return label + ": does nothing now. " + s.offWhy;
        std::string base = s.title;
        if (const auto nl = base.find ('\n'); nl != std::string::npos)
            base.resize (nl);
        if (st != nullptr && st->faded)
            return label + ": does nothing now. " + st->reason;
        const std::string tail = std::string (" Drag up or down (Shift = fine), double-click = reset")
                                 + (s.parse != KnobParse::none ? ", click the value to type it." : ".");
        return label + ": " + base + tail;
    }

    //==============================================================================================
    KnobDrag beginDrag (const KnobSpec& s, double y, double v) noexcept
    {
        KnobDrag d;
        d.y0 = y;
        d.n0 = toNorm (s.range, v);
        d.v0 = v;
        d.last = v;
        d.moved = false;
        return d;
    }

    std::optional<double> dragTo (const KnobSpec& s, KnobDrag& d, double y, bool fine)
    {
        const double dy = d.y0 - y;
        if (! d.moved && std::fabs (dy) < kDeadZone)
            return std::nullopt;
        const double n = clampD (d.n0 + dy / kDragPixels * (fine ? kFineFactor : 1.0), 0.0, 1.0);
        const double v = clampD (knobQuant (s, fromNorm (s.range, n), fine), s.range.min, s.range.max);
        if (v == d.last)
            return std::nullopt;
        d.last = v;
        d.moved = true;
        return v;
    }
}
