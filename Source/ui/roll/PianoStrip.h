#pragma once

// The piano keys beside the roll (PLUGIN-PLAN 4 M4, 7.6 R2; sl-ui.js drawPiano and its pointer handlers, shell.html
// #pianoWrap): one key per row, scrolled with the roll; the keys outside the scale shaded; every C named (Cubase:
// 60 = C3); a key that sounds (pressed here, or played live from Cubase) in the held colour. Pressing a key plays
// it until the mouse lets go (the view sends the audition through the CommandQueue). The wheel goes to the roll.

#include "RollGeometry.h"

#include "ui/Theme.h"

#include <functional>
#include <vector>

namespace sl::ui::roll
{
    class PianoStrip final : public juce::Component
    {
    public:
        PianoStrip (const Theme&, const RollGeometry&);

        /** The key and the scale (keys outside it are shaded). */
        void setScale (int key, int scale);

        /** The keys that sound now besides the one pressed here (the live keys of the display frame). */
        void setLiveKeys (std::vector<int> pitches);

        /** A key pressed and let go (MIDI pitch). */
        std::function<void (int)> onPress, onRelease;

        /** The key pressed with the mouse now, or -1. */
        int getPressed() const noexcept { return pressed; }

        /** Whether a pitch is drawn in the held colour. */
        bool isHeld (int pitch) const;

        /** The pitch of the row at y (this component's pixels). */
        int pitchAt (float y) const noexcept;

        /** Presses and lets go of a key as the mouse does (also for the tests). */
        void press (int pitch);
        void release();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        static constexpr int blackWidth = 26;   // a black key's width; the white part right of it is 14 px

    private:
        void repaintKey (int pitch);

        const Theme& theme;
        const RollGeometry& geo;
        int key = 0, scale = 0;
        int pressed = -1;
        std::vector<int> live;
    };
}
