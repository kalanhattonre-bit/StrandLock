#pragma once

#include "TabPanel.h"

#include "core/Inst.h"

#include <optional>

class StrandLockProcessor;

namespace sl::ui
{
    /** Vibrato sync (vibSync, M8 W1; PLUGIN-PLAN 7.10 decision 8) in the Sound tab's knob row, beside Vibrato speed: its
        caption over a list of Free and the 21 rates, and under it what the vibrato plays now ("5.5 Hz, the knob" or
        "4.00 Hz at 120 bpm"). The list is the parameter's, and reads "1 bar" as "4 beats" when the clip's bars do not
        hold 4 beats (PLUGIN-PLAN 2: rates are quarter-note beats, so a synced rate keeps its sound in 3/4). */
    class VibratoSyncColumn final : public juce::Component
    {
    public:
        VibratoSyncColumn (const Theme&, juce::AudioProcessorValueTreeState&);

        ChoiceBox& getChoice() noexcept { return choice; }
        const juce::String& getReadout() const noexcept { return readout; }
        void setReadout (const juce::String& text);

        /** The list's words for a clip whose bars hold beatsPerBar beats (Free first, then the 21 rates). */
        static juce::StringArray labelsFor (int beatsPerBar);

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int width = 128;

    private:
        const Theme& theme;
        ChoiceBox choice;
        juce::String readout;
    };

    /** The Sound tab (M0, M7, M8): tuning and playing settings as plain controls bound to their parameters, and (M7, the
        prototype's Sound pane) "Detect pitch & key", the Key and Scale the roll shades and snaps to, and Scale lock.
        Detect listens to the sound (the Region, when it is a part of the file) on the worker and sets the root (no undo
        step: the root belongs to the sound, plan 2.4) and the key (one undo step). Key, Scale and Scale lock are INST
        state, not parameters (plan 2.2): each change is one undo step ("Key", "Scale", "Scale lock on"). The sound menu
        and Browse are on the waveform strip (ui/strip/WaveStrip).
        M8 W1: Vibrato sync sits beside Vibrato speed, the knob fades while a rate sets the speed, and each of the two is
        one undo step ("Vibrato speed" when the knob is let go, as the prototype's commit; "Vibrato sync 1/8"). */
    class SoundTab final : public TabPanel
    {
    public:
        SoundTab (const Theme&, StrandLockProcessor&);

        /** The editor's timer: the Key, Scale and Scale lock show the INST state (Undo, a suggested key, a project); the
            vibrato group shows the sync (the knob faded, the rate in Hz at the host's tempo: 120 bpm when it gives none,
            as HostSync plays then), and the list's words follow the clip's beats per bar. */
        void refresh (std::optional<double> hostBpm = std::nullopt);

        FlatButton& getDetectButton() noexcept { return *detect; }
        juce::ComboBox& getKeyBox() noexcept { return *keyBox; }
        juce::ComboBox& getScaleBox() noexcept { return *scaleBox; }
        FlatButton& getScaleLock() noexcept { return *lock; }
        FlatButton& getKeepToggle() noexcept { return *keepToggle; }        // M7 V3: Keep the sound in the project
        FlatButton& getLibraryToggle() noexcept { return *libraryToggle; }  // M7 V3: the library
        const juce::String& getKeepStatus() const noexcept { return keepStatus->getText(); }
        const juce::String& getLibraryStatus() const noexcept { return libraryStatus->getText(); }
        Knob& getVibratoSpeed() noexcept { return *vibSpeed; }              // M8 W1
        VibratoSyncColumn& getVibratoSync() noexcept { return *vibSync; }   // M8 W1

        /** The scale names as the menu shows them (sl-ui.js scaleLabel: "Harm. minor" reads "Harmonic minor"). */
        static juce::String scaleLabel (const char* name);

        /** The library's line: on or off, and how much it holds (bytes, -1 when not measured yet). */
        static juce::String libraryText (bool on, std::int64_t bytes);

        /** What the vibrato plays (the sync's readout): "5.5 Hz, the knob" for Free, else "4.00 Hz at 120 bpm". */
        static juce::String vibratoText (const core::Inst& inst, double bpm);

    private:
        StrandLockProcessor& processor;
        FlatButton* detect = nullptr;
        juce::ComboBox* keyBox = nullptr;
        juce::ComboBox* scaleBox = nullptr;
        FlatButton* lock = nullptr;
        FlatButton* keepToggle = nullptr;
        Caption* keepStatus = nullptr;
        FlatButton* libraryToggle = nullptr;
        Caption* libraryStatus = nullptr;
        Knob* vibSpeed = nullptr;
        VibratoSyncColumn* vibSync = nullptr;
        int shownBeatsPerBar = 4;   // the words the sync's list has now
        double hostTempo = 120.0;   // the host's tempo at the last refresh that gave one (HostSync's 120 when none)
        bool syncing = false;
    };
}
