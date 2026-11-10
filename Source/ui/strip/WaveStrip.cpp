#include "WaveStrip.h"

#include "PluginProcessor.h"
#include "core/JsCompat.h"
#include "core/Music.h"
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
        float clampf (float v, float a, float b) noexcept { return v < a ? a : v > b ? b : v; }

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
          showAllButton (t, "Show all")
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
    }

    int WaveStrip::heightFor (int windowHeight) noexcept
    {
        return windowHeight > 820 ? 124 : windowHeight > 720 ? 112 : 104;
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
        return r;
    }

    void WaveStrip::setRegion (double start, double end)
    {
        // The parameters (inside the drag's gestures) and the exact f64 edges (plan 2.2), as one change.
        startBinding->setPlain ((float) start);
        endBinding->setPlain ((float) end);
        auto s = processor.getInstState();
        s.regionStartExact = start;
        s.regionEndExact = end;
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
            if (sound->cut)
                headerRest << dot() << "its first 10 minutes";
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

    void WaveStrip::refresh()
    {
        auto now = processor.getCurrentSound();
        const auto r = regionNow();
        const int rootNow = juce::roundToInt (params::plainValueOf (processor.getParam (params::P::rootNote)));
        const float centsNow = params::plainValueOf (processor.getParam (params::P::rootCents));

        const bool newSound = now != sound;
        if (! newSound && juce::exactlyEqual (r.start, exactStart) && juce::exactlyEqual (r.end, exactEnd) && rootNow == root
            && juce::exactlyEqual (centsNow, cents))
            return;

        if (newSound)
        {
            sound = std::move (now);
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
        }
        exactStart = r.start;
        exactEnd = r.end;
        root = rootNow;
        cents = centsNow;
        makeHeader();
        repaint();
    }

    void WaveStrip::setDisplayFrame (const engine::DisplayFrame* f)
    {
        if (f == nullptr)
        {
            if (frame != nullptr)
            {
                frame.reset();
                repaint();
            }
            return;
        }
        if (frame == nullptr)
            frame = std::make_unique<engine::DisplayFrame>();
        *frame = *f;
        repaint (waveArea);
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
        waveArea = r;
        columnsSound = nullptr;
    }

    void WaveStrip::paint (juce::Graphics& g)
    {
        g.setColour (theme (Tok::panel));
        g.fillAll();
        g.setColour (theme (Tok::line));
        g.fillRect (columnWidth - 1, 0, 1, getHeight());
        g.fillRect (0, 0, getWidth(), 1);   // the line under the roll

        // The file line: the name in bold, then the length and the root (cut short with ... when there is no room).
        {
            auto line = juce::Rectangle<int> (columnWidth + 8, 0, juce::jmax (0, atEnd->getX() - columnWidth - 16), headerHeight);
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
        paintHeads (g, area);

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
            columns = peaks->columns (sound->left, sound->right, viewA, viewB, width);
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

    void WaveStrip::paintHeads (juce::Graphics& g, juce::Rectangle<float> area)
    {
        lastHeads = 0;
        lastGrains = 0;
        if (frame == nullptr)
            return;

        // sl-ui.js drawStrip: every head as a numbered line with its direction, every grain as a dot.
        const float y = area.getY(), H = area.getHeight();
        const auto headColour = theme (Tok::head), headText = theme (Tok::headTxt);
        g.setFont (Theme::px (9.0f, true));
        const int voices = juce::jmin ((int) frame->numVoices, engine::kDisplayVoices);
        for (int v = 0; v < voices; ++v)
        {
            const auto& voice = frame->voices[v];
            const int heads = juce::jmin ((int) voice.numHeads, engine::kMaxHeads);
            for (int k = 0; k < heads; ++k)
            {
                const auto& h = voice.heads[k];
                const float x = xOf ((double) h.pos);
                if (! (x > area.getX() - 12.0f && x < area.getRight() + 12.0f))
                    continue;
                const float lv = clampf (h.level * voice.env * 1.6f, 0.0f, 1.0f);
                const float alpha = (voice.released ? 0.45f : 1.0f) * (0.35f + 0.65f * lv);
                g.setColour (headColour.withMultipliedAlpha (alpha));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, y + (float) band, 2.0f, H - (float) band));
                g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x, y + (float) band + 8.0f }));
                juce::Path arrow;
                const float b = y + H;
                if (h.dir > 0)
                    arrow.addTriangle (x + 2.0f, b - 13.0f, x + 10.0f, b - 8.0f, x + 2.0f, b - 3.0f);
                else if (h.dir < 0)
                    arrow.addTriangle (x - 2.0f, b - 13.0f, x - 10.0f, b - 8.0f, x - 2.0f, b - 3.0f);
                else
                    arrow.addRectangle (x - 3.0f, b - 11.0f, 6.0f, 6.0f);
                g.fillPath (arrow);
                g.setColour (headText.withMultipliedAlpha (alpha));
                g.drawText (juce::String ((int) h.index + 1), juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x, y + (float) band + 8.0f }),
                            juce::Justification::centred, false);
                ++lastHeads;
            }
        }

        const auto grain = theme (Tok::grain);
        const int grains = juce::jmin ((int) frame->numGrains, engine::kDisplayGrains);
        for (int i = 0; i < grains; ++i)
        {
            const auto& gr = frame->grains[i];
            const float x = xOf ((double) gr.pos);
            if (! (x >= area.getX() && x <= area.getRight()))
                continue;
            const float age = clampf (gr.age, 0.0f, 1.0f);
            g.setColour (grain.withMultipliedAlpha (0.9f - 0.6f * age));
            g.fillRect (juce::Rectangle<float> (x - 1.5f, y + (float) band + 20.0f + (H - (float) band - 34.0f) * age, 3.0f, 3.0f));
            ++lastGrains;
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
        dragAt = juce::Time::getMillisecondCounterHiRes();
        auditionStop();
        if (dragInside)
        {
            // Hear that spot: the root note, starting there (the integration step plays the command queue).
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

    void WaveStrip::mouseUp (const juce::MouseEvent&)
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
            // Holding the mouse was listening; a short click sets the selected notes' Start (with the roll, M4).
            auditionStop();
            return;
        }
        endRegionGesture();
        processor.pushUndoStep ("Region", { params::P::regionStart, params::P::regionEnd });
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
