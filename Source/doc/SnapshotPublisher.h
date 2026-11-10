#pragma once

// The compiler thread (PLUGIN-PLAN 1.3): one juce::Thread that waits on an event, takes the newest clip version
// the document handed it (older ones that arrived meanwhile are never compiled), compiles it with the
// SnapshotCache it keeps between compiles (a note seen before shares its tables), and publishes the snapshot to
// the audio thread through rt::VersionExchange<core::Snapshot>.
//
// Memory (NOTES-M1: "the memory a snapshot may take is for M2's SnapshotPublisher to bound"). A snapshot's lane
// tables take max (2, ceil (l x res) + 1) floats per lane that is not flat, so 4096 long notes with drawn lanes
// could ask for gigabytes. SnapshotSizer gives an upper bound of the table memory at a resolution (a lane counts
// as flat only when it has no generator and every node holds the same value, which the compiler always stores
// as 2 floats); the publisher compiles at the finest of 256, 128, 64, 32, 16 and 8 whose tables fit
// kSnapshotBudgetBytes, measures what it made, and goes one step coarser when the result is over. A clip whose
// tables do not fit even at 8 is not compiled: an empty snapshot (the clip's settings, no notes) is published
// and tooDetailed () says so. ClipDocument refuses an edit that would make such a clip, so only a hand-made
// state can hold one.

#include "core/Snapshot.h"
#include "model/Clip.h"
#include "model/SnapshotCompiler.h"
#include "rt/VersionExchange.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace sl::doc
{
    /** The most lane-table memory one snapshot may hold. */
    inline constexpr std::size_t kSnapshotBudgetBytes = 256u * 1024u * 1024u;

    /** The resolutions the publisher tries, finest first (compileSnapshot's default is 256, its minimum 8). */
    inline constexpr int kSnapshotResolutions[] = { 256, 128, 64, 32, 16, 8 };

    /** An upper bound of a snapshot's lane-table bytes. Each note's flat lanes are found once, by its serial;
        the cache keeps only the notes of the last clip it measured (the ClipSizer rule). One thread at a time. */
    class SnapshotSizer
    {
    public:
        std::size_t bytesAt (const model::Clip& clip, int res);

        /** The finest of kSnapshotResolutions whose bytesAt fits kSnapshotBudgetBytes, or 0 when none does.
            bytes (when given) gets the estimate at that resolution (at 8 when none fits). */
        int resolutionFor (const model::Clip& clip, std::size_t* bytes = nullptr);

    private:
        struct NoteInfo
        {
            double l = 1.0;
            int fullLanes = 0;   // lanes that are not surely flat
        };

        std::unordered_map<std::uint64_t, NoteInfo> notes;
    };

    /** The lane-table bytes a compiled snapshot holds (every table's values, as stored). */
    std::size_t snapshotTableBytes (const core::Snapshot& snapshot) noexcept;

    class SnapshotPublisher final : private juce::Thread
    {
    public:
        /** Starts the compiler thread. */
        explicit SnapshotPublisher (rt::VersionExchange<core::Snapshot>& out);

        /** Stops the thread (a compile in progress finishes first). */
        ~SnapshotPublisher() override;

        /** Any thread but the audio thread: the newest clip version (version counts up with every change). The
            thread wakes, takes the newest version handed to it and compiles that one only. A version older than
            one handed over before is dropped (stats ().stale). */
        void submit (model::ClipPtr clip, std::uint64_t version);

        /** Waits until a version at least this new has been published, at most timeoutMs. True when it was. */
        bool waitForVersion (std::uint64_t version, int timeoutMs);

        struct Stats
        {
            std::uint64_t submitted = 0;      // submit calls
            std::uint64_t compiled = 0;       // compiles made (each one of the newest version at the time)
            std::uint64_t skipped = 0;        // versions replaced by a newer one before the thread took them
            std::uint64_t publishedVersion = 0;
            int lastRes = 0;                  // the resolution of the last compile (0 = too detailed: empty snapshot)
            std::size_t lastTableBytes = 0;   // its lane tables
            double lastCompileMs = 0.0;
            bool tooDetailed = false;         // the last clip did not fit even at res 8 (an empty snapshot plays)
            std::size_t cachedNotes = 0;      // the SnapshotCache after the last compile
            std::uint64_t stale = 0;          // versions dropped because a newer one had been handed over (fix pass)
            std::uint64_t failed = 0;         // compiles that threw (out of memory): an empty snapshot played instead
        };

        Stats stats() const;

        /** For the tests: while held, the thread takes nothing new (a compile already running finishes). */
        void setHeld (bool held);

        /** For the tests: the next compile throws std::bad_alloc, as one that runs out of memory would. */
        void failNextCompile();

    private:
        void run() override;
        void compileAndPublish (const model::ClipPtr& clip, std::uint64_t version);
        void publishFailed (const model::Clip& clip, std::uint64_t version);   // after a compile threw

        rt::VersionExchange<core::Snapshot>& exchange;

        mutable std::mutex lock;                // guards pending*, stats, held; never held while compiling
        std::condition_variable published;
        model::ClipPtr pendingClip;
        std::uint64_t pendingVersion = 0;
        bool hasPending = false;
        bool held = false;
        std::uint64_t newestSubmitted = 0;
        Stats st;
        std::atomic<bool> failNext { false };

        // The compiler thread's own.
        model::SnapshotCache cache;
        SnapshotSizer sizer;
    };
}
