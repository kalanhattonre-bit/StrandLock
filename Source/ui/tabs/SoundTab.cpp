#include "SoundTab.h"

#include "PluginProcessor.h"
#include "core/Music.h"

namespace sl::ui
{
    juce::String SoundTab::scaleLabel (const char* name)
    {
        const juce::String s (name);
        if (s == "Harm. minor")
            return "Harmonic minor";
        if (s == "Pent. major")
            return "Pentatonic major";
        if (s == "Pent. minor")
            return "Pentatonic minor";
        return s;
    }

    SoundTab::SoundTab (const Theme& t, StrandLockProcessor& p)
        : TabPanel (t, p.getApvts()), processor (p)
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

        // M7: Detect pitch & key, then the Key, Scale and Scale lock (shell.html's Sound pane).
        newRow();
        {
            auto b = std::make_unique<FlatButton> (t, "Detect pitch & key");
            b->setTooltip ("Listen to your own sound file (the Region, when it is a part of the file) and set the root note, and the key as one undo step");
            b->onClick = [this] { processor.detectPitchAndKey(); };
            const int w = b->preferredWidth();
            detect = &add (std::move (b), w, 26);
        }
        shortSeparator();
        {
            auto c = std::make_unique<Caption> (t, "Key", Caption::Style::label);
            const int w = c->preferredWidth();
            add (std::move (c), w, 18);
        }
        {
            auto k = std::make_unique<juce::ComboBox>();
            k->setTitle ("Key");
            k->setTooltip ("The key the roll shades, and Scale lock snaps to");
            for (int i = 0; i < 12; ++i)
                k->addItem (core::kNoteNames[i], i + 1);
            k->onChange = [this]
            {
                if (syncing || keyBox->getSelectedId() <= 0)
                    return;
                auto s = processor.getInstState();
                const auto key = (std::uint8_t) (keyBox->getSelectedId() - 1);
                if (s.key == key)
                    return;
                s.key = key;
                processor.setInstState (s);
                processor.pushUndoStep ("Key", {});   // sl-ui.js: patchInst ({ key }, 'Key')
            };
            keyBox = &add (std::move (k), 64, 26);
        }
        {
            auto sc = std::make_unique<juce::ComboBox>();
            sc->setTitle ("Scale");
            sc->setTooltip ("The scale the roll shades, and Scale lock snaps to");
            for (int i = 0; i < core::kNumScales; ++i)
                sc->addItem (scaleLabel (core::kScales[i].name), i + 1);
            sc->onChange = [this]
            {
                if (syncing || scaleBox->getSelectedId() <= 0)
                    return;
                auto s = processor.getInstState();
                const std::string name = core::kScales[scaleBox->getSelectedId() - 1].name;
                if (s.scale == name)
                    return;
                s.scale = name;
                processor.setInstState (s);
                processor.pushUndoStep ("Scale", {});   // sl-ui.js: patchInst ({ scale }, 'Scale')
            };
            scaleBox = &add (std::move (sc), 150, 26);
        }
        {
            auto l = std::make_unique<FlatButton> (t, "Scale lock");
            l->setTooltip ("New and moved notes land on the scale");
            l->setClickingTogglesState (false);
            l->onClick = [this]
            {
                auto s = processor.getInstState();
                const bool was = s.scaleLock != 0;
                s.scaleLock = was ? 0 : 1;
                processor.setInstState (s);
                processor.pushUndoStep (was ? "Scale lock off" : "Scale lock on", {});
                refresh();
            };
            const int w = l->preferredWidth();
            lock = &add (std::move (l), w, 26);
        }
        // M7 V3: keeping the sound with the project (PLUGIN-PLAN 3): the project's switch and what it keeps now, then the
        // library (the person's own switch) and what it holds.
        newRow();
        {
            auto k = std::make_unique<FlatButton> (t, "Keep the sound in the project");
            k->setTooltip ("Save the sound file inside the Cubase project, so the project still plays it if the file moves: up to 20 MB "
                           "without asking, up to 50 MB if you say yes, else the Region and 1 s either side (24-bit FLAC)");
            k->setClickingTogglesState (false);
            k->onClick = [this]
            {
                processor.setKeepInProject (! processor.getKeepInProject());
                refresh();
            };
            const int w = k->preferredWidth();
            keepToggle = &add (std::move (k), w, 26);
        }
        {
            auto c = std::make_unique<Caption> (t, " ", Caption::Style::sub);
            keepStatus = &add (std::move (c), 120, 18);
        }
        newRow();
        {
            auto l = std::make_unique<FlatButton> (t, "Library");
            l->setTooltip ("Also keep a copy of every sound file you load in Documents\\StrandLock\\Samples, named by its fingerprint "
                           "(SHA-256), so a project finds a moved file there (for every project; off unless you turn it on)");
            l->setClickingTogglesState (false);
            l->onClick = [this]
            {
                processor.setLibraryOn (! processor.isLibraryOn());
                refresh();
            };
            const int w = l->preferredWidth();
            libraryToggle = &add (std::move (l), w, 26);
        }
        {
            auto c = std::make_unique<Caption> (t, " ", Caption::Style::sub);
            libraryStatus = &add (std::move (c), 120, 18);
        }
        newRow();
        caption ("The sound menu and Browse are on the waveform strip below; its chips suggest the root, the key and the parts.",
                 Caption::Style::sub);
        refresh();
    }

    juce::String SoundTab::libraryText (bool on, std::int64_t bytes)
    {
        const juce::String size = juce::String ((double) bytes / 1.0e6, 1) + " MB";
        if (on)
            return "On: a copy of each sound file you load goes to Documents\\StrandLock\\Samples" + (bytes >= 0 ? " (" + size + " there now)" : juce::String());
        return "Off: no copies are made" + (bytes >= 0 ? " (the library holds " + size + ")" : juce::String());
    }

    void SoundTab::refresh()
    {
        const auto s = processor.getInstState();
        const juce::ScopedValueSetter<bool> hold (syncing, true);
        const int key = (int) s.key + 1;
        if (keyBox->getSelectedId() != key)
            keyBox->setSelectedId (key, juce::dontSendNotification);
        const int scale = core::findScale (s.scale);
        if (scale >= 0)
        {
            if (scaleBox->getSelectedId() != scale + 1)
                scaleBox->setSelectedId (scale + 1, juce::dontSendNotification);
        }
        else if (scaleBox->getText() != juce::String::fromUTF8 (s.scale.c_str()))
        {
            // a scale this version does not know (a newer project): shown as it is named, and kept
            scaleBox->setText (juce::String::fromUTF8 (s.scale.c_str()), juce::dontSendNotification);
        }
        lock->setToggleState (s.scaleLock != 0, juce::dontSendNotification);

        // M7 V3: the switches and what they keep now; a text that changes takes its own width (the panel lays out again).
        keepToggle->setToggleState (processor.getKeepInProject(), juce::dontSendNotification);
        libraryToggle->setToggleState (processor.isLibraryOn(), juce::dontSendNotification);
        bool moved = false;
        auto show = [this, &moved] (Caption& c, const juce::String& words)
        {
            if (c.getText() == words)
                return;
            c.setText (words);
            moved = resizeItem (c, juce::jmax (40, c.preferredWidth() + 2)) || moved;
        };
        show (*keepStatus, juce::String::fromUTF8 (processor.getEmbedStatus().c_str()));
        if (processor.getLibraryBytes() < 0)
            processor.measureLibrary();   // once: the line shows the library's size, on or off
        show (*libraryStatus, libraryText (processor.isLibraryOn(), processor.getLibraryBytes()));
        if (moved)
            layoutChanged();
    }
}
