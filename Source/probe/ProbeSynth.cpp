#include "ProbeSynth.h"

#include <algorithm>
#include <cmath>

namespace sl::probe
{
    namespace
    {
        constexpr double kAttackSeconds = 0.008;
        constexpr double kReleaseSeconds = 0.060;
        constexpr double kStealSeconds = 0.005;
        constexpr double kRampSeconds = 0.020;
        constexpr double kLevel = 0.8 * 0.8;   // the Volume lane's default 0.8, squared, as the engine plays it
        constexpr double kCeiling = 0.9;
        constexpr double kClamp = 0.98;

        /** 4-point Hermite at frame i + f over a looping sound (the indices wrap at n), as readAbs computes it. */
        inline double hermite (const float* x, std::size_t n, std::size_t i, double f) noexcept
        {
            const std::size_t i0 = i == 0 ? n - 1 : i - 1;
            const std::size_t i2 = i + 1 >= n ? i + 1 - n : i + 1;
            const std::size_t i3 = i + 2 >= n ? i + 2 - n : i + 2;
            const double a = x[i0], b = x[i], c = x[i2], e = x[i3];
            return (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2.0 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
        }
    }

    void ProbeSynth::prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
        limRel = 1.0 - std::exp (-1.0 / (0.08 * sr));
        reset();
    }

    void ProbeSynth::reset() noexcept
    {
        for (auto& v : voices)
            v = Voice {};
        sustainDown = false;
        limG = 1.0;
        gainNow = gainTarget;
        gainSteps = 0;
        bypassNow = bypassTarget;
        firstBlock = true;
    }

    void ProbeSynth::setSound (const engine::SampleData* s) noexcept
    {
        if (s == sound)
            return;
        for (auto& v : voices)
            v.stage = Stage::off;
        sound = s;
    }

    void ProbeSynth::setInst (const core::Inst& inst) noexcept
    {
        velSens = inst.velSens;
        polyphony = std::clamp (static_cast<int> (inst.polyphony), 1, kMaxPolyphony);

        const double newRoot = inst.root + inst.cents / 100.0;
        if (newRoot != root || inst.pitch != pitch || inst.bendRange != bendRange)
        {
            root = newRoot;
            pitch = inst.pitch;
            bendRange = inst.bendRange;
            updateRates();
        }

        const double target = std::pow (10.0, inst.outGain / 20.0);
        if (firstBlock)
        {
            gainTarget = gainNow = target;
            gainSteps = 0;
        }
        else if (target != gainTarget)
        {
            gainTarget = target;
            gainSteps = std::max (1, static_cast<int> (std::lround (kRampSeconds * sr)));
            gainStep = (gainTarget - gainNow) / gainSteps;
        }
    }

    void ProbeSynth::setBypass (bool bypassed) noexcept
    {
        bypassTarget = bypassed ? 0.0 : 1.0;
        if (firstBlock)
            bypassNow = bypassTarget;
    }

    double ProbeSynth::rateFor (int note) const noexcept
    {
        const double soundRate = sound != nullptr && sound->sr > 0.0 ? sound->sr : sounds::kStarterSampleRate;
        const double semis = static_cast<double> (note) - root + bend * bendRange + pitch;
        const double r = std::pow (2.0, semis / 12.0) * soundRate / sr;
        return std::isfinite (r) ? r : 1.0;
    }

    void ProbeSynth::updateRates() noexcept
    {
        for (auto& v : voices)
            if (v.stage != Stage::off)
                v.rate = rateFor (v.note);
    }

    int ProbeSynth::activeVoices() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            if (v.stage != Stage::off)
                ++n;
        return n;
    }

    void ProbeSynth::startVoice (Voice& v, int note, double velocity) noexcept
    {
        v.stage = Stage::attack;
        v.note = note;
        v.keyDown = true;
        v.sustained = false;
        v.pos = 0.0;
        v.rate = rateFor (note);
        const double vel = std::clamp (velocity, 0.0, 1.0);
        v.gain = (1.0 - velSens * (1.0 - vel)) * kLevel;
        v.env = 0.0;
        v.envStep = 1.0 / (kAttackSeconds * sr);
        v.age = nextAge++;
    }

    void ProbeSynth::releaseVoice (Voice& v) noexcept
    {
        if (v.stage == Stage::off || v.stage == Stage::release || v.stage == Stage::steal)
            return;
        v.keyDown = false;
        v.sustained = false;
        v.stage = Stage::release;
        v.envStep = v.env / (kReleaseSeconds * sr);
        if (! (v.envStep > 0.0))
            v.stage = Stage::off;
    }

    void ProbeSynth::stealVoice (Voice& v) noexcept
    {
        if (v.stage == Stage::off || v.stage == Stage::steal)
            return;
        v.keyDown = false;
        v.sustained = false;
        v.stage = Stage::steal;
        v.envStep = v.env / (kStealSeconds * sr);
        if (! (v.envStep > 0.0))
            v.stage = Stage::off;
    }

    void ProbeSynth::noteOn (int note, int velocity) noexcept
    {
        if (velocity <= 0)
        {
            noteOff (note);
            return;
        }

        // The same key again: the old one lets go first.
        for (auto& v : voices)
            if (v.note == note && v.keyDown && v.stage != Stage::off)
                releaseVoice (v);

        // Room for one more sounding voice: steal the oldest when the polyphony is full.
        int sounding = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
        {
            if (v.stage == Stage::off || v.stage == Stage::steal)
                continue;
            ++sounding;
            if (oldest == nullptr || v.age < oldest->age)
                oldest = &v;
        }
        if (sounding >= polyphony && oldest != nullptr)
        {
            stealVoice (*oldest);
            ++st.steals;
            --sounding;
        }

        Voice* slot = nullptr;
        for (auto& v : voices)
            if (v.stage == Stage::off)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr)
        {
            // Every slot holds a tail: reuse the quietest.
            slot = &voices[0];
            for (auto& v : voices)
                if (v.env < slot->env)
                    slot = &v;
        }

        startVoice (*slot, note, static_cast<double> (velocity) / 127.0);
        st.maxSounding = std::max (st.maxSounding, sounding + 1);
    }

    void ProbeSynth::noteOff (int note) noexcept
    {
        for (auto& v : voices)
        {
            if (v.note != note || ! v.keyDown || v.stage == Stage::off || v.stage == Stage::steal || v.stage == Stage::release)
                continue;
            if (sustainDown)
            {
                v.keyDown = false;
                v.sustained = true;
            }
            else
            {
                releaseVoice (v);
            }
        }
    }

    void ProbeSynth::sustain (bool down) noexcept
    {
        sustainDown = down;
        if (down)
            return;
        for (auto& v : voices)
            if (v.sustained && ! v.keyDown)
                releaseVoice (v);
    }

    void ProbeSynth::allNotesOff() noexcept
    {
        sustainDown = false;
        for (auto& v : voices)
            releaseVoice (v);
    }

    void ProbeSynth::allSoundOff() noexcept
    {
        sustainDown = false;
        for (auto& v : voices)
            stealVoice (v);
    }

    void ProbeSynth::pitchBend (int value14) noexcept
    {
        // Below centre over 8192 steps, above it over 8191, so full up is exactly +1.
        const int v = std::clamp (value14, 0, 16383) - 8192;
        bend = v < 0 ? v / 8192.0 : v / 8191.0;
        updateRates();
    }

    void ProbeSynth::render (float* left, float* right, int n) noexcept
    {
        const bool playable = sound != nullptr && sound->n >= 4 && sound->L != nullptr && sound->R != nullptr;
        const float* srcL = playable ? sound->L : nullptr;
        const float* srcR = playable ? sound->R : nullptr;
        const std::size_t frames = playable ? static_cast<std::size_t> (sound->n) : 0;
        const double framesD = static_cast<double> (frames);
        const double bypassStep = 1.0 / (kRampSeconds * sr);

        for (int j = 0; j < n; ++j)
        {
            double l = 0.0, r = 0.0;
            if (playable)
            {
                for (auto& v : voices)
                {
                    if (v.stage == Stage::off)
                        continue;

                    const auto i = static_cast<std::size_t> (v.pos);
                    const double f = v.pos - static_cast<double> (i);
                    const double g = v.gain * v.env;
                    l += hermite (srcL, frames, i, f) * g;
                    r += hermite (srcR, frames, i, f) * g;

                    v.pos += v.rate;
                    if (v.pos >= framesD)
                        v.pos = v.pos < 2.0 * framesD ? v.pos - framesD : std::fmod (v.pos, framesD);
                    if (! (v.pos >= 0.0 && v.pos < framesD))
                        v.pos = 0.0;

                    switch (v.stage)
                    {
                        case Stage::attack:
                            v.env += v.envStep;
                            if (v.env >= 1.0)
                            {
                                v.env = 1.0;
                                v.stage = Stage::hold;
                            }
                            break;
                        case Stage::release:
                        case Stage::steal:
                            v.env -= v.envStep;
                            if (v.env <= 0.0)
                            {
                                v.env = 0.0;
                                v.stage = Stage::off;
                            }
                            break;
                        case Stage::hold:
                        case Stage::off:
                            break;
                    }
                }
            }

            // Output Gain, ramped per sample.
            if (gainSteps > 0)
            {
                gainNow += gainStep;
                if (--gainSteps == 0)
                    gainNow = gainTarget;
            }
            l *= gainNow;
            r *= gainNow;

            // The output guard: a NaN, an infinity or a runaway sample becomes silence, and is counted.
            if (! (l > -1e6 && l < 1e6))
            {
                l = 0.0;
                ++st.nonFinite;
            }
            if (! (r > -1e6 && r < 1e6))
            {
                r = 0.0;
                ++st.nonFinite;
            }

            // The engine's limiter: instant attack, 80 ms release, ceiling 0.9.
            const double al = l < 0.0 ? -l : l, ar = r < 0.0 ? -r : r, pk = al > ar ? al : ar;
            st.preLimiterPeak = std::max (st.preLimiterPeak, pk);
            const double tg = pk > kCeiling ? kCeiling / pk : 1.0;
            if (tg < limG)
                limG = tg;
            else
                limG += (tg - limG) * limRel;
            l *= limG;
            r *= limG;

            // The host bypass fades the output over 20 ms while the voices keep running.
            if (bypassNow < bypassTarget)
                bypassNow = std::min (bypassTarget, bypassNow + bypassStep);
            else if (bypassNow > bypassTarget)
                bypassNow = std::max (bypassTarget, bypassNow - bypassStep);
            l *= bypassNow;
            r *= bypassNow;

            l = l > kClamp ? kClamp : l < -kClamp ? -kClamp : l;
            r = r > kClamp ? kClamp : r < -kClamp ? -kClamp : r;

            if (right != nullptr)
            {
                left[j] = static_cast<float> (l);
                right[j] = static_cast<float> (r);
            }
            else
            {
                left[j] = static_cast<float> (0.5 * (l + r));
            }
        }

        if (n > 0)
            firstBlock = false;
    }
}
