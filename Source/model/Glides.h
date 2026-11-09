#pragma once

// Glides and chains (E2; sl-model.js "glides and chains"): which note slides into which, and the voices (one
// per chain of joined notes). computeGlides' result keeps the JavaScript Map's insertion order, because
// buildChains breaks ties by it (PLUGIN-PLAN 1.6 rule 7).

#include "Clip.h"

#include "core/Inst.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sl::model
{
    /** One glide out of a note. */
    struct Glide
    {
        double d = 0.0;          // semitones, from where the note really ends to where the target really starts
        double len = 0.0;        // beats (0 = an instant jump that still joins)
        double curve = 0.0;      // the source note's gc
        std::string toId;        // the target note
        bool linked = false;     // an explicit link (glideTo), not an automatic glide
    };

    /** An insertion-ordered map from a source note id to its glide, as a JavaScript Map: set () on an id that
        is already there replaces its value and keeps its place. */
    class GlideMap
    {
    public:
        using Entry = std::pair<std::string, Glide>;

        void set (const std::string& sourceId, Glide glide);
        const Glide* find (const std::string& sourceId) const noexcept;
        bool contains (const std::string& sourceId) const noexcept { return find (sourceId) != nullptr; }

        std::size_t size() const noexcept { return entries.size(); }
        bool empty() const noexcept { return entries.empty(); }
        std::vector<Entry>::const_iterator begin() const noexcept { return entries.begin(); }
        std::vector<Entry>::const_iterator end() const noexcept { return entries.end(); }
        const Entry& operator[] (std::size_t i) const noexcept { return entries[i]; }

    private:
        std::vector<Entry> entries;
        std::unordered_map<std::string, std::size_t> index;
    };

    /** The semitones the pitch drawing adds at the note's start / end: (first / last pitch node - 0.5) * 24. */
    double startSemis (const Note& n);
    double endSemis (const Note& n);

    /** glideD (n, m): from where n really ends (p + its last pitch node) to where m really starts (p + its first
        pitch node), clamped -48..48. */
    double glideD (const Note& n, const Note& m);

    /** SLModel.computeGlides (clip, inst): explicit links first (in note order), then, unless Auto Slide is
        off, the automatic glides (legato: the next notes that start within 0.13 beat of the end; always:
        within 4 beats), voice-led by rank among peers that end together and lead into the same chord. The
        glide length is the note's own glide or inst.glideTime, at most 0.95 of the note. inst is normalized
        first. */
    GlideMap computeGlides (const Clip& clip, const core::Inst& inst);

    /** One chain: the ids of the notes one voice plays, in order. */
    using Chains = std::vector<std::vector<std::string>>;

    /** SLModel.buildChains (clip, glides): a glide joins two notes unless the target has Restart on or does
        not start after the source; a target joins at most one source (an explicit link first, then the
        smallest glide, then the earliest source). Chains are listed by their first note in time order. */
    Chains buildChains (const Clip& clip, const GlideMap& glides);

    /** buildChains with the glides of the default instrument (the prototype's buildChains (clip)). */
    Chains buildChains (const Clip& clip);
}
