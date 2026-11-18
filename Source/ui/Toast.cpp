#include "Toast.h"

#include <cmath>

namespace sl::ui
{
    Toast::Toast (const Theme& t)
        : theme (t)
    {
        setInterceptsMouseClicks (false, false);
    }

    void Toast::show (const juce::String& message, int ms)
    {
        text = message;
        setVisible (true);
        repaint();
        startTimer (juce::jmax (500, ms));
    }

    int Toast::preferredWidth() const
    {
        return (int) std::ceil (Theme::textWidth (Theme::px (12.0f), text)) + 26;
    }

    void Toast::paint (juce::Graphics& g)
    {
        // shell.html .toast: toast-bg, 8 px corners, 12 px text in toast-txt, centred
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme (Tok::toastBg));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme (Tok::toastTxt));
        g.setFont (Theme::px (12.0f));
        g.drawFittedText (text, getLocalBounds().reduced (12, 4), juce::Justification::centred, 3, 0.9f);
    }

    void Toast::timerCallback()
    {
        stopTimer();
        setVisible (false);
    }
}
