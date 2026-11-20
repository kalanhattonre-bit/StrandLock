#pragma once

// The roll's per-note decorations (PLUGIN-PLAN 4 M6, 7.8 T2 decision 5; sl-ui-panels.js "roll decorations"): under
// every note wide enough, chevrons for the way and the speed its first head moves through the sound (H4); just above
// its line a faint picture of what it plays, in the waveform's colour (U7); a dot on a note with settings of its own
// (S7); and a warning mark where a slide goes into a note that starts well inside the one before it. They are drawn in
// every mode, with the prototype's alphas: the thumbnails strongest in Sample mode and Build, the chevrons in Sample
// mode (thumbAlpha, chevronStrength).
//
// The pure part (tabAt, simulate, peakBins, thumbAmps) is SLPanels' own, ported line by line and checked against
// Tests/fixtures/ui/inspector.json (test_inspector.cpp). NoteDecor keeps what the drawing needs between paints: the
// sound's peak bins (made once per sound, on the message thread) and each note's simulation (made again only when the
// note or what it reads changed). RollPainter draws with it, into the NoteLayerCache's image. Message thread only.

#include "core/Inst.h"
#include "doc/SampleManager.h"
#include "model/Clip.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace sl::ui::roll
{
    /** sl-ui-panels.js tabAt: a table read at t (clamped to 0..1) with straight lines between its values; a table of
        one value gives it, an empty one 0.5. A NaN t gives NaN, as the prototype's does. */
    double tabAt (const std::vector<float>& table, double t) noexcept;

    /** What the first head of a note reads, approximately (sl-ui-panels.js simulate): no Drift and no Spread, Carry
        on read as the note's own Start, a Random Start read as the middle of its range, and Scatter jumping to its own
        seeded spots. N + 1 points across the note. */
    struct NoteSim
    {
        int N = 0;
        std::vector<float> pos;            // where in the sound (0..1 of the file); NaN = silent from there (At the end: Stop)
        std::vector<std::int8_t> dir;      // -1 backwards, 0 still, 1 forwards
        std::vector<float> sp;             // |speed| in x real time
        std::vector<std::uint8_t> jump;    // 1 where Scatter jumps
    };

    /** sl-ui-panels.js simulate: inst is the instrument (normalized), bpm the tempo, secs the sound's length, travel
        and pitch the note's Travel and Pitch tables (model::laneTable). */
    NoteSim simulate (const model::Note& n, const core::Inst& inst, double bpm, double secs, const std::vector<float>& travel,
                      const std::vector<float>& pitch, int N);

    /** sl-ui-panels.js peakBins: a coarse peak envelope of a sound for the thumbnails, max |mid| per bin of about 128
        frames (at most 65536 bins), normalised to 1. right may equal left (mono). */
    std::vector<float> peakBins (const float* left, const float* right, std::int64_t frames);

    /** sl-ui-panels.js thumbAmps: the thumbnail's N column heights (0..1; NaN = silent) from a simulation and the
        peak bins. regLen is the Region's length (a fraction of the sound). */
    std::vector<float> thumbAmps (const NoteSim& sim, const std::vector<float>& bins, double secs, double regLen);

    /** How many points a note this many pixels wide is simulated at: round (width / 3), 4 to 200. */
    int simPoints (double noteWidth) noexcept;

    /** The thumbnail's alpha: Sample mode and Build 0.42 selected, 0.24 not; the lane modes 0.22 and 0.11. */
    float thumbAlpha (int mode, bool selected) noexcept;

    /** The chevrons' strength: Sample mode 1 selected, 0.8 not; every other mode 0.8 and 0.5. */
    float chevronStrength (int mode, bool selected) noexcept;

    /** A note with a Sample setting of its own (engine, motion, tape, each grain backwards, jump or lock): the dot on
        the roll (sl-ui-panels.js hasOwn). */
    bool hasOwnSettings (const model::Note& n) noexcept;

    /** A slide from n into target that the engine cuts short, because target starts well inside n (decorate: target
        starts after n's start and more than 0.13 beat before its end). */
    bool slideWarns (const model::Note& n, const model::Note& target) noexcept;

    /** The smallest note width (pixels) that gets chevrons and a thumbnail (decorate: W >= 12). */
    inline constexpr double kDecorMinWidth = 12.0;

    //==============================================================================================
    class NoteDecor
    {
    public:
        /** The sound the thumbnails read. A new sound's peak bins are made at once (one pass over its samples).
            Null: no thumbnails, and simulations read a 6 s sound (sl-ui.js sampleSeconds). */
        void setSound (const std::shared_ptr<const doc::SoundView>& sound);

        /** The sound's length in seconds (6 without one). */
        double seconds() const noexcept { return secs; }

        /** The sound's peak bins, or null without a sound. */
        const std::vector<float>* bins() const noexcept { return haveBins ? &peaks : nullptr; }

        /** Lets go of the entries of notes the clip no longer holds (a new clip version). */
        void setClip (const model::ClipPtr& clip);

        /** What one note shows: its simulation and its thumbnail's column heights (empty without a sound). */
        struct Entry
        {
            NoteSim sim;
            std::vector<float> amps;

            // what it was made from
            double regionStart = 0.0, regionEnd = 1.0, speed = 1.0, macro0 = 0.5, root = 60.0, cents = 0.0, pitch = 0.0;
            int atEnd = 0, motion = 0, engine = 0, jump = 0;
            std::int32_t seed = 0;
            double bpm = 0.0, secs = 0.0, tableRes = 0.0;
            int N = 0;
            const void* sound = nullptr;
        };

        /** A note's entry for the instrument, the tempo and N points (travel and pitch: its tables at the line's
            resolution tableRes), made again only when one of those, the note's version or the sound changed. */
        const Entry& entry (const model::Note& n, const core::Inst& inst, double bpm, const std::vector<float>& travel,
                            const std::vector<float>& pitch, int N, double tableRes);

        /** A number that changes whenever what every note's decorations read changes: the instrument's fields the
            simulation and the Mirror chevrons read, and the sound. The note layer draws every note again then. */
        std::uint64_t key (const core::Inst& inst) const noexcept;

        /** How many simulations were made (the tests: a paint with nothing changed makes none). */
        int simulationsMade() const noexcept { return made; }

    private:
        std::shared_ptr<const doc::SoundView> sound;
        std::vector<float> peaks;
        bool haveBins = false;
        double secs = 6.0;
        model::ClipPtr clip;
        std::unordered_map<std::uint64_t, Entry> entries;   // by Note::serial
        int made = 0;
    };
}
