// Chase cost (PLUGIN-PLAN 1.8, 8.2 #22): a transport start inside 16 x 16-beat 8-head Tape notes (12 beats in), and
// inside 16 x 64-beat notes (60 beats in), at 48 kHz. Gated: no 32-sample stretch runs more than 4096 + 256 chase
// ticks (stats ().chaseTicks); every voice sounds within 0.35 s of Play; the chased playheads (head 1 of each voice)
// are within 0.002 of the file from where they would be had the notes played from their start. Printed: the time of
// the chase moment's block, and when the last voice came in. Also: the stepped chase is block-size independent.
// Group "engine".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include "core/Inst.h"
#include "core/Seeds.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    struct ChaseRun
    {
        std::int64_t worstStretch = 0, totalTicks = 0;
        double chaseBlockMs = 0;
        std::int64_t lastIn = -1;         // the first sample at which every voice was sounding
        std::map<std::uint32_t, float> pos;   // head 1 of each voice (by note id hash) at the probe sample
        int voicesAtProbe = 0;
        std::vector<float> L, R;
    };

    /** 16 notes C2 .. D#3 from beat 0, lengthBeats long, Play at chaseBeat, rendered in 32-sample blocks (each one
        stretch of the control grid) for `seconds`. The probe is at 0.35 s. */
    ChaseRun runChase (int bars, double lengthBeats, double chaseBeat, double seconds, int blockSize = 32)
    {
        ChaseRun out;
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        inst.heads = 8;
        inst.polyphony = 16;
        e->setInst (inst);
        const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
        auto sample = engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw");
        e->setSample (sample.get(), 1);
        model::Clip clip = model::newClip (bars);
        for (int i = 0; i < 16; ++i)
            clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (48 + i, 0, lengthBeats, "c" + std::to_string (i))));
        auto snap = render::compile (clip);
        e->setSnapshot (snap.get(), 1);

        const auto n = static_cast<std::int64_t> (seconds * 48000);
        const std::int64_t probe = static_cast<std::int64_t> (0.35 * 48000);
        out.L.assign (static_cast<std::size_t> (n), 0.0f);
        out.R.assign (static_cast<std::size_t> (n), 0.0f);
        engine::Transport t;
        t.playing = true;
        t.bpm = 120;
        t.beat = chaseBeat;
        e->setTransport (t);
        auto frame = std::make_unique<engine::DisplayFrame>();
        std::int64_t ticksBefore = 0;
        for (std::int64_t k = 0; k < n; k += blockSize)
        {
            const int len = static_cast<int> (std::min<std::int64_t> (blockSize, n - k));
            const auto t0 = std::chrono::steady_clock::now();
            e->process (out.L.data() + k, out.R.data() + k, len);
            const auto t1 = std::chrono::steady_clock::now();
            if (k == 0)
                out.chaseBlockMs = std::chrono::duration<double, std::milli> (t1 - t0).count();
            const std::int64_t ticks = e->stats().chaseTicks;
            out.worstStretch = std::max (out.worstStretch, ticks - ticksBefore);
            ticksBefore = ticks;
            e->fillDisplay (*frame);
            int sounding = 0;
            for (int v = 0; v < frame->numVoices; ++v)
                if (frame->voices[v].env > 0 && frame->voices[v].numHeads > 0)
                    ++sounding;
            if (out.lastIn < 0 && sounding == 16)
                out.lastIn = k + len;
            if (k + len == probe)
            {
                out.voicesAtProbe = sounding;
                for (int v = 0; v < frame->numVoices; ++v)
                    for (int h = 0; h < frame->voices[v].numHeads; ++h)
                        if (frame->voices[v].heads[h].index == 0)
                            out.pos[frame->voices[v].noteIdHash] = frame->voices[v].heads[h].pos;
            }
        }
        out.totalTicks = e->stats().chaseTicks;
        return out;
    }

    /** Where head 1 of a note started at song sample 0 is at song sample s, had it played from its start: a Tape head at
        1x moves 2^((p - 60) / 12) x 44100 / 48000 frames per sample from frame 0 (no lanes move, no vibrato, no drift),
        and the Region (the whole saw) loops. */
    double straightPos (int p, double s, double frames)
    {
        const double spd = std::pow (2.0, (p - 60) / 12.0) * (44100.0 / 48000.0);
        const double x = spd * s;
        return (x - frames * std::floor (x / frames)) / frames;
    }

    double circular (double a, double b)
    {
        double d = std::abs (a - b);
        d -= std::floor (d);
        return std::min (d, 1 - d);
    }

    void checkChase (const char* label, int bars, double lengthBeats, double chaseBeat)
    {
        const ChaseRun r = runChase (bars, lengthBeats, chaseBeat, 0.5);
        const double frames = static_cast<double> (sounds::makeStarterSound (sounds::StarterId::saw, 1).frames());
        check (r.worstStretch <= 4096 + 256,
               fmt ("%s: no 32-sample stretch runs more than 4096 + 256 chase ticks (the most %lld; %lld in all)", label,
                    static_cast<long long> (r.worstStretch), static_cast<long long> (r.totalTicks)));
        check (r.lastIn >= 0 && r.lastIn <= static_cast<std::int64_t> (0.35 * 48000) && r.voicesAtProbe == 16,
               fmt ("%s: every voice sounds within 0.35 s of Play (all 16 by %.3f s)", label, r.lastIn < 0 ? -1.0 : static_cast<double> (r.lastIn) / 48000));
        const double probeSong = chaseBeat * 24000 + 0.35 * 48000;
        double worst = 0;
        int found = 0;
        for (int i = 0; i < 16; ++i)
        {
            const auto it = r.pos.find (core::idHash (std::string_view ("c" + std::to_string (i))));
            if (it == r.pos.end())
                continue;
            ++found;
            worst = std::max (worst, circular (it->second, straightPos (48 + i, probeSong, frames)));
        }
        check (found == 16 && worst <= 0.002, fmt ("%s: the 16 chased playheads within 0.002 of the straight render at 0.35 s (worst %.2e)", label, worst));
        note (fmt ("%s: the chase moment's 32-sample block took %.3f ms (one block is %.3f ms)", label, r.chaseBlockMs, 32000.0 / 48000));
    }
}

SL_TEST ("engine", chaseCostBounded)
{
    section ("chase cost: Play inside 16 x 16-beat and 16 x 64-beat 8-head Tape notes at 48 kHz (plan 1.8, 8.2 #22)");

    // The straight positions are worked out, not rendered: first make sure that formula is the engine's, on a note
    // played from its start (no chase) and probed 0.35 s in.
    {
        const ChaseRun straight = runChase (4, 16, 0, 0.5);
        const double frames = static_cast<double> (sounds::makeStarterSound (sounds::StarterId::saw, 1).frames());
        double worst = 0;
        for (int i = 0; i < 16; ++i)
        {
            const auto it = straight.pos.find (core::idHash (std::string_view ("c" + std::to_string (i))));
            worst = std::max (worst, it == straight.pos.end() ? 1.0 : circular (it->second, straightPos (48 + i, 0.35 * 48000, frames)));
        }
        check (worst <= 1e-6 && straight.totalTicks == 0, fmt ("the straight playhead formula matches 16 notes played from their start (worst %.2e)", worst));
    }

    checkChase ("16 x 16 beats, Play at beat 12", 4, 16, 12);
    checkChase ("16 x 64 beats, Play at beat 60", 16, 64, 60);

    // Block sizes never change the result: the same stepped chase rendered in 7-sample blocks is bit-identical.
    const ChaseRun a = runChase (4, 16, 12, 0.4, 32);
    const ChaseRun b = runChase (4, 16, 12, 0.4, 7);
    check (a.L == b.L && a.R == b.R && a.totalTicks == b.totalTicks,
           fmt ("the stepped chase is the same in 32- and 7-sample blocks, bit for bit (%lld ticks)", static_cast<long long> (a.totalTicks)));
}
