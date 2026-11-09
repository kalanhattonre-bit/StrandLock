#include "SoundTab.h"

namespace sl::ui
{
    SoundTab::SoundTab (const Theme& t, juce::AudioProcessorValueTreeState& s)
        : TabPanel (t, s)
    {
        using params::P;

        knob (P::rootNote, "Root", Tok::lanePitch);
        knob (P::rootCents, "Cents", Tok::lanePitch, true);
        separator();
        knob (P::vibRate, "Vibrato speed", Tok::laneVib);
        knob (P::bendRange, "Bend range", Tok::lanePitch);
        knob (P::velSens, "Velocity", Tok::laneAmp);
        knob (P::polyphony, "Voices", Tok::acc);
        separator();
        // Not bipolar: the skewed range puts -12 dB at twelve o'clock, so a bipolar arc would be lit at 0 dB.
        knob (P::outGain, "Output", Tok::laneAmp);

        newRow();
        choice (P::vibSync, "Vibrato sync", 120);
        toggle (P::liveLegato, "Live legato");
        caption ("The sound menu and Browse arrive in M2; key, scale and Detect pitch in M7.", Caption::Style::sub);
    }
}
