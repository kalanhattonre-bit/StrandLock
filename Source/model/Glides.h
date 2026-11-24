#pragma once

// Glides and chains (E2; sl-model.js "glides and chains"): which note slides into which, and the voices (one
// per chain of joined notes). computeGlides' result keeps the JavaScript Map's insertion order, because
// buildChains breaks ties by it (PLUGIN-PLAN 1.6 rule 7).
//
// F3 (M8, PLUGIN-PLAN 7.10 decision 7, 8.2 #11): when the clip loops with tieAcrossLoop on, the clip's end meets its
// start. A note that crosses the loop end holds on into the next pass (for at most one loop, so it never overlaps its
// own next start), and the next pass's notes follow the notes that end there: Auto Slide joins the last note to the
// next pass's first note as it joins touching notes inside the clip (a whole-loop note joins its own next start), and
// an explicit link to a note that does not start after its source slides into that note in the next pass. Such a
// glide, and its step in the chain, "wraps". With the tie off (the parity tests) everything is the prototype's.

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
        bool wraps = false;      // F3: the target plays in the next loop pass (the glide crosses the loop end)
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

    /** F3: the clip loops with tieAcrossLoop on, so notes hold on and slide over the loop end. */
    constexpr bool tiesAcrossLoop (const Clip& clip) noexcept { return clip.loop && clip.tieAcrossLoop; }

    /** The clip's length in beats as the engine plays it: max (1, bars) x max (1, beatsPerBar). */
    double loopBeats (const Clip& clip) noexcept;

    /** Where a note ends for its glides: s + l (the prototype). With the tie on, a note that starts inside the clip
        plays at most one loop (the engine's effLen), so it ends at s + min (l, the loop's length). */
    double glideEnd (const Note& n, const Clip& clip) noexcept;

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
        first. With the tie on (F3) the next pass's notes are candidates too (at their start plus one loop), and a
        glide to one of them wraps. */
    GlideMap computeGlides (const Clip& clip, const core::Inst& inst);

    /** One chain: the ids of the notes one voice plays, in order. */
    using Chains = std::vector<std::vector<std::string>>;

    /** A neighbour in a chain: its id, and whether it plays in another loop pass (F3): a next note in the next pass, a
        previous note in the previous one. */
    struct ChainLink
    {
        std::string id;
        bool wraps = false;
    };

    /** buildChains' links and chains. Each note has at most one next and one previous note, so the links make paths
        and, with the tie on, loops (a whole-loop note joined to its own next start, or a line that goes round the clip
        for ever). The chains list each path from its first note and each loop from its earliest note in time order; the
        links keep the step that closes a loop, which a chain cannot show. With the tie off there are no loops and the
        links are exactly the chains' neighbours. */
    struct ChainLinks
    {
        std::unordered_map<std::string, ChainLink> next, prev;
        Chains chains;
    };

    /** SLModel.buildChains (clip, glides), with its links: a glide joins two notes unless the target has Restart on
        or does not start after the source (F3: a target in the next pass starts after it, the source itself
        included); a target joins at most one source (an explicit link first, then the smallest glide, then the
        earliest source). */
    ChainLinks buildChainLinks (const Clip& clip, const GlideMap& glides);

    /** SLModel.buildChains (clip, glides): buildChainLinks' chains, listed by their first note in time order. */
    Chains buildChains (const Clip& clip, const GlideMap& glides);

    /** buildChains with the glides of the default instrument (the prototype's buildChains (clip)). */
    Chains buildChains (const Clip& clip);
}
