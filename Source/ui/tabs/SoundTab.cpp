#include "SoundTab.h"

#include "PluginProcessor.h"
#include "core/Music.h"
#include "core/Rates.h"
#include "params/ParameterLayout.h"

#include <algorithm>
#include <cmath>

namespace sl::ui
{
    //==============================================================================================
    VibratoSyncColumn::VibratoSyncColumn (const Theme& t, juce::AudioProcessorValueTreeState& state)
        : theme (t), choice (t, state, params::kParams[params::index (params::P::vibSync)].id, juce::String(), width)
    {
        choice.getBox().setTitle ("Vibrato sync");
        choice.getBox().setTooltip ("Free: the vibrato runs at Vibrato speed. A note value locks it to the song's tempo "
                                    "(1/8: one wobble every eighth note)");
        addAndMakeVisible (choice);
    }

    juce::StringArray VibratoSyncColumn::labelsFor (int beatsPerBar)
    {
        // the parameter's words (Free, then rateLabel), with "1 bar" read as the 4 beats it is when a bar holds another
        // number of beats (PLUGIN-PLAN 2, 8.2 #31)
        juce::StringArray out;
        out.add ("Free");
        for (int i = 0; i < core::kNumRates; ++i)
        {
            juce::String label (core::kRates[i].label);
            if (beatsPerBar != 4 && label.startsWith ("1 bar"))
                label = "4 beats" + label.substring (5);
            out.add (label);
        }
        return out;
    }

    void VibratoSyncColumn::setReadout (const juce::String& text)
    {
        if (text == readout)
            return;
        readout = text;
        repaint();
    }

    void VibratoSyncColumn::resized()
    {
        // as a knob: its caption in the top 16 px, the list where the dial sits (centred 36 px down), the readout under it
        choice.setBounds (0, 23, getWidth(), 26);
    }

    void VibratoSyncColumn::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();
        const auto labelArea = area.removeFromTop (16);
        const auto valueArea = area.removeFromBottom (16);
        g.setColour (theme (Tok::dim));
        g.setFont (Theme::px (10.5f, false, 0.06f));   // a knob's caption (Controls.cpp labelFont)
        g.drawFittedText ("VIBRATO SYNC", labelArea, juce::Justification::centred, 1, 0.8f);
        g.setColour (theme (Tok::txt));
        g.setFont (Theme::px (11.0f));
        g.drawFittedText (readout, valueArea, juce::Justification::centred, 1, 0.8f);
    }

    //==============================================================================================
    juce::String SoundTab::vibratoText (const core::Inst& inst, double bpm)
    {
        if (inst.vibSync == core::kVibSyncFree)
            return juce::String (inst.vibRate, 1) + " Hz, the knob";
        const bool whole = std::abs (bpm - std::round (bpm)) < 1e-9;
        return juce::String (core::vibratoHz (inst, bpm), 2) + " Hz at " + juce::String (bpm, whole ? 0 : 2) + " bpm";
    }

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
        // M8 W1: the vibrato's two controls together, as the prototype's Sound pane keeps its Vibrato speed; each is one
        // undo step (sl-ui.js: commit ('Vibrato speed') when the slider is let go)
        vibSpeed = &knob (P::vibRate, "Vibrato speed", Tok::laneVib);
        vibSpeed->onGestureEnd = [this] { processor.pushUndoStep ("Vibrato speed", { params::P::vibRate }); };
        vibSpeed->getSlider().setTooltip ("How fast the vibrato wobbles (Vibrato sync can lock it to the tempo)");
        {
            auto column = std::make_unique<VibratoSyncColumn> (t, state);
            column->getChoice().onUserChange = [this]
            {
                processor.pushUndoStep ("Vibrato sync " + vibSync->getChoice().getBox().getText().toStdString(), { params::P::vibSync });
                refresh();
            };
            bound (P::vibSync);
            vibSync = &add (std::move (column), VibratoSyncColumn::width, Knob::preferredHeight);
        }
        separator();
        knob (P::bendRange, "Bend range", Tok::lanePitch);
        knob (P::velSens, "Velocity", Tok::laneAmp);
        knob (P::polyphony, "Voices", Tok::acc);
        separator();
        // Not bipolar: the skewed range puts -12 dB at twelve o'clock, so a bipolar arc would be lit at 0 dB.
        knob (P::outGain, "Output", Tok::laneAmp);

        newRow();
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

    void SoundTab::refresh (std::optional<double> hostBpm)
    {
        // M8 W1: the vibrato group. A rate sets the speed from the tempo (HostSync plays 120 bpm when the host gives none),
        // so the knob fades then; the list reads "4 beats" for "1 bar" when the clip's bars hold another number of beats.
        if (hostBpm.has_value())
            hostTempo = *hostBpm >= 1.0 && *hostBpm <= 1000.0 ? *hostBpm : 120.0;
        {
            const core::Inst inst = processor.readInst();
            const bool synced = inst.vibSync != core::kVibSyncFree;
            const float alpha = synced ? 0.45f : 1.0f;
            if (! juce::exactlyEqual (vibSpeed->getAlpha(), alpha))
            {
                vibSpeed->setAlpha (alpha);
                vibSpeed->getSlider().setTooltip (synced ? "Vibrato sync sets the speed from the song's tempo now; pick Free there to use this knob"
                                                         : "How fast the vibrato wobbles (Vibrato sync can lock it to the tempo)");
            }
            vibSync->setReadout (vibratoText (inst, hostTempo));
            const auto clip = processor.getDocument().current();
            const int bpb = clip != nullptr ? std::max (1, clip->beatsPerBar) : 4;
            if ((bpb == 4) != (shownBeatsPerBar == 4))
                vibSync->getChoice().setLabels (VibratoSyncColumn::labelsFor (bpb));
            shownBeatsPerBar = bpb;
        }

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
