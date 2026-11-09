#pragma once

// The snapshot compiler (sl-model.js compileSnapshot; PLUGIN-PLAN 1.3, 1.4): a clip version becomes the
// read-only core::Snapshot the audio thread plays. It runs on the compiler thread, never on the audio thread.
//
// Unlike the JS, the compiler takes no instrument: the settings a note follows stay "follow" markers, and the
// glides and chains are compiled for all three Auto Slide modes, so automation never needs a recompile.
// core::resolve () gives what the JS snapshot baked in, for any instrument.

#include "Clip.h"

#include "core/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace sl::model
{
    /** The longest lane table the compiler makes (2^20 values, 4 MB). A note longer than 4096 beats at res
        256 gets a coarser table of this length (the prototype has no such cap and would ask for any size). */
    inline constexpr std::int32_t kMaxTableLength = 1 << 20;

    /** The tables of the last compiled clip, by Note::serial (PLUGIN-PLAN 1.3: a serial is never reused, so a
        note that may have changed never finds an old entry). A table also depends on the note's id (a
        Different generator hashes it, and normalizeClip may rename a repeated id) and on res, so an entry is
        used only when both match too. */
    class SnapshotCache
    {
    public:
        std::shared_ptr<const core::NoteTables> find (std::uint64_t serial, const std::string& id, int res) const;
        void put (std::uint64_t serial, const std::string& id, int res, std::shared_ptr<const core::NoteTables> tables);

        std::size_t size() const noexcept { return entries.size(); }
        void clear() noexcept { entries.clear(); }

    private:
        struct Entry
        {
            std::string id;
            int res = 0;
            std::shared_ptr<const core::NoteTables> tables;
        };

        std::unordered_map<std::uint64_t, Entry> entries;
    };

    /** One note's 10 lane tables at res: laneTable on every lane (the drawing sampled and rounded to Float32,
        plus its generator, rounded again), a flat table (every value the same bits) kept as 2 values, and each
        lane's full length. The note is used as it is (compileSnapshot normalizes first). */
    std::shared_ptr<const core::NoteTables> compileTables (const Note& note, int res);

    /** SLModel.compileSnapshot (clip, inst, {res}) without the instrument: res at least 8; the clip normalized
        (normalizeClip); the notes in time order (timeOrder); per note its id, idHash, p, s, l, vel, its own
        settings as follow markers, its Start, its own glide length, the glide and the chain of each Auto Slide
        mode (computeGlides on the default instrument with glideMode off, legato and always, then buildChains),
        and its 10 lane tables. The clip settings travel with it. A table of a note in reuse (same serial, id
        and res) is shared instead of computed again; when remember is given it is set to this clip's tables
        (reuse and remember may be the same cache). */
    std::unique_ptr<core::Snapshot> compileSnapshot (const Clip& clip, int res = 256, const SnapshotCache* reuse = nullptr,
                                                     SnapshotCache* remember = nullptr);
}
