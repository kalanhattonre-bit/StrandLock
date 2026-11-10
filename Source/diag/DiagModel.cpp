#include "DiagModel.h"

#include "core/Music.h"
#include "core/Version.h"

#include <algorithm>
#include <cmath>

namespace sl::diag
{
    namespace
    {
        constexpr auto relaxed = std::memory_order_relaxed;

        void bump (std::atomic<std::uint64_t>& a) noexcept { a.fetch_add (1, relaxed); }

        template <class T>
        void keepLast (std::deque<T>& d, T item, std::size_t limit)
        {
            d.push_back (std::move (item));
            while (d.size() > limit)
                d.pop_front();
        }

        template <class T>
        std::vector<T> listOf (const std::deque<T>& d) { return std::vector<T> (d.begin(), d.end()); }

        juce::String fixed (double v, int decimals) { return juce::String (v, decimals); }

        juce::String noteText (int note)
        {
            return note < 0 ? juce::String ("-") : juce::String (core::noteName (note)) + " (" + juce::String (note) + ")";
        }

        juce::String bucketText (int b)
        {
            if (b == 0)
                return "1";
            const int lo = 1 << b;
            if (b == DiagModel::kBuckets - 1)
                return juce::String (lo) + "+";
            return juce::String (lo) + "-" + juce::String ((lo << 1) - 1);
        }
    }

    HostBlock readHostBlock (juce::AudioPlayHead* playHead, int numSamples, bool nonRealtime) noexcept
    {
        HostBlock h;
        h.numSamples = numSamples;
        h.nonRealtime = nonRealtime;
        if (playHead == nullptr)
            return h;

        const auto pos = playHead->getPosition();
        if (! pos.hasValue())
            return h;

        h.hasPosition = true;
        h.playing = pos->getIsPlaying();
        h.looping = pos->getIsLooping();
        h.recording = pos->getIsRecording();
        if (const auto ppq = pos->getPpqPosition(); ppq.hasValue() && std::isfinite (*ppq))
        {
            h.hasPpq = true;
            h.ppq = *ppq;
        }
        if (const auto bpm = pos->getBpm(); bpm.hasValue() && std::isfinite (*bpm))
        {
            h.hasBpm = true;
            h.bpm = *bpm;
        }
        if (const auto loop = pos->getLoopPoints(); loop.hasValue() && std::isfinite (loop->ppqStart) && std::isfinite (loop->ppqEnd))
        {
            h.hasLoop = true;
            h.loopStart = loop->ppqStart;
            h.loopEnd = loop->ppqEnd;
        }
        if (const auto sig = pos->getTimeSignature(); sig.hasValue())
        {
            h.hasTimeSig = true;
            h.sigNum = sig->numerator;
            h.sigDen = sig->denominator;
        }
        return h;
    }

    //==============================================================================================
    DiagModel::DiagModel() = default;
    DiagModel::~DiagModel() = default;

    int DiagModel::bucketOf (int numSamples) noexcept
    {
        int b = 0;
        for (int n = numSamples; n > 1 && b < kBuckets - 1; n >>= 1)
            ++b;
        return b;
    }

    void DiagModel::prepare (double rate, int maxBlockSize) noexcept
    {
        sampleRate.store (rate, relaxed);
        preparedBlockSize.store (maxBlockSize, relaxed);
        prevPlaying = false;
        prevHasPpq = false;
        prevEndPpq = 0.0;
    }

    void DiagModel::push (const Event& e) noexcept
    {
        if (! log.push (e))
            bump (lostEvents);
    }

    void DiagModel::audioBlock (const HostBlock& h) noexcept
    {
        const std::uint64_t b = blockIndex++;
        const int n = h.numSamples;

        // Block sizes.
        bump (blocks);
        if (b == 0 || n < minBlock.load (relaxed))
            minBlock.store (n, relaxed);
        if (b == 0 || n > maxBlock.load (relaxed))
            maxBlock.store (n, relaxed);
        lastBlock.store (n, relaxed);
        bump (histogram[(std::size_t) bucketOf (n)]);

        bool counted = false;
        for (std::size_t i = 0; i < (std::size_t) kExactSizes && ! counted; ++i)
        {
            const int size = exactSize[i].load (relaxed);
            if (size == n + 1)   // stored as size + 1, so 0 means a free slot (and a 0-sample block has one too)
            {
                bump (exactCount[i]);
                counted = true;
            }
            else if (size == 0)
            {
                exactCount[i].store (1, relaxed);
                exactSize[i].store (n + 1, relaxed);
                counted = true;
            }
        }
        if (! counted)
            bump (otherSizes);

        nonRealtimeNow.store (h.nonRealtime, relaxed);
        if (h.nonRealtime)
            bump (nonRealtimeBlocks);

        // Where the host is.
        if (! h.hasPosition)
            bump (noPositionBlocks);
        else if (h.playing)
            bump (playingBlocks);
        else
            bump (stoppedBlocks);

        hasPosition.store (h.hasPosition, relaxed);
        playing.store (h.playing, relaxed);
        looping.store (h.looping, relaxed);
        recording.store (h.recording, relaxed);
        hasPpq.store (h.hasPpq, relaxed);
        ppq.store (h.ppq, relaxed);
        hasLoop.store (h.hasLoop, relaxed);
        loopStart.store (h.loopStart, relaxed);
        loopEnd.store (h.loopEnd, relaxed);
        if (h.hasTimeSig)
        {
            sigNum.store (h.sigNum, relaxed);
            sigDen.store (h.sigDen, relaxed);
        }

        if (h.hasBpm)
        {
            hasBpm.store (true, relaxed);
            bpm.store (h.bpm, relaxed);
            if (! haveBpm)
            {
                minBpm.store (h.bpm, relaxed);
                maxBpm.store (h.bpm, relaxed);
            }
            else
            {
                if (h.bpm != prevBpm)
                    bump (tempoChanges);
                if (h.bpm < minBpm.load (relaxed))
                    minBpm.store (h.bpm, relaxed);
                if (h.bpm > maxBpm.load (relaxed))
                    maxBpm.store (h.bpm, relaxed);
            }
            haveBpm = true;
            prevBpm = h.bpm;
        }

        Event start;
        start.kind = EventKind::blockStart;
        start.a = n;
        start.b = (h.playing ? kPlaying : 0) | (h.looping ? kLooping : 0) | (h.hasPosition && h.hasPpq ? kHasPosition : 0) | (h.nonRealtime ? kNonRealtime : 0);
        start.x = h.ppq;
        start.y = h.bpm;
        start.block = b;
        push (start);

        // Cycle wraps and jumps: where this block starts against where the last playing block ended.
        const double sr = sampleRate.load (relaxed);
        const double beatsPerSample = h.hasBpm && sr > 0.0 ? h.bpm / 60.0 / sr : 0.0;
        const bool positioned = h.playing && h.hasPpq;
        if (positioned)
        {
            if (prevPlaying && prevHasPpq && std::abs (h.ppq - prevEndPpq) > kJumpBeats)
            {
                const bool wrap = h.looping && h.ppq < prevEndPpq;
                bump (wrap ? cycleWraps : jumps);
                Event e;
                e.kind = EventKind::cycleWrap;
                e.b = wrap ? 1 : 0;
                e.x = prevEndPpq;
                e.y = h.ppq;
                e.block = b;
                push (e);
            }

            // A block that runs over the cycle's end: the wrap falls inside it (ASIO-Guard's large blocks).
            if (h.looping && h.hasLoop && h.loopEnd > h.loopStart && beatsPerSample > 0.0)
            {
                const double end = h.ppq + n * beatsPerSample, half = 0.5 * beatsPerSample;
                if (h.ppq < h.loopEnd - half && end > h.loopEnd + half)
                {
                    bump (wrapsInsideBlock);
                    Event e;
                    e.kind = EventKind::wrapInBlock;
                    e.x = h.ppq;
                    e.y = end;
                    e.block = b;
                    push (e);
                }
            }
            prevEndPpq = h.ppq + n * beatsPerSample;
        }
        prevPlaying = h.playing;
        prevHasPpq = h.hasPpq;
    }

    void DiagModel::audioMidi (const std::uint8_t* data, int size, int samplePosition) noexcept
    {
        if (data == nullptr || size < 1)
            return;

        Event e;
        e.block = blockIndex > 0 ? blockIndex - 1 : 0;
        e.sample = samplePosition;
        const int status = data[0];
        const int type = status & 0xf0;
        const int d1 = size > 1 ? data[1] & 0x7f : 0;
        const int d2 = size > 2 ? data[2] & 0x7f : 0;

        if (type == 0x90 && size >= 3 && d2 > 0)
        {
            bump (noteOns);
            lastNote.store (d1, relaxed);
            lastVelocity.store (d2, relaxed);
            e.kind = EventKind::noteOn;
            e.a = d1;
            e.b = d2;
        }
        else if ((type == 0x80 || type == 0x90) && size >= 3)
        {
            bump (noteOffs);
            e.kind = EventKind::noteOff;
            e.a = d1;
            e.b = d2;
        }
        else if (type == 0xb0 && size >= 3)
        {
            bump (controllers);
            lastController.store (d1, relaxed);
            lastControllerValue.store (d2, relaxed);
            if (d1 == 1)
                modWheel.store (d2, relaxed);
            e.kind = EventKind::controller;
            e.a = d1;
            e.b = d2;
        }
        else if (type == 0xe0 && size >= 3)
        {
            bump (pitchBends);
            const int value = d1 | (d2 << 7);
            lastPitchBend.store (value, relaxed);
            e.kind = EventKind::pitchBend;
            e.a = value;
        }
        else
        {
            bump (otherMidi);
            e.kind = EventKind::otherMidi;
            e.a = status;
        }
        push (e);
    }

    void DiagModel::hostParameterChanged (int paramIndex, float plainValue) noexcept
    {
        lastHostParamValue.store (plainValue, relaxed);
        lastHostParam.store (paramIndex, relaxed);
        bump (hostParamChanges);
    }

    void DiagModel::stateSaved (std::size_t bytes) noexcept
    {
        lastSavedBytes.store ((long long) bytes, relaxed);
        bump (stateSaves);
    }

    void DiagModel::stateLoaded (std::size_t bytes, bool readable) noexcept
    {
        lastLoadedBytes.store ((long long) bytes, relaxed);
        lastLoadReadable.store (readable, relaxed);
        bump (stateLoads);
    }

    void DiagModel::audioWaitingForSound() noexcept
    {
        bump (blocksWaitingForSound);
    }

    //==============================================================================================
    void DiagModel::drain()
    {
        Event e;
        while (log.pop (e))
        {
            switch (e.kind)
            {
                case EventKind::blockStart:
                    keepLast (blockStartHistory, BlockStart { e.block, e.a, (e.b & kHasPosition) != 0, (e.b & kPlaying) != 0,
                                                              (e.b & kLooping) != 0, (e.b & kNonRealtime) != 0, e.x, e.y }, kKeepBlockStarts);
                    break;
                case EventKind::cycleWrap:
                    keepLast (wrapHistory, Wrap { e.block, e.x, e.y, e.b != 0 }, kKeepWraps);
                    break;
                case EventKind::wrapInBlock:
                    keepLast (overEndHistory, BlockOverEnd { e.block, e.x, e.y }, kKeepWraps);
                    break;
                case EventKind::noteOn:
                case EventKind::noteOff:
                case EventKind::controller:
                case EventKind::pitchBend:
                case EventKind::otherMidi:
                    keepLast (midiHistory, MidiItem { e.block, e.sample, e.kind, e.a, e.b }, kKeepMidi);
                    break;
            }
        }
    }

    void DiagModel::keyReceived (const juce::String& keyDescription, bool keysSwitchOn)
    {
        keepLast (keyHistory, KeyItem { keyDescription, keysSwitchOn }, kKeepKeys);
    }

    void DiagModel::filesDropped (const juce::StringArray& paths)
    {
        if (formats == nullptr)
        {
            formats = std::make_unique<juce::AudioFormatManager>();
            formats->registerBasicFormats();
        }

        for (const auto& path : paths)
        {
            DropItem d;
            d.isFile = true;
            d.what = path;
            const juce::File f (path);
            if (f.existsAsFile())
            {
                d.bytes = f.getSize();
                if (std::unique_ptr<juce::AudioFormatReader> reader { formats->createReaderFor (f) })
                {
                    d.readable = true;
                    d.frames = reader->lengthInSamples;
                    d.sampleRate = reader->sampleRate;
                    d.channels = (int) reader->numChannels;
                    d.seconds = d.sampleRate > 0.0 ? (double) d.frames / d.sampleRate : 0.0;
                }
            }
            keepLast (dropHistory, std::move (d), kKeepDrops);
        }
    }

    void DiagModel::textDropped (const juce::String& text)
    {
        DropItem d;
        d.what = text;
        keepLast (dropHistory, std::move (d), kKeepDrops);
    }

    void DiagModel::setDisplayScale (double desktop, int uiPercent)
    {
        desktopScale = desktop;
        uiScalePercent = uiPercent;
    }

    void DiagModel::setHostInfo (const host::HostInfo& info)
    {
        hostInfo = info;
    }

    void DiagModel::setParameterDescriber (std::function<juce::String (int, float)> describer)
    {
        describeParameter = std::move (describer);
    }

    DiagModel::Snapshot DiagModel::snapshot()
    {
        drain();

        Snapshot s;
        s.sampleRate = sampleRate.load (relaxed);
        s.preparedBlockSize = preparedBlockSize.load (relaxed);
        s.blocks = blocks.load (relaxed);
        s.playingBlocks = playingBlocks.load (relaxed);
        s.stoppedBlocks = stoppedBlocks.load (relaxed);
        s.noPositionBlocks = noPositionBlocks.load (relaxed);
        s.nonRealtimeBlocks = nonRealtimeBlocks.load (relaxed);
        s.minBlock = minBlock.load (relaxed);
        s.maxBlock = maxBlock.load (relaxed);
        s.lastBlock = lastBlock.load (relaxed);
        for (std::size_t i = 0; i < (std::size_t) kBuckets; ++i)
            s.histogram[i] = histogram[i].load (relaxed);
        for (std::size_t i = 0; i < (std::size_t) kExactSizes; ++i)
            if (const int size = exactSize[i].load (relaxed); size > 0)
                s.exactSizes.emplace_back (size - 1, exactCount[i].load (relaxed));
        s.otherSizes = otherSizes.load (relaxed);
        s.nonRealtimeNow = nonRealtimeNow.load (relaxed);

        s.hasPosition = hasPosition.load (relaxed);
        s.playing = playing.load (relaxed);
        s.looping = looping.load (relaxed);
        s.recording = recording.load (relaxed);
        s.hasPpq = hasPpq.load (relaxed);
        s.hasBpm = hasBpm.load (relaxed);
        s.hasLoop = hasLoop.load (relaxed);
        s.ppq = ppq.load (relaxed);
        s.bpm = bpm.load (relaxed);
        s.loopStart = loopStart.load (relaxed);
        s.loopEnd = loopEnd.load (relaxed);
        s.sigNum = sigNum.load (relaxed);
        s.sigDen = sigDen.load (relaxed);

        s.cycleWraps = cycleWraps.load (relaxed);
        s.wrapsInsideBlock = wrapsInsideBlock.load (relaxed);
        s.jumps = jumps.load (relaxed);
        s.minBpm = minBpm.load (relaxed);
        s.maxBpm = maxBpm.load (relaxed);
        s.tempoChanges = tempoChanges.load (relaxed);

        s.noteOns = noteOns.load (relaxed);
        s.noteOffs = noteOffs.load (relaxed);
        s.controllers = controllers.load (relaxed);
        s.pitchBends = pitchBends.load (relaxed);
        s.otherMidi = otherMidi.load (relaxed);
        s.lastNote = lastNote.load (relaxed);
        s.lastVelocity = lastVelocity.load (relaxed);
        s.lastController = lastController.load (relaxed);
        s.lastControllerValue = lastControllerValue.load (relaxed);
        s.lastPitchBend = lastPitchBend.load (relaxed);
        s.modWheel = modWheel.load (relaxed);

        s.lastHostParam = lastHostParam.load (relaxed);
        s.lastHostParamValue = lastHostParamValue.load (relaxed);
        s.hostParamChanges = hostParamChanges.load (relaxed);
        s.stateSaves = stateSaves.load (relaxed);
        s.stateLoads = stateLoads.load (relaxed);
        s.lastSavedBytes = lastSavedBytes.load (relaxed);
        s.lastLoadedBytes = lastLoadedBytes.load (relaxed);
        s.lastLoadReadable = lastLoadReadable.load (relaxed);
        s.blocksWaitingForSound = blocksWaitingForSound.load (relaxed);

        s.blockStarts = listOf (blockStartHistory);
        s.wraps = listOf (wrapHistory);
        s.blocksOverEnd = listOf (overEndHistory);
        s.midi = listOf (midiHistory);
        s.keys = listOf (keyHistory);
        s.drops = listOf (dropHistory);
        s.lostEvents = lostEvents.load (relaxed);
        s.desktopScale = desktopScale;
        s.uiScalePercent = uiScalePercent;
        return s;
    }

    juce::String DiagModel::reportText()
    {
        const Snapshot s = snapshot();
        juce::StringArray lines;

        lines.add (juce::String ("StrandLock ") + core::kPluginVersion + " diagnostics report");
        lines.add ("Host: " + hostInfo.summary() + (hostInfo.path.isNotEmpty() ? " at " + hostInfo.path : juce::String()));
        lines.add ("Sample rate: " + fixed (s.sampleRate, 0) + " Hz; block size given at prepare: " + juce::String (s.preparedBlockSize));
        lines.add ("Display scale: Windows " + fixed (s.desktopScale * 100.0, 0) + " %, StrandLock UI " + juce::String (s.uiScalePercent) + " %");

        // Block sizes (ASIO-Guard shows up here: larger and varying blocks while a track is not record-enabled).
        lines.add ("Blocks: " + juce::String ((juce::int64) s.blocks) + " (min " + juce::String (s.minBlock) + ", max "
                   + juce::String (s.maxBlock) + ", last " + juce::String (s.lastBlock) + " samples)");
        juce::String sizes = "Block sizes seen:";
        for (const auto& [size, count] : s.exactSizes)
            sizes << " " << size << " x " << (juce::int64) count << ";";
        if (s.otherSizes > 0)
            sizes << " other sizes x " << (juce::int64) s.otherSizes << ";";
        lines.add (sizes);
        juce::String hist = "Block size ranges:";
        for (int b = 0; b < kBuckets; ++b)
            if (s.histogram[(std::size_t) b] > 0)
                hist << " " << bucketText (b) << ": " << (juce::int64) s.histogram[(std::size_t) b] << ";";
        lines.add (hist);
        juce::String recent = "Recent block sizes:";
        const std::size_t from = s.blockStarts.size() > 32 ? s.blockStarts.size() - 32 : 0;
        for (std::size_t i = from; i < s.blockStarts.size(); ++i)
            recent << " " << s.blockStarts[i].samples;
        lines.add (recent);
        lines.add ("Blocks while playing: " + juce::String ((juce::int64) s.playingBlocks) + "; while stopped: "
                   + juce::String ((juce::int64) s.stoppedBlocks) + "; with no host position: " + juce::String ((juce::int64) s.noPositionBlocks));
        lines.add ("Offline (non-real-time) blocks: " + juce::String ((juce::int64) s.nonRealtimeBlocks) + "; now: "
                   + (s.nonRealtimeNow ? "offline" : "real time"));

        // Position, tempo and cycle.
        juce::String where = "Position: ";
        if (! s.hasPosition)
            where << "the host gives none";
        else
            where << (s.playing ? "playing" : "stopped") << (s.recording ? ", recording" : "") << ", ppq "
                  << (s.hasPpq ? fixed (s.ppq, 4) : juce::String ("none")) << ", " << (s.hasBpm ? fixed (s.bpm, 3) + " bpm" : juce::String ("no tempo"))
                  << ", " << s.sigNum << "/" << s.sigDen << ", cycle " << (s.looping ? "on" : "off")
                  << (s.hasLoop ? " (" + fixed (s.loopStart, 4) + " to " + fixed (s.loopEnd, 4) + ")" : juce::String());
        lines.add (where);
        lines.add ("Tempo: min " + fixed (s.minBpm, 3) + ", max " + fixed (s.maxBpm, 3) + ", changes " + juce::String ((juce::int64) s.tempoChanges));
        juce::String starts = "Block starts (ppq, last 16):";
        const std::size_t from16 = s.blockStarts.size() > 16 ? s.blockStarts.size() - 16 : 0;
        for (std::size_t i = from16; i < s.blockStarts.size(); ++i)
            starts << " " << (s.blockStarts[i].hasPosition ? fixed (s.blockStarts[i].ppq, 4) : juce::String ("-"));
        lines.add (starts);
        juce::String wraps = "Cycle wraps: " + juce::String ((juce::int64) s.cycleWraps) + "; wraps inside a block: "
                             + juce::String ((juce::int64) s.wrapsInsideBlock) + "; other jumps: " + juce::String ((juce::int64) s.jumps);
        for (const auto& w : s.wraps)
            wraps << "; " << (w.looping ? "wrap " : "jump ") << fixed (w.before, 4) << " -> " << fixed (w.after, 4);
        lines.add (wraps);

        // MIDI.
        lines.add ("MIDI: " + juce::String ((juce::int64) s.noteOns) + " note on, " + juce::String ((juce::int64) s.noteOffs) + " note off (last "
                   + noteText (s.lastNote) + ", velocity " + juce::String (s.lastVelocity) + "), " + juce::String ((juce::int64) s.controllers)
                   + " controllers (last CC " + juce::String (s.lastController) + " = " + juce::String (s.lastControllerValue) + "), "
                   + juce::String ((juce::int64) s.pitchBends) + " pitch bends (last " + juce::String (s.lastPitchBend) + "), mod wheel "
                   + juce::String (s.modWheel) + ", other " + juce::String ((juce::int64) s.otherMidi));
        juce::String midi = "Recent MIDI:";
        const std::size_t fromMidi = s.midi.size() > 24 ? s.midi.size() - 24 : 0;
        for (std::size_t i = fromMidi; i < s.midi.size(); ++i)
        {
            const auto& m = s.midi[i];
            switch (m.kind)
            {
                case EventKind::noteOn:     midi << " on " << noteText (m.a) << "/" << m.b << ";"; break;
                case EventKind::noteOff:    midi << " off " << noteText (m.a) << ";"; break;
                case EventKind::controller: midi << " CC" << m.a << "=" << m.b << ";"; break;
                case EventKind::pitchBend:  midi << " bend " << m.a << ";"; break;
                case EventKind::otherMidi:
                case EventKind::blockStart:
                case EventKind::cycleWrap:
                case EventKind::wrapInBlock: midi << " status 0x" << juce::String::toHexString (m.a) << ";"; break;
            }
        }
        lines.add (midi);

        // The last parameter the host changed (Quick Controls, automation).
        if (s.lastHostParam >= 0)
            lines.add ("Last parameter the host changed: "
                       + (describeParameter ? describeParameter (s.lastHostParam, s.lastHostParamValue)
                                            : "#" + juce::String (s.lastHostParam) + " = " + juce::String (s.lastHostParamValue))
                       + " (" + juce::String ((juce::int64) s.hostParamChanges) + " changes)");
        else
            lines.add ("Last parameter the host changed: none yet");

        // The project's state (checklist step 12).
        juce::String stateLine = "State: saved " + juce::String ((juce::int64) s.stateSaves) + " x";
        if (s.lastSavedBytes >= 0)
            stateLine << ", last " << (juce::int64) s.lastSavedBytes << " bytes";
        stateLine << "; loaded " << (juce::int64) s.stateLoads << " x";
        if (s.lastLoadedBytes >= 0)
            stateLine << ", last " << (juce::int64) s.lastLoadedBytes << " bytes ("
                      << (s.lastLoadReadable ? "read" : "could not be read: kept as it was") << ")";
        if (s.blocksWaitingForSound > 0)
            stateLine << "; " << (juce::int64) s.blocksWaitingForSound << " blocks played before the loaded sound was ready";
        lines.add (stateLine);

        // Keys and drops.
        juce::String keys = "Keys received (" + juce::String ((int) s.keys.size()) + "):";
        for (const auto& k : s.keys)
            keys << " " << k.key << (k.keysSwitchOn ? " [Keys on];" : " [Keys off];");
        lines.add (keys);
        for (const auto& d : s.drops)
        {
            if (! d.isFile)
                lines.add ("Dropped text: \"" + d.what.substring (0, 200) + "\"");
            else if (d.readable)
                lines.add ("Dropped file: " + d.what + " (" + juce::String (d.frames) + " frames, " + fixed (d.seconds, 3) + " s at "
                           + fixed (d.sampleRate, 0) + " Hz, " + juce::String (d.channels) + " channels, " + juce::String (d.bytes) + " bytes)");
            else
                lines.add ("Dropped file: " + d.what + (d.bytes >= 0 ? " (" + juce::String (d.bytes) + " bytes, not an audio file StrandLock can read)"
                                                                     : juce::String (" (not found)")));
        }
        if (s.lostEvents > 0)
            lines.add ("History not kept while the page was closed: " + juce::String ((juce::int64) s.lostEvents) + " events");

        return lines.joinIntoString ("\n") + "\n";
    }
}
