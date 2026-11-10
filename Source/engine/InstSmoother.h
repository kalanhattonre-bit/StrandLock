#pragma once

// The parameter smoothing classes of PLUGIN-PLAN 2.3, on the engine's control grid (1.8).
//
//  - none and engine: the new value applies at once (setTarget), so the voices read it at their next tick,
//    exactly as the prototype's setInst. (Engine-class values are made click-free by the prototype's own slews.)
//  - ramp20ms (the C++ addition): the value moves linearly to its target over the ramp time, one step per
//    32-sample boundary of the control grid (the engine calls step () there, inside process, so the block size
//    never matters). The fields: macros 1, 7 and 8, the five offsets, outGain, speed, drift, pitch, rootCents
//    (docs/parameters.json `smoothing`), and the host bypass fade (parameter 49), which the engine's output loop
//    applies per sample.
//
// A value that does not change never ramps. A target that changes during a ramp starts a new ramp from where the
// value is. With a ramp time of 0 every value jumps at setTarget (the parity tests: Engine::setRampMs (0)). snap ()
// puts every value on its target (prepareToPlay, reset, a state load and the first block).

#include "core/Inst.h"
#include "engine/EngineConfig.h"

#include <cmath>

namespace sl::engine
{
    class InstSmoother
    {
    public:
        /** The fields that ramp, in parameter order. */
        enum Field : int
        {
            macro1 = 0, macro7, macro8, ampOffset, vibOffset, toneOffset, formantOffset, hollowOffset, outGain, speed, drift, pitch, cents,
            bypass,   // the bypass fade: 1 = sound, 0 = bypassed (not an Inst field)
            numFields
        };

        /** Nothing set yet: the first target of every field applies at once; the bypass fade starts at 1 (sound). */
        InstSmoother() noexcept
        {
            r[bypass].value = r[bypass].target = 1;
            r[bypass].set = true;
        }

        /** The control rate and ramp time; recomputes the steps a ramp takes. Values are kept. */
        void prepare (double sampleRate, double rampMs) noexcept
        {
            sr = sampleRate > 0 && std::isfinite (sampleRate) ? sampleRate : kDefaultSampleRate;
            ms = rampMs > 0 && std::isfinite (rampMs) ? rampMs : 0;
            const double steps = ms * 0.001 * sr / kCtrl;
            rampSteps = ms > 0 ? (steps < 1 ? 1 : static_cast<int> (std::lround (steps))) : 0;
        }

        /** The ramp time alone (Engine::setRampMs). */
        void setRampMs (double rampMs) noexcept { prepare (sr, rampMs); }

        /** Steps a whole ramp takes (0 = jump). */
        int stepsPerRamp() const noexcept { return rampSteps; }

        /** The instrument the voices should read. Every field that is not of the ramp class takes the target's
            value now; a ramp-class field whose target changed starts a ramp (or jumps, with a ramp time of 0). */
        void setTarget (const core::Inst& next) noexcept
        {
            tgt = next;
            cur = next;
            for (int f = 0; f < bypass; ++f)
                retarget (r[f], *field (tgt, f));
            writeRamps();
        }

        /** The bypass fade's target: false = sound (1), true = bypassed (0). */
        void setBypass (bool bypassed) noexcept { retarget (r[bypass], bypassed ? 0.0 : 1.0); }

        /** One boundary of the control grid: every ramp moves one step. */
        void step() noexcept
        {
            bool any = false;
            for (Ramp& x : r)
                if (x.left > 0)
                {
                    --x.left;
                    x.value = x.left == 0 ? x.target : x.value + x.inc;
                    any = true;
                }
            if (any)
                writeRamps();
        }

        /** Every value on its target now. */
        void snap() noexcept
        {
            for (Ramp& x : r)
            {
                x.value = x.target;
                x.left = 0;
                x.inc = 0;
            }
            writeRamps();
        }

        /** True while any value is still moving. */
        bool ramping() const noexcept
        {
            for (const Ramp& x : r)
                if (x.left > 0)
                    return true;
            return false;
        }

        /** What the voices read. */
        const core::Inst& current() const noexcept { return cur; }

        /** The last target given. */
        const core::Inst& target() const noexcept { return tgt; }

        /** A ramp field's value now (bypass included). */
        double value (Field f) const noexcept { return r[f].value; }

    private:
        struct Ramp
        {
            double value = 0, target = 0, inc = 0;
            int left = 0;
            bool set = false;
        };

        static double* field (core::Inst& i, int f) noexcept
        {
            switch (f)
            {
                case macro1:        return &i.macros[0];
                case macro7:        return &i.macros[6];
                case macro8:        return &i.macros[7];
                case ampOffset:     return &i.offsets.amp;
                case vibOffset:     return &i.offsets.vib;
                case toneOffset:    return &i.offsets.tone;
                case formantOffset: return &i.offsets.formant;
                case hollowOffset:  return &i.offsets.hollow;
                case outGain:       return &i.outGain;
                case speed:         return &i.speed;
                case drift:         return &i.drift;
                case pitch:         return &i.pitch;
                case cents:         return &i.cents;
                default:            return nullptr;
            }
        }

        void retarget (Ramp& x, double goal) noexcept
        {
            if (! x.set)
            {
                x.value = x.target = goal;   // the first value never ramps
                x.left = 0;
                x.set = true;
                return;
            }
            if (goal == x.target)
                return;   // a value that does not change never ramps
            x.target = goal;
            if (rampSteps <= 0)
            {
                x.value = goal;
                x.left = 0;
                x.inc = 0;
                return;
            }
            x.left = rampSteps;
            x.inc = (goal - x.value) / rampSteps;
        }

        void writeRamps() noexcept
        {
            for (int f = 0; f < bypass; ++f)
                *field (cur, f) = r[f].value;
        }

        double sr = kDefaultSampleRate;
        double ms = kRampMs;
        int rampSteps = 30;   // 20 ms at 48 kHz in 32-sample steps
        Ramp r[numFields] {};
        core::Inst tgt = core::defaultInst(), cur = core::defaultInst();
    };
}
