#include "LaneFormat.h"

#include "Num.h"
#include "core/JsCompat.h"
#include "core/Rates.h"
#include "core/Travel.h"

#include <cmath>
#include <limits>

namespace sl::model
{
    LaneCtx formatCtx (const core::Inst& inst, double bpm, const Note* note) noexcept
    {
        LaneCtx c;
        c.inst = inst;
        c.bpm = bpm;
        c.travelKind = note != nullptr && note->sample.travel == TravelKind::path ? TravelKind::path : TravelKind::speed;
        return c;
    }

    GrainBase grainBase (const core::Inst& inst) noexcept
    {
        if (inst.engine == core::Engine::voice)
            return { inst.grainVoice, GrainUnit::cycles };
        if (inst.engine == core::Engine::cloud)
            return inst.lock ? GrainBase { inst.grainLock, GrainUnit::wavelengths } : GrainBase { inst.grainCloud, GrainUnit::ms };
        return { 1.0, GrainUnit::x };
    }

    namespace
    {
        const char* unitName (GrainUnit u) noexcept
        {
            switch (u)
            {
                case GrainUnit::cycles:      return "cycles";
                case GrainUnit::ms:          return "ms";
                case GrainUnit::wavelengths: return "wavelengths";
                case GrainUnit::x:           return "x";
            }
            return "x";
        }

        std::string str (double x) { return core::jsNumberToString (x); }

        double ctxBpm (const LaneCtx& c) noexcept { return clamp (num (c.bpm, 120.0), 1.0, 1000.0); }
    }

    double mapLane (core::LaneId lane, double v, const LaneCtx& c)
    {
        if (! std::isfinite (v))
            v = core::laneDef (lane);   // a NaN node reads as the lane default (never prints NaN)

        switch (lane)
        {
            case core::LaneId::pitch:   return (v - 0.5) * 24.0;
            case core::LaneId::amp:     return v * v * 1.3;
            case core::LaneId::vib:     return v * 1.0;
            case core::LaneId::tone:    return 60.0 * std::pow (2.0, v * 8.3);
            case core::LaneId::formant: return (v - 0.5) * 24.0;
            case core::LaneId::hollow:  return -v * 28.0;
            // speed: read the Float32 value the engine's table will hold, so the dead-band edges agree with it
            case core::LaneId::travel:  return c.travelKind == TravelKind::path ? clamp (v, 0.0, 1.0) : core::travelSpeed (core::f32 (v));
            case core::LaneId::heads:
            case core::LaneId::grain:
            case core::LaneId::refresh:
                break;
        }

        const core::Inst inst = core::normalizeInst (c.inst);
        switch (lane)
        {
            case core::LaneId::heads:
                return clamp (inst.heads + (v - 0.5) * 14.0, 1.0, 8.0);
            case core::LaneId::grain:
                return grainBase (inst).base * std::pow (2.0, (v - 0.5) * 4.0);
            case core::LaneId::refresh:
            {
                // read the Float32 value the engine's table will hold, as for travel
                const double vf = core::f32 (v);
                if (core::refreshSmooth (vf) || inst.refresh.mode == core::RefreshMode::smooth)
                    return 0.0;
                const double bpm = ctxBpm (c);
                const double baseMs = inst.refresh.mode == core::RefreshMode::sync ? core::rateBeats (inst.refresh.rate) * 60000.0 / bpm
                                                                                    : inst.refresh.ms;
                return baseMs / std::pow (2.0, (vf - 0.5) * 6.0);
            }
            case core::LaneId::pitch:
            case core::LaneId::amp:
            case core::LaneId::vib:
            case core::LaneId::tone:
            case core::LaneId::formant:
            case core::LaneId::hollow:
            case core::LaneId::travel:
                break;
        }
        return std::numeric_limits<double>::quiet_NaN();   // the prototype throws for an unknown lane
    }

    std::string trimNum (std::string s)
    {
        if (s.find ('.') == std::string::npos)
            return s;
        while (! s.empty() && s.back() == '0')
            s.pop_back();
        if (! s.empty() && s.back() == '.')
            s.pop_back();
        return s;
    }

    std::string fmtMag (double x)
    {
        if (x >= 10.0)
            return core::jsToFixed (x, 0);
        if (x >= 1.0)
            return trimNum (core::jsToFixed (x, 1));
        return trimNum (core::jsToFixed (x, 2));
    }

    std::string fmtSemis (double x)
    {
        const double r = core::jsRound (x * 10.0) / 10.0;
        if (std::fabs (r) < 0.05)
            return "0 st";
        const double a = std::fabs (r);
        return std::string (r > 0.0 ? "+" : "-") + (isInteger (r) ? str (a) : core::jsToFixed (a, 1)) + " st";
    }

    std::string fmtDb (double db)
    {
        if (! std::isfinite (db))
            return "-inf dB";
        const double r = core::jsRound (db * 10.0) / 10.0;
        return std::string (r > 0.0 ? "+" : r < 0.0 ? "-" : "") + core::jsToFixed (std::fabs (r), 1) + " dB";
    }

    std::string formatLane (core::LaneId lane, double v, const LaneCtx& c)
    {
        if (! std::isfinite (v))
            v = core::laneDef (lane);
        const double x = mapLane (lane, v, c);

        switch (lane)
        {
            case core::LaneId::pitch:
            case core::LaneId::formant:
                return fmtSemis (x);
            case core::LaneId::amp:
                return fmtDb (x > 0.0 ? 20.0 * std::log10 (x) : -std::numeric_limits<double>::infinity());
            case core::LaneId::vib:
                return core::jsToFixed (x, 2) + " st vibrato";
            case core::LaneId::tone:
                return x >= 1000.0 ? core::jsToFixed (x / 1000.0, 1) + " kHz" : str (core::jsRound (x)) + " Hz";
            case core::LaneId::hollow:
                return fmtDb (x);
            case core::LaneId::travel:
                if (c.travelKind == TravelKind::path)
                    return str (core::jsRound (x * 100.0)) + "% of the Region";
                if (x == 0.0)
                    return "stopped";
                return fmtMag (std::fabs (x)) + "x " + (x > 0.0 ? "forwards" : "backwards");
            case core::LaneId::heads:
            {
                const double r = core::jsRound (x * 10.0) / 10.0;
                return (isInteger (r) ? str (r) : core::jsToFixed (r, 1)) + (r == 1.0 ? " head" : " heads");
            }
            case core::LaneId::grain:
            {
                const GrainBase g = grainBase (core::normalizeInst (c.inst));
                if (g.unit == GrainUnit::x)
                    return "Grain x" + fmtMag (x) + " (no grains in Tape)";
                if (g.unit == GrainUnit::ms)
                    return str (core::jsRound (x)) + " ms";
                return trimNum (core::jsToFixed (x, 1)) + " " + unitName (g.unit);
            }
            case core::LaneId::refresh:
            {
                if (x == 0.0)
                    return "Smooth";
                const core::Inst inst = core::normalizeInst (c.inst);
                const double ms = core::jsRound (x);
                if (inst.refresh.mode == core::RefreshMode::sync)
                {
                    const double bpm = ctxBpm (c);
                    const double beats = x * bpm / 60000.0;
                    const auto id = core::rateFromBeats (beats, 0.01);
                    return "Refresh " + (id.has_value() ? std::string (core::rateCode (*id)) : trimNum (core::jsToFixed (beats, 3)) + " beat")
                         + " (" + str (ms) + " ms at " + str (core::jsRound (bpm)) + " bpm)";
                }
                return "Refresh " + str (ms) + " ms";
            }
        }
        return str (x);
    }
}
