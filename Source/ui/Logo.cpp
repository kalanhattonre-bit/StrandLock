#include "Logo.h"

#include <cmath>

namespace sl::ui
{
    void paintLogo (juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme)
    {
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        const auto ring = juce::Rectangle<float> (d, d).withCentre (area.getCentre()).reduced (2.0f);
        g.setColour (theme (Tok::acc));
        g.drawEllipse (ring, 2.2f);

        const float angle = juce::MathConstants<float>::pi * 0.28f;
        const float r = ring.getWidth() * 0.5f;
        const auto dot = ring.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * r;
        g.setColour (theme (Tok::bg));
        g.fillEllipse (juce::Rectangle<float> (d * 0.42f, d * 0.42f).withCentre (dot));
        g.setColour (theme (Tok::txt));
        g.fillEllipse (juce::Rectangle<float> (d * 0.26f, d * 0.26f).withCentre (dot));
    }

    juce::Font wordmarkFont()
    {
        return Theme::font (17.0f, true, 0.2f);
    }

    void paintWordmark (juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme)
    {
        g.setColour (theme (Tok::txt));
        g.setFont (wordmarkFont());
        g.drawText (kWordmark, area, juce::Justification::centredLeft, false);
    }
}
