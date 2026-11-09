// The scaffold's editor: 1100 x 680, resizable, the dark theme with the orchid logo; and its snapshots.
// (Moved from the skeleton's inline TestMain; M0 owns this file.)

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace
{
    using namespace sl::test;
    using sl::ui::Theme;

    int channelDistance (juce::Colour a, juce::Colour b)
    {
        return std::max ({ std::abs ((int) a.getRed() - (int) b.getRed()),
                           std::abs ((int) a.getGreen() - (int) b.getGreen()),
                           std::abs ((int) a.getBlue() - (int) b.getBlue()) });
    }

    /** How many pixels in area are within tolerance of colour. */
    int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour, int tolerance)
    {
        int n = 0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pixels.getPixelColour (x, y), colour) <= tolerance)
                    ++n;
        return n;
    }

    /** The editor exactly as the host shows it at its default size, drawn at the given scale. */
    juce::Image renderEditor (float scale)
    {
        StrandLockProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        if (editor == nullptr)
            return {};
        return editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    }
}

SL_TEST ("ui", editorLayoutAndTheme)
{
    section ("Editor: 1100 x 680, resizable 75% to 200%, renders the dark theme");

    StrandLockProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    check (editor != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 680,
           fmt ("opens at %d x %d", editor != nullptr ? editor->getWidth() : 0, editor != nullptr ? editor->getHeight() : 0));
    if (editor == nullptr)
        return;

    const auto* limits = editor->getConstrainer();
    check (editor->isResizable() && limits != nullptr
               && limits->getMinimumWidth() == 825 && limits->getMinimumHeight() == 510
               && limits->getMaximumWidth() == 2200 && limits->getMaximumHeight() == 1360,
           fmt ("resizable from 825 x 510 to 2200 x 1360 (%d x %d to %d x %d)",
                limits != nullptr ? limits->getMinimumWidth() : 0, limits != nullptr ? limits->getMinimumHeight() : 0,
                limits != nullptr ? limits->getMaximumWidth() : 0, limits != nullptr ? limits->getMaximumHeight() : 0));

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    check (image.isValid() && image.getWidth() == 1100 && image.getHeight() == 680,
           fmt ("renders a %d x %d image", image.getWidth(), image.getHeight()));

    juce::MemoryOutputStream png;
    juce::PNGImageFormat format;
    const bool encoded = image.isValid() && format.writeImageToStream (image, png);
    juce::MemoryInputStream pngIn (png.getData(), png.getDataSize(), false);
    const auto decoded = encoded ? format.decodeImage (pngIn) : juce::Image();
    check (decoded.isValid() && decoded.getWidth() == 1100 && decoded.getHeight() == 680,
           fmt ("encodes as a %d-byte PNG that reads back at %d x %d", (int) png.getDataSize(), decoded.getWidth(), decoded.getHeight()));

    if (! image.isValid() || image.getWidth() != 1100 || image.getHeight() != 680)
        return;

    // The dark theme: the background beside the wordmark, and the empty panel below it.
    const auto bg = image.getPixelAt (700, 22);
    const auto panel = image.getPixelAt (300, 200);
    check (channelDistance (bg, Theme::background) <= 1,
           fmt ("top bar is the suite's background #151618 (#%s)", bg.toDisplayString (false).toRawUTF8()));
    check (channelDistance (panel, Theme::panel) <= 1,
           fmt ("the empty panel is the suite's panel #1D1E21 (#%s)", panel.toDisplayString (false).toRawUTF8()));

    // The logo's ring is drawn in the orchid accent, and the wordmark in the text colour.
    const int ringPixels = countNear (image, { 16, 11, 22, 22 }, Theme::accent, 12);
    const int wordPixels = countNear (image, { 46, 10, 200, 24 }, Theme::text, 12);
    const int strayAccent = countNear (image, { 46, 0, 1054, 44 }, Theme::accent, 12);
    check (ringPixels >= 20, fmt ("logo ring in orchid #B58CFF (%d pixels)", ringPixels));
    check (wordPixels >= 30, fmt ("STRANDLOCK wordmark in the text colour (%d pixels)", wordPixels));
    check (strayAccent == 0, fmt ("no orchid elsewhere in the top bar (%d pixels)", strayAccent));
}

// The snapshots the CI uploads (and the Debug runner draws with jassert live).
SL_SNAPSHOT (strandlock_scaffold_dark)
{
    return renderEditor (1.0f);
}

SL_SNAPSHOT (strandlock_scaffold_dark_2x)
{
    return renderEditor (2.0f);
}
