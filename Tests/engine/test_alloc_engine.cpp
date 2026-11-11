// The Engine's audio-thread work allocates nothing (PLUGIN-PLAN 1.3, 4 M2 "allocations"): 10 s of Strand Demo with
// live keys and chords, parameter sweeps every block (with the 20 ms ramps; the engine moves through Tape, Voice,
// Cloud and Lock: M3 parts G and L), the bypass fade, wheels, bend and sustain, a transport jump, a bounded chase into long
// 8-head notes, a sample swap, snapshot swaps, a stop and a start, all notes off, the display, stats and reset.
// Counted by AllocCounter (operator new and delete; malloc and free too on glibc and the Debug CRT). Registered in
// group "engine" and again in group "alloc", which the Debug-CRT job runs. The processor-level allocation row is the
// integration step's.

#include "engine/EngineRender.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include "core/Inst.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "model/Presets.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    engine::Transport transportAt (bool playing, double beat)
    {
        engine::Transport t;
        t.playing = playing;
        t.bpm = 120;
        t.beat = beat;
        return t;
    }

    void engineAllocationCheck()
    {
        section (std::string ("the Engine's audio-thread work allocates nothing (") + alloc::coverage() + ")");

        // Everything that allocates happens here, before counting.
        const sounds::StarterSound open = sounds::makeStarterSound (sounds::StarterId::sawopen, 1);
        const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
        auto sampleA = engine::makeSampleData (open.left, open.right, open.sampleRate, "sawopen");
        auto sampleB = engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw");
        auto demo = render::compile (*model::loadClipPreset ("strand-demo"));
        model::Clip longClip = model::newClip (16);
        for (int i = 0; i < 16; ++i)
            longClip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (48 + i, 0, 64, "long" + std::to_string (i))));
        auto longNotes = render::compile (longClip);

        const core::Inst base = model::applySoundPreset (core::defaultInst(), "init")->inst;
        std::array<core::Inst, 8> sweep;
        for (int i = 0; i < 8; ++i)
        {
            core::Inst x = base;
            // M3: the sweep moves through Tape, Voice, Cloud and Lock (part G: runGrains, with Spray and Each grain
            // backwards on some; part L: runLock, with Spray and Lock N = 12 on one), so every engine runs here
            x.engine = i % 4 == 0 ? core::Engine::tape : i % 4 == 1 ? core::Engine::voice : core::Engine::cloud;
            x.lock = i % 4 == 3;
            x.grainLock = i == 7 ? 12 : 4;
            x.spray = i % 4 >= 2 ? 0.3 : 0.0;
            x.rev = i == 5;
            x.heads = 1 + i;
            x.macros[0] = 0.3 + 0.05 * i;
            x.macros[6] = 0.6 - 0.04 * i;
            x.outGain = -6 + i;
            x.drift = 0.1 * i;
            x.spread = 0.1 * i;
            x.motion = i % 3 == 0 ? core::Motion::scatter : core::Motion::forward;
            x.refresh.mode = i % 2 == 0 ? core::RefreshMode::sync : core::RefreshMode::free;
            x.refresh.skip = 0.25;
            sweep[static_cast<std::size_t> (i)] = x;
        }
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setInst (sweep[0]);
        auto frame = std::make_unique<engine::DisplayFrame>();
        std::vector<float> L (128), R (128);
        double energy = 0;
        std::int64_t maxChaseVoices = 0;
        int lockFrames = 0;   // display frames with a Lock voice showing its captures (Lock played)

        alloc::reset();
        {
            const alloc::Scope counting;
            e->setSample (sampleA.get(), 1);
            e->setSnapshot (demo.get(), 1);
            e->setTransport (transportAt (true, 0));
            for (std::int64_t k = 0; k < 480000; k += 128)
            {
                const auto b = static_cast<std::size_t> (k / 128);
                e->setInst (sweep[(b / 40) % 8]);
                e->setBypass (b % 300 >= 280);
                if (b % 50 == 10)
                    e->noteOn (55 + static_cast<int> (b % 7), 0.8);
                if (b % 50 == 40)
                    e->noteOff (55 + static_cast<int> ((b - 30) % 7));
                if (b % 400 == 100)
                {
                    e->noteOn (48, 1.0, engine::NoteOnOpts { 0.3 });
                    e->noteOn (52, 1.0);
                    e->noteOn (55, 1.0);
                }
                if (b % 400 == 300)
                    e->allNotesOff();
                if (b % 90 == 5)
                    e->setController (engine::Ctrl::mod1, 0.25 + 0.001 * static_cast<double> (b % 400));
                if (b % 90 == 60)
                    e->releaseController (engine::Ctrl::mod1);
                if (b % 120 == 20)
                    e->setController (engine::Ctrl::bend, 0.5);
                if (b % 120 == 80)
                    e->releaseController (engine::Ctrl::bend);
                if (b % 200 == 0)
                    e->setController (engine::Ctrl::sustain, 1);
                if (b % 200 == 150)
                    e->releaseController (engine::Ctrl::sustain);
                if (k == 96000)
                    e->setTransport (transportAt (true, 9.3));   // a jump: release and chase
                if (k == 144000)
                {
                    e->setSnapshot (longNotes.get(), 2);
                    e->setTransport (transportAt (true, 40));    // a bounded chase into 16 long 8-head notes
                }
                if (k == 240000)
                    e->setSample (sampleB.get(), 2);              // the stolen voices fade on sampleA
                if (k == 288000)
                    e->setSnapshot (demo.get(), 3);
                if (k == 336000)
                    e->setTransport (transportAt (false, std::nan ("")));
                if (k == 360000)
                    e->setTransport (transportAt (true, 2.5));
                if (k == 400000)
                    e->setRampMs (0);
                e->process (L.data(), R.data(), 128);
                for (int j = 0; j < 128; ++j)
                    energy += static_cast<double> (L[static_cast<std::size_t> (j)]) * L[static_cast<std::size_t> (j)];
                if (k % 1536 == 0)
                {
                    e->fillDisplay (*frame);
                    bool lockShown = false;
                    for (int v = 0; v < frame->numVoices; ++v)
                        lockShown = lockShown || frame->voices[v].engine == static_cast<std::uint8_t> (engine::Kc::lock);
                    lockFrames += lockShown && frame->numGrains > 0 ? 1 : 0;
                }
                if (k >= 144000 && k < 200000)
                    maxChaseVoices = std::max<std::int64_t> (maxChaseVoices, e->stats().voices);
                (void) e->minEpochInUse (engine::VersionKind::snapshot);
                (void) e->minEpochInUse (engine::VersionKind::sample);
            }
            e->snapRamps();
            e->reset();
            e->resetStats();
            e->forgetVersions();
        }
        const alloc::Counts c = alloc::counts();
        check (c.news == 0 && c.deletes == 0, fmt ("0 allocations and 0 frees in 10 s of everything at once (%lld and %lld)", c.news, c.deletes));
        check (energy > 1.0 && maxChaseVoices >= 16 && lockFrames > 0,
               fmt ("and it played (energy %.1f; %lld voices during the bounded chase; %d display frames with Lock voices playing)", energy,
                    static_cast<long long> (maxChaseVoices), lockFrames));

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

SL_TEST ("engine", engineNoAllocations)
{
    engineAllocationCheck();
}

SL_TEST ("alloc", engineAudioPathNoAllocations)
{
    engineAllocationCheck();
}
