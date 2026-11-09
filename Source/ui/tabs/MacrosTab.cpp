#include "MacrosTab.h"

namespace sl::ui
{
    MacrosTab::MacrosTab (const Theme& t, juce::AudioProcessorValueTreeState& s)
        : TabPanel (t, s)
    {
        using params::P;

        caption ("Macros", Caption::Style::label);
        caption ("Each knob moves every note at once (Cubase's Quick Controls 1 to 8). Centre = no change.", Caption::Style::sub);
        caption ("Whole-clip offsets", Caption::Style::label);
        caption ("Added to every note's line.", Caption::Style::sub);

        // sl-ui-sample.js buildMacros: sand knobs, Tone and Formant in their lane colours; all centred.
        newRow();
        const P macros[] = { P::macro1, P::macro2, P::macro3, P::macro4, P::macro5, P::macro6, P::macro7, P::macro8 };
        for (int i = 0; i < 8; ++i)
        {
            const Tok colour = i == 6 ? Tok::laneTone : i == 7 ? Tok::laneFormant : Tok::laneSample;
            knob (macros[i], params::kParams[params::index (macros[i])].name, colour, true);
        }

        separator();
        knob (P::ampOffset, "Volume", Tok::laneAmp, true);
        knob (P::vibOffset, "Vibrato", Tok::laneVib, true);
        knob (P::toneOffset, "Tone", Tok::laneTone, true);
        knob (P::formantOffset, "Formant", Tok::laneFormant, true);
        knob (P::hollowOffset, "Hollow", Tok::laneHollow, true);
    }
}
