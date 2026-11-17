#pragma once

// The note layer (PLUGIN-PLAN 4 M4: "a note-layer image cache"; 7.6 R2). Every note's drawing is kept in an image
// the size of the view, in device pixels, so painting the roll is its rows, one image and the handles on top.
//
// When the clip or the selection changes, only the notes that changed are drawn again. The cache keeps, for every
// note, what its image shows: the note version (the model shares every note an edit did not touch, so a moved
// note is a new pointer), whether it is selected or the primary note, and its slide (the note before a moved
// note slides somewhere else now). noteChanges compares the new state with that, note by note, and marks the old
// and the new bounds of each note that changed; the next paint clears each marked rectangle and draws every note
// that reaches it again, in the drawing order, the clear and the notes through one clip. Everything else the notes depend on (the view's size,
// scroll and zoom, the theme, the mode, the tempo, the vibrato rate, the slide settings, the device scale) is the
// Key: when it changes, the whole layer is drawn again. So is a change of more than maxChangedNotes notes, or of
// the order of notes that did not change.
//
// A partial update draws exactly what a full one draws inside the marked rectangles (the same notes, the same
// order, the same transform; the clear goes through the notes' clip, so a renderer that places a clip's edge its
// own way, as Direct2D does, clears exactly what it draws again), so the image after it is the image a full redraw
// would give, with JUCE's renderer and with Direct2D (test_roll_perf).
//
// The image is made with the type the paint's renderer prefers (Direct2D in a Windows window, software in the
// tests on Linux), and drawn without resampling. Message thread only.

#include "RollPainter.h"

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sl::ui::roll
{
    class NoteLayerCache
    {
    public:
        /** What every note's drawing depends on besides the notes and the selection. */
        struct Key
        {
            double viewWidth = 0.0, viewHeight = 0.0, scrollX = 0.0, scrollY = 0.0, beatWidth = 0.0, rowHeight = 0.0;
            int theme = -1, mode = 0, lane = -1;
            bool dimRefresh = false;
            double bpm = 0.0, vibRate = 0.0, glideTime = 0.0;
            int glideMode = -1;

            bool operator== (const Key&) const noexcept;
            bool operator!= (const Key& o) const noexcept { return ! (*this == o); }
        };

        static Key keyOf (const PaintState&);

        /** What changed for the notes since the layer last took the state. */
        struct Change
        {
            bool all = false;                             // the whole layer will be drawn again
            int notes = 0;                                // notes whose drawing changed (added, gone or different)
            std::vector<juce::Rectangle<double>> rects;   // their old and new bounds, roll pixels (one per note
                                                          // when the two overlap)
        };

        /** Compares state with what the layer shows and marks what changed for the next paint. */
        Change noteChanges (const PaintState& state);

        /** The next paint draws every note again. */
        void invalidateAll() noexcept { needsFull = true; }

        /** Brings the image up to date for state and draws it with the Graphics at the view's origin. visible: the
            view in roll pixels. */
        void paint (juce::Graphics& g, const PaintState& state, juce::Rectangle<double> visible);

        /** What the last paint drew again. */
        struct Stats
        {
            bool full = false;     // every note
            int rects = 0;         // device-pixel rectangles cleared and drawn again (a partial update)
            int notesDrawn = 0;    // note drawings made
            double pixels = 0.0;   // device pixels drawn again
            double ms = 0.0;       // how long the update took
        };

        const Stats& lastRender() const noexcept { return stats; }
        const juce::Image& getImage() const noexcept { return image; }
        float getScale() const noexcept { return scale; }

        static constexpr int maxChangedNotes = 48;

    private:
        struct Shown
        {
            model::NotePtr note;
            bool selected = false, primary = false;
            bool glide = false;
            double glideD = 0.0, glideLen = 0.0, glideCurve = 0.0;
            std::optional<juce::Rectangle<double>> bounds;   // where its drawing is, when it was drawn or changed
            int order = 0;                                   // its place in the clip
        };

        static Shown record (const PaintState&, const model::NotePtr&, int order);
        void render (const PaintState&, juce::Rectangle<double> visible, bool full);
        void took (const PaintState&);

        // the clip version and the selection shown (a refresh that brings the same ones changes nothing)
        model::ClipPtr shownClip;
        std::vector<std::string> shownSelection;
        std::string shownPrimary;

        juce::Image image;
        int imageType = -1;
        float scale = 0.0f;
        Key key;
        bool needsFull = true;
        bool haveShown = false;
        std::vector<juce::Rectangle<double>> pending;   // roll pixels to draw again at the next paint
        std::unordered_map<std::string, Shown> shown;
        Stats stats;
    };
}
