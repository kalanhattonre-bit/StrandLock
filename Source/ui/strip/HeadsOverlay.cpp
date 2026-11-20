#include "HeadsOverlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sl::ui
{
    namespace
    {
        float clampf (float v, float a, float b) noexcept { return v < a ? a : v > b ? b : v; }
    }

    bool HeadsOverlay::StartMark::operator== (const StartMark& o) const noexcept
    {
        return juce::exactlyEqual (pos, o.pos) && juce::exactlyEqual (range, o.range) && random == o.random;
    }

    void HeadsOverlay::setFrame (const engine::DisplayFrame* f)
    {
        if (f == nullptr)
        {
            frame.reset();
            return;
        }
        if (frame == nullptr)
            frame = std::make_unique<engine::DisplayFrame>();
        *frame = *f;
    }

    bool HeadsOverlay::setStarts (std::vector<StartMark> marks)
    {
        if (marks.size() > (std::size_t) maxStarts)
            marks.resize ((std::size_t) maxStarts);
        if (marks == starts)
            return false;
        starts = std::move (marks);
        return true;
    }

    std::vector<HeadsOverlay::Head> HeadsOverlay::heads (juce::Rectangle<float> area, const XOf& xOf) const
    {
        std::vector<Head> out;
        if (frame == nullptr || ! xOf)
            return out;
        // sl-ui.js drawStrip: every head of every voice, a little past the edges too (its circle and arrow reach in)
        const int voices = juce::jmin ((int) frame->numVoices, engine::kDisplayVoices);
        for (int v = 0; v < voices; ++v)
        {
            const auto& voice = frame->voices[v];
            const int count = juce::jmin ((int) voice.numHeads, engine::kMaxHeads);
            for (int k = 0; k < count; ++k)
            {
                const auto& h = voice.heads[k];
                const float x = xOf ((double) h.pos);
                if (! (x > area.getX() - 12.0f && x < area.getRight() + 12.0f))
                    continue;
                const float lv = clampf (h.level * voice.env * 1.6f, 0.0f, 1.0f);
                Head d;
                d.x = x;
                d.number = (int) h.index + 1;
                d.dir = h.dir > 0 ? 1 : h.dir < 0 ? -1 : 0;
                d.alpha = (voice.released ? 0.45f : 1.0f) * (0.35f + 0.65f * lv);
                d.voice = v;
                out.push_back (d);
            }
        }
        return out;
    }

    std::vector<HeadsOverlay::Grain> HeadsOverlay::grains (juce::Rectangle<float> area, const XOf& xOf) const
    {
        std::vector<Grain> out;
        if (frame == nullptr || ! xOf)
            return out;
        // a dot that sinks from just under the head numbers to the arrows as the grain ages, and fades
        const float y = area.getY(), H = area.getHeight();
        const int count = juce::jmin ((int) frame->numGrains, engine::kDisplayGrains);
        for (int i = 0; i < count; ++i)
        {
            const auto& gr = frame->grains[i];
            const float x = xOf ((double) gr.pos);
            if (! (x >= area.getX() && x <= area.getRight()))
                continue;
            const float age = clampf (gr.age, 0.0f, 1.0f);
            Grain d;
            d.x = x;
            d.y = y + (float) band + 20.0f + (H - (float) band - 34.0f) * age + 1.5f;
            d.alpha = 0.9f - 0.6f * age;
            out.push_back (d);
        }
        return out;
    }

    void HeadsOverlay::paint (juce::Graphics& g, juce::Rectangle<float> area, const XOf& xOf, double regionStart, double regionEnd,
                              const Theme& theme)
    {
        lastHeads = 0;
        lastGrains = 0;
        if (! xOf)
            return;
        const float y = area.getY(), H = area.getHeight(), bottom = y + H;

        // the Start markers of the selected notes: a triangle where each starts, a Random start's range as a faint bar
        if (! starts.empty())
        {
            const double span = regionEnd - regionStart;
            const auto mark = theme (Tok::startMark);
            for (const auto& st : starts)
            {
                const float x = xOf (regionStart + st.pos * span);
                if (st.random)
                {
                    const float x2 = xOf (regionStart + std::min (1.0, st.pos + st.range) * span);
                    g.setColour (mark.withMultipliedAlpha (0.3f));
                    g.fillRect (juce::Rectangle<float> (x, bottom - 5.0f, juce::jmax (0.0f, x2 - x), 4.0f));
                }
                juce::Path tri;
                tri.addTriangle (x - 5.0f, bottom, x + 5.0f, bottom, x, bottom - 8.0f);
                g.setColour (mark);
                g.fillPath (tri);
            }
        }

        if (frame == nullptr)
            return;

        // every head as a numbered line with its way
        const auto headColour = theme (Tok::head), headText = theme (Tok::headTxt);
        g.setFont (Theme::px (9.0f, true));
        for (const auto& h : heads (area, xOf))
        {
            g.setColour (headColour.withMultipliedAlpha (h.alpha));
            g.fillRect (juce::Rectangle<float> (h.x - 1.0f, y + (float) band, 2.0f, H - (float) band));
            const auto circle = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ h.x, y + (float) band + 8.0f });
            g.fillEllipse (circle);
            juce::Path arrow;
            if (h.dir > 0)
                arrow.addTriangle (h.x + 2.0f, bottom - 13.0f, h.x + 10.0f, bottom - 8.0f, h.x + 2.0f, bottom - 3.0f);
            else if (h.dir < 0)
                arrow.addTriangle (h.x - 2.0f, bottom - 13.0f, h.x - 10.0f, bottom - 8.0f, h.x - 2.0f, bottom - 3.0f);
            else
                arrow.addRectangle (h.x - 3.0f, bottom - 11.0f, 6.0f, 6.0f);
            g.fillPath (arrow);
            g.setColour (headText.withMultipliedAlpha (h.alpha));
            g.drawText (juce::String (h.number), circle, juce::Justification::centred, false);
            ++lastHeads;
        }

        // every grain as a dot
        const auto grain = theme (Tok::grain);
        for (const auto& d : grains (area, xOf))
        {
            g.setColour (grain.withMultipliedAlpha (d.alpha));
            g.fillRect (juce::Rectangle<float> (d.x - 1.5f, d.y - 1.5f, 3.0f, 3.0f));
            ++lastGrains;
        }
    }
}
