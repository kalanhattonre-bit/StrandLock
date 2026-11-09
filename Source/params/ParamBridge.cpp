#include "ParamBridge.h"

#include "ParameterLayout.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace sl::params
{
    ParameterRefs makeParameterRefs (juce::AudioProcessorValueTreeState& apvts)
    {
        ParameterRefs refs;
        for (int i = 0; i < kNumParams; ++i)
        {
            auto* p = apvts.getParameter (kParams[i].id);
            if (auto* f = dynamic_cast<FloatParameter*> (p))
                refs.values[(std::size_t) i] = &f->plainValue();
            else
                refs.values[(std::size_t) i] = apvts.getRawParameterValue (kParams[i].id);
            jassert (refs.values[(std::size_t) i] != nullptr);
        }
        return refs;
    }

    namespace
    {
        struct Reader
        {
            const ParameterRefs& refs;

            /** The plain value, or the parameter's default when it is not finite (or missing). */
            double value (P p) const noexcept
            {
                const auto* a = refs.values[(std::size_t) index (p)];
                const float v = a != nullptr ? a->load (std::memory_order_relaxed) : spec (p).def;
                return std::isfinite (v) ? (double) v : (double) spec (p).def;
            }

            /** A choice's index (rounded, in its list; else the default). */
            int choice (P p) const noexcept
            {
                const double v = value (p);
                const long r = std::lround (v);
                const ParamSpec& s = spec (p);
                return r >= 0 && r < s.numChoices ? (int) r : (int) s.def;
            }

            bool toggle (P p) const noexcept { return value (p) >= 0.5; }

            double whole (P p) const noexcept { return (double) std::lround (value (p)); }
        };

        /** The exact edge while the parameter still holds (float) of it, else the parameter's value. */
        double regionEdge (double parameter, double exact) noexcept
        {
            return std::isfinite (exact) && juce::exactlyEqual ((float) exact, (float) parameter) ? exact : parameter;
        }
    }

    core::Inst readInst (const ParameterRefs& refs, const NonParamAtomics& state) noexcept
    {
        const Reader r { refs };
        core::Inst inst = core::defaultInst();

        for (int m = 0; m < core::kNumMacros; ++m)
            inst.macros[(std::size_t) m] = r.value (static_cast<P> (index (P::macro1) + m));

        inst.offsets.amp = r.value (P::ampOffset);
        inst.offsets.vib = r.value (P::vibOffset);
        inst.offsets.tone = r.value (P::toneOffset);
        inst.offsets.formant = r.value (P::formantOffset);
        inst.offsets.hollow = r.value (P::hollowOffset);
        inst.outGain = r.value (P::outGain);

        inst.engine = static_cast<core::Engine> (r.choice (P::engine));
        inst.lock = r.toggle (P::lock);
        inst.motion = static_cast<core::Motion> (r.choice (P::motion));
        inst.speed = r.value (P::speed);
        inst.heads = r.value (P::heads);
        inst.spread = r.value (P::spread);
        inst.grainVoice = r.value (P::grainVoice);
        inst.grainCloud = r.value (P::grainCloud);
        inst.grainLock = r.value (P::grainLock);
        inst.spray = r.value (P::spray);
        inst.drift = r.value (P::drift);
        inst.pitch = r.value (P::pitch);
        inst.tape = r.toggle (P::tape);
        inst.rev = r.toggle (P::reverseGrains);
        inst.headTune = static_cast<core::HeadTune> (r.choice (P::headTune));
        inst.jump = static_cast<core::RateId> (r.choice (P::jump));

        inst.refresh.mode = static_cast<core::RefreshMode> (r.choice (P::refreshMode));
        inst.refresh.ms = r.value (P::refreshMs);
        inst.refresh.rate = static_cast<core::RateId> (r.choice (P::refreshRate));
        inst.refresh.lockToBeat = r.toggle (P::refreshBeatLock);
        inst.refresh.skip = r.value (P::refreshSkip);

        inst.region.start = regionEdge (r.value (P::regionStart), state.regionStartExact.load (std::memory_order_relaxed));
        inst.region.end = regionEdge (r.value (P::regionEnd), state.regionEndExact.load (std::memory_order_relaxed));
        inst.region.atEnd = static_cast<core::AtEnd> (r.choice (P::atEnd));
        inst.region.xfadeMs = r.value (P::loopXfade);

        inst.glideMode = static_cast<core::GlideMode> (r.choice (P::glideMode));
        inst.glideTime = r.value (P::glideTime);
        inst.vibRate = r.value (P::vibRate);
        const int sync = r.choice (P::vibSync);
        inst.vibSync = sync == 0 ? core::kVibSyncFree : static_cast<std::int8_t> (sync - 1);
        inst.bendRange = r.whole (P::bendRange);
        inst.velSens = r.value (P::velSens);
        inst.polyphony = r.whole (P::polyphony);
        inst.liveLegato = r.toggle (P::liveLegato);
        inst.root = r.whole (P::rootNote);
        inst.cents = r.value (P::rootCents);

        // The INST chunk's fields (PLUGIN-PLAN 2.2).
        inst.seed = state.seed.load (std::memory_order_relaxed);
        inst.mod1.target = state.mod1Target.load (std::memory_order_relaxed);
        inst.mod1.cc = state.mod1Cc.load (std::memory_order_relaxed);
        inst.mod2.target = state.mod2Target.load (std::memory_order_relaxed);
        inst.mod2.cc = state.mod2Cc.load (std::memory_order_relaxed);
        inst.key = state.key.load (std::memory_order_relaxed);
        inst.scale = state.scale.load (std::memory_order_relaxed);
        inst.scaleLock = state.scaleLock.load (std::memory_order_relaxed);

        return core::clampForEngine (inst);
    }

    bool readBypass (const ParameterRefs& refs) noexcept
    {
        return Reader { refs }.toggle (P::bypass);
    }
}
