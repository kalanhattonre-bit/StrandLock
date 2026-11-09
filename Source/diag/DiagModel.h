#pragma once

// The Diagnostics page's model (PLUGIN-PLAN 4 M0, 6): everything Kalan's Cubase checklist asks about, so the
// page and its "Copy report" text can answer it.
//
// Fed from the audio thread without locks or allocation (audioBlock, audioMidi: atomics for the totals, the
// DiagLog ring for the recent history), from any thread for the parameters the host changes
// (hostParameterChanged: atomics only), and from the message thread for what the editor sees (keys, drops,
// the display scale). Read on the message thread (snapshot, reportText), which drains the ring first.

#include "DiagLog.h"
#include "host/HostInfo.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace sl::diag
{
    /** What the host said about one block, read from its play head (audio thread, no allocation). */
    struct HostBlock
    {
        int numSamples = 0;
        bool hasPosition = false;   // the play head gave a position at all
        bool playing = false, looping = false, recording = false;
        bool hasPpq = false;
        double ppq = 0.0;
        bool hasBpm = false;
        double bpm = 0.0;
        bool hasLoop = false;
        double loopStart = 0.0, loopEnd = 0.0;
        bool hasTimeSig = false;
        int sigNum = 4, sigDen = 4;
        bool nonRealtime = false;
    };

    /** Reads the play head (may be nullptr) for a block of numSamples. */
    HostBlock readHostBlock (juce::AudioPlayHead* playHead, int numSamples, bool nonRealtime) noexcept;

    class DiagModel
    {
    public:
        static constexpr int kBuckets = 14;      // block sizes 1, 2-3, 4-7, ..., 4096-8191, 8192 and up
        static constexpr int kExactSizes = 8;    // the first 8 different block sizes are counted exactly
        static constexpr std::size_t kKeepBlockStarts = 512, kKeepWraps = 32, kKeepMidi = 128, kKeepKeys = 64, kKeepDrops = 16;

        /** A backward (or forward) move of the host position by more than this many beats between two playing
            blocks is a wrap or a jump; Cubase's cycle jitter is about 0.003 beat. */
        static constexpr double kJumpBeats = 0.05;

        DiagModel();
        ~DiagModel();

        //==========================================================================================
        // Audio thread (one writer). prepare may also run on the message thread before audio starts.

        void prepare (double sampleRate, int maxBlockSize) noexcept;
        void audioBlock (const HostBlock& block) noexcept;
        void audioMidi (const std::uint8_t* data, int size, int samplePosition) noexcept;

        /** Any thread: a parameter the host (or anything but StrandLock's own controls) changed. */
        void hostParameterChanged (int paramIndex, float plainValue) noexcept;

        //==========================================================================================
        // Message thread.

        /** A key the editor received, and whether the Keys switch was on (checklist steps 4 and 5). */
        void keyReceived (const juce::String& keyDescription, bool keysSwitchOn);

        /** Files dropped on the window (checklist step 6): each path, with its length in frames and seconds when
            it is an audio file JUCE can read. */
        void filesDropped (const juce::StringArray& paths);

        /** Text dropped on the window (some hosts drop a description instead of a file). */
        void textDropped (const juce::String& text);

        /** The editor's display scale: the desktop's (Windows display scaling) and StrandLock's UI scale. */
        void setDisplayScale (double desktopScale, int uiScalePercent);

        void setHostInfo (const host::HostInfo& info);

        /** How the report names a parameter and prints its value (the processor gives its parameters' own). */
        void setParameterDescriber (std::function<juce::String (int paramIndex, float plainValue)> describer);

        struct BlockStart { std::uint64_t block = 0; int samples = 0; bool hasPosition = false, playing = false, looping = false, nonRealtime = false; double ppq = 0.0, bpm = 0.0; };
        struct Wrap { std::uint64_t block = 0; double before = 0.0, after = 0.0; bool looping = false; };
        struct BlockOverEnd { std::uint64_t block = 0; double start = 0.0, end = 0.0; };
        struct MidiItem { std::uint64_t block = 0; int sample = 0; EventKind kind = EventKind::otherMidi; int a = 0, b = 0; };
        struct KeyItem { juce::String key; bool keysSwitchOn = false; };
        struct DropItem
        {
            bool isFile = false;
            juce::String what;            // the path, or the text
            juce::int64 bytes = -1;       // the file's size (-1 when it does not exist)
            bool readable = false;        // an audio file JUCE can read
            juce::int64 frames = 0;
            double sampleRate = 0.0, seconds = 0.0;
            int channels = 0;
        };

        struct Snapshot
        {
            double sampleRate = 0.0;
            int preparedBlockSize = 0;

            std::uint64_t blocks = 0, playingBlocks = 0, stoppedBlocks = 0, noPositionBlocks = 0, nonRealtimeBlocks = 0;
            int minBlock = 0, maxBlock = 0, lastBlock = 0;   // 0 before the first block
            std::array<std::uint64_t, kBuckets> histogram {};
            std::vector<std::pair<int, std::uint64_t>> exactSizes;   // the first sizes seen, with their counts
            std::uint64_t otherSizes = 0;                            // blocks of any other size
            bool nonRealtimeNow = false;

            bool hasPosition = false, playing = false, looping = false, recording = false, hasPpq = false, hasBpm = false, hasLoop = false;
            double ppq = 0.0, bpm = 0.0, loopStart = 0.0, loopEnd = 0.0;
            int sigNum = 4, sigDen = 4;

            std::uint64_t cycleWraps = 0, wrapsInsideBlock = 0, jumps = 0;
            double minBpm = 0.0, maxBpm = 0.0;
            std::uint64_t tempoChanges = 0;

            std::uint64_t noteOns = 0, noteOffs = 0, controllers = 0, pitchBends = 0, otherMidi = 0;
            int lastNote = -1, lastVelocity = -1, lastController = -1, lastControllerValue = -1, lastPitchBend = -1, modWheel = -1;

            int lastHostParam = -1;
            float lastHostParamValue = 0.0f;
            std::uint64_t hostParamChanges = 0;

            std::vector<BlockStart> blockStarts;   // recent, oldest first
            std::vector<Wrap> wraps;
            std::vector<BlockOverEnd> blocksOverEnd;
            std::vector<MidiItem> midi;
            std::vector<KeyItem> keys;
            std::vector<DropItem> drops;
            std::uint64_t lostEvents = 0;          // history the ring had no room for (no page was draining it)

            double desktopScale = 1.0;
            int uiScalePercent = 100;
        };

        /** Everything now (drains the ring first). */
        Snapshot snapshot();

        /** The text the Copy report button puts on the clipboard. */
        juce::String reportText();

        /** The bucket of a block size (0 for 1 sample, 1 for 2-3, ..., 13 for 8192 and up). */
        static int bucketOf (int numSamples) noexcept;

    private:
        void drain();

        // Totals and the latest values: written by the audio thread, read anywhere.
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<int> preparedBlockSize { 0 };
        std::atomic<std::uint64_t> blocks { 0 }, playingBlocks { 0 }, stoppedBlocks { 0 }, noPositionBlocks { 0 }, nonRealtimeBlocks { 0 };
        std::atomic<int> minBlock { 0 }, maxBlock { 0 }, lastBlock { 0 };
        std::array<std::atomic<std::uint64_t>, kBuckets> histogram {};
        std::array<std::atomic<int>, kExactSizes> exactSize {};
        std::array<std::atomic<std::uint64_t>, kExactSizes> exactCount {};
        std::atomic<std::uint64_t> otherSizes { 0 };
        std::atomic<bool> nonRealtimeNow { false };

        std::atomic<bool> hasPosition { false }, playing { false }, looping { false }, recording { false }, hasPpq { false }, hasBpm { false }, hasLoop { false };
        std::atomic<double> ppq { 0.0 }, bpm { 0.0 }, loopStart { 0.0 }, loopEnd { 0.0 };
        std::atomic<int> sigNum { 4 }, sigDen { 4 };

        std::atomic<std::uint64_t> cycleWraps { 0 }, wrapsInsideBlock { 0 }, jumps { 0 };
        std::atomic<double> minBpm { 0.0 }, maxBpm { 0.0 };
        std::atomic<std::uint64_t> tempoChanges { 0 };

        std::atomic<std::uint64_t> noteOns { 0 }, noteOffs { 0 }, controllers { 0 }, pitchBends { 0 }, otherMidi { 0 };
        std::atomic<int> lastNote { -1 }, lastVelocity { -1 }, lastController { -1 }, lastControllerValue { -1 }, lastPitchBend { -1 }, modWheel { -1 };

        std::atomic<int> lastHostParam { -1 };
        std::atomic<float> lastHostParamValue { 0.0f };
        std::atomic<std::uint64_t> hostParamChanges { 0 };
        std::atomic<std::uint64_t> lostEvents { 0 };

        // The audio thread's own state (only it touches these, or prepare while no audio runs).
        std::uint64_t blockIndex = 0;
        bool prevPlaying = false, prevHasPpq = false, haveBpm = false;
        double prevEndPpq = 0.0, prevBpm = 0.0;

        DiagLog log;

        // Message thread only.
        std::deque<BlockStart> blockStartHistory;
        std::deque<Wrap> wrapHistory;
        std::deque<BlockOverEnd> overEndHistory;
        std::deque<MidiItem> midiHistory;
        std::deque<KeyItem> keyHistory;
        std::deque<DropItem> dropHistory;
        double desktopScale = 1.0;
        int uiScalePercent = 100;
        host::HostInfo hostInfo;
        std::function<juce::String (int, float)> describeParameter;
        std::unique_ptr<juce::AudioFormatManager> formats;

        void push (const Event& e) noexcept;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiagModel)
    };
}
