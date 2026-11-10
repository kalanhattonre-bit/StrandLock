// host/HostSync and host/MidiRouter (PLUGIN-PLAN 1.10, 8.2 #3-#5, #20, #24) against a recording fake of the
// engine's public API: a cycle wrap inside a 2048-sample block (1, 200 and 2047 samples in) splits the block
// there; negative beats pass through; while stopped the host beat goes only when it moved more than 1e-6; the
// tempo falls back to 120; offsetBeats is subtracted; every MIDI mapping (note on vel / 127, velocity 0, note
// off, the mod CCs, CC64, CC120 and CC123, the pitch wheel with full up exactly +1) lands on its own sample.

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/Travel.h"
#include "diag/DiagModel.h"
#include "engine/Engine.h"
#include "host/HostSync.h"
#include "host/MidiRouter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** Records every call, with the sample (samples rendered so far) it came at. */
    struct FakeEngine
    {
        enum class Kind { transport, noteOn, noteOff, allOff, ctrl, process };

        struct Call
        {
            Kind kind = Kind::process;
            long long at = 0;          // samples rendered before the call
            double a = 0.0, b = 0.0;   // noteOn p, vel; noteOff p; ctrl value; process n
            engine::Ctrl ctrl = engine::Ctrl::mod1;
            engine::Transport t;
        };

        std::vector<Call> calls;
        long long rendered = 0;
        float* expectL = nullptr;   // where the next process must write (the block's buffer at `rendered`)

        void setTransport (const engine::Transport& t) { Call c; c.kind = Kind::transport; c.at = rendered; c.t = t; calls.push_back (c); }
        void noteOn (double p, double vel) { Call c; c.kind = Kind::noteOn; c.at = rendered; c.a = p; c.b = vel; calls.push_back (c); }
        void noteOff (double p) { Call c; c.kind = Kind::noteOff; c.at = rendered; c.a = p; calls.push_back (c); }
        void allNotesOff() { Call c; c.kind = Kind::allOff; c.at = rendered; calls.push_back (c); }
        void setController (engine::Ctrl ctrl, double v) { Call c; c.kind = Kind::ctrl; c.at = rendered; c.ctrl = ctrl; c.a = v; calls.push_back (c); }
        void process (float* L, float* R, int n)
        {
            Call c;
            c.kind = Kind::process;
            c.at = rendered;
            c.a = n;
            calls.push_back (c);
            for (int i = 0; i < n; ++i)
            {
                L[i] = (float) (rendered + i);
                R[i] = -(float) (rendered + i);
            }
            rendered += n;
        }

        std::vector<Call> of (Kind k) const
        {
            std::vector<Call> out;
            for (const auto& c : calls)
                if (c.kind == k)
                    out.push_back (c);
            return out;
        }
    };

    diag::HostBlock playingAt (double ppq, double bpm = 120.0)
    {
        diag::HostBlock h;
        h.hasPosition = true;
        h.playing = true;
        h.hasPpq = true;
        h.ppq = ppq;
        h.hasBpm = true;
        h.bpm = bpm;
        return h;
    }

    diag::HostBlock cycling (double ppq, double loopStart, double loopEnd)
    {
        auto h = playingAt (ppq);
        h.looping = true;
        h.hasLoop = true;
        h.loopStart = loopStart;
        h.loopEnd = loopEnd;
        return h;
    }

    bool same (double a, double b) { return a == b || (std::isnan (a) && std::isnan (b)); }

    /** Plays one block of n samples through MidiRouter::renderBlock and checks the buffer holds every sample
        exactly once, in order. */
    bool playBlock (FakeEngine& e, const host::BlockTransport& t, const juce::MidiBuffer& midi, int n, const host::MidiSettings& s = {})
    {
        std::vector<float> L ((std::size_t) n, -1.0f), R ((std::size_t) n, 1.0f);
        const long long start = e.rendered;
        host::MidiRouter::renderBlock (e, t, midi, s, L.data(), R.data(), n);
        bool ok = e.rendered == start + n;
        for (int i = 0; ok && i < n; ++i)
            ok = L[(std::size_t) i] == (float) (start + i);
        return ok;
    }

    juce::MidiBuffer midiOf (std::initializer_list<std::pair<int, juce::MidiMessage>> events)
    {
        juce::MidiBuffer m;
        for (const auto& [at, msg] : events)
            m.addEvent (msg, at);
        return m;
    }
}

//==================================================================================================
SL_TEST ("hostsync", cycleWrapInsideBlock)
{
    section ("HostSync: a host cycle wrap 1, 200 and 2047 samples into a 2048-sample block splits the block there");

    const double sr = 48000.0, bps = 120.0 / 60.0 / sr;   // beats per sample at 120 bpm
    for (const int wrapAt : { 1, 200, 2047 })
    {
        host::HostSync sync;
        sync.prepare (sr);
        // The host's block starts (wrapAt - 0.5) samples before the loop end, so sample wrapAt is the first past it.
        const double ppq = 12.0 - (wrapAt - 0.5) * bps;
        const auto t = sync.plan (cycling (ppq, 4.0, 12.0), 2048, 1.5, true);
        check (t.wrapAt == wrapAt && t.start.playing && same (t.start.beat, ppq - 1.5) && t.wrap.playing && same (t.wrap.beat, 4.0 - 1.5)
                   && t.wrap.bpm == 120.0 && t.wrap.loop,
               fmt ("wrap %d samples in: split at %d; beat %.6f at sample 0, then loopStart - offsetBeats = 2.5 at the wrap", wrapAt, t.wrapAt, t.start.beat));

        FakeEngine e;
        const bool contiguous = playBlock (e, t, {}, 2048);
        const auto tr = e.of (FakeEngine::Kind::transport);
        const auto pr = e.of (FakeEngine::Kind::process);
        check (contiguous && tr.size() == 2 && tr[0].at == 0 && tr[1].at == wrapAt && same (tr[1].t.beat, 2.5) && pr.size() == 2
                   && pr[0].a == wrapAt && pr[1].a == 2048 - wrapAt,
               fmt ("the engine renders %d samples, gets the wrap's setTransport, renders the other %d (every sample once, in order)", wrapAt, 2048 - wrapAt));
    }

    host::HostSync sync;
    sync.prepare (sr);
    const auto atEnd = sync.plan (cycling (12.0 - 2048 * bps, 4.0, 12.0), 2048, 0.0, true);
    const auto notLooping = sync.plan (playingAt (12.0 - 100 * bps), 2048, 0.0, true);
    const auto pastEnd = sync.plan (cycling (12.5, 4.0, 12.0), 2048, 0.0, true);
    check (atEnd.wrapAt == -1 && notLooping.wrapAt == -1 && pastEnd.wrapAt == -1,
           "no split when the loop end is the next block's first sample, when the host is not looping, or when it is past the loop end");

    // An event on the wrap's own sample comes after the wrap.
    host::HostSync s2;
    s2.prepare (sr);
    const auto t = s2.plan (cycling (12.0 - 199.5 * bps, 4.0, 12.0), 2048, 0.0, true);
    FakeEngine e;
    const auto midi = midiOf ({ { 100, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }, { 200, juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100) },
                                { 300, juce::MidiMessage::noteOff (1, 60) } });
    const bool contiguous = playBlock (e, t, midi, 2048);
    std::string order;
    for (const auto& c : e.calls)
        order += c.kind == FakeEngine::Kind::transport ? "T" : c.kind == FakeEngine::Kind::noteOn ? "N" : c.kind == FakeEngine::Kind::noteOff ? "F" : "p";
    check (contiguous && order == "TpNpTNpFp", fmt ("with notes at 100, 200 (the wrap) and 300: %s (transport, render, note, render, wrap, note, ...)", order.c_str()));
}

SL_TEST ("hostsync", transportRules)
{
    section ("HostSync: pre-roll, stopped, tempo fallback, offsetBeats");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    host::HostSync sync;
    sync.prepare (48000.0);

    // Pre-roll: negative beats pass through (8.2 #4), with the clip's loop setting.
    const auto pre = sync.plan (playingAt (-2.0), 512, 0.0, true);
    const auto preOffset = sync.plan (playingAt (-2.0), 512, 1.25, false);
    check (pre.start.playing && same (pre.start.beat, -2.0) && pre.start.loop && same (preOffset.start.beat, -3.25) && ! preOffset.start.loop,
           fmt ("playing from ppq -2: beat %.2f (never wrapped into the clip); with offsetBeats 1.25: %.2f; loop follows the clip", pre.start.beat, preOffset.start.beat));

    // Stopped: the host beat only when it moved more than 1e-6 beat since the last block.
    diag::HostBlock stopped;
    stopped.hasPosition = true;
    stopped.playing = false;
    stopped.hasPpq = true;
    stopped.ppq = 5.0;
    stopped.hasBpm = true;
    stopped.bpm = 100.0;
    host::HostSync st;
    st.prepare (48000.0);
    const auto s1 = st.plan (stopped, 512, 0.5, true);
    const auto s2 = st.plan (stopped, 512, 0.5, true);
    stopped.ppq = 5.0 + 5e-7;
    const auto s3 = st.plan (stopped, 512, 0.5, true);
    stopped.ppq = 5.0 + 5e-7 + 2e-6;
    const auto s4 = st.plan (stopped, 512, 0.5, true);
    const auto s5 = st.plan (stopped, 512, 0.5, true);
    check (! s1.start.playing && same (s1.start.beat, 4.5) && std::isnan (s2.start.beat) && std::isnan (s3.start.beat)
               && same (s4.start.beat, 5.0 + 5e-7 + 2e-6 - 0.5) && std::isnan (s5.start.beat) && s1.start.bpm == 100.0 && s1.wrapAt == -1,
           "stopped: the first block passes the host beat, then NaN while it stays (or moves 5e-7), the beat again after a 2e-6 move, then NaN");
    stopped.hasPpq = false;
    const auto s6 = st.plan (stopped, 512, 0.5, true);
    stopped.hasPpq = true;
    const auto s7 = st.plan (stopped, 512, 0.5, true);
    check (std::isnan (s6.start.beat) && same (s7.start.beat, 5.0 + 5e-7 + 2e-6 - 0.5),
           "a block with no position sends NaN, and the next block with one passes it again");

    // Tempo: the host's in 1..1000, else 120.
    struct T { const char* what; bool has; double bpm; double want; };
    const T tempos[] = { { "87 bpm", true, 87.0, 87.0 }, { "none", false, 0.0, 120.0 }, { "0", true, 0.0, 120.0 }, { "1500", true, 1500.0, 120.0 },
                         { "NaN", true, nan, 120.0 }, { "1", true, 1.0, 1.0 }, { "1000", true, 1000.0, 1000.0 } };
    bool tempoOk = true;
    std::string got;
    for (const auto& x : tempos)
    {
        auto h = playingAt (8.0);
        h.hasBpm = x.has;
        h.bpm = x.bpm;
        const auto r = sync.plan (h, 64, 0.0, true);
        tempoOk = tempoOk && r.start.bpm == x.want;
        got += fmt ("%s -> %g; ", x.what, r.start.bpm);
    }
    check (tempoOk, "tempo: " + got);

    // No play head position at all: stopped, no beat, 120 bpm.
    diag::HostBlock none;
    const auto n0 = sync.plan (none, 64, 0.0, false);
    check (! n0.start.playing && std::isnan (n0.start.beat) && n0.start.bpm == 120.0 && ! n0.start.loop,
           "no position from the host: stopped, beat NaN, 120 bpm, the clip's loop");

    // A wrap uses the fallback tempo too: 87 bpm missing -> 120.
    auto h = cycling (12.0 - 9.5 * (120.0 / 60.0 / 48000.0), 0.0, 12.0);
    h.hasBpm = false;
    const auto w = sync.plan (h, 64, 0.0, true);
    check (w.wrapAt == 10, fmt ("the wrap sample is found at the fallback tempo (%d)", w.wrapAt));
}

SL_TEST ("hostsync", midiMappings)
{
    section ("MidiRouter: every MIDI mapping, each event on its own sample");

    FakeEngine e;
    host::MidiSettings s;   // mod1 CC1, mod2 CC2
    host::BlockTransport t;
    t.start.playing = false;

    const auto midi = midiOf ({
        { 0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
        { 37, juce::MidiMessage::noteOn (4, 64, (juce::uint8) 127) },
        { 37, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 0) },
        { 100, juce::MidiMessage::noteOff (1, 64) },
        { 120, juce::MidiMessage::controllerEvent (1, 1, 32) },
        { 130, juce::MidiMessage::controllerEvent (1, 2, 127) },
        { 140, juce::MidiMessage::controllerEvent (1, 64, 64) },
        { 150, juce::MidiMessage::controllerEvent (1, 64, 63) },
        { 160, juce::MidiMessage::controllerEvent (1, 120, 0) },
        { 170, juce::MidiMessage::controllerEvent (1, 123, 0) },
        { 175, juce::MidiMessage::controllerEvent (1, 121, 0) },
        { 180, juce::MidiMessage::pitchWheel (1, 0) },
        { 190, juce::MidiMessage::pitchWheel (1, 8192) },
        { 200, juce::MidiMessage::pitchWheel (1, 16383) },
        { 210, juce::MidiMessage::pitchWheel (1, 4096) },
        { 220, juce::MidiMessage::pitchWheel (1, 12287) },
        { 230, juce::MidiMessage::channelPressureChange (1, 90) },
        { 240, juce::MidiMessage::programChange (1, 5) },
        { 250, juce::MidiMessage::aftertouchChange (1, 60, 50) },
        { 511, juce::MidiMessage::noteOn (1, 72, (juce::uint8) 1) },
    });
    const bool contiguous = playBlock (e, t, midi, 512, s);

    std::vector<FakeEngine::Call> events;
    for (const auto& c : e.calls)
        if (c.kind != FakeEngine::Kind::process && c.kind != FakeEngine::Kind::transport)
            events.push_back (c);
    check (contiguous && events.size() == 16, fmt ("16 events reach the engine (pressure, program change and aftertouch are ignored): %d", (int) events.size()));
    if (events.size() != 16)
        return;

    using K = FakeEngine::Kind;
    auto is = [&] (std::size_t i, K k, long long at, double a, double b = 0.0)
    {
        const auto& c = events[i];
        return c.kind == k && c.at == at && c.a == a && c.b == b;
    };
    check (is (0, K::noteOn, 0, 60, 100.0 / 127.0) && is (1, K::noteOn, 37, 64, 127.0 / 127.0),
           "note on -> noteOn (p, vel / 127) at its sample, any channel (100 -> 0.787402, 127 -> 1)");
    check (is (2, K::noteOff, 37, 60) && is (3, K::noteOff, 100, 64), "velocity 0 and note off -> noteOff, after an event on the same sample, in order");
    check (is (4, K::ctrl, 120, 32.0 / 127.0) && events[4].ctrl == engine::Ctrl::mod1 && is (5, K::ctrl, 130, 1.0) && events[5].ctrl == engine::Ctrl::mod2,
           "CC1 -> mod1 (value / 127), CC2 -> mod2");
    check (std::abs (std::abs (core::travelSpeed (events[4].a)) - 0.98236) < 1e-4 && core::travelSpeed (events[4].a) < 0.0,
           fmt ("CC1 value 32 is 32/127, which a Travel target plays at %.5fx (the live row's -0.98236x)", core::travelSpeed (events[4].a)));
    check (is (6, K::ctrl, 140, 1.0) && events[6].ctrl == engine::Ctrl::sustain && is (7, K::ctrl, 150, 0.0) && events[7].ctrl == engine::Ctrl::sustain,
           "CC64 64 -> sustain down (1), CC64 63 -> up (0)");
    check (is (8, K::allOff, 160, 0) && is (9, K::allOff, 170, 0), "CC120 and CC123 -> allNotesOff; CC121 does nothing");
    check (is (10, K::ctrl, 180, -1.0) && is (11, K::ctrl, 190, 0.0) && is (12, K::ctrl, 200, 1.0) && is (13, K::ctrl, 210, -0.5)
               && is (14, K::ctrl, 220, 4095.0 / 8191.0) && events[12].ctrl == engine::Ctrl::bend,
           fmt ("the pitch wheel: 0 -> -1, 8192 -> 0, 16383 -> +1 exactly (%.17g), 4096 -> -0.5, 12287 -> 4095/8191", events[12].a));

    // The note on at sample 511 (velocity 1) is the last event: 1/127.
    const auto ons = e.of (K::noteOn);
    check (ons.size() == 3 && ons[2].at == 511 && ons[2].b == 1.0 / 127.0 && e.rendered == 512, "an event on the block's last sample lands there");

    // The wheels follow the CCs the INST state names.
    FakeEngine f;
    host::MidiSettings remapped;
    remapped.mod1Cc = 11;
    remapped.mod2Cc = 11;
    const auto cc = midiOf ({ { 0, juce::MidiMessage::controllerEvent (1, 11, 64) }, { 1, juce::MidiMessage::controllerEvent (1, 1, 64) } });
    playBlock (f, t, cc, 8, remapped);
    const auto ctrls = f.of (K::ctrl);
    check (ctrls.size() == 2 && ctrls[0].ctrl == engine::Ctrl::mod1 && ctrls[1].ctrl == engine::Ctrl::mod2 && ctrls[0].a == 64.0 / 127.0,
           "with both wheels on CC11: CC11 moves both, CC1 moves neither");

    static_assert (host::bendOf (16383) == 1.0 && host::bendOf (0) == -1.0 && host::bendOf (8192) == 0.0, "the pitch wheel's ends");
}
