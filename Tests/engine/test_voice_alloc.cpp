// A voice's audio-thread work allocates nothing (plan 1.3): init in place, the first tick, render with Scatter
// jumps, Refresh stutters, a seam and Spread, the chain hand-over, follow, release, steal, the one-shot and the
// stepped chase (coarse), and the smoother's steps. Counted by AllocCounter (operator new and delete; malloc and
// free too on glibc and the Debug CRT). Registered in group "engine" and again in group "alloc", which the
// Debug-CRT job runs.

#include "engine/VoiceRig.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include "core/Inst.h"
#include "engine/InstSmoother.h"
#include "engine/Voice.h"
#include "sounds/StarterSounds.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    void voiceAllocationCheck()
    {
        section (std::string ("a voice's audio-thread work allocates nothing (") + alloc::coverage() + ")");

        // Everything that allocates happens here, before counting: the rig, the sample, the snapshot, buffers.
        rig::VoiceRig rg (48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        inst.motion = core::Motion::scatter;
        inst.jump = core::RateId::r1_16;
        inst.refresh.mode = core::RefreshMode::sync;
        inst.heads = 3;
        inst.spread = 0.5;
        inst.drift = 0.4;
        inst.region.start = 0.1;
        inst.region.end = 0.4;
        rg.setInst (inst);
        const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
        rg.setSample (engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"));
        rig::RawNote a, b, longNote;
        a.id = "A";
        a.p = 60;
        a.s = 0;
        a.l = 2;
        a.hasGlide = true;
        a.glideD = 7;
        a.glideLen = 0.5;
        a.glideTo = "B";
        b.id = "B";
        b.p = 67;
        b.s = 2;
        b.l = 2;
        longNote.id = "long";
        longNote.p = 55;
        longNote.s = 4;
        longNote.l = 60;
        rg.setSnapshot (rig::rawSnapshot ({ a, b }, 16, true));
        auto other = rig::rawSnapshot ({ longNote }, 16, false);
        rg.play (120, 0, false);
        std::vector<float> L (128), R (128);
        auto second = std::make_unique<engine::Voice>();
        engine::InstSmoother smoother;
        smoother.prepare (48000, 20);
        smoother.setTarget (inst);
        core::Inst moved = inst;
        moved.speed = 2;
        moved.macros[0] = 0.8;

        alloc::reset();
        {
            const alloc::Scope counting;
            engine::Voice& v = rg.clip (0);
            const std::int64_t kB = rg.clock.kOf (2);
            for (std::int64_t k = 0; k < 96000; k += 128)
            {
                if (k == kB)
                    v.switchTo (&rg.snap->notes[1], 2);
                v.render (L.data(), R.data(), 0, 128, k);
            }
            v.follow (rg.snap.get(), 2, &rg.snap->notes[1], 2);
            v.release();
            v.render (L.data(), R.data(), 0, 128, 96000);
            v.steal();
            v.render (L.data(), R.data(), 0, 128, 96128);

            engine::Voice& w = rg.live (64, 0.8, 0);
            w.setLivePitch (67, 0.7);
            w.render (L.data(), R.data(), 0, 128, 0);
            w.setLivePitch (62);
            w.render (L.data(), R.data(), 0, 128, 128);

            // the long note, chased 50 beats in (coarse), once in one go and once in steps
            second->initClip (rg.ctx, rg.source, other.get(), 3, 0, 4, 0, rg.clock.kOf (4), 99);
            second->chase (rg.clock.kOf (54));
            second->initClip (rg.ctx, rg.source, other.get(), 3, 0, 4, 0, rg.clock.kOf (4), 100);
            const std::int64_t kNow = rg.clock.kOf (54);
            second->chaseBegin (kNow);
            while (! second->chaseCaughtUp (kNow))
                second->chaseRun (kNow, 256);
            second->chaseEnd (kNow);
            second->ffChain (kNow);
            second->startAt (kNow);
            second->render (L.data(), R.data(), 0, 128, kNow);

            smoother.setTarget (moved);
            for (int i = 0; i < 40; ++i)
                smoother.step();
            smoother.setBypass (true);
            smoother.snap();
        }
        const alloc::Counts c = alloc::counts();
        check (c.news == 0 && c.deletes == 0, fmt ("0 allocations and 0 frees (%lld and %lld)", c.news, c.deletes));
        check (rig::rms (L, 0, 128) > 0, "and the voices made sound");

        // A no-op is not a pass: the counter sees an allocation made the same way.
        alloc::reset();
        {
            const alloc::Scope counting;
            auto p = std::make_unique<std::vector<float>> (64);
            (void) p;
        }
        check (alloc::counts().news >= 1, "negative control: an allocation inside the same kind of scope is counted");
    }
}

SL_TEST ("engine", voiceNoAllocations)
{
    voiceAllocationCheck();
}

SL_TEST ("alloc", voiceAudioPathNoAllocations)
{
    voiceAllocationCheck();
}
