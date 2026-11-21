#include "WaveStrip.h"

#include "PluginProcessor.h"
#include "core/JsCompat.h"
#include "core/Music.h"
#include "model/NoteSettings.h"
#include "ui/roll/RollGeometry.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"

#include <algorithm>
#include <cmath>

namespace sl::ui
{
    namespace
    {
        constexpr int kItemHeight = 26;
        constexpr int kFileItemId = 100;   // the menu's line for a loaded file (sl-ui.js fillSoundSel's 'file')

        /** sl-ui.js clamp. */
        double clampd (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        /** " · " (the prototype's file line separator, written as a code point so the source stays ASCII). */
        juce::String dot()
        {
            return juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " ";
        }

        juce::String toFixed (double x, int digits) { return juce::String (core::jsToFixed (x, digits)); }

        /** sl-ui.js fmtTime: minutes, then seconds with two decimals ("0:01.25"). */
        juce::String fmtTime (double s)
        {
            s = std::max (0.0, s);
            const double m = std::floor (s / 60.0), r = s - m * 60.0;
            return juce::String ((juce::int64) m) + ":" + (r < 10.0 ? "0" : "") + toFixed (r, 2);
        }

        /** sl-ui.js fmtCents: "+12 cents", "-5 cents". */
        juce::String fmtCents (double c)
        {
            return juce::String (c >= 0.0 ? "+" : "") + juce::String ((juce::int64) core::jsRound (c)) + " cents";
        }

        const char* paramId (params::P p) noexcept { return params::kParams[params::index (p)].id; }
    }

    //==============================================================================================
    WaveStrip::WaveStrip (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          browseButton (t, "Browse...", FlatButton::Icon::folder),
          presetsButton (t, "Sound presets", FlatButton::Icon::notes),
          zoomOut (t, "-"),
          zoomIn (t, "+"),
          showAllButton (t, "Show all"),
          chips (t, p),
          picker (t, p)
    {
        startBinding = std::make_unique<ParamBinding> (p.getParam (params::P::regionStart));
        endBinding = std::make_unique<ParamBinding> (p.getParam (params::P::regionEnd));

        soundMenu.setTitle ("Sound");
        soundMenu.setTooltip ("Built-in starter sounds");
        soundMenu.onChange = [this]
        {
            const int id = soundMenu.getSelectedId();
            if (id >= 1 && id <= sounds::kNumStarters)
                loadStarter (id - 1);
        };
        addAndMakeVisible (soundMenu);

        browseButton.setTooltip ("Load your own sound file (or drop one anywhere on the window)");
        browseButton.onClick = [this] { browse(); };
        presetsButton.setEnabled (false);
        presetsButton.setTooltip ("Sound presets arrive in M8");
        zoomOut.setTooltip ("Zoom out on the sound");
        zoomOut.onClick = [this] { zoom (1.6); };
        zoomIn.setTooltip ("Zoom in on the sound");
        zoomIn.onClick = [this] { zoom (1.0 / 1.6); };
        showAllButton.setTooltip ("Zoom out to show the whole file (the Region stays as it is)");
        showAllButton.onClick = [this] { showAll(); };
        for (auto* b : { &browseButton, &presetsButton, &zoomOut, &zoomIn, &showAllButton })
            addAndMakeVisible (*b);

        atEnd = std::make_unique<ChoiceBox> (t, p.getApvts(), paramId (params::P::atEnd), "At the end", 74);
        atEnd->getBox().setTooltip ("What a note does when it reaches the end of the Region");
        // sl-ui.js: patchInst ({ region: { atEnd } }, 'At the end: ' + the option's text): one undo step.
        atEnd->onUserChange = [this]
        {
            processor.pushUndoStep ("At the end: " + atEnd->getBox().getText().toStdString(), { params::P::atEnd });
        };
        addAndMakeVisible (*atEnd);

        // M7: the suggestions and the part chips beside the file line (or on a row of their own), and the part picker
        // over the waveform while it is open.
        chips.onToast = [this] (const juce::String& text, int ms) { toast (text, ms); };
        chips.onShowPart = [this] (double a, double b) { showPart (a, b); };
        chips.onShowAll = [this] { showAll(); };
        chips.onPickPart = [this] { picker.open(); };
        chips.onHint = [this] (const juce::String& text)
        {
            if (onHint)
                onHint (text);
        };
        // A click on a chip runs inside that chip: the chips are made again on the editor's next tick, never here.
        chips.onChange = [this] { repaint(); };
        addAndMakeVisible (chips);
        picker.onToast = [this] (const juce::String& text, int ms) { toast (text, ms); };
        picker.onClose = [this] { repaint(); };
        addChildComponent (picker);

        // The view the project was left on (UIST); checked against the sound once it is there.
        const auto ui = p.getUiState();
        viewA = std::isfinite (ui.stripViewA) ? ui.stripViewA : 0.0;
        viewB = std::isfinite (ui.stripViewB) ? ui.stripViewB : 1.0;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        fillSoundMenu();
        makeHeader();
        refresh();
    }

    WaveStrip::~WaveStrip()
    {
        auditionStop();
        endRegionGesture();
        chooser.reset();
        chips.onChange = nullptr;
    }

    int WaveStrip::heightFor (int windowHeight) noexcept
    {
        return windowHeight > 820 ? 124 : windowHeight > 720 ? 112 : 104;
    }

    int WaveStrip::preferredHeight (int windowHeight) const noexcept
    {
        return heightFor (windowHeight) + (chipRow ? chipRowHeight : 0);
    }

    void WaveStrip::showPart (double partStart, double partEnd)
    {
        // sl-ui-sample.js showPart: zoom so a small part fills about half of the strip (a part is a fraction of the loaded
        // range; on a Region excerpt kept in the project, M7 V3, it is shown where it falls on the excerpt)
        const double start = sound != nullptr ? sound->toSound (partStart) : partStart;
        const double end = sound != nullptr ? sound->toSound (partEnd) : partEnd;
        const double w = end - start, vw = viewB - viewA;
        const bool outside = start < viewA - 1e-9 || end > viewB + 1e-9;
        if (! outside && w >= vw * 0.3)
            return;
        const double m = w * 0.5;
        setView (start - m, end + m);
    }

    void WaveStrip::layoutChips()
    {
        // A file's suggestions and part chips get a row of their own under the file line; a starter's one "Parts"
        // chip sits beside it (the prototype's stripSugs follow the file line when there is room).
        const bool want = sound != nullptr && ! sound->builtIn && chips.preferredWidth() > 0;
        if (want != chipRow)
        {
            chipRow = want;
            if (onLayoutChange)
                onLayoutChange();
        }
        resized();
    }

    //==============================================================================================
    double WaveStrip::soundSeconds() const noexcept
    {
        return sound != nullptr ? sound->seconds() : 6.0;   // sl-ui.js sampleSeconds
    }

    double WaveStrip::minRegionWidth() const noexcept
    {
        return std::max (0.002, 0.01 / std::max (1.0e-9, soundSeconds()));   // stripDown's minW
    }

    WaveStrip::Region WaveStrip::regionNow() const
    {
        // What the audio thread plays (ParamBridge::readInst): the exact edge StrandLock set while the parameter
        // still holds (float) of it, else the parameter's value (host automation moved it).
        const auto s = processor.getInstState();
        const float ps = params::plainValueOf (processor.getParam (params::P::regionStart));
        const float pe = params::plainValueOf (processor.getParam (params::P::regionEnd));
        const auto edge = [] (float param, double exact)
        {
            return std::isfinite (exact) && juce::exactlyEqual ((float) exact, param) ? exact : (double) param;
        };
        Region r { edge (ps, s.regionStartExact), edge (pe, s.regionEndExact) };
        if (r.end < r.start)
            std::swap (r.start, r.end);
        // M7 V3: on a Region excerpt kept in the project, the Region (a fraction of the loaded range) where it falls on
        // these samples, kept inside them as it plays (ParamBridge::readInst). Any other sound: as it is.
        if (sound != nullptr && sound->excerpt.active)
        {
            r.start = sound->toSound (r.start);
            r.end = sound->toSound (r.end);
        }
        return r;
    }

    void WaveStrip::setRegion (double start, double end)
    {
        // The parameters (inside the drag's gestures) and the exact f64 edges (plan 2.2), as one change. start and end are
        // fractions of the samples shown; on a Region excerpt (M7 V3) they go back to fractions of the loaded range.
        const double a = sound != nullptr ? sound->toLoaded (start) : start;
        const double b = sound != nullptr ? sound->toLoaded (end) : end;
        startBinding->setPlain ((float) a);
        endBinding->setPlain ((float) b);
        auto s = processor.getInstState();
        s.regionStartExact = a;
        s.regionEndExact = b;
        processor.setInstState (s);
        exactStart = start;
        exactEnd = end;
    }

    void WaveStrip::beginRegionGesture (bool start, bool end)
    {
        if (start)
            startBinding->beginGesture();
        if (end)
            endBinding->beginGesture();
    }

    void WaveStrip::endRegionGesture()
    {
        startBinding->endGesture();
        endBinding->endGesture();
    }

    juce::String WaveStrip::regionText (const Region& r) const
    {
        // sl-ui.js regionText.
        const double s = soundSeconds();
        return "Region " + fmtTime (r.start * s) + " to " + fmtTime (r.end * s) + " (" + toFixed ((r.end - r.start) * s, 2) + " s)";
    }

    float WaveStrip::xOf (double pos) const noexcept
    {
        const double w = std::max (1.0e-12, viewB - viewA);
        return (float) waveArea.getX() + (float) ((pos - viewA) / w * (double) waveArea.getWidth());
    }

    double WaveStrip::posOf (float x) const noexcept
    {
        return viewA + (double) (x - (float) waveArea.getX()) / (double) std::max (1, waveArea.getWidth()) * (viewB - viewA);
    }

    //==============================================================================================
    void WaveStrip::fillSoundMenu()
    {
        // sl-ui.js fillSoundSel: the starters, then the loaded file (when the sound is one).
        soundMenu.clear (juce::dontSendNotification);
        int selected = 0;
        for (int i = 0; i < sounds::kNumStarters; ++i)
        {
            soundMenu.addItem (sounds::kStarterList[i].name, i + 1);
            if (sound != nullptr && sound->builtIn && sound->starter == sounds::kStarterList[i].id)
                selected = i + 1;
        }
        if (sound != nullptr && ! sound->builtIn)
        {
            soundMenu.addSeparator();
            soundMenu.addItem ("File: " + juce::String::fromUTF8 (sound->name.c_str()), kFileItemId);
            selected = kFileItemId;
        }
        if (sound == nullptr)
            selected = 1;   // sawopen, which a new instance is making
        soundMenu.setSelectedId (selected, juce::dontSendNotification);
    }

    void WaveStrip::makeHeader()
    {
        // sl-ui.js updateStripText: the name in bold, then the length and the root.
        if (sound == nullptr)
        {
            headerName = "Loading...";
            headerRest = {};
        }
        else
        {
            headerName = juce::String::fromUTF8 (sound->name.c_str());
            headerRest = dot() + toFixed (sound->seconds(), 2) + " s" + dot() + "root " + juce::String (core::noteName (root))
                         + (juce::exactlyEqual (cents, 0.0f) ? juce::String() : " " + fmtCents ((double) cents))
                         + (sound->builtIn ? " (built-in)" : "");
            extra = headerExtra();
            if (extra.isNotEmpty())
                headerRest << dot() << extra;
            if (sound->missing)
            {
                const auto& f = sound->ref.file;
                const auto name = juce::String::fromUTF8 ((f.displayName.empty() ? f.path : f.displayName).c_str());
                headerRest << dot() << (sound->ref.kind == state::kSampleFile ? "the project's file " + name + " was not found"
                                                                              : juce::String ("the project's sound is not in this version"));
            }
        }
        header = headerName + headerRest;
    }

    juce::String WaveStrip::headerExtra() const
    {
        // M7: which part of a long file is loaded, then what listening found (sl-ui.js updateStripText's S.detected,
        // or its 'listening...' while Detect runs). A built-in sound is tuned to C3 already, and says nothing.
        juce::String out;
        if (sound == nullptr || sound->missing)
            return out;
        auto add = [&out] (const juce::String& s)
        {
            if (s.isNotEmpty())
                out << (out.isEmpty() ? juce::String() : dot()) << s;
        };
        if (sound->cut && sound->ref.kind == state::kSampleFile && sound->ref.file.fileSampleRate > 0.0)
        {
            const auto& f = sound->ref.file;
            const auto mmss = [] (double s)
            {
                const auto w = (juce::int64) std::floor (std::max (0.0, s) + 1e-9);
                return juce::String (w / 60) + ":" + (w % 60 < 10 ? "0" : "") + juce::String (w % 60);
            };
            if (f.loadedStartFrame == 0)
                add ("its first 10 minutes");
            else
                add (mmss ((double) f.loadedStartFrame / f.fileSampleRate) + " to " + mmss ((double) (f.loadedStartFrame + f.loadedFrames) / f.fileSampleRate)
                     + " of " + mmss ((double) f.fileFrames / f.fileSampleRate));
        }
        if (! sound->builtIn)
        {
            const auto& s = processor.getSuggestions();
            if (s.soundRequest == sound->request)
                add (s.listening ? juce::String ("listening...") : juce::String::fromUTF8 (s.analysis.detectedText().c_str()));
        }
        return out;
    }

    void WaveStrip::refresh()
    {
        auto now = processor.getCurrentSound();
        const auto r = regionNow();
        const int rootNow = juce::roundToInt (params::plainValueOf (processor.getParam (params::P::rootNote)));
        const float centsNow = params::plainValueOf (processor.getParam (params::P::rootCents));

        const bool newSound = now != sound;
        const bool startsMoved = updateStarts();
        const auto before = sound;
        if (newSound)
            sound = std::move (now);

        // M7: the chips (and whether they need a row of their own), the part picker's overview, the file line's words.
        if (chips.refresh() || newSound)
            layoutChips();
        picker.refresh();
        const bool extraMoved = headerExtra() != extra;

        if (! newSound && ! extraMoved && juce::exactlyEqual (r.start, exactStart) && juce::exactlyEqual (r.end, exactEnd) && rootNow == root
            && juce::exactlyEqual (centsNow, cents))
        {
            if (startsMoved)
                repaint (waveArea);
            return;
        }

        if (newSound)
        {
            peaks = WavePeaks::of (sound);
            columnsSound = nullptr;
            if (sound != nullptr)
            {
                // sl-ui.js setSample shows the whole of a new sound; the first one keeps the project's view.
                if (firstSound)
                    setView (viewA, viewB);
                else
                    setView (0.0, 1.0);
                firstSound = false;
            }
            fillSoundMenu();

            // A new file longer than 10 minutes that the user loaded: the picker offers the other parts (question 9.3).
            // A part picked from the same file does not open it again.
            const bool newFile = before == nullptr || before->ref.kind != state::kSampleFile || sound == nullptr
                              || before->ref.file.path != sound->ref.file.path;
            if (sound != nullptr && sound->cut && ! sound->missing && sound->origin == doc::LoadOrigin::user && newFile
                && sound->request != pickerOffered)
            {
                pickerOffered = sound->request;
                picker.open();
            }
            else if (picker.isOpen() && (sound == nullptr || ! sound->cut))
            {
                picker.close();
            }
        }
        exactStart = r.start;
        exactEnd = r.end;
        root = rootNow;
        cents = centsNow;
        makeHeader();
        if (! chipRow && chips.preferredWidth() > 0)
            resized();   // a starter's chips follow the file line's new text
        repaint();
    }

    void WaveStrip::setDisplayFrame (const engine::DisplayFrame* f)
    {
        if (f == nullptr)
        {
            if (overlay.getFrame() != nullptr)
            {
                overlay.setFrame (nullptr);
                repaint();
            }
            return;
        }
        overlay.setFrame (f);
        repaint (waveArea);
    }

    bool WaveStrip::updateStarts()
    {
        // sl-ui.js drawStrip: the Start markers of the selected notes (in time order, the first 64) that start at a spot
        // in the Region or at a random one
        std::vector<HeadsOverlay::StartMark> marks;
        const auto& doc = processor.getDocument();
        const auto clip = doc.current();
        const auto st = doc.rollState();
        if (clip != nullptr && ! st.selected.empty())
            for (const auto& n : roll::RollGeometry::selectedNotes (*clip, st))
            {
                if ((int) marks.size() >= HeadsOverlay::maxStarts)
                    break;
                const auto& s = n->sample.start;
                if (s.mode != model::StartMode::region && s.mode != model::StartMode::random)
                    continue;
                HeadsOverlay::StartMark m;
                m.pos = s.pos;
                m.range = s.range;
                m.random = s.mode == model::StartMode::random;
                marks.push_back (m);
            }
        return overlay.setStarts (std::move (marks));
    }

    void WaveStrip::setStartAt (double pos0, bool inside)
    {
        // sl-ui.js stripUp: a short click sets the Start of the selected notes there (one undo step), or says why not
        const auto& doc = processor.getDocument();
        const auto clip = doc.current();
        const auto st = doc.rollState();
        if (clip == nullptr || st.selected.empty())
            return;
        std::vector<std::string> ids;
        for (const auto& n : roll::RollGeometry::selectedNotes (*clip, st))
            ids.push_back (n->id);
        if (ids.empty())
            return;
        if (! inside)
        {
            toast ("That spot is outside the Region, and notes start inside it. Drag across the waveform to make a Region there.", 3500);
            return;
        }
        const auto R = regionNow();
        const double pos = clampd ((pos0 - R.start) / std::max (1.0e-9, R.end - R.start), 0.0, 1.0);
        model::StartPatch start;
        start.mode = model::StartMode::region;
        start.pos = pos;
        model::SamplePatch patch;
        patch.start = start;
        const auto out = processor.editClip ("Set start", model::setNoteSample (*clip, ids, patch));
        if (out.refused)
        {
            toast (juce::String::fromUTF8 (out.message.c_str()), 4000);
            return;
        }
        updateStarts();
        repaint (waveArea);
        if (onEdit)
            onEdit();
        const int count = (int) ids.size();
        toast ("Start set for " + juce::String (count) + (count == 1 ? " note" : " notes") + " at "
               + juce::String ((int) core::jsRound (pos * 100.0)) + "% of the Region");
    }

    //==============================================================================================
    void WaveStrip::saveView()
    {
        auto ui = processor.getUiState();
        ui.stripViewA = viewA;
        ui.stripViewB = viewB;
        processor.setUiState (ui);
    }

    void WaveStrip::setView (double a, double b)
    {
        // sl-ui.js setStripView.
        const double n = sound != nullptr ? (double) sound->frames : 1.0;
        const double minW = std::max (256.0 / std::max (1.0, n), 0.0005);
        a = clampd (std::isfinite (a) ? a : 0.0, 0.0, 1.0);
        b = clampd (std::isfinite (b) ? b : 1.0, 0.0, 1.0);
        if (b < a)
            std::swap (a, b);
        if (b - a < minW)
        {
            const double c = (a + b) / 2.0;
            a = c - minW / 2.0;
            b = c + minW / 2.0;
        }
        if (a < 0.0)
        {
            b -= a;
            a = 0.0;
        }
        if (b > 1.0)
        {
            a = std::max (0.0, a - (b - 1.0));
            b = 1.0;
        }
        viewA = a;
        viewB = b;
        saveView();
        repaint();
    }

    void WaveStrip::zoom (double factor, std::optional<double> centre)
    {
        // sl-ui.js zoomStrip.
        const double w0 = viewB - viewA;
        const double n = sound != nullptr ? (double) sound->frames : 1.0;
        const double w = clampd (w0 * factor, std::max (256.0 / std::max (1.0, n), 0.0005), 1.0);
        const double c = centre.has_value() ? *centre : (viewA + viewB) / 2.0;
        double a = c - (c - viewA) * (w / w0);
        a = clampd (a, 0.0, 1.0 - w);
        viewA = a;
        viewB = a + w;
        saveView();
        repaint();
    }

    void WaveStrip::showAll()
    {
        viewA = 0.0;
        viewB = 1.0;
        saveView();
        repaint();
    }

    //==============================================================================================
    void WaveStrip::toast (const juce::String& text, int ms)
    {
        if (onToast)
            onToast (text, ms);
    }

    void WaveStrip::loadStarter (int index)
    {
        if (index < 0 || index >= sounds::kNumStarters)
            return;
        const auto& info = sounds::kStarterList[index];
        toast ("Loading " + juce::String (info.name) + "...");
        processor.loadStarterSound (info.id, 1);
    }

    void WaveStrip::loadFile (const juce::File& file)
    {
        if (file == juce::File())
            return;
        toast ("Loading " + file.getFileName() + "...");
        processor.loadSoundFile (file);
    }

    void WaveStrip::browse()
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        chooser = std::make_unique<juce::FileChooser> ("Load a sound file", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       formats.getWildcardForAllFormats(), true);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<WaveStrip> (this)] (const juce::FileChooser& fc)
                              {
                                  const auto picked = fc.getResult();
                                  if (safe != nullptr && picked != juce::File())
                                      safe->loadFile (picked);
                              });
    }

    void WaveStrip::auditionStop()
    {
        if (auditionNote < 0)
            return;
        processor.getCommandQueue().push (doc::UiCommand::auditionOff (auditionNote));
        auditionNote = -1;
    }

    //==============================================================================================
    void WaveStrip::resized()
    {
        auto r = getLocalBounds();
        auto column = r.removeFromLeft (columnWidth).reduced (8, 6);
        const int h = juce::jmin (kItemHeight, (column.getHeight() - 8) / 3);
        soundMenu.setBounds (column.removeFromTop (h));
        column.removeFromTop (4);
        browseButton.setBounds (column.removeFromTop (h));
        column.removeFromTop (4);
        presetsButton.setBounds (column.removeFromTop (h));

        auto head = r.removeFromTop (headerHeight).reduced (8, 2);
        const int sa = showAllButton.preferredWidth();
        showAllButton.setBounds (head.removeFromRight (sa).withSizeKeepingCentre (sa, 22));
        head.removeFromRight (4);
        zoomIn.setBounds (head.removeFromRight (24).withSizeKeepingCentre (24, 22));
        head.removeFromRight (4);
        zoomOut.setBounds (head.removeFromRight (24).withSizeKeepingCentre (24, 22));
        head.removeFromRight (8);
        const int aw = atEnd->preferredWidth();
        atEnd->setBounds (head.removeFromRight (aw).withSizeKeepingCentre (aw, 22));

        // M7: a file's chips on their own row; a starter's beside the file line, right after its text when there is room.
        if (chipRow)
        {
            chips.setBounds (r.removeFromTop (chipRowHeight).reduced (8, 2));
        }
        else
        {
            head.removeFromRight (8);
            const auto bold = Theme::px (11.5f, true);
            const int textW = (int) std::ceil (Theme::textWidth (bold, headerName) + Theme::textWidth (Theme::px (11.5f), headerRest)) + 12;
            const int cw = juce::jmin (chips.preferredWidth(), head.getWidth());
            const int x = juce::jlimit (head.getX(), juce::jmax (head.getX(), head.getRight() - cw), head.getX() + textW);
            chips.setBounds (juce::Rectangle<int> (x, head.getY(), cw, head.getHeight()));
        }
        waveArea = r;
        picker.setBounds (waveArea);
        columnsSound = nullptr;
    }

    void WaveStrip::paint (juce::Graphics& g)
    {
        g.setColour (theme (Tok::panel));
        g.fillAll();
        g.setColour (theme (Tok::line));
        g.fillRect (columnWidth - 1, 0, 1, getHeight());
        g.fillRect (0, 0, getWidth(), 1);   // the line under the roll

        // The file line: the name in bold, then the length and the root (cut short with ... when there is no room; a
        // starter's chips beside it take their room first).
        {
            const int lineEnd = ! chipRow && chips.preferredWidth() > 0 ? chips.getX() - 6 : atEnd->getX() - 8;
            auto line = juce::Rectangle<int> (columnWidth + 8, 0, juce::jmax (0, lineEnd - columnWidth - 8), headerHeight);
            const auto bold = Theme::px (11.5f, true);
            const int nameWidth = juce::jmin (line.getWidth(), (int) std::ceil (Theme::textWidth (bold, headerName)) + 1);
            g.setColour (theme (Tok::txt));
            g.setFont (bold);
            g.drawText (headerName, line.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
            g.setColour (theme (Tok::dim));
            g.setFont (Theme::px (11.5f));
            g.drawText (headerRest, line, juce::Justification::centredLeft, true);
        }

        if (waveArea.isEmpty())
            return;
        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (waveArea);
        const auto area = waveArea.toFloat();
        paintWave (g, area);
        overlay.paint (g, area, [this] (double p) { return xOf (p); }, exactStart, exactEnd, theme);

        // M7: the part chip under the mouse, as a dashed box over its stretch (sl-ui-sample.js drawHoverPart).
        if (const auto hp = chips.hoveredPart(); hp.has_value())
        {
            const float x0 = xOf (sound != nullptr ? sound->toSound (hp->start) : hp->start);
            const float x1 = xOf (sound != nullptr ? sound->toSound (hp->end) : hp->end);
            g.setColour (theme (Tok::regionBand).withMultipliedAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.0f, x1 - x0), area.getHeight()));
            juce::Path box, dashed;
            box.addRectangle (x0 + 0.75f, area.getY() + 0.75f, juce::jmax (1.0f, x1 - x0 - 1.5f), area.getHeight() - 1.5f);
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, box, dashes, 2);
            g.setColour (theme (Tok::regionEdge));
            g.fillPath (dashed);
        }

        // The view, when zoomed in: a bar along the bottom (drag it to move the view).
        if (viewB - viewA < 0.999)
        {
            g.setColour (theme (Tok::stripShade));
            g.fillRect (juce::Rectangle<float> (area.getX(), area.getBottom() - 4.0f, area.getWidth(), 4.0f));
            g.setColour (theme (Tok::regionEdge));
            g.fillRect (juce::Rectangle<float> (area.getX() + (float) viewA * area.getWidth(), area.getBottom() - 4.0f,
                                                juce::jmax (4.0f, (float) (viewB - viewA) * area.getWidth()), 4.0f));
        }

        // The readout while the mouse is down (shell.html #stripReadout: right 6 px, top 14 px).
        if (readout.isNotEmpty())
        {
            const auto font = Theme::px (11.0f);
            const float w = juce::jmin (area.getWidth() - 12.0f, std::ceil (Theme::textWidth (font, readout)) + 12.0f);
            const auto box = juce::Rectangle<float> (area.getRight() - 6.0f - w, area.getY() + 14.0f, w, 17.0f);
            g.setColour (theme (Tok::tipBg));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (theme (Tok::tipTxt));
            g.setFont (font);
            g.drawText (readout, box.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }

    void WaveStrip::paintWave (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float W = area.getWidth(), H = area.getHeight();
        g.setColour (theme (Tok::stripBg));
        g.fillRect (area);
        if (sound == nullptr || peaks == nullptr)
        {
            g.setColour (theme (Tok::stripTxt));
            g.setFont (Theme::px (12.0f));
            g.drawText (sound == nullptr ? "Loading the sound..." : "This sound is empty", area.reduced (10.0f, 0.0f),
                        juce::Justification::centredLeft, false);
            return;
        }

        // renderWave: one column per pixel of the view, the wave colour at 80 %.
        const int width = juce::jmax (1, (int) W);
        if (columnsSound != sound.get() || columnsWidth != width || ! juce::exactlyEqual (columnsA, viewA) || ! juce::exactlyEqual (columnsB, viewB))
        {
            columns = peaks->columns (*sound, viewA, viewB, width);
            columnsSound = sound.get();
            columnsWidth = width;
            columnsA = viewA;
            columnsB = viewB;
        }
        const float top = (float) band + 2.0f, mid = top + (H - top) / 2.0f, amp = (H - top) * 0.46f;
        juce::RectangleList<float> rects;
        for (int x = 0; x < (int) columns.size(); ++x)
        {
            const auto& c = columns[(std::size_t) x];
            if (c.drawn)
                rects.addWithoutMerging ({ area.getX() + (float) x, area.getY() + mid - c.hi * amp, 1.0f, juce::jmax (1.0f, (c.hi - c.lo) * amp) });
        }
        g.setColour (theme (Tok::wave).withMultipliedAlpha (waveAlpha));
        g.fillRectList (rects);

        // The Region: shade outside it, the band along its top, its orange edges and their handles.
        const auto r = Region { exactStart, exactEnd };
        const float rx0 = xOf (r.start), rx1 = xOf (r.end);
        const float y = area.getY();
        g.setColour (theme (Tok::stripShade));
        if (rx0 > area.getX())
            g.fillRect (juce::Rectangle<float> (area.getX(), y, juce::jmin (W, rx0 - area.getX()), H));
        if (rx1 < area.getRight())
        {
            const float from = juce::jmax (area.getX(), rx1);
            g.fillRect (juce::Rectangle<float> (from, y, area.getRight() - from, H));
        }
        g.setColour (theme (Tok::regionBand));
        g.fillRect (juce::Rectangle<float> (rx0, y, juce::jmax (0.0f, rx1 - rx0), (float) band));
        g.setColour (theme (Tok::regionEdge));
        g.fillRect (juce::Rectangle<float> (rx0 - 1.0f, y, 2.0f, H));
        g.fillRect (juce::Rectangle<float> (rx1 - 1.0f, y, 2.0f, H));
        g.fillRect (juce::Rectangle<float> (rx0, y + H / 2.0f - 9.0f, 5.0f, 18.0f));
        g.fillRect (juce::Rectangle<float> (rx1 - 5.0f, y + H / 2.0f - 9.0f, 5.0f, 18.0f));
        if (rx1 - rx0 > 60.0f)
        {
            g.setColour (theme (Tok::stripTxt));
            g.setFont (Theme::px (9.0f, true));
            g.drawText ("REGION", juce::Rectangle<float> (rx0 + 6.0f, y, 60.0f, (float) band), juce::Justification::centredLeft, false);
        }
    }

    //==============================================================================================
    void WaveStrip::mouseDown (const juce::MouseEvent& e)
    {
        drag = Drag::none;
        if (sound == nullptr || e.mods.isPopupMenu() || ! waveArea.contains (e.getPosition()))
            return;

        // sl-ui.js stripDown.
        const float x = e.position.x, y = e.position.y - (float) waveArea.getY(), H = (float) waveArea.getHeight();
        const auto R = regionNow();
        const float rx0 = xOf (R.start), rx1 = xOf (R.end);
        dragMinWidth = minRegionWidth();
        if (viewB - viewA < 0.999 && y > H - 7.0f)
        {
            drag = Drag::pan;
            panWidth = viewB - viewA;
            mouseDrag (e);
            return;
        }
        const float d0 = std::abs (x - rx0), d1 = std::abs (x - rx1);
        if (std::min (d0, d1) < edgeGrab)
        {
            drag = d0 <= d1 ? Drag::startEdge : Drag::endEdge;
            beginRegionGesture (drag == Drag::startEdge, drag == Drag::endEdge);
            readout = regionText (R);
            repaint();
            return;
        }
        if (y < (float) band + 2.0f && x > rx0 && x < rx1)
        {
            drag = Drag::box;
            dragPos0 = posOf (x);
            dragRegion0 = R;
            beginRegionGesture (true, true);
            readout = regionText (R);
            repaint();
            return;
        }

        const double pos0 = clampd (posOf (x), 0.0, 1.0);
        drag = Drag::click;
        dragX0 = x;
        dragPos0 = pos0;
        dragInside = pos0 >= R.start - 1.0e-9 && pos0 <= R.end + 1.0e-9;
        dragMoved = false;
        dragAt = (double) e.eventTime.toMilliseconds();   // the event time, as the roll reads it (M6: the click rule)
        auditionStop();
        if (dragInside)
        {
            // Hear that spot: the root note, starting there, through the command queue.
            auditionNote = juce::jlimit (0, 127, root);
            const double start = clampd ((pos0 - R.start) / std::max (1.0e-9, R.end - R.start), 0.0, 1.0);
            processor.getCommandQueue().push (doc::UiCommand::auditionOn (auditionNote, 0.8, start));
            readout = fmtTime (pos0 * soundSeconds());
        }
        else
        {
            readout = fmtTime (pos0 * soundSeconds()) + dot() + "outside the Region";
        }
        repaint();
    }

    void WaveStrip::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag == Drag::none)
            return;

        // sl-ui.js stripMove.
        const float x = e.position.x;
        const double p = clampd (posOf (x), 0.0, 1.0);
        const auto R = regionNow();
        switch (drag)
        {
            case Drag::pan:
            {
                const double w = panWidth;
                const double a = clampd ((double) (x - (float) waveArea.getX()) / (double) std::max (1, waveArea.getWidth()) - w / 2.0, 0.0, 1.0 - w);
                viewA = a;
                viewB = a + w;
                saveView();
                repaint();
                return;
            }
            case Drag::startEdge: setRegion (clampd (p, 0.0, R.end - dragMinWidth), R.end); break;
            case Drag::endEdge:   setRegion (R.start, clampd (p, R.start + dragMinWidth, 1.0)); break;
            case Drag::box:
            {
                const double w = dragRegion0.end - dragRegion0.start;
                const double s = clampd (dragRegion0.start + p - dragPos0, 0.0, 1.0 - w);
                setRegion (s, s + w);
                break;
            }
            case Drag::click:
            {
                if (! dragMoved && std::abs (x - dragX0) > 5.0f)
                {
                    dragMoved = true;
                    auditionStop();
                    beginRegionGesture (true, true);
                }
                if (! dragMoved)
                    return;
                const double a = std::min (dragPos0, p);
                double b = std::max (dragPos0, p);
                if (b - a < dragMinWidth)
                    b = std::min (1.0, a + dragMinWidth);
                setRegion (a, b);
                break;
            }
            case Drag::none:
                return;
        }
        readout = regionText (regionNow());
        repaint();
    }

    void WaveStrip::mouseUp (const juce::MouseEvent& e)
    {
        // sl-ui.js stripUp.
        const auto d = drag;
        drag = Drag::none;
        readout = {};
        repaint();
        if (d == Drag::none || d == Drag::pan)
            return;
        if (d == Drag::click && ! dragMoved)
        {
            // Holding the mouse was listening; a short click (300 ms at most) sets the selected notes' Start there.
            auditionStop();
            if ((double) e.eventTime.toMilliseconds() - dragAt <= (double) clickMs)
                setStartAt (dragPos0, dragInside);
            return;
        }
        endRegionGesture();
        processor.pushUndoStep ("Region", { params::P::regionStart, params::P::regionEnd });
        processor.regionChanged();   // sl-ui.js stripUp: a new part of a user file suggests its root (M7)
        refresh();
    }

    void WaveStrip::mouseMove (const juce::MouseEvent& e)
    {
        // sl-ui.js hoverStrip.
        if (sound == nullptr || ! waveArea.contains (e.getPosition()))
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }
        const float x = e.position.x, y = e.position.y - (float) waveArea.getY();
        const float rx0 = xOf (exactStart), rx1 = xOf (exactEnd);
        if (std::min (std::abs (x - rx0), std::abs (x - rx1)) < edgeGrab)
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (y < (float) band + 2.0f && x > rx0 && x < rx1)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    void WaveStrip::mouseExit (const juce::MouseEvent&)
    {
        if (drag == Drag::none)
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void WaveStrip::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (sound == nullptr || ! waveArea.contains (e.getPosition()))
        {
            juce::Component::mouseWheelMove (e, wheel);
            return;
        }
        const float sign = wheel.isReversed ? -1.0f : 1.0f;
        if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        {
            // Move the view a sixth of its width per step (sl-ui.js: the wheel's pixels / 600 of the view).
            const float amount = (e.mods.isShiftDown() ? -wheel.deltaY : wheel.deltaX) * sign;
            if (juce::exactlyEqual (amount, 0.0f))
                return;
            const double w = viewB - viewA;
            viewA = clampd (viewA + (amount > 0.0f ? w : -w) / 6.0, 0.0, 1.0 - w);
            viewB = viewA + w;
            saveView();
            repaint();
            return;
        }
        const float up = wheel.deltaY * sign;
        if (! juce::exactlyEqual (up, 0.0f))
            zoom (up > 0.0f ? 1.0 / 1.25 : 1.25, posOf (e.position.x));
    }
}
