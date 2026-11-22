// The raw audition (M7 V4; PLUGIN-PLAN 7.9 decision 7a, NOTES-M6 Fix M6R-PAR-2): holding the mouse on the waveform outside
// the Region plays the sound as recorded from that spot, as sl-ui.js rawAuditionStart does: at the sound's own rate, a gain
// rising from 0 to 0.7 over 12 ms, and on release falling to 0 over 30 ms (rawAuditionStop). The editor sends it through
// the command queue; a few fixed players in the engine play it, after the output stage.
//  - rawAuditionPlaysAsRecorded (engine): between the fades the output is exactly 0.7 x the stored samples from the held
//    frame, bit for bit; the fade in and out as the prototype's ramps; silence from 30 ms after the release; a second hold
//    replaces the first (after the first's 30 ms fade, the second's samples alone); outGain and the limiter leave it alone;
//    a sound at another rate plays at its own rate (the sound's readAbs at every 48/44.1 frames); the display's line; a
//    swap while it fades keeps the old sound's epoch until the fade ends; no sound, nothing.
//  - rawAuditionAllocatesNothing (alloc, so the Debug CRT counts every heap call too): holds, releases, a second hold and a
//    sound swap while it fades, with live notes, through the processor's command queue.
// The readout and the strip's press are in Tests/ui/test_strip.cpp (stripRegion, section 6; stripRawAuditionLine).

#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "PluginProcessor.h"
#include "core/Inst.h"
#include "doc/CommandQueue.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "engine/SourceRead.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** An engine at a rate with one sound and nothing else playing. */
    struct RawRig
    {
        std::unique_ptr<engine::Engine> e = std::make_unique<engine::Engine>();
        std::vector<float> L, R;

        explicit RawRig (double sr)
        {
            e->prepare (sr);
            e->setRampMs (0);
            e->setInst (core::defaultInst());
        }

        /** n more samples in blocks of 128 (the tail of L and R). */
        void render (std::int64_t n)
        {
            const std::size_t base = L.size();
            L.resize (base + static_cast<std::size_t> (n), 0.0f);
            R.resize (base + static_cast<std::size_t> (n), 0.0f);
            for (std::int64_t i = 0; i < n; i += 128)
            {
                const int m = static_cast<int> (std::min<std::int64_t> (128, n - i));
                e->process (L.data() + base + static_cast<std::size_t> (i), R.data() + base + static_cast<std::size_t> (i), m);
            }
        }

        std::int64_t at() const noexcept { return static_cast<std::int64_t> (L.size()); }
    };

    /** The same float bits (no float compare). */
    bool sameFloat (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

    bool sameFloats (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
    }

    /** 0.7 x a stored sample, as the player adds it to a silent output: (float) (0 + v x 0.7 x 1 x 1). */
    float asRecorded (double v) { return static_cast<float> (0.0f + v * (engine::kRawGain * 1.0 * 1.0)); }

    double rmsOf (const std::vector<float>& x)
    {
        double s = 0;
        for (const float v : x)
            s += static_cast<double> (v) * v;
        return x.empty() ? 0.0 : std::sqrt (s / static_cast<double> (x.size()));
    }
}

SL_TEST ("engine", rawAuditionPlaysAsRecorded)
{
    section ("raw audition (7.9 decision 7a): the sound as recorded from the held spot: 12 ms in, then 0.7 x its samples bit for bit; silence "
             "from 30 ms after the release; a second hold replaces the first; outGain and the limiter leave it alone; another rate plays at "
             "the sound's own rate");

    const double sr = 48000;
    const std::vector<float> noiseL = signals::noise (2.0, sr, 5, 0.5), noiseR = signals::noise (2.0, sr, 9, 0.5);
    auto sound = engine::makeSampleData (noiseL, noiseR, sr, "noise");
    const auto n = sound->n;
    // the fades in samples, as the engine works them out (576 and 1440 at 48 kHz)
    const double attack = engine::kRawFadeInSeconds * sr, release = engine::kRawFadeOutSeconds * sr;
    const auto attackSamples = static_cast<std::int64_t> (std::ceil (attack));

    // One hold at 30 %: the fade in, then the samples themselves.
    {
        RawRig rig (sr);
        rig.e->setSample (sound.get(), 1);
        rig.e->rawAuditionOn (0.3);
        rig.render (static_cast<std::int64_t> (0.3 * sr));
        const auto f0 = static_cast<std::int64_t> (std::floor (0.3 * static_cast<double> (n)));
        int exact = 0, rising = 0, wrong = 0;
        double fadeErr = 0;
        for (std::int64_t k = 0; k < rig.at(); ++k)
        {
            const auto i = static_cast<std::size_t> (k);
            const double l = noiseL[static_cast<std::size_t> (f0 + k)], r = noiseR[static_cast<std::size_t> (f0 + k)];
            if (static_cast<double> (k) >= attack)
            {
                const bool same = sameFloat (rig.L[i], asRecorded (l)) && sameFloat (rig.R[i], asRecorded (r));
                exact += same ? 1 : 0;
                wrong += same ? 0 : 1;
            }
            else
            {
                const double g = engine::kRawGain * static_cast<double> (k) / attack;
                fadeErr = std::max (fadeErr, std::max (std::abs (rig.L[i] - l * g), std::abs (rig.R[i] - r * g)));
                ++rising;
            }
        }
        check (wrong == 0 && exact == static_cast<int> (rig.at() - attackSamples) && fadeErr < 1e-6 && rising == static_cast<int> (attackSamples),
               fmt ("held at 30 %% (frame %lld): the first %lld samples rise from 0 to 0.7 (within %.1e of the line), then %d samples are exactly "
                    "0.7 x the stored ones, both channels (%d differ)",
                    (long long) f0, (long long) attackSamples, fadeErr, exact, wrong));

        engine::DisplayFrame frame {};
        rig.e->fillDisplay (frame);
        const double shown = static_cast<double> (frame.rawPos), want = static_cast<double> (f0 + rig.at()) / static_cast<double> (n);
        check (frame.rawOn && std::abs (shown - want) < 1e-6,
               fmt ("the display frame says where it reads: %.6f of the sound (it is at %.6f)", shown, want));

        // The release: 30 ms down from where it was, then silence.
        const auto let = rig.at();
        rig.e->rawAuditionOff();
        rig.render (static_cast<std::int64_t> (0.06 * sr));
        double fallErr = 0;
        std::int64_t lastSound = -1;
        for (std::int64_t k = 0; k < rig.at() - let; ++k)
        {
            const auto i = static_cast<std::size_t> (let + k);
            if (! sameFloat (rig.L[i], 0.0f) || ! sameFloat (rig.R[i], 0.0f))
                lastSound = k;
            if (static_cast<double> (k) < release)
            {
                const double g = engine::kRawGain * (1.0 - static_cast<double> (k) / release);
                const auto s = static_cast<std::size_t> (f0 + let + k);
                fallErr = std::max (fallErr, std::max (std::abs (rig.L[i] - noiseL[s] * g), std::abs (rig.R[i] - noiseR[s] * g)));
            }
        }
        rig.e->fillDisplay (frame);
        check (lastSound >= 0 && static_cast<double> (lastSound) < release && fallErr < 1e-6 && ! frame.rawOn,
               fmt ("let go: it falls to 0 over 30 ms (within %.1e of the line); the last sound %lld samples after the release (under %lld), then "
                    "silence, and the display shows no line",
                    fallErr, (long long) lastSound, (long long) std::ceil (release)));
    }

    // A second hold replaces the first: the first fades out over 30 ms while the second comes in; after that, only the second.
    {
        RawRig rig (sr);
        rig.e->setSample (sound.get(), 1);
        rig.e->rawAuditionOn (0.3);
        rig.render (static_cast<std::int64_t> (0.1 * sr));
        const auto second = rig.at();
        rig.e->rawAuditionOn (0.6);
        rig.render (static_cast<std::int64_t> (0.2 * sr));
        const auto f1 = static_cast<std::int64_t> (std::floor (0.6 * static_cast<double> (n)));
        int exact = 0, wrong = 0;
        bool overlap = false;
        for (std::int64_t k = 0; k < rig.at() - second; ++k)
        {
            const auto i = static_cast<std::size_t> (second + k);
            const auto s = static_cast<std::size_t> (f1 + k);
            const bool same = sameFloat (rig.L[i], asRecorded (noiseL[s])) && sameFloat (rig.R[i], asRecorded (noiseR[s]));
            if (static_cast<double> (k) >= release)
            {
                exact += same ? 1 : 0;
                wrong += same ? 0 : 1;
            }
            else if (static_cast<double> (k) >= attack && ! same)
            {
                overlap = true;   // the first one is still fading under it
            }
        }
        check (wrong == 0 && exact > 0 && overlap,
               fmt ("a second hold at 60 %%: for 30 ms both sound (the first fading), then %d samples are exactly the second's (%d differ)", exact, wrong));
    }

    // outGain and the limiter leave it alone (it comes after the output stage, as the prototype's source goes to its master).
    {
        RawRig plain (sr), loud (sr);
        core::Inst inst = core::defaultInst();
        inst.outGain = 12;
        loud.e->setInst (inst);
        plain.e->setSample (sound.get(), 1);
        loud.e->setSample (sound.get(), 1);
        plain.e->rawAuditionOn (0.5);
        loud.e->rawAuditionOn (0.5);
        plain.render (12000);
        loud.render (12000);
        check (sameFloats (plain.L, loud.L) && sameFloats (plain.R, loud.R) && rmsOf (plain.L) > 0.05,
               fmt ("outGain +12 dB: the raw audition plays the same samples, bit for bit (rms %.3f; it is added after the output stage, as the "
                    "prototype's raw source)", rmsOf (plain.L)));
    }

    // A 48 kHz sound in a 44.1 kHz session plays at its own rate: the sound's readAbs every 48 / 44.1 frames, times 0.7.
    {
        const double rate = 44100;
        RawRig rig (rate);
        rig.e->setSample (sound.get(), 1);
        rig.e->rawAuditionOn (0.25);
        rig.render (8000);
        const double step = sr / rate;
        double pos = std::floor (0.25 * static_cast<double> (n));
        const auto in = static_cast<std::int64_t> (engine::kRawFadeInSeconds * rate);
        int exact = 0, wrong = 0;
        for (std::int64_t k = 0; k < rig.at(); ++k)
        {
            double l = 0, r = 0;
            engine::readAbs (*sound, pos, l, r);
            pos += step;
            if (static_cast<double> (k) < engine::kRawFadeInSeconds * rate + 1)
                continue;
            const auto i = static_cast<std::size_t> (k);
            const bool same = sameFloat (rig.L[i], asRecorded (l)) && sameFloat (rig.R[i], asRecorded (r));
            exact += same ? 1 : 0;
            wrong += same ? 0 : 1;
        }
        check (wrong == 0 && exact > 7000,
               fmt ("a 48 kHz sound at 44.1 kHz: after the %lld-sample fade in, %d samples are exactly 0.7 x the sound read every %.6f frames (%d "
                    "differ)", (long long) in, exact, step, wrong));
    }

    // A swap while it fades: the old sound stays in use (its epoch) until the fade has ended; no sound, nothing plays.
    {
        RawRig rig (sr);
        auto other = engine::makeSampleData (signals::noise (1.0, sr, 11, 0.3), {}, sr, "other");
        rig.e->setSample (sound.get(), 1);
        rig.e->rawAuditionOn (0.4);
        rig.render (4000);
        rig.e->setSample (other.get(), 2);   // the hold fades out on the sound it started on
        const auto during = rig.e->minEpochInUse (engine::VersionKind::sample);
        rig.render (static_cast<std::int64_t> (std::ceil (release)) + 256);
        const auto after = rig.e->minEpochInUse (engine::VersionKind::sample);
        check (during == 1 && after == 2, fmt ("a sound swap while it plays: it fades on the old sound, which stays in use (epoch %llu, then %llu)",
                                               (unsigned long long) during, (unsigned long long) after));

        RawRig none (sr);
        none.e->rawAuditionOn (0.5);
        none.render (2000);
        bool silent = true;
        for (const float v : none.L)
            silent = silent && sameFloat (v, 0.0f);
        check (silent, "with no sound a hold plays nothing");
    }
}

SL_TEST ("alloc", rawAuditionAllocatesNothing)
{
    section ("raw audition (7.9 decision 7a): holds and releases, a second hold, a sound swap while one fades, with live notes, through the "
             "processor's command queue: 0 allocations and frees on the audio thread");

    const double rate = 48000.0;
    const int block = 256;
    StrandLockProcessor proc;
    proc.setPlayConfigDetails (0, 2, rate, block);
    proc.prepareToPlay (rate, block);
    proc.waitForSounds();
    proc.waitForSnapshot();

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    auto& queue = proc.getCommandQueue();
    const int total = 600;   // 3.2 s
    bool finite = true, heard = false, swapped = false;
    alloc::reset();
    for (int b = 0; b < total; ++b)
    {
        // the message thread's side (outside the counted scope): holds, releases and a second hold now and then, a note,
        // and once a new sound while a hold plays
        if (b % 60 == 0)
            queue.push (doc::UiCommand::rawAuditionOn (0.1 + 0.001 * b));
        if (b % 60 == 25)
            queue.push (doc::UiCommand::rawAuditionOn (0.5));   // a second hold replaces the first
        if (b % 60 == 45)
            queue.push (doc::UiCommand::rawAuditionOff());
        midi.clear();
        if (b % 90 == 10)
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 3);
        if (b % 90 == 70)
            midi.addEvent (juce::MidiMessage::noteOff (1, 64), 5);
        if (b == 300)
        {
            proc.loadStarterSound (sounds::StarterId::pad);
            proc.waitForSounds();
            swapped = true;
        }
        buffer.clear();
        {
            const alloc::Scope counting;
            proc.processBlock (buffer, midi);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buffer.getSample (ch, i);
                finite = finite && std::isfinite (v);
                heard = heard || std::abs (v) > 1e-3f;
            }
        if (b % 100 == 0)
            proc.collectVersions();
    }
    const auto c = alloc::counts();
    check (finite && heard && swapped, "every sample finite, the auditions heard, the sound swapped while one played");
    check (c.news == 0 && c.deletes == 0,
           fmt ("allocations in processBlock: %lld, frees: %lld (largest request %zu bytes; counting %s)", c.news, c.deletes, c.largest, alloc::coverage()));
}
