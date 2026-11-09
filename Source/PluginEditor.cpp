#include "PluginEditor.h"

#include <cmath>

using strandlock::ui::Theme;

namespace
{
    // The layout is drawn at 1100 x 680 and scaled as one piece, from 75% to 200%.
    constexpr int minWidth = Theme::baseWidth * 3 / 4;
    constexpr int maxWidth = Theme::baseWidth * 2;

    int heightFor (int width)
    {
        return juce::roundToInt ((double) width * Theme::baseHeight / Theme::baseWidth);
    }

    void paintLogo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        // The family mark (GrainLock, CycleLock, PulseLock) in StrandLock's orchid: a loop with one point riding it.
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        const auto ring = juce::Rectangle<float> (d, d).withCentre (area.getCentre()).reduced (2.0f);
        g.setColour (Theme::accent);
        g.drawEllipse (ring, 2.2f);

        const float angle = juce::MathConstants<float>::pi * 0.28f;
        const float r = ring.getWidth() * 0.5f;
        const auto dot = ring.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * r;
        g.setColour (Theme::background);
        g.fillEllipse (juce::Rectangle<float> (d * 0.42f, d * 0.42f).withCentre (dot));
        g.setColour (Theme::text);
        g.fillEllipse (juce::Rectangle<float> (d * 0.26f, d * 0.26f).withCentre (dot));
    }
}

StrandLockEditor::StrandLockEditor (StrandLockProcessor& p)
    : AudioProcessorEditor (&p)
{
    setLookAndFeel (&lookAndFeel);

    // Size first: setResizeLimits clamps the current size, and a 0 x 0 editor would become the minimum.
    setSize (Theme::baseWidth, Theme::baseHeight);

    setResizable (true, true);
    setResizeLimits (minWidth, heightFor (minWidth), maxWidth, heightFor (maxWidth));
    if (auto* bounds = getConstrainer())
        bounds->setFixedAspectRatio ((double) Theme::baseWidth / (double) Theme::baseHeight);
}

StrandLockEditor::~StrandLockEditor()
{
    setLookAndFeel (nullptr);
}

void StrandLockEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Everything is laid out at the base size and drawn scaled to the window.
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) Theme::baseWidth));

    // Wordmark, at the same place as in every Lock plugin.
    paintLogo (g, { 16.0f, 11.0f, 22.0f, 22.0f });
    g.setColour (Theme::text);
    g.setFont (Theme::font (17.0f, true, 0.2f));
    g.drawText ("STRANDLOCK", juce::Rectangle<int> (46, 10, 200, 24), juce::Justification::centredLeft, false);

    // The rest of the window: one empty panel until the milestones fill it.
    const auto area = juce::Rectangle<float> (12.0f, 44.0f, (float) Theme::baseWidth - 24.0f, (float) Theme::baseHeight - 56.0f);
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (area, Theme::corner);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (15.0f));
    g.drawText ("StrandLock " STRANDLOCK_VERSION " skeleton", area, juce::Justification::centred, false);
}
