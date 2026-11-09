// The diagnostics model (PLUGIN-PLAN 4 M0, 6): with a scripted play head and scripted MIDI going through the
// processor, DiagModel reports the block sizes, the ppq at every block start, cycle wraps (split and inside a
// block), jumps, tempo, isNonRealtime, the MIDI and the last parameter the host changed, exactly; and the
// message-thread side (keys, drops, display scale) and the Copy report text.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Version.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"

#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using diag::DiagModel;

    /** The size range a block falls in, worked out here without DiagModel: floor (log2 n), at most 13. */
    int expectedBucket (int n) { return std::min (13, (int) std::bit_width ((unsigned) n) - 1); }

    struct Expected
    {
        std::uint64_t blocks = 0, playing = 0, stopped = 0, noPosition = 0, nonRealtime = 0;
        int minBlock = 1 << 30, maxBlock = 0;
        std::array<std::uint64_t, DiagModel::kBuckets> histogram {};
        std::vector<int> sizesInOrder;                 // distinct sizes, first seen first
        std::map<int, std::uint64_t> sizeCounts;
        std::vector<double> starts;                    // the ppq at each block start
        std::vector<bool> startHasPosition;
        std::uint64_t wraps = 0, wrapsInBlock = 0, jumps = 0, tempoChanges = 0;
    };

    /** A scripted host: it sets the play head, runs one block through the processor and books what it did. */
    struct Host
    {
        StrandLockProcessor& p;
        harness::FakePlayHead head;
        Expected e;
        juce::AudioBuffer<float> buffer { 2, 4096 };
        juce::MidiBuffer midi;
        double sampleRate = 48000.0;
        double ppq = 0.0, bpm = 120.0, lastBpm = 120.0;
        bool haveBpm = false;
        double loopStart = 8.0, loopEnd = 10.0;
        bool looping = true;

        explicit Host (StrandLockProcessor& proc) : p (proc)
        {
            p.setPlayHead (&head);
            p.setPlayConfigDetails (0, 2, sampleRate, 4096);
            p.prepareToPlay (sampleRate, 4096);
            midi.ensureSize (4096);
        }

        double beatsIn (int n) const { return n * bpm / 60.0 / sampleRate; }

        void book (int n, bool present, bool playing)
        {
            ++e.blocks;
            e.minBlock = std::min (e.minBlock, n);
            e.maxBlock = std::max (e.maxBlock, n);
            ++e.histogram[(std::size_t) expectedBucket (n)];
            if (e.sizeCounts.count (n) == 0)
                e.sizesInOrder.push_back (n);
            ++e.sizeCounts[n];
            if (! present)
                ++e.noPosition;
            else if (playing)
                ++e.playing;
            else
                ++e.stopped;
            if (p.isNonRealtime())
                ++e.nonRealtime;
            e.starts.push_back (present ? ppq : 0.0);
            e.startHasPosition.push_back (present);
        }

        void run (int n, bool present, bool playing)
        {
            head.present = present;
            head.info = {};
            head.info.setIsPlaying (playing);
            head.info.setIsLooping (looping);
            head.info.setPpqPosition (ppq);
            head.info.setBpm (bpm);
            head.info.setLoopPoints (juce::AudioPlayHead::LoopPoints { loopStart, loopEnd });
            head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 3, 4 });
            if (present)
            {
                if (haveBpm && ! juce::exactlyEqual (bpm, lastBpm))
                    ++e.tempoChanges;
                haveBpm = true;
                lastBpm = bpm;
            }
            book (n, present, playing);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
            if (present && playing)
                ppq += beatsIn (n);
        }

        void stoppedBlock (int n) { run (n, true, false); }
        void noPositionBlock (int n) { run (n, false, false); }
        void play (int n) { run (n, true, true); }
    };
}

SL_TEST ("diag", hostBlocksAndPositions)
{
    section ("Diagnostics: a scripted Cubase (no position, stopped, playing a cycle, wraps, a locate, tempo, offline)");

    StrandLockProcessor proc;
    Host h (proc);

    // No position yet, then stopped.
    for (int i = 0; i < 5; ++i)
        h.noPositionBlock (256);
    for (int i = 0; i < 7; ++i)
        h.stoppedBlock (100);

    // Playing the 2-beat cycle 8..10 at 120 bpm in 0.1-beat blocks, from 9.5.
    h.ppq = 9.5;
    while (h.ppq < 10.0 - 1e-9)
        h.play (2400);

    // Wrap 1: the host splits the block at the cycle end; the next block starts at the cycle start.
    h.ppq = 8.0;
    ++h.e.wraps;
    while (h.ppq < 9.9 - 1e-9)
        h.play (2400);
    h.play (2400);

    // Wrap 2: the start Cubase reports after a wrap is a little off (its +-0.003 beat jitter).
    h.ppq = 8.003;
    ++h.e.wraps;
    while (h.ppq < 9.9 - 1e-9)
        h.play (2400);

    // Wrap 3, inside an ASIO-Guard sized block: 4096 samples from 9.9 run over the cycle end at 10.
    const double over = h.ppq + h.beatsIn (4096) - h.loopEnd;
    ++h.e.wrapsInBlock;
    h.play (4096);
    h.ppq = h.loopStart + over;
    ++h.e.wraps;
    for (int i = 0; i < 4; ++i)
        h.play (37);

    // A locate to just before the cycle end (a jump), then a block that ends exactly there: not a wrap inside a block.
    h.ppq = h.loopEnd - h.beatsIn (480);
    ++h.e.jumps;
    h.play (480);
    h.ppq = h.loopStart;
    ++h.e.wraps;
    h.play (480);

    // A small step in the reported position (less than 0.05 beat) is jitter: neither a wrap nor a jump.
    h.ppq += 0.003;
    h.play (480);

    // A tempo change, then a locate forward (a jump, not a wrap).
    h.bpm = 90.0;
    h.play (3200);
    h.play (3200);
    h.ppq += 0.9;
    ++h.e.jumps;
    h.play (3200);

    // Three blocks rendered offline (Cubase's export), then real time again.
    proc.setNonRealtime (true);
    for (int i = 0; i < 3; ++i)
        h.play (1000);
    proc.setNonRealtime (false);

    // Stopped, then playing with the cycle off: going back is a jump.
    for (int i = 0; i < 4; ++i)
        h.stoppedBlock (1);
    h.looping = false;
    h.ppq = 9.0;
    h.play (64);
    h.ppq = 8.0;
    ++h.e.jumps;
    h.play (64);

    const auto s = proc.getDiag().snapshot();
    const auto& e = h.e;

    check (s.sampleRate == 48000.0 && s.preparedBlockSize == 4096, fmt ("prepared at %.0f Hz, %d samples", s.sampleRate, s.preparedBlockSize));
    check (s.blocks == e.blocks && s.minBlock == e.minBlock && s.maxBlock == e.maxBlock && s.lastBlock == 64,
           fmt ("%llu blocks (%llu), sizes %d..%d (%d..%d), last %d", (unsigned long long) s.blocks, (unsigned long long) e.blocks,
                s.minBlock, s.maxBlock, e.minBlock, e.maxBlock, s.lastBlock));
    check (s.histogram == e.histogram, "the size histogram (1, 2-3, 4-7, ... 8192 and up) counts every block in its range");

    bool exactOk = (int) e.sizesInOrder.size() > DiagModel::kExactSizes && s.exactSizes.size() == (std::size_t) DiagModel::kExactSizes;
    std::uint64_t others = 0;
    for (std::size_t i = 0; i < e.sizesInOrder.size(); ++i)
    {
        const int size = e.sizesInOrder[i];
        if (i < (std::size_t) DiagModel::kExactSizes)
            exactOk = exactOk && i < s.exactSizes.size() && s.exactSizes[i].first == size && s.exactSizes[i].second == e.sizeCounts.at (size);
        else
            others += e.sizeCounts.at (size);
    }
    check (exactOk && s.otherSizes == others,
           fmt ("the first %d distinct sizes are counted exactly, the %d others together (%llu)", DiagModel::kExactSizes,
                (int) e.sizesInOrder.size() - DiagModel::kExactSizes, (unsigned long long) s.otherSizes));

    check (s.playingBlocks == e.playing && s.stoppedBlocks == e.stopped && s.noPositionBlocks == e.noPosition,
           fmt ("blocks while playing %llu (%llu), while stopped %llu (%llu), with no position %llu (%llu)",
                (unsigned long long) s.playingBlocks, (unsigned long long) e.playing, (unsigned long long) s.stoppedBlocks,
                (unsigned long long) e.stopped, (unsigned long long) s.noPositionBlocks, (unsigned long long) e.noPosition));
    check (s.nonRealtimeBlocks == 3 && e.nonRealtime == 3 && ! s.nonRealtimeNow, fmt ("3 offline blocks (%llu), real time now",
                                                                                 (unsigned long long) s.nonRealtimeBlocks));

    bool startsOk = s.blockStarts.size() == e.starts.size();
    for (std::size_t i = 0; startsOk && i < e.starts.size(); ++i)
        startsOk = s.blockStarts[i].hasPosition == e.startHasPosition[i]
                && (! e.startHasPosition[i] || juce::exactlyEqual (s.blockStarts[i].ppq, e.starts[i]))
                && s.blockStarts[i].block == i;
    check (startsOk, fmt ("the ppq at each of the %d block starts, exactly as the host gave it (%d kept)", (int) e.starts.size(), (int) s.blockStarts.size()));

    check (s.cycleWraps == e.wraps, fmt ("cycle wraps: %llu (%llu)", (unsigned long long) s.cycleWraps, (unsigned long long) e.wraps));
    check (s.wrapsInsideBlock == e.wrapsInBlock, fmt ("wraps inside a block: %llu (%llu)", (unsigned long long) s.wrapsInsideBlock,
                                                      (unsigned long long) e.wrapsInBlock));
    check (s.jumps == e.jumps, fmt ("other jumps: %llu (%llu)", (unsigned long long) s.jumps, (unsigned long long) e.jumps));
    bool wrapList = s.wraps.size() == e.wraps + e.jumps;
    if (wrapList)
        wrapList = juce::exactlyEqual (s.wraps[1].after, 8.003) && s.wraps[1].looping && ! s.wraps.back().looping
                && std::abs (s.wraps[0].before - 10.0) < 1e-9;
    check (wrapList, "each wrap keeps the position before and after it (10.000 -> 8.000, then 10.000 -> 8.003, ...)");

    check (s.hasBpm && juce::exactlyEqual (s.bpm, 90.0) && juce::exactlyEqual (s.minBpm, 90.0) && juce::exactlyEqual (s.maxBpm, 120.0)
               && s.tempoChanges == e.tempoChanges && e.tempoChanges == 1,
           fmt ("tempo %.1f (min %.1f, max %.1f), %llu change", s.bpm, s.minBpm, s.maxBpm, (unsigned long long) s.tempoChanges));
    check (s.hasPosition && s.playing && ! s.looping && s.hasLoop && juce::exactlyEqual (s.loopStart, 8.0) && juce::exactlyEqual (s.loopEnd, 10.0)
               && s.sigNum == 3 && s.sigDen == 4,
           "the latest position: playing, cycle off, cycle 8 to 10, 3/4");
}

SL_TEST ("diag", midiAndHostParameters)
{
    section ("Diagnostics: note, CC, pitch bend and mod wheel numbers, and the last parameter the host changed");

    StrandLockProcessor proc;
    Host h (proc);

    auto add = [&h] (std::initializer_list<int> bytes, int sample)
    {
        juce::uint8 b[3] {};
        int n = 0;
        for (const int x : bytes)
            b[n++] = (juce::uint8) x;
        h.midi.addEvent (b, n, sample);
    };

    h.ppq = 8.0;
    add ({ 0x90, 60, 100 }, 0);
    add ({ 0x90, 64, 80 }, 17);
    add ({ 0xb0, 1, 77 }, 30);     // the mod wheel
    h.play (256);
    add ({ 0x80, 60, 0 }, 5);
    add ({ 0xb0, 7, 50 }, 6);
    add ({ 0xe0, 12000 & 0x7f, 12000 >> 7 }, 200);
    add ({ 0x90, 64, 0 }, 255);    // velocity 0 is a note off
    add ({ 0xd0, 33 }, 255);       // channel pressure: counted as other
    h.play (256);

    const auto s = proc.getDiag().snapshot();
    check (s.noteOns == 2 && s.noteOffs == 2 && s.controllers == 2 && s.pitchBends == 1 && s.otherMidi == 1,
           fmt ("2 note on, 2 note off (one of them velocity 0), 2 controllers, 1 pitch bend, 1 other: %llu %llu %llu %llu %llu",
                (unsigned long long) s.noteOns, (unsigned long long) s.noteOffs, (unsigned long long) s.controllers,
                (unsigned long long) s.pitchBends, (unsigned long long) s.otherMidi));
    check (s.lastNote == 64 && s.lastVelocity == 80 && s.lastController == 7 && s.lastControllerValue == 50
               && s.modWheel == 77 && s.lastPitchBend == 12000,
           fmt ("last note 64 at 80 (%d at %d), last CC 7 = 50 (%d = %d), mod wheel 77 (%d), bend 12000 (%d)", s.lastNote, s.lastVelocity,
                s.lastController, s.lastControllerValue, s.modWheel, s.lastPitchBend));

    bool order = s.midi.size() == 8;
    if (order)
        order = s.midi[0].kind == diag::EventKind::noteOn && s.midi[0].a == 60 && s.midi[0].b == 100 && s.midi[0].block == 0
             && s.midi[1].sample == 17 && s.midi[2].kind == diag::EventKind::controller && s.midi[2].a == 1 && s.midi[2].b == 77
             && s.midi[3].kind == diag::EventKind::noteOff && s.midi[3].block == 1 && s.midi[3].sample == 5
             && s.midi[5].kind == diag::EventKind::pitchBend && s.midi[5].a == 12000 && s.midi[5].sample == 200
             && s.midi[6].kind == diag::EventKind::noteOff && s.midi[6].a == 64 && s.midi[7].kind == diag::EventKind::otherMidi
             && s.midi[7].a == 0xd0;
    check (order, fmt ("the recent MIDI list holds every event in order, with its block and sample (%d kept)", (int) s.midi.size()));

    // The host moves Speed (a Quick Control, or automation): reported. A StrandLock control's own gesture and a state
    // load are not the host's.
    check (s.lastHostParam == -1, "no host parameter change yet");
    proc.getParam (params::P::macro1).setValueNotifyingHost (0.8f);

    auto& spread = proc.getParam (params::P::macro2);
    spread.beginChangeGesture();
    spread.setValueNotifyingHost (0.1f);
    spread.endChangeGesture();

    juce::MemoryBlock saved;
    {
        StrandLockProcessor other;
        params::setPlainValue (other.getParam (params::P::pitch), params::index (params::P::pitch), 12.0f);
        other.getStateInformation (saved);
    }
    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    const auto t = proc.getDiag().snapshot();
    check (t.lastHostParam == params::index (params::P::macro1) && std::abs (t.lastHostParamValue - 0.8f) < 1e-6f && t.hostParamChanges == 1,
           fmt ("the last host change is Speed (macro1) = 0.8 (#%d = %.3f), counted once (%llu): the gesture and the state load are not counted",
                t.lastHostParam, (double) t.lastHostParamValue, (unsigned long long) t.hostParamChanges));
    check (proc.getDiag().reportText().contains ("Last parameter the host changed: Speed (macro1) = "),
           "the report names it with its readout");
}

SL_TEST ("diag", messageThreadAndReport)
{
    section ("Diagnostics: keys with the Keys switch, dropped files and text, the display scale, and the Copy report text");

    StrandLockProcessor proc;
    Host h (proc);
    h.ppq = 8.0;
    for (int i = 0; i < 3; ++i)
        h.play (512);

    auto& d = proc.getDiag();
    d.keyReceived ("1", false);
    d.keyReceived ("Space", false);
    d.keyReceived ("ctrl + Z", true);

    // A one-second stereo WAV at 44.1 kHz, as Explorer would drop it, and a path that does not exist.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLockTests");
    dir.createDirectory();
    const auto wavFile = dir.getNonexistentChildFile ("drop", ".wav", false);
    {
        juce::AudioBuffer<float> one (2, 44100);
        for (int i = 0; i < 44100; ++i)
            for (int ch = 0; ch < 2; ++ch)
                one.setSample (ch, i, 0.25f * std::sin (0.05f * (float) i));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wavFile);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (16));
        check (writer != nullptr && writer->writeFromAudioSampleBuffer (one, 0, 44100), "a test WAV is written");
    }
    const auto missing = dir.getChildFile ("no such file.wav").getFullPathName();
    juce::StringArray dropped;
    dropped.add (wavFile.getFullPathName());
    dropped.add (missing);
    d.filesDropped (dropped);
    d.textDropped ("MediaBay: Pad 01");
    d.setDisplayScale (1.5, 125);

    const auto s = d.snapshot();
    check (s.keys.size() == 3 && s.keys[0].key == "1" && ! s.keys[0].keysSwitchOn && s.keys[2].key == "ctrl + Z" && s.keys[2].keysSwitchOn,
           "the keys received, in order, each with the Keys switch state");
    const bool wavOk = s.drops.size() == 3 && s.drops[0].isFile && s.drops[0].readable && s.drops[0].frames == 44100
                    && juce::exactlyEqual (s.drops[0].sampleRate, 44100.0) && std::abs (s.drops[0].seconds - 1.0) < 1e-12 && s.drops[0].channels == 2
                    && s.drops[0].bytes == wavFile.getSize();
    check (wavOk, fmt ("the dropped WAV: its path, %lld frames, %.3f s at %.0f Hz, %d channels",
                       s.drops.empty() ? 0ll : (long long) s.drops[0].frames, s.drops.empty() ? 0.0 : s.drops[0].seconds,
                       s.drops.empty() ? 0.0 : s.drops[0].sampleRate, s.drops.empty() ? 0 : s.drops[0].channels));
    check (s.drops.size() == 3 && s.drops[1].isFile && ! s.drops[1].readable && s.drops[1].bytes == -1 && ! s.drops[2].isFile
               && s.drops[2].what == "MediaBay: Pad 01",
           "a path that does not exist is listed as not found, and dropped text as text");
    check (juce::exactlyEqual (s.desktopScale, 1.5) && s.uiScalePercent == 125, "the display scale the editor reports");

    const auto report = d.reportText();
    const auto& info = proc.getHostInfo();
    struct Want { const char* what; juce::String text; };
    const Want wants[] = {
        { "the plugin version", juce::String ("StrandLock ") + core::kPluginVersion + " diagnostics report" },
        { "the host", "Host: " + info.summary() },
        { "the sample rate and block size", "Sample rate: 48000 Hz; block size given at prepare: 4096" },
        { "the display scale", "Display scale: Windows 150 %, StrandLock UI 125 %" },
        { "the block sizes", "Blocks: 3 (min 512, max 512, last 512 samples)" },
        { "blocks while stopped", "while stopped: 0" },
        { "offline blocks", "Offline (non-real-time) blocks: 0; now: real time" },
        { "the cycle counters", "Cycle wraps: 0; wraps inside a block: 0" },
        { "the keys", "Keys received (3): 1 [Keys off]; Space [Keys off]; ctrl + Z [Keys on];" },
        { "the dropped file", wavFile.getFullPathName() + " (44100 frames, 1.000 s at 44100 Hz, 2 channels" },
        { "the missing file", missing + " (not found)" },
        { "the dropped text", "Dropped text: \"MediaBay: Pad 01\"" },
    };
    for (const auto& w : wants)
        check (report.contains (w.text), fmt ("the report has %s: \"%s\"", w.what, w.text.toRawUTF8()));
    check (info.name.isNotEmpty() && info.format.isNotEmpty(), fmt ("the host is named (%s)", info.summary().toRawUTF8()));

    wavFile.deleteFile();
}
