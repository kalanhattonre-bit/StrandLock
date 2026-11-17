#pragma once

// The piano roll's geometry (PLUGIN-PLAN 4 M4, 7.6 R1; sl-ui.js "roll geometry"): beats <-> x, pitch rows <-> y, the
// drawn line of a note (its pitch lane, its glide and its vibrato squiggle), where each lane's nodes sit, the end
// dots, the glide diamonds and the segment bend diamonds, and what a point hits. All in roll pixels: x = beats x
// beat width, y = rows from the top (MIDI 96) x row height, with nothing scrolled. The view part (its size, the
// scroll, the zoom buttons, the wheel zoom) turns view pixels into roll pixels and back.
// Pure C++ (no JUCE), the prototype's arithmetic in its order; tested against Tests/fixtures/roll/geometry.json
// and, for the view, against values from sl-ui.js's zoom, wheel and first-view lines (test_roll_geometry.cpp).

#include "core/Inst.h"
#include "core/Lanes.h"
#include "doc/RollState.h"
#include "model/Clip.h"
#include "model/Glides.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sl::ui::roll
{
    /** The rows the roll shows: MIDI 24 (C0) to 96 (C6), one per semitone. */
    inline constexpr int kMinPitch = 24, kMaxPitch = 96, kRows = kMaxPitch - kMinPitch + 1;

    /** Zoom: pixels per beat and per row, with the prototype's limits (sl-ui.js BW, RH and its zoom buttons). */
    inline constexpr double kDefaultBeatWidth = 64.0, kMinBeatWidth = 12.0, kMaxBeatWidth = 400.0;
    inline constexpr double kDefaultRowHeight = 14.0, kMinRowHeight = 8.0, kMaxRowHeight = 30.0;

    /** A lane's band above the note line is 2.6 rows high. */
    inline constexpr double kLaneRows = 2.6;

    /** Hit sizes in pixels (sl-ui.js rollDown and its helpers). */
    inline constexpr double kGlideHandleHit = 12.0;   // the glide diamonds of the primary note
    inline constexpr double kNodeHit = 11.0;          // a lane node
    inline constexpr double kSegmentHit = 6.0;        // a segment's bend diamond ("only right on the diamond")
    inline constexpr double kEndDotHit = 12.0;        // a selected note's end and start dots in Build
    inline constexpr double kMinSegmentPixels = 20.0; // a segment shorter than this has no diamond

    struct Point
    {
        double x = 0.0, y = 0.0;
    };

    struct NoteBox
    {
        double x0 = 0.0, x1 = 0.0, top = 0.0, bottom = 0.0;
    };

    /** The two diamonds of a note's glide: where it starts (drag sideways = its length) and its middle (drag up and
        down = its curve). */
    struct GlideHandles
    {
        model::Glide glide;
        Point start, mid;
    };

    /** One note's drawn line (sl-ui.js geom): K points across the note, the semitones of its pitch lane plus the glide
        into the next note (base) and the vibrato squiggle (vib), both Float32 as the prototype's Float32Arrays, and
        the lowest and highest point. */
    struct NoteGeom
    {
        int K = 2;
        double res = 0.0;                   // the table resolution, points per beat
        std::vector<float> base, vib;       // semitones above the note's row
        double smin = 0.0, smax = 0.0;
        std::optional<model::Glide> glide;  // the glide out of this note in the shown clip
    };

    /** A node a point hit: the note and the node's index. */
    struct NodeHit
    {
        model::NotePtr note;
        int k = 0;
    };

    /** JavaScript's Math.hypot for two values (V8: scaled by the larger one, Kahan summation), so a distance at a hit
        size's edge decides as the prototype does. */
    double jsHypot (double a, double b) noexcept;

    class RollGeometry
    {
    public:
        RollGeometry() = default;

        //==========================================================================================
        // What the geometry depends on.

        /** The clip shown: glides are looked up in it (the prototype's glides () of app.clip). */
        void setClip (const model::ClipPtr& clip);
        const model::ClipPtr& getClip() const noexcept { return clip; }

        /** The instrument: its vibrato rate, and its slide mode and time for the glides. */
        void setInst (const core::Inst& inst);
        const core::Inst& getInst() const noexcept { return inst; }

        /** The tempo (the vibrato's squiggle is drawn in real time). Not finite or not above 0: 120. */
        void setBpm (double bpm);
        double getBpm() const noexcept { return bpm; }

        /** Pixels per beat and per row, clamped to the prototype's limits (12..400 and 8..30). */
        void setZoom (double beatWidth, double rowHeight);
        double beatWidth() const noexcept { return bw; }
        double rowHeight() const noexcept { return rh; }
        double laneHeight() const noexcept { return rh * kLaneRows; }

        //==========================================================================================
        // The view onto the roll (sl-ui.js: the scroller over its spacer). The roll is contentWidth x contentHeight
        // pixels; the view shows viewWidth x viewHeight of it from the scroll, and the scroll stays inside the roll
        // the way a browser keeps a scroller's scroll. The gestures take roll pixels: the view hands them
        // toRoll (its point), and draws at toView (a roll point).

        /** The roll's size in whole pixels (sl-ui.js sizeSpacer): the clip's beats plus 60 px across, every row down. */
        double contentWidth() const noexcept;
        double contentHeight() const noexcept;

        /** The view's size in pixels (not finite or under 0: 0). The scroll is kept inside the roll. */
        void setViewSize (double width, double height);
        double viewWidth() const noexcept { return viewW; }
        double viewHeight() const noexcept { return viewH; }

        /** The scroll in roll pixels: what the view's top left corner shows, kept in 0 .. the roll's size less the
            view's (not finite: 0). A new clip or zoom keeps it inside the roll too. */
        void setScroll (double x, double y);
        double scrollX() const noexcept { return scrX; }
        double scrollY() const noexcept { return scrY; }

        /** A point of the view in roll pixels (sl-ui.js rollPos), and a roll point in the view. */
        Point toRoll (double viewX, double viewY) const noexcept { return { viewX + scrX, viewY + scrY }; }
        Point toView (double rollX, double rollY) const noexcept { return { rollX - scrX, rollY - scrY }; }

        /** The zoom buttons (sl-ui.js zoom (h, v)): the beat width times h (12..400) and the row height times v,
            rounded to whole pixels (8..30); the beat and the row at the view's centre stay there. */
        void zoomBy (double h, double v);

        /** Ctrl + the mouse wheel over the roll: the beat width times 1.15 (in) or divided by 1.15 (out), 12..400;
            the beat under the pointer (viewX, view pixels) stays under it. */
        void wheelZoom (bool zoomIn, double viewX);

        /** The first view of a clip (sl-ui.js init): the row 5 semitones above the clip's median pitch (66 with no
            notes) in the middle of the view, never above the top. The sideways scroll is left as it is. */
        void centreOnNotes();

        /** The zoom and scroll as UIST keeps them: zoom factors of 64 px per beat and 14 px per row, the scroll in
            beats and rows. setViewState rounds the row height to whole pixels, as the zoom buttons leave it. */
        struct ViewState
        {
            double zoomH = 1.0, zoomV = 1.0, scrollBeat = 0.0, scrollRow = 0.0;
        };
        ViewState viewState() const noexcept;
        void setViewState (const ViewState& s);

        //==========================================================================================
        // Rows and beats.

        /** The centre of a pitch's row. */
        double cy (double p) const noexcept { return (kMaxPitch - p) * rh + rh / 2; }

        /** The pitch of the row at y, clamped to the roll (24..96). */
        int pitchAt (double y) const noexcept;

        double xOfBeat (double b) const noexcept { return b * bw; }
        double beatAt (double x) const noexcept { return x / bw; }

        /** The clip's length in beats (bars x beats per bar). */
        static double totalBeats (const model::Clip& c) noexcept { return (double) c.bars * (double) c.beatsPerBar; }

        //==========================================================================================
        // A note.

        /** The glides of the clip shown, for the instrument's slide mode and time (computeGlides, made once per
            clip version): the slide links the roll draws, and what a note's line ends in. */
        const model::GlideMap& glideMap() const;

        /** Its drawn line (cached per note version and zoom, tempo, vibrato rate and glide). */
        const NoteGeom& geom (const model::Note& n) const;

        /** A lane's table at the line's resolution (K values; made on first use; the drawing's ribbons). */
        const std::vector<float>& laneTable (const model::Note& n, core::LaneId lane) const;

        /** The y of the note line (pitch and glide, no vibrato) at note time t (clamped 0..1). */
        double baseYAt (const model::Note& n, double t) const;

        /** The y of lane value v at note time t: the pitch lane is drawn in semitones around the note's row (24
            semitones from 0 to 1), every other lane in its band above the note line. */
        double valueY (const model::Note& n, core::LaneId lane, double t, double v) const;

        /** The inverse of valueY: the lane value at y (not clamped). */
        double laneValue (const model::Note& n, core::LaneId lane, double t, double y) const;

        /** Node k of a lane. */
        Point nodeXY (const model::Note& n, core::LaneId lane, int k) const;

        /** The bend diamond in the middle of segment k (nodes k .. k + 1); nothing when the segment is under 20 px. */
        std::optional<Point> segXY (const model::Note& n, core::LaneId lane, int k) const;

        /** The glide diamonds; nothing when the note has no glide (or one of 0 beats). */
        std::optional<GlideHandles> glideHandles (const model::Note& n) const;

        /** The note's box: from its start to its end, around its line (and its lane band above when withRibbon). */
        NoteBox noteBox (const model::Note& n, bool withRibbon) const;

        /** The end dot (sideways = length, up and down = bend the end) and the start dot of a note in Build. */
        Point endDot (const model::Note& n) const;
        Point startDot (const model::Note& n) const;

        /** On the note line itself (not the lane band above it): from 0.7 row above to 0.8 row below the line. */
        bool onNoteLine (const model::Note& n, double x, double y) const;

        //==========================================================================================
        // What a point hits (sl-ui.js hitNote, hitNode, hitSeg).

        /** The selected notes in time order (start, pitch, id). */
        static std::vector<model::NotePtr> selectedNotes (const model::Clip& c, const doc::RollState& state);

        /** The notes whose nodes show: every selected note when there are at most 16, else the primary alone. */
        static std::vector<model::NotePtr> handleNotes (const model::Clip& c, const doc::RollState& state);

        /** The note under (x, y): within 5 px of its start or end, from its lane band (2 px above) to 0.8 row below its
            line. Selected notes first, then the later notes in the clip first. */
        model::NotePtr hitNote (const model::Clip& c, const doc::RollState& state, double x, double y) const;

        /** A node of the active lane within 11 px (the handle notes, the primary first). */
        std::optional<NodeHit> hitNode (const model::Clip& c, const doc::RollState& state, double x, double y, core::LaneId lane) const;

        /** The segment whose bend diamond is within 6 px, or -1. */
        int hitSeg (const model::Note& n, double x, double y, core::LaneId lane) const;

    private:
        struct Key
        {
            double bw = 0.0, bpm = 0.0, vibRate = 0.0;
            bool hasGlide = false;
            double d = 0.0, len = 0.0, curve = 0.0;
            bool operator== (const Key& o) const noexcept;
        };

        struct Entry
        {
            Key key;
            NoteGeom g;
            mutable std::array<std::vector<float>, core::kNumLanes> tables;   // empty until asked for
        };

        const Entry& entry (const model::Note& n) const;
        void refreshGlides() const;

        model::ClipPtr clip;
        core::Inst inst = core::defaultInst();
        double bpm = 120.0;
        double bw = kDefaultBeatWidth, rh = kDefaultRowHeight;
        double viewW = 0.0, viewH = 0.0, scrX = 0.0, scrY = 0.0;

        mutable model::GlideMap glides;   // computeGlides of the clip shown, for its instrument's slide mode and time
        mutable model::ClipPtr glidesClip;
        mutable core::GlideMode glidesMode = core::GlideMode::legato;
        mutable double glidesTime = -1.0;

        mutable std::unordered_map<std::uint64_t, Entry> cache;   // by Note::serial
    };
}
