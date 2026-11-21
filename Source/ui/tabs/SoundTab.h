#pragma once

#include "TabPanel.h"

class StrandLockProcessor;

namespace sl::ui
{
    /** The Sound tab (M0, M7): tuning and playing settings as plain controls bound to their parameters, and (M7, the
        prototype's Sound pane) "Detect pitch & key", the Key and Scale the roll shades and snaps to, and Scale lock.
        Detect listens to the sound (the Region, when it is a part of the file) on the worker and sets the root (no undo
        step: the root belongs to the sound, plan 2.4) and the key (one undo step). Key, Scale and Scale lock are INST
        state, not parameters (plan 2.2): each change is one undo step ("Key", "Scale", "Scale lock on"). The sound menu
        and Browse are on the waveform strip (ui/strip/WaveStrip). */
    class SoundTab final : public TabPanel
    {
    public:
        SoundTab (const Theme&, StrandLockProcessor&);

        /** The editor's timer: the Key, Scale and Scale lock show the INST state (Undo, a suggested key, a project). */
        void refresh();

        FlatButton& getDetectButton() noexcept { return *detect; }
        juce::ComboBox& getKeyBox() noexcept { return *keyBox; }
        juce::ComboBox& getScaleBox() noexcept { return *scaleBox; }
        FlatButton& getScaleLock() noexcept { return *lock; }
        FlatButton& getKeepToggle() noexcept { return *keepToggle; }        // M7 V3: Keep the sound in the project
        FlatButton& getLibraryToggle() noexcept { return *libraryToggle; }  // M7 V3: the library
        const juce::String& getKeepStatus() const noexcept { return keepStatus->getText(); }
        const juce::String& getLibraryStatus() const noexcept { return libraryStatus->getText(); }

        /** The scale names as the menu shows them (sl-ui.js scaleLabel: "Harm. minor" reads "Harmonic minor"). */
        static juce::String scaleLabel (const char* name);

        /** The library's line: on or off, and how much it holds (bytes, -1 when not measured yet). */
        static juce::String libraryText (bool on, std::int64_t bytes);

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
        bool syncing = false;
    };
}
