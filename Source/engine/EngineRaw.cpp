// The raw audition (M7 V4; PLUGIN-PLAN 7.9 decision 7a): holding the mouse on the waveform outside the Region plays
// the sound as recorded from that spot, as sl-ui.js rawAuditionStart does with a buffer source: at the sound's own rate,
// through a gain that rises from 0 to 0.7 over 12 ms, and on release falls from where it is to 0 over 30 ms. The editor
// sends it through the command queue (doc/CommandQueue.h); the processor calls rawAuditionOn and rawAuditionOff.
//
// A few fixed players, so a second hold can start while the first fades: no allocation. Each keeps the sound it
// started on, and its epoch, until it is done (minEpochInUse, forEachVersionRead), as a voice stolen by a sample swap
// does. The output is added after the engine's output stage, as the prototype's source goes straight to its master:
// outGain and the limiter leave it alone; the bypass fade applies, so a bypassed StrandLock is silent.

#include "engine/Engine.h"

#include "engine/EngineMath.h"
#include "engine/OctaveLevels.h"
#include "engine/SourceRead.h"

#include <cmath>

namespace sl::engine
{
    namespace
    {
        /** A player's envelope now (0 .. 1): rising over the attack while held, falling over the release once let go. */
        double envelopeOf (double age, bool held, double relEnv, double relAge, double attack, double release) noexcept
        {
            if (held)
                return age < attack ? age / attack : 1.0;
            return relAge < release ? relEnv * (1.0 - relAge / release) : 0.0;
        }
    }

    void Engine::rawAuditionOn (double pos) noexcept
    {
        rawAuditionOff();   // sl-ui.js stripDown: stopAudition () before rawAuditionStart
        if (src == nullptr || src->n < 1)
            return;
        const double attack = kRawFadeInSeconds * sr, release = kRawFadeOutSeconds * sr;
        RawPlayer* p = nullptr;
        for (RawPlayer& r : raw)
            if (r.data == nullptr)
            {
                p = &r;
                break;
            }
        if (p == nullptr)
        {   // every player busy (four holds within 30 ms): the quietest gives way
            p = &raw[0];
            double quietest = 2.0;
            for (RawPlayer& r : raw)
            {
                const double e = envelopeOf (r.age, r.held, r.relEnv, r.relAge, attack, release);
                if (e < quietest)
                {
                    quietest = e;
                    p = &r;
                }
            }
        }
        const double n = static_cast<double> (src->n);
        const double at = std::floor (clampJs (fin (pos, 0), 0, 1) * n);
        *p = RawPlayer();
        p->data = src;
        p->epoch = srcEpoch;
        p->pos = at < n - 1 ? at : n - 1;
        p->step = source.sr / sr;   // as recorded: the sound's frames per second over the engine's samples per second
        p->level = ctx.levels ? levelFor (p->step, src->numLevels) : 0;
        p->held = true;
        p->serial = ++rawSerial;
    }

    void Engine::rawAuditionOff() noexcept
    {
        const double attack = kRawFadeInSeconds * sr, release = kRawFadeOutSeconds * sr;
        for (RawPlayer& r : raw)
        {
            if (r.data == nullptr || ! r.held)
                continue;
            r.relEnv = envelopeOf (r.age, true, 0.0, 0.0, attack, release);
            r.relAge = 0;
            r.held = false;
            if (! (r.relEnv > 0))
                r.data = nullptr;   // let go before it was heard
        }
    }

    void Engine::stopRaw() noexcept
    {
        for (RawPlayer& r : raw)
            r = RawPlayer();
    }

    void Engine::renderRaw (float* L, float* R, int from, int count, int gridPos, double byp0, double byp1) noexcept
    {
        const double attack = kRawFadeInSeconds * sr, release = kRawFadeOutSeconds * sr;
        for (RawPlayer& r : raw)
        {
            if (r.data == nullptr)
                continue;
            const SampleData& S = *r.data;
            const double last = static_cast<double> (S.n - 1);
            for (int j = 0; j < count; ++j)
            {
                // the sound's end stops it, as the prototype's buffer source; the release's end does too (silence from
                // 30 ms after the release on)
                if (r.pos > last || (! r.held && r.relAge >= release))
                {
                    r.data = nullptr;
                    break;
                }
                const double env = envelopeOf (r.age, r.held, r.relEnv, r.relAge, attack, release);
                double l = 0, rr = 0;
                if (r.level > 0)
                    readLevel (S, r.level, r.pos, l, rr);
                else
                    readAbs (S, r.pos, l, rr);
                const double c = static_cast<double> (gridPos + j + 1) / kCtrl;
                const double bp = byp1 == byp0 ? byp0 : byp0 + (byp1 - byp0) * c;
                const double g = kRawGain * env * bp;
                const int o = from + j;
                L[o] = static_cast<float> (L[o] + l * g);
                R[o] = static_cast<float> (R[o] + rr * g);
                r.pos += r.step;
                r.age += 1;
                if (! r.held)
                    r.relAge += 1;
            }
        }
    }
}
