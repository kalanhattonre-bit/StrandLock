// The M0 probe voice through the processor (PLUGIN-PLAN 4 M0): the starter Saw in tune at three notes and three
// sample rates, root, bend, velocity and Output Gain, 16 voices of chords that never go non-finite, and output
// that does not depend on how the host cuts its blocks.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "params/ParameterLayout.h"
#include "sounds/StarterSounds.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/Measure.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using harness::MidiEvent;

    double expectedHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

    /** The probe tests play the starter Saw, M0's sound (a new instance opens on the Saw that opens up, D9). The load
        sets root 60, cents 0 and the whole sound as the Region, the defaults, so call it before setting anything. */
    void useSaw (StrandLockProcessor& p)
    {
        p.loadStarterSound (sounds::StarterId::saw);
        p.waitForSounds();
    }

    void setParam (StrandLockProcessor& p, params::P which, float plain)
    {
        params::setPlainValue (p.getParam (which), params::index (which), plain);
    }

    /** One note held from sample 0, long enough for a 2^17-point FFT after the attack. */
    harness::Rendered held (StrandLockProcessor& p, double sampleRate, int note, int velocity, std::vector<MidiEvent> extra = {})
    {
        std::vector<MidiEvent> events = { harness::noteOn (0, note, velocity) };
        events.insert (events.end(), extra.begin(), extra.end());
        const auto total = (juce::int64) (131072 + 0.1 * sampleRate);
        return harness::render (p, sampleRate, total, harness::fixedBlocks (512), events);
    }

    double rmsOfTail (const std::vector<float>& x, std::size_t n)
    {
        return measure::rms (x.data() + (x.size() - n), n);
    }
}

SL_TEST ("probe", tuning)
{
    section ("Probe tuning: the Saw at C1, C3 and C5 against root C3, at 44.1, 48 and 96 kHz: f0 within +-3 cents");

    double worst = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const int note : { 36, 60, 84 })
        {
            StrandLockProcessor p;
            useSaw (p);
            const auto r = held (p, rate, note, 100);
            const double hz = measure::fftFundamentalHz (r.left, rate, 17);
            const double ac = measure::autocorrFundamentalHz (r.left, rate, 25.0, 1500.0);
            const double cents = measure::centsBetween (hz, expectedHz (note));
            worst = std::max (worst, std::abs (cents));
            check (std::abs (cents) <= 3.0, fmt ("%.1f kHz, MIDI %d: %.4f Hz by FFT (%.4f by autocorrelation), expected %.4f: %+.3f cents",
                                                 rate / 1000.0, note, hz, ac, expectedHz (note), cents));
        }
    }
    note (fmt ("largest tuning error %.3f cents", worst));

    // The root, cents, Pitch and a full bend move the note by exactly what they say.
    struct Shift { const char* what; params::P param; float value; double semitones; };
    const Shift shifts[] = {
        { "Root Note D3 (62): C3 plays 2 semitones low", params::P::rootNote, 62.0f, -2.0 },
        { "Root Cents -50: C3 plays half a semitone high", params::P::rootCents, -50.0f, 0.5 },
        { "Pitch +7: C3 plays a fifth up", params::P::pitch, 7.0f, 7.0 },
    };
    for (const auto& s : shifts)
    {
        StrandLockProcessor p;
        useSaw (p);
        setParam (p, s.param, s.value);
        const auto r = held (p, 48000.0, 60, 100);
        const double cents = measure::centsBetween (measure::fftFundamentalHz (r.left, 48000.0, 17), expectedHz (60.0 + s.semitones));
        check (std::abs (cents) <= 3.0, fmt ("%s (%+.3f cents)", s.what, cents));
    }
    {
        StrandLockProcessor p;
        useSaw (p);
        const auto r = held (p, 48000.0, 60, 100, { harness::pitchBend (100, 16383) });
        const double cents = measure::centsBetween (measure::fftFundamentalHz (r.left, 48000.0, 17), expectedHz (62.0));
        check (std::abs (cents) <= 3.0, fmt ("pitch bend fully up with Bend Range 2: C3 plays D3 (%+.3f cents)", cents));
    }
}

SL_TEST ("probe", levels)
{
    section ("Probe levels: velocity through Velocity Sensitivity, and Output Gain");

    const std::size_t tail = 48000;
    StrandLockProcessor a;
    useSaw (a);
    const double loud = rmsOfTail (held (a, 48000.0, 60, 127).left, tail);
    StrandLockProcessor b;
    useSaw (b);
    const double soft = rmsOfTail (held (b, 48000.0, 60, 64).left, tail);
    const double want = 1.0 - (double) (float) 0.7 * (1.0 - 64.0 / 127.0);
    check (loud > 0.05 && std::abs (soft / loud - want) <= 0.002,
           fmt ("velocity 64 against 127 at Velocity Sensitivity 70 %%: %.4f of the level (1 - 0.7 x (1 - 64/127) = %.4f)", soft / loud, want));

    StrandLockProcessor c;
    useSaw (c);
    setParam (c, params::P::velSens, 0.0f);
    const double flat = rmsOfTail (held (c, 48000.0, 60, 20).left, tail);
    check (std::abs (flat / loud - 1.0) <= 0.002, fmt ("at Velocity Sensitivity 0 velocity 20 plays as loud as 127 (%.4f)", flat / loud));

    StrandLockProcessor d;
    useSaw (d);
    setParam (d, params::P::outGain, -6.0f);
    const double quiet = rmsOfTail (held (d, 48000.0, 60, 127).left, tail);
    const double gain = std::pow (10.0, -6.0 / 20.0);
    check (std::abs (quiet / loud - gain) <= 0.002, fmt ("Output Gain -6 dB: %.4f of the level (%.4f)", quiet / loud, gain));

    StrandLockProcessor e;
    useSaw (e);
    setParam (e, params::P::bypass, 1.0f);
    const double bypassed = rmsOfTail (held (e, 48000.0, 60, 127).left, tail);
    check (bypassed == 0.0, fmt ("the host bypass silences the output (rms %.3g)", bypassed));
}

SL_TEST ("probe", stability)
{
    section ("Probe stability: 16 voices of chords for 10 s");

    const double rate = 48000.0;
    StrandLockProcessor p;
    useSaw (p);
    const auto events = harness::chords (rate, 10.0, 16, 0.5, 2026);
    p.resetProbeStats();
    const auto r = harness::render (p, rate, (juce::int64) (10.0 * rate), harness::fixedBlocks (256), events);
    const auto& st = p.getProbeStats();

    double peak = 0.0, sum = 0.0;
    long long nonFiniteOut = 0;
    for (const auto* ch : { &r.left, &r.right })
        for (const float x : *ch)
        {
            if (! std::isfinite (x))
                ++nonFiniteOut;
            peak = std::max (peak, (double) std::abs (x));
            sum += (double) x * (double) x;
        }
    const double rms = std::sqrt (sum / (double) (r.left.size() + r.right.size()));

    check (st.nonFinite == 0 && nonFiniteOut == 0,
           fmt ("non-finite samples counted before the output guard: %lld; in the output: %lld", st.nonFinite, nonFiniteOut));
    check (st.preLimiterPeak > 0.05, fmt ("pre-limiter peak %.3f (more than 0.05)", st.preLimiterPeak));
    check (rms > 0.01, fmt ("output rms %.4f (more than 0.01)", rms));
    check (peak <= 0.98, fmt ("output peak %.4f (at most 0.98)", peak));
    check (st.maxSounding == 16, fmt ("16 voices sounded at once (%d)", st.maxSounding));
    check (p.getLatencySamples() == 0 && p.getBusCount (true) == 0 && p.getTotalNumOutputChannels() == 2 && p.acceptsMidi(),
           "latency 0, no input bus, stereo out, accepts MIDI");

    // More keys than voices: the oldest are stolen, and nothing goes past the polyphony.
    StrandLockProcessor q;
    useSaw (q);
    setParam (q, params::P::polyphony, 4.0f);
    std::vector<MidiEvent> many;
    for (int k = 0; k < 10; ++k)
        many.push_back (harness::noteOn (k * 480, 48 + k, 100));
    q.resetProbeStats();
    harness::render (q, rate, 24000, harness::fixedBlocks (128), many);
    check (q.getProbeStats().maxSounding == 4 && q.getProbeStats().steals == 6,
           fmt ("Voices 4 and 10 keys: at most 4 sound (%d), 6 are stolen (%lld)", q.getProbeStats().maxSounding, q.getProbeStats().steals));
}

SL_TEST ("probe", blockSizesBitIdentical)
{
    section ("Probe: 10 s of MIDI with blocks of 1, 37, 64, 480, 4096 and random 1..700 samples: bit-identical");

    const double rate = 48000.0;
    const auto total = (juce::int64) (10.0 * rate);
    auto events = harness::chords (rate, 10.0, 6, 0.37, 77);
    for (int k = 0; k < 40; ++k)
        events.push_back (harness::pitchBend ((juce::int64) (k * 11987 + 5), (k * 2731) % 16384));
    events.push_back (harness::controller (90001, 64, 127));   // sustain down
    events.push_back (harness::controller (200003, 64, 0));    // and up
    events.push_back (harness::controller (300007, 123, 0));   // all notes off
    events.push_back (harness::controller (400009, 120, 0));   // all sound off

    StrandLockProcessor ref;
    useSaw (ref);
    const auto reference = harness::render (ref, rate, total, harness::fixedBlocks (64), events);
    const double refRms = measure::rms (reference.left.data(), reference.left.size());
    check (refRms > 0.01, fmt ("the reference (64-sample blocks) plays (rms %.4f)", refRms));

    struct Run { const char* what; harness::BlockSizes sizes; };
    const Run runs[] = {
        { "1-sample blocks", harness::fixedBlocks (1) },
        { "37-sample blocks", harness::fixedBlocks (37) },
        { "480-sample blocks", harness::fixedBlocks (480) },
        { "4096-sample blocks", harness::fixedBlocks (4096) },
        { "random blocks of 1..700", harness::randomBlocks (1, 700, 4242) },
    };
    for (const auto& run : runs)
    {
        StrandLockProcessor p;
        useSaw (p);
        const auto out = harness::render (p, rate, total, run.sizes, events);
        const double diff = harness::maxDifference (reference, out);
        check (diff == 0.0, fmt ("%s (%d blocks): largest difference %.3g", run.what, out.blocks, diff));
    }
}
