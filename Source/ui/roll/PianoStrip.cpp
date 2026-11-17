#include "PianoStrip.h"

#include "core/Music.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        bool isBlack (int p) noexcept
        {
            const int pc = ((p % 12) + 12) % 12;
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        }
    }

    PianoStrip::PianoStrip (const Theme& t, const RollGeometry& g)
        : theme (t), geo (g)
    {
        setOpaque (true);
    }

    void PianoStrip::setScale (int newKey, int newScale)
    {
        if (newKey == key && newScale == scale)
            return;
        key = newKey;
        scale = newScale;
        repaint();
    }

    void PianoStrip::setLiveKeys (std::vector<int> pitches)
    {
        std::sort (pitches.begin(), pitches.end());
        pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());
        if (pitches == live)
            return;
        for (const int p : live)
            repaintKey (p);
        live = std::move (pitches);
        for (const int p : live)
            repaintKey (p);
    }

    bool PianoStrip::isHeld (int pitch) const
    {
        return pitch == pressed || std::binary_search (live.begin(), live.end(), pitch);
    }

    int PianoStrip::pitchAt (float y) const noexcept
    {
        return geo.pitchAt ((double) y + geo.scrollY());
    }

    void PianoStrip::repaintKey (int pitch)
    {
        const double y = (double) (kMaxPitch - pitch) * geo.rowHeight() - geo.scrollY();
        repaint (0, (int) std::floor (y) - 1, getWidth(), (int) std::ceil (geo.rowHeight()) + 2);
    }

    void PianoStrip::press (int pitch)
    {
        release();
        pressed = juce::jlimit (kMinPitch, kMaxPitch, pitch);
        repaintKey (pressed);
        if (onPress)
            onPress (pressed);
    }

    void PianoStrip::release()
    {
        if (pressed < 0)
            return;
        const int p = pressed;
        pressed = -1;
        repaintKey (p);
        if (onRelease)
            onRelease (p);
    }

    void PianoStrip::mouseDown (const juce::MouseEvent& e)
    {
        // sl-ui.js: the left button plays the key under it until the mouse lets go
        if (e.mods.isLeftButtonDown())
            press (pitchAt (e.position.y));
    }

    void PianoStrip::mouseUp (const juce::MouseEvent&)
    {
        release();
    }

    void PianoStrip::paint (juce::Graphics& g)
    {
        // sl-ui.js drawPiano: a key per row (1 px apart), a black key's right part white, the keys outside the
        // scale shaded, the C's named. Under the keys: the roll's background.
        g.fillAll (theme (Tok::roll));
        const double rh = geo.rowHeight(), sy = geo.scrollY();
        const float w = (float) getWidth(), H = (float) getHeight();
        const int pTop = geo.pitchAt (sy), pBot = geo.pitchAt (sy + (double) H);
        g.setFont (Theme::px (9.0f));
        for (int p = pBot; p <= pTop; ++p)
        {
            const float y = (float) ((double) (kMaxPitch - p) * rh - sy), keyH = (float) rh - 1.0f;
            const bool black = isBlack (p), held = isHeld (p);
            g.setColour (held ? theme (Tok::pianoHeld) : black ? theme (Tok::pianoBlack) : theme (Tok::pianoWhite));
            g.fillRect (juce::Rectangle<float> (0.0f, y, black ? (float) blackWidth : w, keyH));
            if (black)
            {
                g.setColour (held ? theme (Tok::pianoHeld) : theme (Tok::pianoWhite));
                g.fillRect (juce::Rectangle<float> ((float) blackWidth, y, w - (float) blackWidth, keyH));
            }
            if (! core::inScale ((double) p, key, scale))
            {
                g.setColour (theme (Tok::pianoOut));
                g.fillRect (juce::Rectangle<float> (0.0f, y, w, keyH));
            }
            if (p % 12 == 0)
            {
                g.setColour (theme (Tok::pianoText));
                g.drawSingleLineText (juce::String (core::noteName ((double) p)), 3, juce::roundToInt (y + (float) rh - 3.0f));
            }
        }
        // the edge between the keys and the roll (shell.html: #pianoWrap's right border)
        g.setColour (theme (Tok::line));
        g.fillRect (w - 1.0f, 0.0f, 1.0f, H);
    }
}
