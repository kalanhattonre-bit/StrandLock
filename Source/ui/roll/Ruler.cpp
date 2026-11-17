#include "Ruler.h"

#include "core/JsCompat.h"
#include "model/Clip.h"

#include <algorithm>
#include <cmath>

namespace sl::ui::roll
{
    Ruler::Ruler (const Theme& t, const RollGeometry& g)
        : theme (t), geo (g)
    {
        setOpaque (true);
        setInterceptsMouseClicks (false, false);
    }

    void Ruler::setPlayhead (std::optional<double> beat)
    {
        const bool same = beat.has_value() == playhead.has_value() && (! beat.has_value() || juce::exactlyEqual (*beat, *playhead));
        if (same)
            return;
        // the old and the new triangle
        const auto mark = [this] (std::optional<double> b)
        {
            if (b)
            {
                const int x = (int) std::floor (*b * geo.beatWidth() - geo.scrollX());
                repaint (x - 7, 0, 15, getHeight());
            }
        };
        mark (playhead);
        playhead = beat;
        mark (playhead);
    }

    void Ruler::paint (juce::Graphics& g)
    {
        // shell.html #rulerWrap: the header colour with a line under it
        g.fillAll (theme (Tok::hdr));
        const float w = (float) getWidth(), h = (float) getHeight();

        const auto& clip = geo.getClip();
        const double total = clip != nullptr ? RollGeometry::totalBeats (*clip) : 0.0;
        const int beatsPerBar = clip != nullptr ? std::max (1, clip->beatsPerBar) : model::kDefaultBeatsPerBar;
        const double bw = geo.beatWidth(), sx = geo.scrollX();

        // sl-ui.js drawRuler: a tick per beat (every 4th beat when beats are under 10 px), bars taller and numbered.
        // The x is rounded, so a tick sits on the grid's line under it (the grid rounds its lines too).
        g.setFont (Theme::px (10.0f));
        const int step = bw < 10.0 ? 4 : 1;
        const double last = std::min (total, (sx + (double) w) / bw + 1.0);
        for (int b = std::max (0, (int) std::floor (sx / bw)); (double) b < last; ++b)
        {
            if (b % step != 0)
                continue;
            const float x = (float) (core::jsRound ((double) b * bw) - sx);
            const bool bar = b % beatsPerBar == 0;
            g.setColour (bar ? theme (Tok::rulerText) : theme (Tok::rulerTick));
            g.fillRect (juce::Rectangle<float> (x, bar ? 5.0f : 13.0f, 1.0f, h));
            if (bar)
                g.drawSingleLineText (juce::String (b / beatsPerBar + 1), juce::roundToInt (x + 4.0f), 13);
        }

        // the playhead's triangle while Cubase plays
        if (playhead)
        {
            const float x = (float) (*playhead * bw - sx);
            juce::Path tri;
            tri.addTriangle (x - 5.0f, 12.0f, x + 5.0f, 12.0f, x, h);
            g.setColour (theme (Tok::playhead));
            g.fillPath (tri);
        }

        g.setColour (theme (Tok::line));
        g.fillRect (0.0f, h - 1.0f, w, 1.0f);
    }
}
