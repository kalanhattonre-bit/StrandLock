#pragma once

// The live heads and grains on the Sample editor strip (PLUGIN-PLAN 1.5, 4 M6, 7.8 T2; sl-ui.js drawStrip): every head
// of every voice in the engine's newest display frame as a line down the waveform with its number in a circle at the
// top and an arrow for its way at the bottom (a square while it stands still), brighter the louder it plays and dimmer
// once its note is let go; every grain as a dot that sinks as it ages. Under them, the Start markers of the selected
// notes: a triangle at the bottom edge where each one starts in the Region, and a faint bar over a Random start's range.
//
// The frames come from the processor's DisplaySlot (rt::LatestSlot: the audio thread never waits for the editor); the
// editor's 30 Hz timer takes the newest and hands it here, and the overlay keeps a copy of it. No strings cross threads.
// The WaveStrip owns the overlay and draws it over the wave; heads () and grains () say where everything goes, for the
// drawing and the tests. Message thread only.

#include "engine/DisplayFrame.h"
#include "ui/Theme.h"

#include <functional>
#include <memory>
#include <vector>

namespace sl::ui
{
    class HeadsOverlay
    {
    public:
        /** The engine's newest frame (copied), or null: no heads or grains. */
        void setFrame (const engine::DisplayFrame* frame);
        const engine::DisplayFrame* getFrame() const noexcept { return frame.get(); }

        /** Where the strip draws a place in the sound (0..1 of the file): an x in the strip's pixels. */
        using XOf = std::function<float (double)>;

        /** One head as it is drawn: its line's x, its number (1..8), its way (1 forwards, -1 backwards, 0 still) and its
            alpha (sl-ui.js: (released ? 0.45 : 1) x (0.35 + 0.65 x clamp (level x env x 1.6, 0, 1))). */
        struct Head
        {
            float x = 0.0f;
            int number = 1;
            int dir = 0;
            float alpha = 1.0f;
            int voice = 0;   // the frame's voice it belongs to
        };

        /** One grain: its dot's centre and its alpha (0.9 - 0.6 x its age). */
        struct Grain
        {
            float x = 0.0f, y = 0.0f, alpha = 1.0f;
        };

        /** The heads of the frame that fall within 12 px of area (sl-ui.js: x > -12 and x < W + 12). */
        std::vector<Head> heads (juce::Rectangle<float> area, const XOf& xOf) const;

        /** The grains of the frame that fall inside area. */
        std::vector<Grain> grains (juce::Rectangle<float> area, const XOf& xOf) const;

        /** A selected note's Start marker: pos is where in the Region it starts (0..1), range how far past it a Random
            start may land (random only). */
        struct StartMark
        {
            double pos = 0.0, range = 0.0;
            bool random = false;

            bool operator== (const StartMark& o) const noexcept;
            bool operator!= (const StartMark& o) const noexcept { return ! (*this == o); }
        };

        /** The markers of the selected notes (at most 64, sl-ui.js selectedNotes ().slice (0, 64)). True when they
            changed. */
        bool setStarts (std::vector<StartMark> marks);
        const std::vector<StartMark>& getStarts() const noexcept { return starts; }

        /** The Start markers, then the heads, then the grains, over the wave area. regionStart and regionEnd: the
            Region the Start markers sit in. */
        void paint (juce::Graphics&, juce::Rectangle<float> area, const XOf& xOf, double regionStart, double regionEnd, const Theme&);

        /** How many heads and grains the last paint drew. */
        int headsDrawn() const noexcept { return lastHeads; }
        int grainsDrawn() const noexcept { return lastGrains; }

        static constexpr int band = 12;        // sl-ui.js BAND: the Region band along the top
        static constexpr int maxStarts = 64;   // drawStrip's slice (0, 64)

    private:
        std::unique_ptr<engine::DisplayFrame> frame;   // about 8.5 KB, on the heap
        std::vector<StartMark> starts;
        int lastHeads = 0, lastGrains = 0;
    };
}
