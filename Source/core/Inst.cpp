#include "Inst.h"

#include "JsCompat.h"
#include "Music.h"

#include <cmath>
#include <initializer_list>

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
}
