// The waveform strip (PLUGIN-PLAN 4 M2): the peaks (sl-ui.js buildPeaks and renderWave, made off the audio thread
// and kept per sound), loading a sound (the starter menu, Browse's file, a drop anywhere on the window, rooted at
// C3) with its file line and toasts (loaded, cut to 10 minutes, missing, could not be read), the Region box (a drag
// across the wave, its edges, its band) with one host gesture per edge and one undo step, zoom (+, -, Show all, the
// wheel, the view bar), and the heads drawn from a display frame. The snapshot writers at the end draw the strip
// with a TestSignals sound and a Region, dark and light, at 100 % and at 900 x 560, zoomed, with heads, and with
// the sound menu open.
//
// Every test that changes the per-user settings puts them back (dark, 100 %, Keys off).

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"
#include "ui/Theme.h"
#include "ui/strip/WavePeaks.h"
#include "ui/strip/WaveStrip.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::ThemeId;
    using ui::Tok;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    juce::String dot()
    {
        return juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " ";
    }

    juce::File stripFolder()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLockTests").getChildFile ("strip");
        dir.createDirectory();
        return dir;
    }

    /** Writes channels as a WAV: 16 or 24-bit PCM, or 32-bit float. */
    bool writeWav (const juce::File& file, const std::vector<std::vector<float>>& channels, double sampleRate, int bits)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto options = juce::AudioFormatWriterOptions {}
                           .withSampleRate (sampleRate)
                           .withNumChannels ((int) channels.size())
                           .withBitsPerSample (bits)
                           .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                         : juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;
        std::vector<const float*> ptrs;
        for (const auto& c : channels)
            ptrs.push_back (c.data());
        const bool ok = writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) channels[0].size());
        writer.reset();
        return ok && file.existsAsFile();
    }

    /** The TestSignals sound the strip tests load: a C major chord (C3 E3 G3), 3 s at 44.1 kHz, mono, 24-bit. */
    juce::File chordFile()
    {
        const auto file = stripFolder().getChildFile ("strip chord.wav");
        writeWav (file, { signals::chord ({ 60.0, 64.0, 67.0 }, 3.0, 44100.0) }, 44100.0, 24);
        return file;
    }

    /** A second one, stereo: a 220 Hz tone on the left and noise on the right, 1.5 s at 48 kHz. */
    juce::File toneFile()
    {
        const auto file = stripFolder().getChildFile ("strip tone.wav");
        auto left = signals::tone (220.0, 1.5, 48000.0);
        auto right = signals::noise (1.5, 48000.0, 5.0, 0.3);
        left.resize (72000, 0.0f);
        right.resize (72000, 0.0f);
        writeWav (file, { left, right }, 48000.0, 16);
        return file;
    }

    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged,
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                 &c, &c, now, down, now, 1, dragged);
    }

    /** What a person does: press at from, drag to to in steps, let go. */
    void dragStrip (ui::WaveStrip& s, juce::Point<float> from, juce::Point<float> to, int steps = 8)
    {
        s.mouseDown (mouse (s, from, from, false));
        for (int i = 1; i <= steps; ++i)
            s.mouseDrag (mouse (s, i == steps ? to : from + (to - from) * ((float) i / (float) steps), from, true));
        s.mouseUp (mouse (s, to, from, true, juce::ModifierKeys()));
    }

    /** Counts the gestures and changes a parameter reports. */
    struct ParamWatch final : juce::AudioProcessorParameter::Listener
    {
        explicit ParamWatch (juce::AudioProcessorParameter& p) : param (p) { param.addListener (this); }
        ~ParamWatch() override { param.removeListener (this); }
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends) += 1; }

        juce::AudioProcessorParameter& param;
        int begins = 0, ends = 0, changes = 0;
    };

    float plainOf (StrandLockProcessor& p, params::P id) { return params::plainValueOf (p.getParam (id)); }

    /** The strip's middle row (not the Region band, not the view bar). */
    float midY (ui::WaveStrip& s) { return (float) s.getWaveArea().getCentreY(); }

    std::vector<doc::UiCommand> drainCommands (StrandLockProcessor& p)
    {
        std::vector<doc::UiCommand> out;
        doc::UiCommand c;
        while (p.getCommandQueue().pop (c))
            out.push_back (c);
        return out;
    }

    int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour, int tolerance)
    {
        int n = 0;
        area = area.getIntersection (image.getBounds());
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const int d = std::max ({ std::abs ((int) c.getRed() - (int) colour.getRed()), std::abs ((int) c.getGreen() - (int) colour.getGreen()),
                                          std::abs ((int) c.getBlue() - (int) colour.getBlue()) });
                n += d <= tolerance ? 1 : 0;
            }
        return n;
    }

    /** A display frame with two voices: one playing with 3 heads (forward, backward, stopped), one releasing with
        one head; and 4 grains. */
    std::unique_ptr<engine::DisplayFrame> fakeFrame()
    {
        auto f = std::make_unique<engine::DisplayFrame>();
        *f = engine::DisplayFrame {};
        f->playing = true;
        f->numVoices = 2;
        auto& v0 = f->voices[0];
        v0.p = 60;
        v0.env = 1.0f;
        v0.numHeads = 3;
        const float pos0[] = { 0.2f, 0.5f, 0.8f };
        const std::int8_t dirs[] = { 1, -1, 0 };
        for (int k = 0; k < 3; ++k)
        {
            v0.heads[k].index = (std::uint8_t) k;
            v0.heads[k].dir = dirs[k];
            v0.heads[k].pos = pos0[k];
            v0.heads[k].level = 0.5f;
        }
        auto& v1 = f->voices[1];
        v1.p = 64;
        v1.env = 0.6f;
        v1.released = true;
        v1.numHeads = 1;
        v1.heads[0].index = 0;
        v1.heads[0].dir = -1;
        v1.heads[0].pos = 0.9f;
        v1.heads[0].level = 0.4f;
        f->numGrains = 4;
        for (int i = 0; i < 4; ++i)
        {
            f->grains[i].pos = 0.3f + 0.05f * (float) i;
            f->grains[i].age = 0.2f * (float) i;
        }
        return f;
    }
}

//==================================================================================================
SL_TEST ("ui", stripPeaksAreRenderWave)
{
    section ("Strip peaks: sl-ui.js buildPeaks (the mid's min and max per 256 frames) and renderWave's columns");

    // A stereo sound whose left and right differ, 10,000 frames (39 blocks and a short last one).
    const auto L = signals::tone (220.0, 10000.0 / 48000.0, 48000.0);
    const auto R = signals::noise (10000.0 / 48000.0, 48000.0, 9.0, 0.4);
    const auto n = (std::int64_t) std::min (L.size(), R.size());
    const ui::WavePeaks peaks (L.data(), R.data(), n);

    // Every block against the samples it covers, with the mid in double rounded to float, as the Float32Array.
    int blockBad = 0;
    for (std::size_t b = 0; b < peaks.blocks(); ++b)
    {
        float lo = std::numeric_limits<float>::infinity(), hi = -std::numeric_limits<float>::infinity();
        for (std::int64_t i = (std::int64_t) b * 256; i < std::min<std::int64_t> (n, (std::int64_t) (b + 1) * 256); ++i)
        {
            const float v = (float) (((double) L[(std::size_t) i] + (double) R[(std::size_t) i]) * 0.5);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        blockBad += juce::exactlyEqual (lo, peaks.blockMin (b)) && juce::exactlyEqual (hi, peaks.blockMax (b)) ? 0 : 1;
    }
    check (n == 10000 && peaks.blocks() == 40 && blockBad == 0,
           fmt ("%d frames in 40 blocks (ceil (10000 / 256)), every one the min and max of its own frames (%d differ)", (int) n, blockBad));

    // Zoomed in (each column under 2 blocks): the column is the samples between its two frames.
    {
        const int width = 400;
        const double a = 0.1, b = 0.3;
        const auto cols = peaks.columns (L.data(), R.data(), a, b, width);
        int bad = 0, drawn = 0;
        for (int x = 0; x < width; ++x)
        {
            const auto i0 = (std::int64_t) std::floor ((a + (b - a) * (double) x / width) * (double) n);
            const auto i1 = std::max<std::int64_t> (i0 + 1, (std::int64_t) std::floor ((a + (b - a) * (double) (x + 1) / width) * (double) n));
            double lo = 1e9, hi = -1e9;
            for (std::int64_t i = i0; i < std::min (n, i1); ++i)
            {
                const double v = ((double) L[(std::size_t) i] + (double) R[(std::size_t) i]) * 0.5;
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
            const auto& c = cols[(std::size_t) x];
            drawn += c.drawn ? 1 : 0;
            bad += c.drawn && juce::exactlyEqual (c.lo, (float) lo) && juce::exactlyEqual (c.hi, (float) hi) ? 0 : 1;
        }
        check (drawn == width && bad == 0, fmt ("a view of 20 %% at 400 columns: every column is its own samples' min and max (%d of %d drawn, %d differ)", drawn, width, bad));
    }

    // The whole sound in 10 columns of about 1000 frames (2 blocks or more): from the block peaks it touches.
    {
        const int width = 10;
        const auto wide = peaks.columns (L.data(), R.data(), 0.0, 1.0, width);
        int bad = 0, fromBlocks = 0;
        for (int x = 0; x < width; ++x)
        {
            const auto i0 = (std::int64_t) std::floor ((0.0 + (1.0 - 0.0) * (double) x / width) * (double) n);
            const auto i1 = std::max<std::int64_t> (i0 + 1, (std::int64_t) std::floor ((0.0 + (1.0 - 0.0) * (double) (x + 1) / width) * (double) n));
            fromBlocks += i1 - i0 >= 512 ? 1 : 0;
            float lo = std::numeric_limits<float>::infinity(), hi = -std::numeric_limits<float>::infinity();
            for (std::int64_t blk = i0 / 256; blk < std::min<std::int64_t> (40, (i1 + 255) / 256); ++blk)
            {
                lo = std::min (lo, peaks.blockMin ((std::size_t) blk));
                hi = std::max (hi, peaks.blockMax ((std::size_t) blk));
            }
            const auto& c = wide[(std::size_t) x];
            bad += c.drawn && juce::exactlyEqual (c.lo, lo) && juce::exactlyEqual (c.hi, hi) ? 0 : 1;
        }
        check (fromBlocks == width && bad == 0,
               fmt ("the whole sound in 10 columns: each the blocks from floor (i0 / 256) to ceil (i1 / 256) (%d differ)", bad));
    }

    // Silence draws a flat line; a sound of 0 frames has no peaks.
    const std::vector<float> quiet (1000, 0.0f);
    const ui::WavePeaks flat (quiet.data(), quiet.data(), 1000);
    const auto flatCols = flat.columns (quiet.data(), quiet.data(), 0.0, 1.0, 50);
    bool allFlat = true;
    for (const auto& c : flatCols)
        allFlat = allFlat && c.drawn && juce::exactlyEqual (c.lo, 0.0f) && juce::exactlyEqual (c.hi, 0.0f);
    const ui::WavePeaks empty (nullptr, nullptr, 0);
    check (allFlat && empty.blocks() == 0 && empty.columns (nullptr, nullptr, 0.0, 1.0, 10).size() == 10,
           "silence is a flat line in every column; an empty sound has no peaks and draws nothing");
}

SL_TEST ("ui", stripPeaksCachedPerSound)
{
    section ("Strip peaks: made once per sound on the message thread, kept while the sound lives");

    StrandLockProcessor proc;
    proc.waitForSounds();
    const auto sound = proc.getCurrentSound();
    const auto made0 = ui::WavePeaks::peaksMade();
    const auto a = ui::WavePeaks::of (sound);
    const auto made1 = ui::WavePeaks::peaksMade();
    const auto b = ui::WavePeaks::of (sound);
    check (sound != nullptr && a != nullptr && a == b && made1 - made0 <= 1 && ui::WavePeaks::peaksMade() == made1 && a->frames() == sound->frames,
           fmt ("the same sound twice gives the same peaks, made once at most (%d, then %d more)", (int) (made1 - made0), (int) (ui::WavePeaks::peaksMade() - made1)));

    // Two windows on the same sound share them.
    const auto madeBefore = ui::WavePeaks::peaksMade();
    {
        StrandLockEditor first (proc);
        StrandLockEditor second (proc);
    }
    check (ui::WavePeaks::peaksMade() == madeBefore, fmt ("two windows on the sound make no new peaks (%d made)", (int) (ui::WavePeaks::peaksMade() - madeBefore)));

    // A sound that is freed takes its peaks with it.
    {
        std::vector<float> samples (4096, 0.25f);
        auto view = std::make_shared<doc::SoundView>();
        auto storage = std::make_shared<std::vector<float>> (std::move (samples));
        view->storage = storage;
        view->left = view->right = storage->data();
        view->frames = 4096;
        const int cachedBefore = ui::WavePeaks::cachedSounds();
        std::shared_ptr<const doc::SoundView> shared = view;
        const auto p = ui::WavePeaks::of (shared);
        const int cachedWith = ui::WavePeaks::cachedSounds();
        shared.reset();
        view.reset();
        storage.reset();
        const int cachedAfter = ui::WavePeaks::cachedSounds();
        check (p != nullptr && p->blocks() == 16 && cachedWith == cachedBefore + 1 && cachedAfter == cachedBefore,
               fmt ("a sound's peaks are cached while it lives (%d, then %d with it, %d after it is freed)", cachedBefore, cachedWith, cachedAfter));
    }
}

SL_TEST ("ui", stripLoadsSounds)
{
    section ("Strip: the starter menu, a file (Browse's pick) and a drop load the sound, rooted at C3; the file line and the toasts say so");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    StrandLockEditor editor (proc);
    auto& view = editor.getView();
    auto& strip = view.getStrip();
    auto& menu = strip.getSoundMenu();

    // The menu: the 8 starters in the prototype's order, sawopen first and chosen.
    bool order = menu.getNumItems() == 8;
    for (int i = 0; i < 8 && order; ++i)
        order = menu.getItemText (i) == sounds::kStarterList[i].name && menu.getItemId (i) == i + 1;
    check (order && menu.getSelectedId() == 1 && strip.headerText() == "Saw that opens up" + dot() + "6.00 s" + dot() + "root C3 (built-in)",
           fmt ("the menu lists the 8 starters, sawopen first and chosen; the file line reads \"%s\"", strip.headerText().toRawUTF8()));

    // A file: what Browse's pick (and a drop) calls.
    const auto chord = chordFile();
    strip.loadFile (chord);
    check (view.getToastText() == "Loading strip chord.wav..." && view.isToastShown(), fmt ("the toast says \"%s\"", view.getToastText().toRawUTF8()));
    proc.waitForSounds();
    editor.refreshNow();
    auto s = proc.getCurrentSound();
    check (s != nullptr && ! s->builtIn && s->name == "strip chord.wav" && s->frames == 132300 && strip.shownSound() == s.get(),
           fmt ("the chord loads: %d frames, and the strip shows it", s != nullptr ? (int) s->frames : 0));
    check (strip.headerText() == "strip chord.wav" + dot() + "3.00 s" + dot() + "root C3",
           fmt ("the file line: \"%s\"", strip.headerText().toRawUTF8()));
    check (view.getToastText() == "Loaded strip chord.wav", fmt ("then the toast says \"%s\"", view.getToastText().toRawUTF8()));
    check (menu.getNumItems() == 9 && menu.getSelectedId() == 100 && menu.getText() == "File: strip chord.wav",
           fmt ("the menu shows the file after the starters (\"%s\")", menu.getText().toRawUTF8()));
    check (juce::exactlyEqual (plainOf (proc, params::P::rootNote), 60.0f) && juce::exactlyEqual (plainOf (proc, params::P::rootCents), 0.0f)
               && juce::exactlyEqual (plainOf (proc, params::P::regionStart), 0.0f) && juce::exactlyEqual (plainOf (proc, params::P::regionEnd), 1.0f)
               && proc.undoLabel().value_or ("") == "Load strip chord.wav",
           fmt ("rooted at C3 with the whole file as the Region, one undo step \"%s\"", proc.undoLabel().value_or ("").c_str()));

    // A starter from the menu.
    menu.setSelectedId (4, juce::sendNotificationSync);
    check (view.getToastText() == "Loading Evolving pad...", fmt ("picking Evolving pad: \"%s\"", view.getToastText().toRawUTF8()));
    proc.waitForSounds();
    editor.refreshNow();
    s = proc.getCurrentSound();
    check (s != nullptr && s->builtIn && s->starter == sounds::StarterId::pad && strip.headerText().startsWith ("Evolving pad" + dot())
               && strip.headerText().endsWith ("(built-in)") && menu.getNumItems() == 8 && menu.getSelectedId() == 4
               && view.getToastText() == "Loaded Evolving pad" && proc.undoLabel().value_or ("") == "Sound: Evolving pad",
           fmt ("it loads, the file line reads \"%s\", the toast \"%s\", undo \"%s\"", strip.headerText().toRawUTF8(),
                view.getToastText().toRawUTF8(), proc.undoLabel().value_or ("").c_str()));

    // A drop anywhere on the window: the first file loads, and the Diagnostics page still records the drop.
    const auto tone = toneFile();
    const int tabBefore = view.getOpenTab();
    editor.fileDragEnter (juce::StringArray (tone.getFullPathName()), 10, 10);
    editor.filesDropped (juce::StringArray (tone.getFullPathName()), 10, 10);
    proc.waitForSounds();
    editor.refreshNow();
    s = proc.getCurrentSound();
    const auto snap = proc.getDiag().snapshot();
    check (s != nullptr && s->name == "strip tone.wav" && s->stereo && s->frames == 72000 && view.getToastText() == "Loaded strip tone.wav"
               && strip.headerText().startsWith ("strip tone.wav" + dot() + "1.50 s"),
           fmt ("a dropped file loads (\"%s\"; toast \"%s\")", strip.headerText().toRawUTF8(), view.getToastText().toRawUTF8()));
    check (snap.drops.size() == 1 && snap.drops[0].readable && snap.drops[0].frames == 72000 && view.getOpenTab() == tabBefore,
           fmt ("the drop is recorded for the Diagnostics page (%d), and the open tab stays", (int) snap.drops.size()));

    // A file that is not a sound: the toast says so and the sound stays.
    const auto text = stripFolder().getChildFile ("not a sound.wav");
    text.replaceWithText ("this is not audio");
    editor.filesDropped (juce::StringArray (text.getFullPathName()), 5, 5);
    proc.waitForSounds();
    editor.refreshNow();
    check (view.getToastText() == "Could not read that file as sound" && proc.getCurrentSound() == s,
           fmt ("a file that is not a sound: \"%s\", the tone stays", view.getToastText().toRawUTF8()));
    resetUserSettings (proc);
}

SL_TEST ("ui", stripLoadToasts)
{
    section ("Strip: the toasts for a file cut to 10 minutes and a project's missing file, and what the file line says");

    const auto dir = stripFolder();

    // Longer than 10 minutes (question 9.3): its first 10 minutes, and the toast and the file line say so.
    {
        const auto longFile = dir.getChildFile ("strip long.wav");
        std::vector<float> samples ((std::size_t) (601 * 8000), 0.0f);
        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * (double) i / 8000.0);
        check (writeWav (longFile, { samples }, 8000.0, 16), "a 601 s mono 16-bit WAV at 8 kHz");
        StrandLockProcessor proc;
        resetUserSettings (proc);
        proc.waitForSounds();
        StrandLockEditor editor (proc);
        editor.getView().getStrip().loadFile (longFile);
        proc.waitForSounds();
        editor.refreshNow();
        const auto& toast = editor.getView().getToastText();
        const auto& header = editor.getView().getStrip().headerText();
        check (toast == "strip long.wav is longer than 10 minutes, so StrandLock loaded its first 10 minutes"
                   && header == "strip long.wav" + dot() + "600.00 s" + dot() + "root C3" + dot() + "its first 10 minutes",
               fmt ("the toast \"%s\"; the file line \"%s\"", toast.toRawUTF8(), header.toRawUTF8()));
        longFile.deleteFile();
    }

    // A project whose file is gone: the default starter plays, the toast names the file, the file line says so.
    {
        const auto gone = dir.getChildFile ("strip gone.wav");
        writeWav (gone, { signals::tone (330.0, 0.5, 44100.0) }, 44100.0, 16);
        juce::MemoryBlock saved;
        {
            StrandLockProcessor first;
            first.loadSoundFile (gone);
            first.waitForSounds();
            first.getStateInformation (saved);
        }
        gone.deleteFile();
        StrandLockProcessor proc;
        resetUserSettings (proc);
        proc.setStateInformation (saved.getData(), (int) saved.getSize());
        StrandLockEditor editor (proc);   // the report waits for the window, which shows it when it opens
        const auto& toast = editor.getView().getToastText();
        const auto& header = editor.getView().getStrip().headerText();
        check (toast.startsWith ("Could not find the sound file \"strip gone.wav\"") && toast.endsWith ("so the Saw that opens up plays")
                   && editor.getView().isToastShown(),
               fmt ("the toast: \"%s\"", toast.toRawUTF8()));
        check (header == "Saw that opens up" + dot() + "6.00 s" + dot() + "root C3 (built-in)" + dot() + "the project's file strip gone.wav was not found",
               fmt ("the file line: \"%s\"", header.toRawUTF8()));
    }
}

SL_TEST ("ui", stripRegionGestures)
{
    section ("Strip: drag across the wave, drag an edge, drag the band; one gesture per edge, exact edges, one undo step");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    proc.loadSoundFile (chordFile());
    proc.waitForSounds();
    StrandLockEditor editor (proc);
    editor.refreshNow();
    auto& strip = editor.getView().getStrip();
    auto& startParam = proc.getParam (params::P::regionStart);
    auto& endParam = proc.getParam (params::P::regionEnd);
    drainCommands (proc);
    const double minW = std::max (0.002, 0.01 / 3.0);

    // 1. Drag across the wave from about 25 % to 60 %: a new Region there.
    {
        ParamWatch ws (startParam), we (endParam);
        const float x0 = strip.xOf (0.25), x1 = strip.xOf (0.6);
        const double a = std::clamp (strip.posOf (x0), 0.0, 1.0), b = std::clamp (strip.posOf (x1), 0.0, 1.0);
        dragStrip (strip, { x0, midY (strip) }, { x1, midY (strip) });
        const auto inst = proc.getInstState();
        const auto played = proc.readInst().region;
        check (juce::exactlyEqual (plainOf (proc, params::P::regionStart), (float) a) && juce::exactlyEqual (plainOf (proc, params::P::regionEnd), (float) b)
                   && juce::exactlyEqual (inst.regionStartExact, a) && juce::exactlyEqual (inst.regionEndExact, b)
                   && juce::exactlyEqual (played.start, a) && juce::exactlyEqual (played.end, b),
               fmt ("the Region is %.6f .. %.6f: the parameters hold their floats, INST and the audio thread the exact edges", a, b));
        check (ws.begins == 1 && ws.ends == 1 && we.begins == 1 && we.ends == 1 && ws.changes >= 1 && we.changes >= 1,
               fmt ("one host gesture per edge for the whole drag (start %d / %d, end %d / %d; %d and %d changes)", ws.begins, ws.ends, we.begins,
                    we.ends, ws.changes, we.changes));
        check (proc.undoLabel().value_or ("") == "Region", fmt ("one undo step \"%s\"", proc.undoLabel().value_or ("").c_str()));
        const auto cmds = drainCommands (proc);
        check (cmds.size() == 2 && cmds[0].kind == doc::UiCommandKind::auditionOn && cmds[0].note == 60 && std::abs (cmds[0].velocity - 0.8) < 1e-12
                   && std::abs (cmds[0].startPos - std::clamp (strip.posOf (x0), 0.0, 1.0)) < 1e-12 && cmds[1].kind == doc::UiCommandKind::auditionOff,
               fmt ("the press inside the old Region asked to hear that spot at C3, and the drag stopped it (%d commands)", (int) cmds.size()));
    }

    // 2. Drag the end edge from 60 % to 80 %: only the end moves, in one gesture of its own.
    {
        const auto before = proc.readInst().region;
        ParamWatch ws (startParam), we (endParam);
        const float x1 = strip.xOf (before.end), x2 = strip.xOf (0.8);
        dragStrip (strip, { x1, midY (strip) }, { x2, midY (strip) });
        const auto after = proc.readInst().region;
        const double want = std::clamp (std::clamp (strip.posOf (x2), 0.0, 1.0), before.start + minW, 1.0);
        check (juce::exactlyEqual (after.start, before.start) && juce::exactlyEqual (after.end, want) && ws.begins == 0 && we.begins == 1 && we.ends == 1,
               fmt ("the end edge moves to %.6f, the start stays %.6f (start gestures %d, end %d)", after.end, after.start, ws.begins, we.begins));
    }

    // 3. Drag the band along the Region's top: the whole Region moves, its width kept.
    {
        const auto before = proc.readInst().region;
        const float bandY = (float) strip.getWaveArea().getY() + 4.0f;
        const float x0 = strip.xOf ((before.start + before.end) / 2.0), x1 = x0 - 0.1f * (float) strip.getWaveArea().getWidth();
        dragStrip (strip, { x0, bandY }, { x1, bandY });
        const auto after = proc.readInst().region;
        const double shift = std::clamp (strip.posOf (x1), 0.0, 1.0) - strip.posOf (x0);
        check (std::abs ((after.end - after.start) - (before.end - before.start)) < 1e-12 && std::abs (after.start - (before.start + shift)) < 1e-12
                   && after.start < before.start,
               fmt ("the band moves the Region by %.4f to %.6f .. %.6f, the same width", shift, after.start, after.end));
    }

    // 4. The start edge dragged past the end stops a minimum width short of it.
    {
        const auto before = proc.readInst().region;
        dragStrip (strip, { strip.xOf (before.start), midY (strip) }, { strip.xOf (before.end) + 40.0f, midY (strip) });
        const auto after = proc.readInst().region;
        check (std::abs (after.start - (before.end - minW)) < 1e-12 && juce::exactlyEqual (after.end, before.end),
               fmt ("the start stops %.4f before the end (%.6f .. %.6f)", minW, after.start, after.end));
    }

    // 5. Undo puts the Region back as it was before that drag, edges exact.
    {
        const auto before = proc.readInst().region;
        check (proc.undo(), "Undo");
        editor.refreshNow();
        const auto after = proc.readInst().region;
        check (after.start < before.start && juce::exactlyEqual (after.end, before.end) && proc.redoLabel().value_or ("") == "Region",
               fmt ("Undo brings back %.6f .. %.6f (redo \"%s\")", after.start, after.end, proc.redoLabel().value_or ("").c_str()));
    }

    // 6. Holding the mouse inside the Region listens; letting go changes nothing.
    {
        const auto before = proc.readInst().region;
        const auto label = proc.undoLabel();
        drainCommands (proc);
        const float x = strip.xOf ((before.start + before.end) / 2.0);
        strip.mouseDown (mouse (strip, { x, midY (strip) }, { x, midY (strip) }, false));
        const auto readout = strip.getReadout();
        strip.mouseUp (mouse (strip, { x, midY (strip) }, { x, midY (strip) }, false, juce::ModifierKeys()));
        const auto cmds = drainCommands (proc);
        const auto after = proc.readInst().region;
        check (cmds.size() == 2 && cmds[0].kind == doc::UiCommandKind::auditionOn && cmds[1].kind == doc::UiCommandKind::auditionOff
                   && std::abs (cmds[0].startPos - 0.5) < 1e-6 && juce::exactlyEqual (after.start, before.start) && juce::exactlyEqual (after.end, before.end)
                   && proc.undoLabel() == label && readout.startsWith ("0:0"),
               fmt ("a held press plays from the middle of the Region (%.4f) and stops; no undo step; the readout says \"%s\"",
                    cmds.empty() ? -1.0 : cmds[0].startPos, readout.toRawUTF8()));

        // Outside the Region: the readout says so, and nothing is asked to play.
        const float out = strip.xOf (std::max (0.0, before.start - 0.05));
        strip.mouseDown (mouse (strip, { out, midY (strip) }, { out, midY (strip) }, false));
        const auto outside = strip.getReadout();
        strip.mouseUp (mouse (strip, { out, midY (strip) }, { out, midY (strip) }, false, juce::ModifierKeys()));
        check (drainCommands (proc).empty() && outside.endsWith ("outside the Region"), fmt ("outside the Region: \"%s\"", outside.toRawUTF8()));
    }

    // 7. At the end on the strip: one undo step named as the prototype names it.
    {
        auto& box = strip.getAtEnd().getBox();
        ParamWatch w (proc.getParam (params::P::atEnd));
        box.setSelectedItemIndex (1, juce::sendNotificationSync);
        check (juce::exactlyEqual (plainOf (proc, params::P::atEnd), 1.0f) && w.begins == 1 && w.ends == 1
                   && proc.undoLabel().value_or ("") == "At the end: Hold",
               fmt ("Hold: the parameter in one gesture and the undo step \"%s\"", proc.undoLabel().value_or ("").c_str()));
    }
    resetUserSettings (proc);
}

SL_TEST ("ui", stripZoom)
{
    section ("Strip: zoom (+, -, Show all, the wheel, the view bar), kept in the project; the Region stays");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    proc.loadSoundFile (chordFile());
    proc.waitForSounds();
    {
        StrandLockEditor editor (proc);
        editor.refreshNow();
        auto& strip = editor.getView().getStrip();
        const auto region = proc.readInst().region;

        strip.getZoomInButton().onClick();
        const double w1 = strip.viewEnd() - strip.viewStart();
        check (std::abs (w1 - 1.0 / 1.6) < 1e-12 && std::abs ((strip.viewStart() + strip.viewEnd()) / 2.0 - 0.5) < 1e-12,
               fmt ("+ shows 1 / 1.6 of the sound around its middle (%.6f .. %.6f)", strip.viewStart(), strip.viewEnd()));
        strip.getZoomOutButton().onClick();
        check (std::abs (strip.viewStart()) < 1e-12 && std::abs (strip.viewEnd() - 1.0) < 1e-12, "- goes back to the whole sound");

        // The wheel zooms around the pointer: the spot under it stays under it.
        const float x = strip.xOf (0.3);
        const double under = strip.posOf (x);
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = 0.5f;
        for (int i = 0; i < 5; ++i)
            strip.mouseWheelMove (mouse (strip, { x, midY (strip) }, { x, midY (strip) }, false, juce::ModifierKeys()), wheel);
        const double width = strip.viewEnd() - strip.viewStart();
        check (std::abs (strip.posOf (x) - under) < 1e-9 && std::abs (width - std::pow (1.0 / 1.25, 5.0)) < 1e-9,
               fmt ("five wheel steps in: 1 / 1.25^5 of the sound (%.6f), the spot under the pointer still there", width));
        const auto ui = proc.getUiState();
        check (juce::exactlyEqual (ui.stripViewA, strip.viewStart()) && juce::exactlyEqual (ui.stripViewB, strip.viewEnd()),
               fmt ("the view is kept in the project (UIST %.6f .. %.6f)", ui.stripViewA, ui.stripViewB));

        // Zoomed in, the view bar along the bottom moves the view.
        const float barY = (float) strip.getWaveArea().getBottom() - 2.0f;
        const auto area = strip.getWaveArea().toFloat();
        dragStrip (strip, { area.getX() + area.getWidth() * 0.5f, barY }, { area.getX() + area.getWidth() * 0.7f, barY }, 4);
        check (std::abs ((strip.viewStart() + strip.viewEnd()) / 2.0 - 0.7) < 0.002 && std::abs ((strip.viewEnd() - strip.viewStart()) - width) < 1e-12
                   && juce::exactlyEqual (proc.readInst().region.start, region.start),
               fmt ("dragging the bar to 70 %% centres the view there (%.4f .. %.4f); the Region does not move", strip.viewStart(), strip.viewEnd()));

        // The zoom stops at 256 frames.
        for (int i = 0; i < 60; ++i)
            strip.zoom (0.5);
        check (std::abs ((strip.viewEnd() - strip.viewStart()) - std::max (256.0 / 132300.0, 0.0005)) < 1e-12,
               fmt ("zooming in stops at %.6f of the sound (256 frames, or 0.0005)", strip.viewEnd() - strip.viewStart()));

        strip.getShowAllButton().onClick();
        const auto r2 = proc.readInst().region;
        check (juce::exactlyEqual (strip.viewStart(), 0.0) && juce::exactlyEqual (strip.viewEnd(), 1.0)
                   && juce::exactlyEqual (r2.start, region.start) && juce::exactlyEqual (r2.end, region.end),
               "Show all shows the whole sound; the Region stays as it is");
        strip.setView (0.2, 0.4);
    }
    {
        StrandLockEditor again (proc);
        const auto& strip = again.getView().getStrip();
        check (std::abs (strip.viewStart() - 0.2) < 1e-12 && std::abs (strip.viewEnd() - 0.4) < 1e-12,
               fmt ("a new window opens on the project's view (%.3f .. %.3f)", strip.viewStart(), strip.viewEnd()));
        proc.loadStarterSound (sounds::StarterId::saw);
        proc.waitForSounds();
        again.refreshNow();
        check (juce::exactlyEqual (strip.viewStart(), 0.0) && juce::exactlyEqual (strip.viewEnd(), 1.0), "another sound is shown whole");
    }
    resetUserSettings (proc);
}

SL_TEST ("ui", stripHeadsFromDisplayFrames)
{
    section ("Strip: the heads and grains of the engine's display frame; none before the integration step sends one");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    StrandLockEditor editor (proc);
    editor.refreshNow();
    auto& strip = editor.getView().getStrip();
    const auto& dark = ui::palette (ThemeId::dark);
    const auto render = [&strip] { return strip.createComponentSnapshot (strip.getLocalBounds(), true, 1.0f); };

    const auto none = render();
    const int headPixelsNone = countNear (none, strip.getWaveArea(), dark[Tok::head], 6);
    check (proc.getDisplaySlot() == nullptr && strip.headsDrawn() == 0 && strip.grainsDrawn() == 0 && headPixelsNone < 5,
           fmt ("no display slot yet (the integration step makes it): no heads drawn (%d head-coloured pixels)", headPixelsNone));

    const auto frame = fakeFrame();
    strip.setDisplayFrame (frame.get());
    editor.refreshNow();   // with no slot the editor leaves the frame it was given
    const auto with = render();
    const int x50 = (int) std::lround (strip.xOf (0.5));
    const auto column = juce::Rectangle<int> (x50 - 1, strip.getWaveArea().getY() + ui::WaveStrip::band, 3, strip.getWaveArea().getHeight() - ui::WaveStrip::band);
    const int headColumn = countNear (with, column, dark[Tok::head], 40);
    check (strip.headsDrawn() == 4 && strip.grainsDrawn() == 4 && headColumn > 40,
           fmt ("4 heads (3 playing, 1 releasing) and 4 grains drawn; the head at 50 %% is a line down the wave (%d pixels)", headColumn));

    strip.setView (0.0, 0.42);
    render();
    check (strip.headsDrawn() == 1 && strip.grainsDrawn() == 3,
           fmt ("zoomed to 0 .. 42 %%: only what is in view (%d heads, %d grains)", strip.headsDrawn(), strip.grainsDrawn()));
    strip.setDisplayFrame (nullptr);
    render();
    check (strip.headsDrawn() == 0, "no frame: nothing drawn");
    resetUserSettings (proc);
}

//==================================================================================================
// Snapshots: the strip with the chord loaded and a Region (dragged), the sound menu open (drawn with JUCE's own
// menu drawing, since a real menu opens in a window of its own), zoomed in with heads.

namespace
{
    /** Draws the sound menu open above its box, as JUCE's PopupMenu draws a combo box's menu (the same look and
        feel calls: background, then each item, the chosen one ticked, the one under the mouse lit). Returns where
        it drew, in the editor. */
    juce::Rectangle<int> paintMenuOpen (juce::Image& image, juce::Component& editor, juce::ComboBox& box, int hoverId)
    {
        auto& lf = box.getLookAndFeel();
        juce::Label label;
        label.setSize (juce::jmax (1, box.getWidth() - 24), juce::jmax (1, box.getHeight() - 2));
        const auto options = lf.getOptionsForComboBoxPopupMenu (box, label);
        juce::PopupMenu menu (*box.getRootMenu());
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().itemID != 0)
                it.getItem().isTicked = it.getItem().itemID == box.getSelectedId();

        const int border = lf.getPopupMenuBorderSizeWithOptions (options);
        int width = options.getMinimumWidth(), height = 0;
        std::vector<int> heights;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            int w = 0, h = 0;
            lf.getIdealPopupMenuItemSizeWithOptions (it.getItem().text, it.getItem().isSeparator, options.getStandardItemHeight(), w, h, options);
            width = juce::jmax (width, w);
            heights.push_back (h);
            height += h;
        }
        const int menuW = width + 2 * border, menuH = height + 2 * border;
        const auto boxArea = editor.getLocalArea (&box, box.getLocalBounds());
        const juce::Rectangle<int> area (boxArea.getX(), juce::jmax (0, boxArea.getY() - menuH), menuW, menuH);
        juce::Graphics g (image);
        g.setOrigin (area.getTopLeft());
        g.reduceClipRegion (0, 0, menuW, menuH);   // the menu's own window: its background fill stops at its edges
        lf.drawPopupMenuBackgroundWithOptions (g, menuW, menuH, options);
        int y = border;
        std::size_t i = 0;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next(); ++i)
        {
            const juce::Rectangle<int> r (border, y, width, heights[i]);
            lf.drawPopupMenuItemWithOptions (g, r, it.getItem().itemID == hoverId, it.getItem(), options);
            y += heights[i];
        }
        return area;
    }

    struct StripShot
    {
        ThemeId theme = ThemeId::dark;
        int logicalWidth = 0, logicalHeight = 0;
        bool menuOpen = false;
        bool zoomed = false;
        bool heads = false;
    };

    juce::Image stripShot (const StripShot& s, juce::Rectangle<int>* menuArea = nullptr)
    {
        StrandLockProcessor proc;
        auto& settings = proc.getUserSettings();
        settings.setTheme (s.theme);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        proc.waitForSounds();
        proc.loadSoundFile (chordFile());
        proc.waitForSounds();

        juce::Image image;
        {
            StrandLockEditor editor (proc);
            if (s.logicalWidth > 0)
                editor.setLogicalSize (s.logicalWidth, s.logicalHeight);
            auto& strip = editor.getView().getStrip();
            editor.refreshNow();
            dragStrip (strip, { strip.xOf (0.22), midY (strip) }, { strip.xOf (0.58), midY (strip) });
            if (s.zoomed)
                strip.setView (0.12, 0.68);
            const auto frame = fakeFrame();
            if (s.heads)
                strip.setDisplayFrame (frame.get());
            editor.refreshNow();
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
            if (s.menuOpen)
            {
                const auto area = paintMenuOpen (image, editor, strip.getSoundMenu(), 4);
                if (menuArea != nullptr)
                    *menuArea = area;
            }
        }
        resetUserSettings (proc);
        return image;
    }
}

SL_TEST ("ui", stripSnapshotsDraw)
{
    section ("Strip snapshots: the chord with a Region, dark and light, 100 % and 900 x 560, the menu open");

    const auto& dark = ui::palette (ThemeId::dark);
    const auto& light = ui::palette (ThemeId::light);
    struct Case { const char* name; StripShot shot; int width, height; const ui::Palette* palette; };
    const Case cases[] = {
        { "dark 100 %", { ThemeId::dark }, 1100, 680, &dark },
        { "light 100 %", { ThemeId::light }, 1100, 680, &light },
        { "dark 900 x 560", { ThemeId::dark, 900, 560 }, 900, 560, &dark },
        { "light 900 x 560", { ThemeId::light, 900, 560 }, 900, 560, &light },
    };
    for (const auto& c : cases)
    {
        const auto image = stripShot (c.shot);
        // The strip is the bottom of the window above the hint bar: its Region edges are orange, its wave is drawn.
        const auto stripRows = juce::Rectangle<int> (0, c.height - 30 - 8 - 104, c.width, 104);
        const int edges = countNear (image, stripRows, (*c.palette)[Tok::regionEdge], 8);
        const auto blended = (*c.palette)[Tok::stripBg].interpolatedWith ((*c.palette)[Tok::wave], ui::WaveStrip::waveAlpha);
        const int wave = countNear (image, stripRows, blended, 6);
        check (image.getWidth() == c.width && image.getHeight() == c.height && edges > 150 && wave > 1000,
               fmt ("%s: %d x %d, the Region's orange edges (%d pixels) and the wave (%d pixels) in the strip", c.name, image.getWidth(),
                    image.getHeight(), edges, wave));
    }

    // The menu open: its 9 lines (8 starters and the file) drawn over the roll above the box, and nothing else.
    juce::Rectangle<int> menu;
    const auto plain = stripShot ({ ThemeId::dark });
    const auto open = stripShot ({ ThemeId::dark, 0, 0, true }, &menu);
    int inside = 0, outside = 0;
    {
        const juce::Image::BitmapData a (plain, juce::Image::BitmapData::readOnly), b (open, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < plain.getHeight() && y < open.getHeight(); ++y)
            for (int x = 0; x < plain.getWidth() && x < open.getWidth(); ++x)
                if (a.getPixelColour (x, y) != b.getPixelColour (x, y))
                    (menu.contains (x, y) ? inside : outside) += 1;
    }
    const int menuPixels = countNear (open, menu, dark[Tok::menu], 2);
    check (menu.getHeight() > 9 * 18 && menu.getBottom() <= 548 && menu.getWidth() >= 150 && menuPixels > menu.getWidth() * menu.getHeight() / 2
               && inside > 10000 && outside < 50,
           fmt ("the open menu is a %d x %d box ending at the sound box (y %d), mostly the menu colour (%d pixels); it changed %d pixels inside it and %d outside",
                menu.getWidth(), menu.getHeight(), menu.getBottom(), menuPixels, inside, outside));
}

SL_SNAPSHOT (strip_chord_dark_100)        { return stripShot ({ ThemeId::dark }); }
SL_SNAPSHOT (strip_chord_light_100)       { return stripShot ({ ThemeId::light }); }
SL_SNAPSHOT (strip_chord_dark_900x560)    { return stripShot ({ ThemeId::dark, 900, 560 }); }
SL_SNAPSHOT (strip_chord_light_900x560)   { return stripShot ({ ThemeId::light, 900, 560 }); }
SL_SNAPSHOT (strip_menu_open_dark)        { return stripShot ({ ThemeId::dark, 0, 0, true }); }
SL_SNAPSHOT (strip_menu_open_light)       { return stripShot ({ ThemeId::light, 0, 0, true }); }
SL_SNAPSHOT (strip_zoomed_heads_dark)     { return stripShot ({ ThemeId::dark, 0, 0, false, true, true }); }
SL_SNAPSHOT (strip_zoomed_heads_light)    { return stripShot ({ ThemeId::light, 0, 0, false, true, true }); }
