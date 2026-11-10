// The processor's audio path (PLUGIN-PLAN 1.3, 1.10, 4 M2; the integration step's processor-level rows): the clip
// plays with the host, the parameters reach the engine (the M0 probe's tuning shifts and levels, now on the real
// engine), the live row through MidiRouter (CC 32 on a Travel wheel plays -0.98236x, velocity, bend, sustain), the
// editor's commands, determinism through processBlock (block sizes, setNonRealtime, stopped samples before Play),
// stability with 8 heads and 16 live keys, Voice and Cloud notes standing in as Tape on the Diagnostics page, and a
// state saved and loaded while playing. Group "plugin" (the state-while-playing test is in "state", which the
// sanitizer job runs).

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Travel.h"
#include "engine/EngineRender.h"
#include "params/ParameterLayout.h"
#include "sounds/StarterSounds.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/Measure.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using harness::MidiEvent;

    constexpr double kSr = 48000.0;

    double expectedHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

    void setParam (StrandLockProcessor& p, params::P which, float plain)
    {
        params::setPlainValue (p.getParam (which), params::index (which), plain);
    }

    /** The starter Saw (the M0 probe's sound), loaded as a person would; its load sets root 60, cents 0 and the
        whole sound as the Region, so call it before setting anything. */
    void useSaw (StrandLockProcessor& p)
    {
        p.loadStarterSound (sounds::StarterId::saw);
        p.waitForSounds();
    }

    /** One note held from sample 0, long enough for a 2^17-point FFT after the attack. */
    harness::Rendered held (StrandLockProcessor& p, int note, int velocity, std::vector<MidiEvent> extra = {})
    {
        std::vector<MidiEvent> events = { harness::noteOn (0, note, velocity) };
        events.insert (events.end(), extra.begin(), extra.end());
        return harness::render (p, kSr, (juce::int64) (131072 + 0.1 * kSr), harness::fixedBlocks (512), events);
    }

    double rmsOfTail (const std::vector<float>& x, std::size_t n)
    {
        return measure::rms (x.data() + (x.size() - n), n);
    }

    /** A play head at 120 bpm that plays from `fromPpq` once `stoppedFor` samples have passed (stopped before). */
    std::function<void (juce::int64, int)> playFrom (harness::FakePlayHead& head, double fromPpq, juce::int64 stoppedFor = 0)
    {
        return [&head, fromPpq, stoppedFor] (juce::int64 start, int) {
            const bool playing = start >= stoppedFor;
            head.info.setIsPlaying (playing);
            head.info.setBpm (120.0);
            head.info.setPpqPosition (playing ? fromPpq + (double) (start - stoppedFor) / 24000.0 : fromPpq);
        };
    }
}

SL_TEST ("plugin", clipPlaysWithTheHost)
{
    section ("A new instance plays Strand Demo with the Saw that opens up while the host plays, and goes quiet when it stops");

    StrandLockProcessor p;
    harness::FakePlayHead head;
    p.setPlayHead (&head);
    // 4 s playing from bar 1, then 1 s stopped
    const auto out = harness::render (p, kSr, (juce::int64) (5 * kSr), harness::fixedBlocks (256), {}, 4096, [&head] (juce::int64 start, int) {
        const bool playing = start < (juce::int64) (4 * kSr);
        head.info.setIsPlaying (playing);
        head.info.setBpm (120.0);
        head.info.setPpqPosition (playing ? (double) start / 24000.0 : 8.0);
    });
    const double playing = measure::rms (out.left.data(), (std::size_t) (4 * kSr));
    const double tail = measure::peak (out.left.data() + (std::size_t) (4.2 * kSr), (std::size_t) (0.8 * kSr));
    const auto st = p.getEngineStats();
    check (playing > 0.01 && juce::exactlyEqual (tail, 0.0), fmt ("playing: rms %.4f; 0.2 s after Stop: peak %.3g (the clip voices released)", playing, tail));
    check (st.nonFinite == 0 && st.filterResets == 0, "no sample was replaced by the output guard and no filter reset");
    const auto snap = p.getAudioSnapshot();
    check (snap != nullptr && snap->notes.size() == p.getDocument().current()->notes.size() && p.getDisplayFramesWritten() >= 130,
           fmt ("the audio thread plays the document's clip (%zu notes) and wrote %llu display frames in 5 s", snap != nullptr ? snap->notes.size() : (std::size_t) 0,
                (unsigned long long) p.getDisplayFramesWritten()));
    p.setPlayHead (nullptr);
}

SL_TEST ("plugin", parametersReachTheEngine)
{
    section ("The parameters reach the engine (the M0 probe's checks on the real engine): tuning, root, cents, Pitch, bend, velocity, Output Gain, bypass");

    // Tape at C3 against its C3 root (the engine-level Tape tuning row is engine/tapeTuning).
    {
        StrandLockProcessor p;
        useSaw (p);
        const auto r = held (p, 60, 100);
        const double cents = measure::centsBetween (measure::fftFundamentalHz (r.left, kSr, 17), expectedHz (60));
        check (std::abs (cents) <= 3.0, fmt ("C3 against root C3: %+.3f cents", cents));
    }
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
        const auto r = held (p, 60, 100);
        const double cents = measure::centsBetween (measure::fftFundamentalHz (r.left, kSr, 17), expectedHz (60.0 + s.semitones));
        check (std::abs (cents) <= 3.0, fmt ("%s (%+.3f cents)", s.what, cents));
    }
    {
        StrandLockProcessor p;
        useSaw (p);
        const auto r = held (p, 60, 100, { harness::pitchBend (100, 16383) });
        const double cents = measure::centsBetween (measure::fftFundamentalHz (r.left, kSr, 17), expectedHz (62.0));
        check (std::abs (cents) <= 3.0, fmt ("pitch bend fully up with Bend Range 2: C3 plays D3 (%+.3f cents)", cents));
    }

    // Levels at Output Gain -12 dB, so the limiter (ceiling 0.9) never touches the reference.
    const std::size_t tail = 48000;
    auto level = [&] (int velocity, float velSens, float outGain, bool bypass) {
        StrandLockProcessor p;
        useSaw (p);
        setParam (p, params::P::outGain, outGain);
        setParam (p, params::P::velSens, velSens);
        if (bypass)
            setParam (p, params::P::bypass, 1.0f);
        return rmsOfTail (held (p, 60, velocity).left, tail);
    };
    const double loud = level (127, 0.7f, -12.0f, false);
    const double soft = level (64, 0.7f, -12.0f, false);
    // (double) (float) 0.7: the value the parameter holds (plan 1.6 #12)
    const double want = 1.0 - (double) (float) 0.7 * (1.0 - 64.0 / 127.0);
    check (loud > 0.02 && std::abs (soft / loud - want) <= 0.002,
           fmt ("velocity 64 against 127 at Velocity Sensitivity 70 %%: %.4f of the level (1 - 0.7 x (1 - 64/127) = %.4f)", soft / loud, want));
    const double flat = level (20, 0.0f, -12.0f, false);
    check (std::abs (flat / loud - 1.0) <= 0.002, fmt ("at Velocity Sensitivity 0 velocity 20 plays as loud as 127 (%.4f)", flat / loud));
    const double quiet = level (127, 0.7f, -18.0f, false);
    const double gain = std::pow (10.0, -6.0 / 20.0);
    check (std::abs (quiet / loud - gain) <= 0.002, fmt ("Output Gain -18 against -12 dB: %.4f of the level (%.4f)", quiet / loud, gain));
    const double bypassed = level (127, 0.7f, -12.0f, true);
    check (juce::exactlyEqual (bypassed, 0.0), fmt ("the host bypass silences the output (rms %.3g)", bypassed));
}

SL_TEST ("plugin", liveThroughMidiRouter)
{
    section ("live through processBlock and MidiRouter: CC 32 on a Travel wheel plays -0.98236x within 1e-4; velocity, bend at range 7, sustain");

    // A 4 s DC sound handed to the engine as the sample exchange would, and the mod wheel (CC 1) on Travel.
    const auto dc = render::monoSample (signals::dc (4.0, kSr, 0.25), kSr);
    auto speedFor = [&dc] (int ccValue) {
        StrandLockProcessor p;
        setParam (p, params::P::engine, 0.0f);   // Tape
        auto s = p.getInstState();
        s.mod1Target = "travel";
        p.setInstState (s);
        p.setPlayConfigDetails (0, 2, kSr, 480);
        p.prepareToPlay (kSr, 480);
        p.getSampleExchange().publish (std::make_unique<engine::SampleData> (*dc));
        juce::AudioBuffer<float> buffer (2, 480);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, ccValue), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
        auto f = std::make_unique<engine::DisplayFrame>();
        double moved = 0, last = std::nan ("");
        for (int i = 0; i < 210; ++i)   // 4800 samples in, then 200 x 480 measured
        {
            p.processBlock (buffer, midi);
            midi.clear();
            p.getEngine().fillDisplay (*f);
            const double pos = f->numVoices == 1 && f->voices[0].numHeads > 0 ? f->voices[0].heads[0].pos : std::nan ("");
            if (i >= 10 && ! std::isnan (last))
            {
                double d = pos - last;
                d -= std::floor (d + 0.5);   // the Region wraps
                moved += d;
            }
            if (i >= 9)
                last = pos;
        }
        p.releaseResources();
        return moved * (double) dc->n / (200.0 * 480.0);
    };
    const double at32 = speedFor (32);
    const double exact = core::travelSpeed (32.0 / 127.0);
    check (std::abs (at32 - -0.98236) <= 1e-4, fmt ("CC 1 at 32 (32/127 on Travel): %.6f x (travelSpeed (32/127) = %.6f, the plan's -0.98236)", at32, exact));
    const double at96 = speedFor (96);
    check (at96 > 0.5, fmt ("negative control: CC 1 at 96 plays forward (%+.4f x), so the measurement tells the directions apart", at96));

    // Bend +1 at Bend Range 7: G3 within 3 cents (the Saw).
    {
        StrandLockProcessor p;
        useSaw (p);
        setParam (p, params::P::bendRange, 7.0f);
        const auto r = held (p, 60, 100, { harness::pitchBend (0, 16383) });
        const double cents = measure::centsBetween (measure::fftFundamentalHz (r.left, kSr, 17), expectedHz (67.0));
        check (std::abs (cents) <= 3.0, fmt ("pitch wheel fully up at Bend Range 7: G3 (%+.3f cents)", cents));
    }

    // Sustain: a released key holds until CC 64 goes below 64.
    for (const bool pedal : { true, false })
    {
        StrandLockProcessor p;
        useSaw (p);
        std::vector<MidiEvent> ev = { harness::noteOn (0, 60, 100), harness::noteOff (4800, 60), harness::controller (24000, 64, 10) };
        if (pedal)
            ev.push_back (harness::controller (100, 64, 127));
        const auto r = harness::render (p, kSr, 24000 + 9600, harness::fixedBlocks (128), ev);
        const double heldOn = measure::rms (r.left.data() + 19200, 4800);   // 0.4 s after the note-off
        const double after = measure::peak (r.left.data() + 24000 + 4800, 4800);
        if (pedal)
            check (heldOn > 0.01 && juce::exactlyEqual (after, 0.0), fmt ("with the pedal down the key still sounds 0.3 s after its note-off (rms %.4f) and ends once it lifts (peak %.3g)",
                                                       heldOn, after));
        else
            check (juce::exactlyEqual (heldOn, 0.0), fmt ("negative control: without the pedal it has ended (rms %.3g)", heldOn));
    }
}

SL_TEST ("plugin", editorCommandsPlay)
{
    section ("The editor's commands reach the engine at the next block: audition on and off (with a start position), let go of a wheel, all notes off");

    StrandLockProcessor p;
    useSaw (p);
    p.setPlayConfigDetails (0, 2, kSr, 256);
    p.prepareToPlay (kSr, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    auto f = std::make_unique<engine::DisplayFrame>();
    auto run = [&] (int blocks) {
        for (int i = 0; i < blocks; ++i)
            p.processBlock (buffer, midi);
        p.getEngine().fillDisplay (*f);
    };

    run (2);
    check (f->numVoices == 0, "nothing sounds before a command");
    p.getCommandQueue().push (doc::UiCommand::auditionOn (64, 0.8, 0.5));
    run (1);
    const bool on = f->numVoices == 1 && f->voices[0].p == 64 && ! f->voices[0].released && f->voices[0].numHeads > 0;
    const double pos = on ? f->voices[0].heads[0].pos : -1.0;
    check (on && std::abs (pos - 0.5) < 0.01, fmt ("audition on E3 at the middle of the Region: one voice, its head at %.4f of the file", pos));
    p.getCommandQueue().push (doc::UiCommand::auditionOff (64));
    run (1);
    check (f->numVoices == 1 && f->voices[0].released, "audition off: the voice releases");
    run (40);
    check (f->numVoices == 0, "and ends");

    p.getCommandQueue().push (doc::UiCommand::auditionOn (60, 0.8));
    p.getCommandQueue().push (doc::UiCommand::auditionOn (67, 0.8));
    run (4);
    const int two = f->numVoices;
    p.getCommandQueue().push (doc::UiCommand::letGoWheel (0));
    p.getCommandQueue().push (doc::UiCommand::previewStart());   // M6: taken and ignored
    p.getCommandQueue().push (doc::UiCommand::midiLearnArm (1)); // M8: taken and ignored
    p.getCommandQueue().push (doc::UiCommand::allNotesOff());
    run (1);
    // A steal fades in 5 ms (240 samples), inside this 256-sample block; a release would take 60 ms.
    check (two == 2 && f->numVoices == 0 && p.getCommandQueue().empty(),
           fmt ("two auditions sound (%d voices); all notes off steals them (5 ms): %d left after one block; every command was taken", two, f->numVoices));
    p.releaseResources();
}

SL_TEST ("plugin", determinismThroughProcessBlock)
{
    section ("determinism through processBlock: 10 s of Strand Demo, Play at beat 1.7 (a chase), live MIDI, Scatter and Skip: bit-identical across block sizes, "
             "with setNonRealtime (true), and after 0, 7 and 1000 stopped samples");

    const double seconds = 10.0;
    const auto total = (juce::int64) (seconds * kSr);
    auto events = harness::chords (kSr, seconds, 3, 0.73, 31);
    for (int k = 0; k < 20; ++k)
        events.push_back (harness::pitchBend ((juce::int64) (k * 23987 + 11), (k * 2731) % 16384));
    events.push_back (harness::controller (90001, 1, 100));    // the mod wheel (Vibrato by default)
    events.push_back (harness::controller (200003, 64, 127));  // sustain down
    events.push_back (harness::controller (260003, 64, 0));    // and up

    auto setUp = [] (StrandLockProcessor& p) {
        setParam (p, params::P::engine, 0.0f);                     // Tape
        setParam (p, params::P::motion, 5.0f);                     // Scatter
        setParam (p, params::P::jump, 12.0f);                      // every 1/16
        setParam (p, params::P::refreshMode, 2.0f);                // Synced
        setParam (p, params::P::refreshSkip, 0.3f);                // Skip
        setParam (p, params::P::heads, 3.0f);
    };
    auto shifted = [&events] (juce::int64 by) {
        auto e = events;
        for (auto& x : e)
            x.sample += by;
        return e;
    };
    auto renderWith = [&] (const harness::BlockSizes& sizes, bool offline, juce::int64 stoppedFor) {
        StrandLockProcessor p;
        setUp (p);
        harness::FakePlayHead head;
        p.setPlayHead (&head);
        p.setNonRealtime (offline);
        auto out = harness::render (p, kSr, total + stoppedFor, sizes, shifted (stoppedFor), 4096, playFrom (head, 1.7, stoppedFor));
        p.setPlayHead (nullptr);
        if (stoppedFor > 0)
        {
            out.left.erase (out.left.begin(), out.left.begin() + stoppedFor);
            out.right.erase (out.right.begin(), out.right.begin() + stoppedFor);
        }
        return out;
    };

    const auto reference = renderWith (harness::fixedBlocks (128), false, 0);
    const double refRms = measure::rms (reference.left.data(), reference.left.size());
    check (refRms > 0.01, fmt ("the scenario plays (rms %.4f: a no-op is not a pass)", refRms));

    struct Run { const char* what; harness::BlockSizes sizes; bool offline; juce::int64 stopped; };
    const Run runs[] = {
        { "1-sample blocks", harness::fixedBlocks (1), false, 0 },
        { "37-sample blocks", harness::fixedBlocks (37), false, 0 },
        { "700-sample blocks", harness::fixedBlocks (700), false, 0 },
        { "4096-sample blocks", harness::fixedBlocks (4096), false, 0 },
        { "random blocks of 1..700", harness::randomBlocks (1, 700, 4242), false, 0 },
        { "setNonRealtime (true), 128-sample blocks", harness::fixedBlocks (128), true, 0 },
        { "setNonRealtime (true), random blocks", harness::randomBlocks (1, 700, 99), true, 0 },
        { "7 stopped samples before Play", harness::cycledBlocks ({ 7, 128 }), false, 7 },
        { "1000 stopped samples before Play", harness::cycledBlocks ({ 1000, 128 }), false, 1000 },
    };
    for (const auto& run : runs)
    {
        const auto out = renderWith (run.sizes, run.offline, run.stopped);
        const double diff = harness::maxDifference (reference, out);
        check (juce::exactlyEqual (diff, 0.0), fmt ("%s: largest difference %.3g", run.what, diff));
    }

    // a no-op is not a pass: Play at beat 1.75 instead of 1.7 changes the output
    StrandLockProcessor q;
    setUp (q);
    harness::FakePlayHead head;
    q.setPlayHead (&head);
    const auto other = harness::render (q, kSr, total, harness::fixedBlocks (128), events, 4096, playFrom (head, 1.75));
    q.setPlayHead (nullptr);
    check (harness::maxDifference (reference, other) > 1e-3, "negative control: Play at beat 1.75 differs");
}

SL_TEST ("plugin", stabilityThroughProcessBlock)
{
    section ("stability (PROMPT 14.6) through processBlock: Strand Demo with 8 heads Tape and 16 live chords for 10 s");

    StrandLockProcessor p;
    setParam (p, params::P::engine, 0.0f);
    setParam (p, params::P::heads, 8.0f);
    harness::FakePlayHead head;
    p.setPlayHead (&head);
    const auto events = harness::chords (kSr, 10.0, 16, 0.5, 2026);
    const auto r = harness::render (p, kSr, (juce::int64) (10.0 * kSr), harness::fixedBlocks (256), events, 4096, playFrom (head, 0.0));
    p.setPlayHead (nullptr);
    const auto st = p.getEngineStats();

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
    check (st.nonFinite == 0 && st.filterResets == 0 && nonFiniteOut == 0,
           fmt ("non-finite samples before the output guard %lld, filter resets %lld, non-finite in the output %lld", (long long) st.nonFinite,
                (long long) st.filterResets, nonFiniteOut));
    check (st.preLimiterPeak > 0.05 && rms > 0.01 && peak <= 0.98,
           fmt ("pre-limiter peak %.3f (> 0.05), output rms %.4f (> 0.01), output peak %.4f (<= 0.98)", st.preLimiterPeak, rms, peak));
    check (p.getLatencySamples() == 0 && p.getBusCount (true) == 0 && p.getTotalNumOutputChannels() == 2 && p.acceptsMidi(),
           "latency 0, no input bus, stereo out, accepts MIDI");
}

SL_TEST ("plugin", voiceAndCloudPlayAsTape)
{
    section ("Notes set to Voice or Cloud play as Tape until M3, and the Diagnostics page says so");

    for (const float engineIndex : { 1.0f, 2.0f, 0.0f })   // Voice, Cloud, Tape
    {
        StrandLockProcessor p;
        useSaw (p);
        setParam (p, params::P::engine, engineIndex);
        const auto events = std::vector<MidiEvent> { harness::noteOn (0, 60, 100), harness::noteOn (0, 64, 100) };
        harness::render (p, kSr, 24000, harness::fixedBlocks (256), events);
        const auto s = p.getDiag().snapshot();
        const auto report = p.getDiag().reportText();
        const bool tape = juce::exactlyEqual (engineIndex, 0.0f);
        const char* name = tape ? "Tape" : juce::exactlyEqual (engineIndex, 1.0f) ? "Voice" : "Cloud";
        check (s.engineReports > 0 && s.voices == 2 && s.tapeStandIns == (tape ? 0 : 2) && s.maxTapeStandIns == (tape ? 0 : 2)
                   && report.contains ("playing as Tape instead of Voice or Cloud: " + juce::String (tape ? 0 : 2) + " now"),
               fmt ("%s: 2 voices, %d of them standing in as Tape (most %d), and the report says so", name, s.tapeStandIns, s.maxTapeStandIns));
    }
}

SL_TEST ("state", stateSavedAndLoadedWhilePlaying)
{
    section ("State with a clip saved and loaded while playing: processBlock on its own thread, the message thread saving, editing and loading");

    StrandLockProcessor p;
    harness::FakePlayHead head;
    head.info.setIsPlaying (true);
    head.info.setBpm (120.0);
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, kSr, 256);
    p.prepareToPlay (kSr, 256);

    // The state as saved while stopped (no block run yet): Strand Demo and the Saw that opens up.
    juce::MemoryBlock stopped;
    p.getStateInformation (stopped);

    std::atomic<bool> stop { false };
    std::atomic<long long> blocks { 0 };
    alloc::Counts audioCounts;
    bool finite = true;
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.ensureSize (256);
        long long b = 0;
        while (! stop.load (std::memory_order_acquire))
        {
            head.info.setPpqPosition ((double) b * 256.0 / 24000.0);
            midi.clear();
            if (b % 50 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + (int) (b % 24), (juce::uint8) 90), 0);
            if (b % 50 == 25)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            alloc::reset();
            {
                const alloc::Scope counting;
                p.processBlock (buffer, midi);
            }
            const auto c = alloc::counts();
            audioCounts.news += c.news;
            audioCounts.deletes += c.deletes;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
            blocks.store (++b, std::memory_order_release);
        }
    });

    auto waitBlocks = [&blocks] (long long more) {
        const long long until = blocks.load (std::memory_order_acquire) + more;
        while (blocks.load (std::memory_order_acquire) < until)
            std::this_thread::yield();
    };

    // Saving while it plays gives the same bytes as saving while stopped.
    waitBlocks (50);
    juce::MemoryBlock playing;
    p.getStateInformation (playing);
    check (playing == stopped, fmt ("a save while playing equals the save while stopped, byte for byte (%zu bytes)", playing.getSize()));

    // Edit the clip (a 1-bar clip of one long note), save that, go back to Strand Demo, then load the saved state:
    // all while the audio thread plays on.
    model::Clip one = model::newClip (1);
    one.notes.push_back (std::make_shared<const model::Note> (model::mkNote (72, 0, 4, "one")));
    const auto outcome = p.editClip ("one note", one);
    p.waitForSnapshot();
    setParam (p, params::P::heads, 3.0f);
    waitBlocks (20);
    juce::MemoryBlock withOne;
    p.getStateInformation (withOne);
    for (int i = 0; i < 5; ++i)
    {
        waitBlocks (10);
        p.setStateInformation (stopped.getData(), (int) stopped.getSize());   // Strand Demo again
        waitBlocks (10);
        p.setStateInformation (withOne.getData(), (int) withOne.getSize());   // and the one-note clip
    }
    waitBlocks (40);
    juce::MemoryBlock after;
    p.getStateInformation (after);
    stop.store (true, std::memory_order_release);
    audio.join();

    const auto* snap = p.getAudioSnapshot();
    check (outcome.accepted && after == withOne && withOne != stopped,
           "after 10 loads while playing, the state is the last one loaded, byte for byte");
    check (snap != nullptr && snap->notes.size() == 1 && juce::exactlyEqual (snap->notes[0].p, 72.0)
               && juce::exactlyEqual (params::plainValueOf (p.getParam (params::P::heads)), 3.0f),
           "the audio thread plays the loaded clip (its one note) with the loaded parameters");
    check (audioCounts.news == 0 && audioCounts.deletes == 0 && finite && blocks.load() > 100,
           fmt ("%lld blocks rendered while it saved and loaded: %lld allocations and %lld frees on the audio thread, every sample finite", blocks.load(),
                audioCounts.news, audioCounts.deletes));
    p.releaseResources();
    p.setPlayHead (nullptr);
}

SL_TEST ("state", lateSoundSilencesClipNotes)
{
    section ("Decision 3: while a loaded state's sound is still being made, clip notes are silent, live keys play and the blocks are counted; when it arrives the note under way comes in at once (fix pass HMS-3, R2-10)");

    // The state: the Saw and a 4-bar clip of one note as long as the clip, so it always sounds and starts again
    // only every 16 beats (8 s at 120 bpm).
    juce::MemoryBlock saved;
    {
        StrandLockProcessor maker;
        useSaw (maker);
        model::Clip whole = model::newClip (4);
        whole.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 16, "whole")));
        maker.editClip ("whole", whole);
        maker.waitForSnapshot();
        maker.getStateInformation (saved);
    }

    StrandLockProcessor p;
    harness::FakePlayHead head;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, kSr, 256);
    p.prepareToPlay (kSr, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (64);
    long long played = 0;
    // One block of the song (120 bpm from ppq 0), with a key going down or up at its start; its peak.
    auto block = [&] (int keyOn, int keyOff) {
        head.info.setIsPlaying (true);
        head.info.setBpm (120.0);
        head.info.setPpqPosition ((double) played * 256.0 / 24000.0);
        midi.clear();
        if (keyOn >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, keyOn, (juce::uint8) 100), 0);
        if (keyOff >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, keyOff), 0);
        p.processBlock (buffer, midi);
        ++played;
        return juce::jmax (buffer.getMagnitude (0, 0, 256), buffer.getMagnitude (1, 0, 256));
    };
    auto peakFor = [&block] (double seconds) {
        float peak = 0.0f;
        for (int i = 0; i < (int) (seconds * kSr / 256.0); ++i)
            peak = juce::jmax (peak, block (-1, -1));
        return peak;
    };
    auto waitedBlocks = [&p] { return p.getDiag().snapshot().blocksWaitingForSound; };

    const float demo = peakFor (0.5);

    // The project's sound cannot be made yet (the worker is held): the host loads the state on its own thread,
    // which waits for the sound, while the audio thread plays on.
    p.getSampleManager().setHeld (true);
    std::atomic<bool> loaded { false };
    std::thread host ([&p, &saved, &loaded] {
        p.setStateInformation (saved.getData(), (int) saved.getSize());
        loaded.store (true);
    });
    for (int spins = 0; waitedBlocks() == 0 && ! loaded.load() && spins < 100000; ++spins)
    {
        block (-1, -1);
        std::this_thread::yield();
    }
    const auto waitingAtStart = waitedBlocks();
    for (int spins = 0; (p.getAudioSnapshot() == nullptr || p.getAudioSnapshot()->notes.size() != 1) && spins < 100000; ++spins)
    {
        block (-1, -1);
        std::this_thread::yield();
    }
    const bool newClip = p.getAudioSnapshot() != nullptr && p.getAudioSnapshot()->notes.size() == 1;
    peakFor (0.1);   // Strand Demo's voices fade out (60 ms)
    const float silent = peakFor (0.3);
    block (64, -1);
    const float live = peakFor (0.1);
    block (-1, 64);
    peakFor (0.1);
    const float silentAgain = peakFor (0.1);
    const auto waitingNow = waitedBlocks();
    const bool stillLoading = ! loaded.load();

    p.getSampleManager().setHeld (false);
    host.join();
    const auto waitingAtArrival = waitedBlocks();
    const double beatAtArrival = (double) played * 256.0 / 24000.0;
    const float first = peakFor (0.05);
    const float on = peakFor (0.2);
    const auto waitingAfter = waitedBlocks();

    check (demo > 0.01 && stillLoading && newClip && waitingAtStart > 0 && juce::exactlyEqual (silent, 0.0f) && juce::exactlyEqual (silentAgain, 0.0f),
           fmt ("while the sound is made: Strand Demo played before (peak %.3f); the loaded clip's note is silent (peak %g, and %g after a live key)", demo,
                (double) silent, (double) silentAgain));
    check (live > 0.01, fmt ("a live key plays meanwhile, on the sound there is (peak %.3f)", (double) live));
    check (waitingNow > waitingAtStart + 100,
           fmt ("the Diagnostics page counts the blocks played without the sound (%llu)", (unsigned long long) waitingNow));
    check (first > 0.01 && on > 0.01,
           fmt ("once the Saw is there (beat %.2f), the note under way sounds within 50 ms (peak %.3f), not at its next start (beat 16)", beatAtArrival,
                (double) first));
    check (waitingAfter == waitingAtArrival, fmt ("the count stops when the sound is there (%llu)", (unsigned long long) waitingAfter));
    p.releaseResources();
    p.setPlayHead (nullptr);
}
