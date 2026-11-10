#include "ParamBridge.h"

#include "ParameterLayout.h"
#include "core/Music.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <string>

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

    //==============================================================================================
    std::vector<ParamChange> patchValues (const core::InstPatch& patch)
    {
        std::vector<ParamChange> out;
        auto put = [&out] (P p, double plain) { out.emplace_back (index (p), legalPlain (index (p), (float) plain)); };
        auto putIf = [&put] (P p, const std::optional<double>& v) { if (v) put (p, *v); };
        auto putFlag = [&put] (P p, const std::optional<bool>& v) { if (v) put (p, *v ? 1.0 : 0.0); };
        auto putEnum = [&put] (P p, const auto& v) { if (v) put (p, (double) static_cast<int> (*v)); };

        if (patch.macros)
            for (int m = 0; m < core::kNumMacros; ++m)
                put (static_cast<P> (index (P::macro1) + m), (*patch.macros)[(std::size_t) m]);
        putIf (P::ampOffset, patch.offsets.amp);
        putIf (P::vibOffset, patch.offsets.vib);
        putIf (P::toneOffset, patch.offsets.tone);
        putIf (P::formantOffset, patch.offsets.formant);
        putIf (P::hollowOffset, patch.offsets.hollow);
        putIf (P::outGain, patch.outGain);
        putEnum (P::engine, patch.engine);
        putFlag (P::lock, patch.lock);
        putEnum (P::motion, patch.motion);
        putIf (P::speed, patch.speed);
        putIf (P::heads, patch.heads);
        putIf (P::spread, patch.spread);
        putIf (P::grainVoice, patch.grainVoice);
        putIf (P::grainCloud, patch.grainCloud);
        putIf (P::grainLock, patch.grainLock);
        putIf (P::spray, patch.spray);
        putIf (P::drift, patch.drift);
        putIf (P::pitch, patch.pitch);
        putFlag (P::tape, patch.tape);
        putFlag (P::reverseGrains, patch.rev);
        putEnum (P::headTune, patch.headTune);
        putEnum (P::jump, patch.jump);
        putEnum (P::refreshMode, patch.refresh.mode);
        putIf (P::refreshMs, patch.refresh.ms);
        putEnum (P::refreshRate, patch.refresh.rate);
        putFlag (P::refreshBeatLock, patch.refresh.lockToBeat);
        putIf (P::refreshSkip, patch.refresh.skip);
        putIf (P::regionStart, patch.region.start);
        putIf (P::regionEnd, patch.region.end);
        putEnum (P::atEnd, patch.region.atEnd);
        putIf (P::loopXfade, patch.region.xfadeMs);
        putEnum (P::glideMode, patch.glideMode);
        putIf (P::glideTime, patch.glideTime);
        putIf (P::vibRate, patch.vibRate);
        if (patch.vibSync)
            put (P::vibSync, *patch.vibSync == core::kVibSyncFree ? 0.0 : (double) *patch.vibSync + 1.0);
        putIf (P::bendRange, patch.bendRange);
        putIf (P::velSens, patch.velSens);
        putIf (P::polyphony, patch.polyphony);
        putFlag (P::liveLegato, patch.liveLegato);
        putIf (P::rootNote, patch.root);
        putIf (P::rootCents, patch.cents);

        std::stable_sort (out.begin(), out.end(), [] (const ParamChange& a, const ParamChange& b) { return a.first < b.first; });
        return out;
    }

    std::vector<ParamChange> applyPatch (juce::AudioProcessorValueTreeState& apvts, const core::InstPatch& patch)
    {
        std::vector<ParamChange> changed;
        for (const auto& [i, v] : patchValues (patch))
        {
            auto* p = apvts.getParameter (kParams[i].id);
            if (p == nullptr || juce::exactlyEqual (plainValueOf (*p), v))
                continue;
            p->beginChangeGesture();
            setPlainValue (*p, i, v);
            p->endChangeGesture();
            changed.emplace_back (i, plainValueOf (*p));
        }
        return changed;
    }

    bool applyStateFields (state::InstState& s, const core::InstPatch& patch)
    {
        auto targetId = [] (std::int8_t lane)
        {
            return lane >= 0 && lane < core::kNumLanes ? std::string (core::kLanes[lane].code) : std::string ("off");
        };

        bool any = false;
        if (patch.seed)
        {
            s.seed = *patch.seed;
            any = true;
        }
        if (patch.mod1.target)
        {
            s.mod1Target = targetId (*patch.mod1.target);
            any = true;
        }
        if (patch.mod1.cc)
        {
            s.mod1Cc = static_cast<std::uint8_t> (std::clamp (*patch.mod1.cc, 0, 127));
            any = true;
        }
        if (patch.mod2.target)
        {
            s.mod2Target = targetId (*patch.mod2.target);
            any = true;
        }
        if (patch.mod2.cc)
        {
            s.mod2Cc = static_cast<std::uint8_t> (std::clamp (*patch.mod2.cc, 0, 127));
            any = true;
        }
        if (patch.key)
        {
            s.key = static_cast<std::uint8_t> (std::clamp (*patch.key, 0, 11));
            any = true;
        }
        if (patch.scale)
        {
            s.scale = core::kScales[*patch.scale < core::kNumScales ? *patch.scale : 0].name;
            any = true;
        }
        if (patch.scaleLock)
        {
            s.scaleLock = *patch.scaleLock ? 1 : 0;
            any = true;
        }
        return any;
    }
}
