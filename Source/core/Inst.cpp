#include "Inst.h"

#include "JsCompat.h"
#include "JsonLite.h"
#include "Music.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

namespace sl::core
{
    namespace
    {
        template <class E, std::size_t N>
        const char* codeOf (E value, const char* const (&codes)[N]) noexcept
        {
            const auto i = static_cast<std::size_t> (value);
            return i < N ? codes[i] : "";
        }

        template <class E, std::size_t N>
        std::optional<E> parseOf (std::string_view text, const char* const (&codes)[N]) noexcept
        {
            for (std::size_t i = 0; i < N; ++i)
                if (text == codes[i])
                    return static_cast<E> (i);
            return std::nullopt;
        }

        template <class E, std::size_t N>
        E validOr (E value, const char* const (&)[N], E fallback) noexcept
        {
            return static_cast<std::size_t> (value) < N ? value : fallback;
        }

        // sl-engine.js: clamp (v, a, b) and fin (x, d).
        double clamp (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }
        double fin (double x, double d) noexcept { return std::isfinite (x) ? x : d; }
        double clampFin (double x, double d, double a, double b) noexcept { return clamp (fin (x, d), a, b); }
    }

    const char* code (Engine v) noexcept      { return codeOf (v, kEngineCodes); }
    const char* code (Motion v) noexcept      { return codeOf (v, kMotionCodes); }
    const char* code (HeadTune v) noexcept    { return codeOf (v, kHeadTuneCodes); }
    const char* code (RefreshMode v) noexcept { return codeOf (v, kRefreshModeCodes); }
    const char* code (AtEnd v) noexcept       { return codeOf (v, kAtEndCodes); }
    const char* code (GlideMode v) noexcept   { return codeOf (v, kGlideModeCodes); }

    std::optional<Engine> parseEngine (std::string_view s) noexcept           { return parseOf<Engine> (s, kEngineCodes); }
    std::optional<Motion> parseMotion (std::string_view s) noexcept           { return parseOf<Motion> (s, kMotionCodes); }
    std::optional<HeadTune> parseHeadTune (std::string_view s) noexcept       { return parseOf<HeadTune> (s, kHeadTuneCodes); }
    std::optional<RefreshMode> parseRefreshMode (std::string_view s) noexcept { return parseOf<RefreshMode> (s, kRefreshModeCodes); }
    std::optional<AtEnd> parseAtEnd (std::string_view s) noexcept             { return parseOf<AtEnd> (s, kAtEndCodes); }
    std::optional<GlideMode> parseGlideMode (std::string_view s) noexcept     { return parseOf<GlideMode> (s, kGlideModeCodes); }

    Inst defaultInst() noexcept
    {
        return Inst {};
    }

    Inst clampForEngine (const Inst& in) noexcept
    {
        // sl-engine.js normInst, field for field, in its order.
        const Inst d = defaultInst();
        Inst o = in;

        o.engine = validOr (o.engine, kEngineCodes, Engine::voice);
        o.motion = validOr (o.motion, kMotionCodes, Motion::forward);
        o.speed = clampFin (o.speed, 1.0, -16.0, 16.0);
        o.heads = clampFin (o.heads, 1.0, 1.0, 8.0);
        o.spread = clampFin (o.spread, 0.25, 0.0, 1.0);
        o.grainVoice = clampFin (o.grainVoice, 2.0, 0.25, 16.0);
        o.grainCloud = clampFin (o.grainCloud, 140.0, 5.0, 2000.0);
        o.grainLock = clampFin (o.grainLock, 4.0, 1.0, 64.0);

        o.refresh.mode = validOr (o.refresh.mode, kRefreshModeCodes, RefreshMode::smooth);
        o.refresh.ms = clampFin (o.refresh.ms, 120.0, 5.0, 10000.0);
        o.refresh.rate = isValidRate (o.refresh.rate) ? o.refresh.rate : RateId::r1_16;
        o.refresh.skip = clampFin (o.refresh.skip, 0.0, 0.0, 1.0);

        o.spray = clampFin (o.spray, 0.0, 0.0, 1.0);
        o.pitch = clampFin (o.pitch, 0.0, -48.0, 48.0);
        o.drift = clampFin (o.drift, 0.0, 0.0, 1.0);
        o.headTune = validOr (o.headTune, kHeadTuneCodes, HeadTune::same);

        o.region.start = clampFin (o.region.start, 0.0, 0.0, 1.0);
        o.region.end = clampFin (o.region.end, 1.0, 0.0, 1.0);
        if (o.region.end < o.region.start)
        {
            const double t = o.region.start;
            o.region.start = o.region.end;
            o.region.end = t;
        }
        o.region.atEnd = validOr (o.region.atEnd, kAtEndCodes, AtEnd::loop);
        o.region.xfadeMs = clampFin (o.region.xfadeMs, 10.0, 0.0, 1000.0);

        o.jump = isValidRate (o.jump) ? o.jump : RateId::r1_16;

        for (auto& m : o.macros)
            m = clampFin (m, 0.5, 0.0, 1.0);

        o.offsets.amp = clampFin (o.offsets.amp, 0.0, -1.0, 1.0);
        o.offsets.vib = clampFin (o.offsets.vib, 0.0, -1.0, 1.0);
        o.offsets.tone = clampFin (o.offsets.tone, 0.0, -1.0, 1.0);
        o.offsets.formant = clampFin (o.offsets.formant, 0.0, -1.0, 1.0);
        o.offsets.hollow = clampFin (o.offsets.hollow, 0.0, -1.0, 1.0);

        o.vibRate = clampFin (o.vibRate, 5.5, 0.0, 50.0);
        o.root = clampFin (o.root, 60.0, 0.0, 127.0);
        o.cents = clampFin (o.cents, 0.0, -100.0, 100.0);
        o.velSens = clampFin (o.velSens, 0.7, 0.0, 1.0);
        o.polyphony = clamp (jsRound (fin (o.polyphony, 16.0)), 1.0, 32.0);
        o.outGain = clampFin (o.outGain, 0.0, -60.0, 24.0);
        o.bendRange = clampFin (o.bendRange, 2.0, 0.0, 48.0);

        // A target that is not 'off' and not a lane turns off; the CC is kept as it is.
        for (ModWheel* m : { &o.mod1, &o.mod2 })
            if (m->target != kModOff && (m->target < 0 || m->target >= kNumLanes))
                m->target = kModOff;

        // Not in the engine's normInst: the plugin's engine indexes by these, so they must be in range.
        o.glideMode = validOr (o.glideMode, kGlideModeCodes, d.glideMode);
        o.glideTime = fin (o.glideTime, d.glideTime);
        if (o.vibSync != kVibSyncFree && (o.vibSync < 0 || o.vibSync >= kNumRates))
            o.vibSync = kVibSyncFree;
        if (o.scale >= kNumScales)
            o.scale = 0;

        return o;
    }

    //==============================================================================================
    // The model's side (sl-model.js mergeInst, normalizeInst).

    namespace
    {
        template <class T>
        void take (T& field, const std::optional<T>& patch)
        {
            if (patch.has_value())
                field = *patch;
        }

        // SLModel's num (x, d): a finite number, else the default.
        double num (double x, double d) noexcept { return std::isfinite (x) ? x : d; }

        bool modTargetValid (std::int8_t t) noexcept { return t == kModOff || (t >= 0 && t < kNumLanes); }
    }

    Inst mergeInst (const Inst& inst, const InstPatch& patch)
    {
        // Nested settings merge field by field, macros replace whole, as SLModel.mergeInst.
        Inst o = inst;
        take (o.engine, patch.engine);
        take (o.lock, patch.lock);
        take (o.motion, patch.motion);
        take (o.speed, patch.speed);
        take (o.heads, patch.heads);
        take (o.spread, patch.spread);
        take (o.grainVoice, patch.grainVoice);
        take (o.grainCloud, patch.grainCloud);
        take (o.grainLock, patch.grainLock);
        take (o.refresh.mode, patch.refresh.mode);
        take (o.refresh.ms, patch.refresh.ms);
        take (o.refresh.rate, patch.refresh.rate);
        take (o.refresh.lockToBeat, patch.refresh.lockToBeat);
        take (o.refresh.skip, patch.refresh.skip);
        take (o.spray, patch.spray);
        take (o.pitch, patch.pitch);
        take (o.tape, patch.tape);
        take (o.rev, patch.rev);
        take (o.drift, patch.drift);
        take (o.headTune, patch.headTune);
        take (o.region.start, patch.region.start);
        take (o.region.end, patch.region.end);
        take (o.region.atEnd, patch.region.atEnd);
        take (o.region.xfadeMs, patch.region.xfadeMs);
        take (o.jump, patch.jump);
        take (o.macros, patch.macros);
        take (o.offsets.amp, patch.offsets.amp);
        take (o.offsets.vib, patch.offsets.vib);
        take (o.offsets.tone, patch.offsets.tone);
        take (o.offsets.formant, patch.offsets.formant);
        take (o.offsets.hollow, patch.offsets.hollow);
        take (o.glideMode, patch.glideMode);
        take (o.glideTime, patch.glideTime);
        take (o.vibRate, patch.vibRate);
        take (o.root, patch.root);
        take (o.cents, patch.cents);
        take (o.key, patch.key);
        take (o.scale, patch.scale);
        take (o.scaleLock, patch.scaleLock);
        take (o.velSens, patch.velSens);
        take (o.polyphony, patch.polyphony);
        take (o.outGain, patch.outGain);
        take (o.bendRange, patch.bendRange);
        take (o.mod1.target, patch.mod1.target);
        take (o.mod1.cc, patch.mod1.cc);
        take (o.mod2.target, patch.mod2.target);
        take (o.mod2.cc, patch.mod2.cc);
        take (o.liveLegato, patch.liveLegato);
        take (o.seed, patch.seed);
        take (o.vibSync, patch.vibSync);
        return o;
    }

    Inst normalizeInst (Inst o)
    {
        // SLModel.normalizeInst, field for field, in its order. A choice outside its list takes the default,
        // a non-finite number its default, then each is clamped to the model's range.
        const Inst d = defaultInst();

        o.engine = validOr (o.engine, kEngineCodes, d.engine);
        o.motion = validOr (o.motion, kMotionCodes, d.motion);
        o.speed = clamp (num (o.speed, 1.0), 0.0, 16.0);
        o.heads = clamp (num (o.heads, 1.0), 1.0, 8.0);
        o.spread = clamp (num (o.spread, 0.25), 0.0, 1.0);
        o.grainVoice = clamp (num (o.grainVoice, 2.0), 0.25, 16.0);
        o.grainCloud = clamp (num (o.grainCloud, 140.0), 5.0, 2000.0);
        o.grainLock = clamp (num (o.grainLock, 4.0), 1.0, 16.0);

        o.refresh.mode = validOr (o.refresh.mode, kRefreshModeCodes, RefreshMode::smooth);
        o.refresh.ms = clamp (num (o.refresh.ms, 120.0), 5.0, 2000.0);
        o.refresh.rate = isValidRate (o.refresh.rate) ? o.refresh.rate : RateId::r1_16;
        o.refresh.skip = clamp (num (o.refresh.skip, 0.0), 0.0, 1.0);

        o.spray = clamp (num (o.spray, 0.0), 0.0, 1.0);
        o.pitch = clamp (num (o.pitch, 0.0), -48.0, 48.0);
        o.drift = clamp (num (o.drift, 0.0), 0.0, 1.0);
        o.headTune = validOr (o.headTune, kHeadTuneCodes, HeadTune::same);

        const double rs = clamp (num (o.region.start, 0.0), 0.0, 1.0);
        const double re = clamp (num (o.region.end, 1.0), 0.0, 1.0);
        o.region.start = re < rs ? re : rs;   // Math.min (rs, re)
        o.region.end = rs > re ? rs : re;     // Math.max (rs, re)
        o.region.atEnd = validOr (o.region.atEnd, kAtEndCodes, AtEnd::loop);
        o.region.xfadeMs = clamp (num (o.region.xfadeMs, 10.0), 0.0, 500.0);

        o.jump = isValidRate (o.jump) ? o.jump : RateId::r1_8;

        for (std::size_t i = 0; i < o.macros.size(); ++i)
            o.macros[i] = clamp (num (o.macros[i], d.macros[i]), 0.0, 1.0);

        o.offsets.amp = clamp (num (o.offsets.amp, 0.0), -1.0, 1.0);
        o.offsets.vib = clamp (num (o.offsets.vib, 0.0), -1.0, 1.0);
        o.offsets.tone = clamp (num (o.offsets.tone, 0.0), -1.0, 1.0);
        o.offsets.formant = clamp (num (o.offsets.formant, 0.0), -1.0, 1.0);
        o.offsets.hollow = clamp (num (o.offsets.hollow, 0.0), -1.0, 1.0);

        o.glideMode = validOr (o.glideMode, kGlideModeCodes, GlideMode::legato);
        o.glideTime = clamp (num (o.glideTime, 0.5), 0.0, 16.0);
        o.vibRate = clamp (num (o.vibRate, 5.5), 0.1, 20.0);
        o.root = clamp (num (o.root, 60.0), 0.0, 127.0);
        o.cents = clamp (num (o.cents, 0.0), -100.0, 100.0);
        o.key = o.key < 0 ? 0 : o.key > 11 ? 11 : o.key;   // an int already: Math.round changes nothing
        if (o.scale >= kNumScales)
            o.scale = 0;
        o.velSens = clamp (num (o.velSens, 0.7), 0.0, 1.0);
        o.polyphony = clamp (jsRound (num (o.polyphony, 16.0)), 1.0, 32.0);   // the engine plays at most 32
        o.outGain = clamp (num (o.outGain, 0.0), -60.0, 12.0);
        o.bendRange = clamp (num (o.bendRange, 2.0), 0.0, 48.0);

        ModWheel* const wheels[2] = { &o.mod1, &o.mod2 };
        const ModWheel* const wheelDefaults[2] = { &d.mod1, &d.mod2 };
        for (int w = 0; w < 2; ++w)
        {
            ModWheel& m = *wheels[w];
            if (! modTargetValid (m.target))
                m.target = wheelDefaults[w]->target;
            m.cc = m.cc < 0 ? 0 : m.cc > 127 ? 127 : m.cc;
        }

        // seed is an int32 already (Math.trunc (x) | 0 happened where it was set).
        if (o.vibSync != kVibSyncFree && (o.vibSync < 0 || o.vibSync >= kNumRates))
            o.vibSync = kVibSyncFree;   // plugin addition (8.2 #12): anything else is Free
        return o;
    }

    //==============================================================================================
    // JSON: what JavaScript does to a prototype save's values before normalizeInst sees them.

    namespace
    {
        // A JSON value as SLModel's num () sees it: a number, else NaN (so num () takes the default).
        double jsonNumber (const JsonValue& v) noexcept
        {
            return v.isNumber() ? v.asNumber() : std::numeric_limits<double>::quiet_NaN();
        }

        // JavaScript truthiness (`!!x`).
        bool truthy (const JsonValue& v) noexcept
        {
            switch (v.type())
            {
                case JsonValue::Type::null:    return false;
                case JsonValue::Type::boolean: return v.asBool();
                case JsonValue::Type::number:  return v.asNumber() != 0.0 && ! std::isnan (v.asNumber());
                case JsonValue::Type::string:  return ! v.asString().empty();
                case JsonValue::Type::array:
                case JsonValue::Type::object:  return true;
            }
            return false;
        }

        // A choice string from its list, else a value outside the list (normalizeInst then takes the default).
        template <class E, std::size_t N>
        E choice (const JsonValue& v, const char* const (&codes)[N]) noexcept
        {
            if (v.isString())
                if (const auto e = parseOf<E> (v.asString(), codes))
                    return *e;
            return static_cast<E> (N);
        }

        RateId rateChoice (const JsonValue& v) noexcept
        {
            if (v.isString())
                if (const auto r = parseRate (v.asString()))
                    return *r;
            return static_cast<RateId> (kNumRates);
        }

        // Math.round (num (x, d)) clamped to lo..hi, for the fields the typed Inst holds as int.
        int roundedInt (const JsonValue& v, double d, double lo, double hi) noexcept
        {
            return static_cast<int> (clamp (jsRound (num (jsonNumber (v), d)), lo, hi));
        }

        // A plain object (JavaScript: v && typeof v === 'object' && ! Array.isArray (v)).
        bool isPlainObject (const JsonValue& v) noexcept { return v.isObject(); }

        void readModWheel (const JsonValue& v, ModWheel& m, const ModWheel& d)
        {
            // normalizeInst: a mod1 that is not an object is the default; else target 'off' or a lane id,
            // anything else the default target; cc rounded and clamped 0..127.
            m = d;
            if (! isPlainObject (v))
                return;
            if (const auto* t = v.find ("target"))
            {
                if (t->isString() && t->asString() == "off")
                    m.target = kModOff;
                else if (const int lane = t->isString() ? laneIndex (t->asString()) : -1; lane >= 0)
                    m.target = static_cast<std::int8_t> (lane);
            }
            if (const auto* c = v.find ("cc"))
                m.cc = roundedInt (*c, static_cast<double> (d.cc), 0.0, 127.0);
        }
    }

    Inst instFromJson (const JsonValue& src)
    {
        // normalizeInst (mergeInst (defaultInst (), src)): anything that is not an object gives the defaults.
        const Inst d = defaultInst();
        Inst o = d;
        if (! isPlainObject (src))
            return normalizeInst (o);

        for (std::size_t i = 0; i < src.size(); ++i)
        {
            const std::string& k = src.keyAt (i);
            const JsonValue& v = src.valueAt (i);

            if (k == "engine")            o.engine = choice<Engine> (v, kEngineCodes);
            else if (k == "lock")         o.lock = truthy (v);
            else if (k == "motion")       o.motion = choice<Motion> (v, kMotionCodes);
            else if (k == "speed")        o.speed = jsonNumber (v);
            else if (k == "heads")        o.heads = jsonNumber (v);
            else if (k == "spread")       o.spread = jsonNumber (v);
            else if (k == "grainVoice")   o.grainVoice = jsonNumber (v);
            else if (k == "grainCloud")   o.grainCloud = jsonNumber (v);
            else if (k == "grainLock")    o.grainLock = jsonNumber (v);
            else if (k == "spray")        o.spray = jsonNumber (v);
            else if (k == "pitch")        o.pitch = jsonNumber (v);
            else if (k == "tape")         o.tape = truthy (v);
            else if (k == "rev")          o.rev = truthy (v);
            else if (k == "drift")        o.drift = jsonNumber (v);
            else if (k == "headTune")     o.headTune = choice<HeadTune> (v, kHeadTuneCodes);
            else if (k == "jump")         o.jump = rateChoice (v);
            else if (k == "glideMode")    o.glideMode = choice<GlideMode> (v, kGlideModeCodes);
            else if (k == "glideTime")    o.glideTime = jsonNumber (v);
            else if (k == "vibRate")      o.vibRate = jsonNumber (v);
            else if (k == "root")         o.root = jsonNumber (v);
            else if (k == "cents")        o.cents = jsonNumber (v);
            else if (k == "key")          o.key = roundedInt (v, 0.0, 0.0, 11.0);
            else if (k == "scaleLock")    o.scaleLock = truthy (v);
            else if (k == "velSens")      o.velSens = jsonNumber (v);
            else if (k == "polyphony")    o.polyphony = jsonNumber (v);
            else if (k == "outGain")      o.outGain = jsonNumber (v);
            else if (k == "bendRange")    o.bendRange = jsonNumber (v);
            else if (k == "liveLegato")   o.liveLegato = truthy (v);
            else if (k == "seed")         o.seed = toInt32 (std::trunc (num (jsonNumber (v), 1.0)));
            else if (k == "mod1")         readModWheel (v, o.mod1, d.mod1);
            else if (k == "mod2")         readModWheel (v, o.mod2, d.mod2);
            else if (k == "scale")
            {
                const int s = v.isString() ? findScale (v.asString()) : -1;
                o.scale = static_cast<std::uint8_t> (s >= 0 ? s : 0);
            }
            else if (k == "macros")
            {
                // An array replaces the macros; normalizeInst then reads num (macros[i], 0.5) for the 8.
                // Anything else leaves the defaults.
                if (v.isArray())
                    for (std::size_t m = 0; m < o.macros.size(); ++m)
                        o.macros[m] = m < v.size() ? jsonNumber (v[m]) : d.macros[m];
                else
                    o.macros = d.macros;
            }
            else if (k == "refresh")
            {
                o.refresh = d.refresh;   // not an object: normalizeInst puts the default back
                if (isPlainObject (v))
                    for (std::size_t j = 0; j < v.size(); ++j)
                    {
                        const std::string& rk = v.keyAt (j);
                        const JsonValue& rv = v.valueAt (j);
                        if (rk == "mode")             o.refresh.mode = choice<RefreshMode> (rv, kRefreshModeCodes);
                        else if (rk == "ms")          o.refresh.ms = jsonNumber (rv);
                        else if (rk == "rate")        o.refresh.rate = rateChoice (rv);
                        else if (rk == "lockToBeat")  o.refresh.lockToBeat = ! (rv.isBool() && ! rv.asBool());   // `!== false`
                        else if (rk == "skip")        o.refresh.skip = jsonNumber (rv);
                    }
            }
            else if (k == "region")
            {
                o.region = d.region;
                if (isPlainObject (v))
                    for (std::size_t j = 0; j < v.size(); ++j)
                    {
                        const std::string& rk = v.keyAt (j);
                        const JsonValue& rv = v.valueAt (j);
                        if (rk == "start")            o.region.start = jsonNumber (rv);
                        else if (rk == "end")         o.region.end = jsonNumber (rv);
                        else if (rk == "atEnd")       o.region.atEnd = choice<AtEnd> (rv, kAtEndCodes);
                        else if (rk == "xfadeMs")     o.region.xfadeMs = jsonNumber (rv);
                    }
            }
            else if (k == "offsets")
            {
                o.offsets = d.offsets;
                if (isPlainObject (v))
                {
                    o.offsets.amp = jsonNumber (v["amp"]);
                    o.offsets.vib = jsonNumber (v["vib"]);
                    o.offsets.tone = jsonNumber (v["tone"]);
                    o.offsets.formant = jsonNumber (v["formant"]);
                    o.offsets.hollow = jsonNumber (v["hollow"]);
                }
            }
            // Keys this version does not know are dropped (normalizeInst would keep them; nothing reads them).
        }
        return normalizeInst (o);
    }

    JsonValue instToJson (const Inst& inst)
    {
        auto obj = JsonValue::object();
        auto num2 = [] (double x) { return JsonValue (x); };

        obj.set ("engine", JsonValue (code (inst.engine)));
        obj.set ("lock", JsonValue (inst.lock));
        obj.set ("motion", JsonValue (code (inst.motion)));
        obj.set ("speed", num2 (inst.speed));
        obj.set ("heads", num2 (inst.heads));
        obj.set ("spread", num2 (inst.spread));
        obj.set ("grainVoice", num2 (inst.grainVoice));
        obj.set ("grainCloud", num2 (inst.grainCloud));
        obj.set ("grainLock", num2 (inst.grainLock));

        auto refresh = JsonValue::object();
        refresh.set ("mode", JsonValue (code (inst.refresh.mode)));
        refresh.set ("ms", num2 (inst.refresh.ms));
        refresh.set ("rate", JsonValue (rateCode (inst.refresh.rate)));
        refresh.set ("lockToBeat", JsonValue (inst.refresh.lockToBeat));
        refresh.set ("skip", num2 (inst.refresh.skip));
        obj.set ("refresh", std::move (refresh));

        obj.set ("spray", num2 (inst.spray));
        obj.set ("pitch", num2 (inst.pitch));
        obj.set ("tape", JsonValue (inst.tape));
        obj.set ("rev", JsonValue (inst.rev));
        obj.set ("drift", num2 (inst.drift));
        obj.set ("headTune", JsonValue (code (inst.headTune)));

        auto region = JsonValue::object();
        region.set ("start", num2 (inst.region.start));
        region.set ("end", num2 (inst.region.end));
        region.set ("atEnd", JsonValue (code (inst.region.atEnd)));
        region.set ("xfadeMs", num2 (inst.region.xfadeMs));
        obj.set ("region", std::move (region));

        obj.set ("jump", JsonValue (rateCode (inst.jump)));

        auto macros = JsonValue::array();
        for (const double m : inst.macros)
            macros.push (num2 (m));
        obj.set ("macros", std::move (macros));

        auto offsets = JsonValue::object();
        offsets.set ("amp", num2 (inst.offsets.amp));
        offsets.set ("vib", num2 (inst.offsets.vib));
        offsets.set ("tone", num2 (inst.offsets.tone));
        offsets.set ("formant", num2 (inst.offsets.formant));
        offsets.set ("hollow", num2 (inst.offsets.hollow));
        obj.set ("offsets", std::move (offsets));

        obj.set ("glideMode", JsonValue (code (inst.glideMode)));
        obj.set ("glideTime", num2 (inst.glideTime));
        obj.set ("vibRate", num2 (inst.vibRate));
        obj.set ("root", num2 (inst.root));
        obj.set ("cents", num2 (inst.cents));
        obj.set ("key", JsonValue (inst.key));
        obj.set ("scale", JsonValue (kScales[inst.scale < kNumScales ? inst.scale : 0].name));
        obj.set ("scaleLock", JsonValue (inst.scaleLock));
        obj.set ("velSens", num2 (inst.velSens));
        obj.set ("polyphony", num2 (inst.polyphony));
        obj.set ("outGain", num2 (inst.outGain));
        obj.set ("bendRange", num2 (inst.bendRange));

        const char* const wheelNames[2] = { "mod1", "mod2" };
        const ModWheel* const wheels[2] = { &inst.mod1, &inst.mod2 };
        for (int w = 0; w < 2; ++w)
        {
            const char* name = wheelNames[w];
            const ModWheel* m = wheels[w];
            auto wheel = JsonValue::object();
            wheel.set ("target", JsonValue (m->target == kModOff || m->target < 0 || m->target >= kNumLanes
                                                ? "off" : kLanes[m->target].code));
            wheel.set ("cc", JsonValue (m->cc));
            obj.set (name, std::move (wheel));
        }

        obj.set ("liveLegato", JsonValue (inst.liveLegato));
        obj.set ("seed", JsonValue (static_cast<double> (inst.seed)));
        return obj;
    }
}
