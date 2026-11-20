// The Sample editor strip's live heads (PLUGIN-PLAN 4 M6, 7.8 T2; plan 1.5; sl-ui.js drawStrip, stripDown and stripUp):
//
// - display slot (the M6 table's row): 10,000 frames written through processBlock while the UI thread is paused, holding
//   the frame it was drawing: the audio thread never waits for it (all 10,000 blocks run while it is paused, with 0
//   allocations), the frame the UI holds is never written into, and the UI's next take returns the last frame written;
// - the heads overlay draws the heads of a fed DisplayFrame at the right x positions (the strip's own xOf), with their
//   numbers, ways and strengths, in a whole view and zoomed in, and the grains where they belong;
// - a short click on the strip with notes selected sets their Start (exactly the selected notes, the bytes of
//   model::setNoteSample, one undo step "Set start", the prototype's toast); a held press only listens (an audition
//   through the CommandQueue, from that spot); outside the Region it says notes start inside it;
// and pictures of the strip with heads, grains and Start markers.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "engine/DisplayFrame.h"
#include "model/ClipCodec.h"
#include "model/NoteSettings.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/Theme.h"
#include "ui/roll/RollGeometry.h"
#include "ui/strip/HeadsOverlay.h"
#include "ui/strip/WaveStrip.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

    /** A mouse event on c at a given time (milliseconds), as the strip reads it. */
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged, juce::int64 ms,
                              juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        const auto when = juce::Time (ms);
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &c, &c, when, down, when, 1, dragged);
    }

    /** A press at x held for heldMs, then let go where it was (no drag): a click, or holding to listen. */
    void pressStrip (ui::WaveStrip& s, float x, juce::int64 atMs, juce::int64 heldMs)
    {
        const juce::Point<float> p (x, (float) s.getWaveArea().getCentreY());
        s.mouseDown (mouseAt (s, p, p, false, atMs));
        s.mouseUp (mouseAt (s, p, p, false, atMs + heldMs, juce::ModifierKeys()));
    }

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

    /** A frame with three voices: one playing loud with 3 heads (forward, backward, still), one playing with 1 head, one
        let go with 1 head; and 5 grains of every age. */
    std::unique_ptr<engine::DisplayFrame> fedFrame()
    {
        auto f = std::make_unique<engine::DisplayFrame>();
        std::memset (f.get(), 0, sizeof (engine::DisplayFrame));
        f->playing = true;
        f->numVoices = 3;
        auto& v0 = f->voices[0];
        v0.p = 60;
        v0.env = 1.0f;
        v0.numHeads = 3;
        const float pos0[] = { 0.1f, 0.35f, 0.62f };
        const std::int8_t dirs[] = { 1, -1, 0 };
        for (int k = 0; k < 3; ++k)
        {
            v0.heads[k].index = (std::uint8_t) k;
            v0.heads[k].dir = dirs[k];
            v0.heads[k].pos = pos0[k];
            v0.heads[k].level = 0.7f;   // x 1.6 = full strength: drawn solid
        }
        auto& v1 = f->voices[1];
        v1.p = 64;
        v1.env = 1.0f;
        v1.numHeads = 1;
        v1.heads[0].index = 0;
        v1.heads[0].dir = 1;
        v1.heads[0].pos = 0.8f;
        v1.heads[0].level = 0.25f;   // 0.4 of full: alpha 0.35 + 0.65 x 0.4
        auto& v2 = f->voices[2];
        v2.p = 67;
        v2.env = 0.5f;
        v2.released = true;
        v2.numHeads = 1;
        v2.heads[0].index = 0;
        v2.heads[0].dir = -1;
        v2.heads[0].pos = 0.9f;
        v2.heads[0].level = 1.0f;
        f->numGrains = 5;
        for (int i = 0; i < 5; ++i)
        {
            f->grains[i].pos = 0.2f + 0.1f * (float) i;
            f->grains[i].age = 0.25f * (float) i;
        }
        return f;
    }

    /** sl-ui.js drawStrip's alpha for a head. */
    float headAlpha (const engine::DisplayVoice& v, const engine::DisplayHead& h)
    {
        const float lv = std::min (1.0f, std::max (0.0f, h.level * v.env * 1.6f));
        return (v.released ? 0.45f : 1.0f) * (0.35f + 0.65f * lv);
    }
}

//==================================================================================================
SL_TEST ("ui", displaySlotNeverWaits)
{
    section ("Display slot: 10,000 frames written while the UI thread is paused: the audio thread never waits, and the UI's next take "
             "returns the last frame written (M6)");

    StrandLockProcessor proc;
    proc.waitForSounds();
    constexpr double sr = 48000.0;
    const int block = engine::displayEvery (sr);   // 1600 samples: the processor writes one frame after every block
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    auto* slot = proc.getDisplaySlot();

    // A key held, so every frame holds a voice and its heads. One block, and the UI takes its frame and is drawing it
    // (it holds the slot's front buffer) when it pauses.
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    proc.processBlock (buffer, midi);
    midi.clear();
    const bool tookFirst = slot != nullptr && slot->takeLatest();
    check (tookFirst, "the first block wrote a frame and the UI took it");
    if (! tookFirst)
        return;
    const engine::DisplayFrame& held = slot->latest();
    auto heldCopy = std::make_unique<engine::DisplayFrame>();
    std::memcpy (heldCopy.get(), &held, sizeof (engine::DisplayFrame));
    const auto written0 = proc.getDisplayFramesWritten();

    // The audio thread: 10,000 blocks, one frame each, counting its allocations.
    constexpr int frames = 10000;
    std::atomic<bool> done { false };
    double longestMs = 0.0, totalMs = 0.0;
    alloc::Counts audioCounts;
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer none;
        alloc::reset();
        const auto start = std::chrono::steady_clock::now();
        {
            const alloc::Scope counting;
            for (int b = 0; b < frames; ++b)
            {
                const auto t0 = std::chrono::steady_clock::now();
                proc.processBlock (buf, none);
                longestMs = std::max (longestMs, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
            }
        }
        totalMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        audioCounts = alloc::counts();
        done.store (true, std::memory_order_release);
    });

    // The UI thread is paused: it takes nothing while the audio thread runs (at most 300 s, the blocks' own real time).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (300);
    while (! done.load (std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    const bool finishedWhilePaused = done.load (std::memory_order_acquire);
    const bool heldUntouched = std::memcmp (heldCopy.get(), &held, sizeof (engine::DisplayFrame)) == 0;
    audio.join();

    const auto written = proc.getDisplayFramesWritten() - written0;
    const double realTimeMs = 1000.0 * (double) frames * (double) block / sr;
    note (fmt ("%d blocks of %d samples (%.0f s of audio) in %.0f ms on the audio thread; the longest block %.2f ms (its real time %.1f ms); "
               "allocations counted: %s", frames, block, realTimeMs / 1000.0, totalMs, longestMs, 1000.0 * (double) block / sr, alloc::coverage()));
    check (finishedWhilePaused && written == (std::uint64_t) frames,
           fmt ("the audio thread ran all %d blocks and wrote %llu frames while the UI thread was paused: it never waited for it", frames,
                (unsigned long long) written));
    check (totalMs < realTimeMs, fmt ("faster than real time: %.0f ms for %.0f ms of audio", totalMs, realTimeMs));
    check (audioCounts.news == 0 && audioCounts.deletes == 0,
           fmt ("0 allocations and 0 frees on the audio thread (%lld, %lld)", audioCounts.news, audioCounts.deletes));
    check (heldUntouched, "the frame the paused UI was drawing was never written into (the writer has buffers of its own)");

    // The UI wakes up: its next take is the newest frame, the one the last block wrote.
    const bool tookLast = slot->takeLatest();
    const auto& latest = slot->latest();
    const auto engineAt = proc.getEngine().sampleTime();
    check (tookLast && latest.sampleTime == engineAt && latest.sampleTime == heldCopy->sampleTime + (std::int64_t) frames * block,
           fmt ("the UI's next take is the last frame written: sample %lld (the engine is at %lld; the frame it held was at %lld)",
                (long long) latest.sampleTime, (long long) engineAt, (long long) heldCopy->sampleTime));
    check (latest.numVoices == 1 && latest.voices[0].numHeads >= 1 && latest.voices[0].p == 60 && ! latest.voices[0].released,
           fmt ("it is whole: the held key's voice with its heads (%d voices, %d heads)", (int) latest.numVoices,
                latest.numVoices > 0 ? (int) latest.voices[0].numHeads : 0));
    check (! slot->takeLatest(), "a second take finds nothing newer");
    proc.releaseResources();
}

//==================================================================================================
SL_TEST ("ui", headsOverlayDrawsTheFrame)
{
    section ("Heads overlay: the heads of a fed display frame drawn where the strip shows their places, with their numbers, ways and "
             "strengths; the grains; zoomed in, only what is in view (M6)");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    StrandLockEditor editor (proc);
    editor.refreshNow();
    auto& strip = editor.getView().getStrip();
    const auto& palette = ui::palette (ThemeId::dark);
    const auto frame = fedFrame();
    strip.setDisplayFrame (frame.get());
    editor.refreshNow();   // no frame in the slot: the strip keeps the one it was given

    const auto checkView = [&] (const char* what)
    {
        const auto image = strip.createComponentSnapshot (strip.getLocalBounds(), true, 1.0f);
        const auto area = strip.getWaveArea().toFloat();
        const auto xOf = [&strip] (double p) { return strip.xOf (p); };
        const auto heads = strip.getOverlay().heads (area, xOf);

        // what the frame says should show: every head of every voice within 12 px of the wave
        int want = 0, right = 0, solidColumns = 0, solidHeads = 0;
        std::string firstBad;
        for (int v = 0; v < (int) frame->numVoices; ++v)
        {
            const auto& voice = frame->voices[v];
            for (int k = 0; k < (int) voice.numHeads; ++k)
            {
                const auto& h = voice.heads[k];
                const float x = strip.xOf ((double) h.pos);
                if (! (x > area.getX() - 12.0f && x < area.getRight() + 12.0f))
                    continue;
                ++want;
                bool found = false;
                for (const auto& m : heads)
                    if (m.voice == v && m.number == (int) h.index + 1)
                    {
                        found = std::abs (m.x - x) < 1.0e-3f && m.dir == (int) h.dir && std::abs (m.alpha - headAlpha (voice, h)) < 1.0e-6f;
                        if (! found && firstBad.empty())
                            firstBad = fmt ("voice %d head %d at x %.3f (want %.3f), way %d, alpha %.3f (want %.3f)", v, m.number, (double) m.x,
                                            (double) x, m.dir, (double) m.alpha, (double) headAlpha (voice, h));
                    }
                right += found ? 1 : 0;
                // a solid head draws its line down the wave in the head colour, at its x
                if (headAlpha (voice, h) > 0.999f && x >= area.getX() + 2.0f && x <= area.getRight() - 2.0f)
                {
                    ++solidHeads;
                    const int top = strip.getWaveArea().getY() + ui::HeadsOverlay::band + 18, bottom = strip.getWaveArea().getBottom() - 16;
                    const auto column = juce::Rectangle<int> ((int) std::floor (x) - 1, top, 2, bottom - top);
                    solidColumns += countNear (image, column, palette[Tok::head], 24) >= column.getHeight() ? 1 : 0;
                }
            }
        }
        check (right == want && (int) heads.size() == want && strip.headsDrawn() == want,
               fmt ("%s: %d of %d heads at the strip's x for their place, with their number, way and strength (%d listed, %d drawn)%s%s", what,
                    right, want, (int) heads.size(), strip.headsDrawn(), firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
        check (solidHeads > 0 && solidColumns == solidHeads,
               fmt ("%s: each solid head's line is drawn down the wave at its x (%d of %d)", what, solidColumns, solidHeads));
        return image;
    };

    const auto whole = checkView ("the whole sound");
    {
        // a column half way between two heads holds no head line (the check above can fail)
        const float x = 0.5f * (strip.xOf (0.35) + strip.xOf (0.62));
        const auto column = juce::Rectangle<int> ((int) x - 1, strip.getWaveArea().getY() + ui::HeadsOverlay::band + 18, 2, 30);
        const int between = countNear (whole, column, palette[Tok::head], 24);
        check (between < 10, fmt ("half way between the heads at 35 %% and 62 %%: %d head-coloured pixels", between));
    }
    {
        // the grains: inside the wave, each a dot that sinks with its age
        const auto area = strip.getWaveArea().toFloat();
        const auto grains = strip.getOverlay().grains (area, [&strip] (double p) { return strip.xOf (p); });
        bool placed = (int) grains.size() == 5;
        for (int i = 0; i < (int) grains.size() && placed; ++i)
        {
            const float age = frame->grains[i].age;
            const float y = area.getY() + (float) ui::HeadsOverlay::band + 20.0f + (area.getHeight() - (float) ui::HeadsOverlay::band - 34.0f) * age + 1.5f;
            placed = std::abs (grains[(std::size_t) i].x - strip.xOf ((double) frame->grains[i].pos)) < 1.0e-3f
                  && std::abs (grains[(std::size_t) i].y - y) < 1.0e-3f && std::abs (grains[(std::size_t) i].alpha - (0.9f - 0.6f * age)) < 1.0e-6f;
        }
        check (placed && strip.grainsDrawn() == 5, fmt ("the 5 grains at their places, sinking and fading with age (%d listed, %d drawn)",
                                                       (int) grains.size(), strip.grainsDrawn()));
    }

    strip.setView (0.3, 0.7);
    checkView ("zoomed to 30 .. 70 %");
    check (strip.headsDrawn() == 2, fmt ("zoomed in, only the 2 heads in view are drawn (%d)", strip.headsDrawn()));
    strip.setView (0.0, 1.0);
    strip.setDisplayFrame (nullptr);
    strip.createComponentSnapshot (strip.getLocalBounds(), true, 1.0f);
    check (strip.headsDrawn() == 0 && strip.grainsDrawn() == 0, "no frame: no heads and no grains");
    resetUserSettings (proc);
}

//==================================================================================================
SL_TEST ("ui", stripClickSetsStart)
{
    section ("Strip: a short click with notes selected sets their Start (exactly those notes, one undo step); a held press only listens; "
             "outside the Region it says notes start inside it (M6)");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    StrandLockEditor editor (proc);
    editor.refreshNow();
    auto& view = editor.getView();
    auto& strip = view.getStrip();
    drainCommands (proc);

    // three notes of the demo selected (the second in time order is the primary)
    const auto clip0 = proc.getDocument().current();
    std::vector<std::string> ids;
    for (const auto& n : model::timeOrder (clip0->notes))
        if (ids.size() < 3 && n->p >= 60.0)
            ids.push_back (n->id);
    proc.getDocument().setSelection (ids, ids[1]);
    view.getRoll().edited();
    editor.refreshNow();

    // a click (100 ms) at 40 % of the wave, the Region being the whole sound
    const auto before = proc.getDocument().current();
    const auto bytes0 = model::encodeClip (*before);
    const float x = strip.xOf (0.4);
    const double pos0 = std::min (1.0, std::max (0.0, strip.posOf (x)));
    pressStrip (strip, x, 10000, 100);
    const auto after = proc.getDocument().current();
    model::SamplePatch patch;
    model::StartPatch sp;
    sp.mode = model::StartMode::region;
    sp.pos = pos0;   // the Region is 0 .. 1: the spot in the Region is the spot in the sound
    patch.start = sp;
    const auto direct = model::setNoteSample (*before, ids, patch);
    int same = 0, changed = 0;
    for (std::size_t i = 0; i < after->notes.size(); ++i)
    {
        const bool selected = std::find (ids.begin(), ids.end(), after->notes[i]->id) != ids.end();
        if (! selected)
            same += after->notes[i] == before->notes[i] ? 1 : 0;
        else
            changed += after->notes[i]->sample.start.mode == model::StartMode::region && juce::exactlyEqual (after->notes[i]->sample.start.pos, pos0) ? 1 : 0;
    }
    check (model::encodeClip (*after) == model::encodeClip (direct) && changed == 3 && same == (int) after->notes.size() - 3,
           fmt ("the click set the Start of the 3 selected notes at %.4f (the bytes of model::setNoteSample) and left the other %d notes as "
                "they were", pos0, same));
    check (proc.undoLabel().value_or ("") == "Set start" && view.getToastText() == "Start set for 3 notes at 40% of the Region",
           fmt ("one undo step \"%s\", and the toast \"%s\"", proc.undoLabel().value_or ("").c_str(), view.getToastText().toRawUTF8()));
    const auto marks = strip.getOverlay().getStarts();
    check (marks.size() == 3 && std::all_of (marks.begin(), marks.end(), [pos0] (const auto& m) { return juce::exactlyEqual (m.pos, pos0) && ! m.random; }),
           fmt ("the strip marks the 3 selected notes' Start there (%d markers)", (int) marks.size()));
    {
        // the press itself listened from that spot, and the release stopped it
        const auto cmds = drainCommands (proc);
        const bool listened = cmds.size() == 2 && cmds[0].kind == doc::UiCommandKind::auditionOn && cmds[1].kind == doc::UiCommandKind::auditionOff
                           && std::abs (cmds[0].startPos - pos0) < 1.0e-12;
        check (listened, fmt ("the press played the spot through the command queue (%d commands)", (int) cmds.size()));
    }
    proc.undo();
    check (model::encodeClip (*proc.getDocument().current()) == bytes0, "one Undo brings every note's Start back");

    // holding the mouse (400 ms) only listens
    {
        const auto bytesBefore = model::encodeClip (*proc.getDocument().current());
        const auto labelBefore = proc.undoLabel();
        pressStrip (strip, strip.xOf (0.7), 20000, 400);
        const auto cmds = drainCommands (proc);
        check (model::encodeClip (*proc.getDocument().current()) == bytesBefore && proc.undoLabel() == labelBefore && cmds.size() == 2
                   && cmds[0].kind == doc::UiCommandKind::auditionOn && cmds[1].kind == doc::UiCommandKind::auditionOff,
               fmt ("a press held 400 ms listens (%d commands) and changes no Start, no undo step", (int) cmds.size()));
    }

    // a Region from 50 to 90 %: a click outside it says so and changes nothing
    {
        const auto wy = (float) strip.getWaveArea().getCentreY();
        const juce::Point<float> from (strip.xOf (0.5), wy), to (strip.xOf (0.9), wy);
        strip.mouseDown (mouseAt (strip, from, from, false, 30000));
        for (int i = 1; i <= 6; ++i)
            strip.mouseDrag (mouseAt (strip, from + (to - from) * ((float) i / 6.0f), from, true, 30000 + 20 * i));
        strip.mouseUp (mouseAt (strip, to, from, true, 30200, juce::ModifierKeys()));
        editor.refreshNow();
        drainCommands (proc);
        const auto bytesBefore = model::encodeClip (*proc.getDocument().current());
        const auto labelBefore = proc.undoLabel();
        pressStrip (strip, strip.xOf (0.2), 40000, 80);
        check (model::encodeClip (*proc.getDocument().current()) == bytesBefore && proc.undoLabel() == labelBefore
                   && view.getToastText()
                          == "That spot is outside the Region, and notes start inside it. Drag across the waveform to make a Region there.",
               fmt ("outside the Region (50 .. 90 %%): no change, and \"%s\"", view.getToastText().toRawUTF8()));

        // inside it, the Start is where the click is in the Region
        const auto before2 = proc.getDocument().current();
        const float x2 = strip.xOf (0.7);
        const double p2 = std::min (1.0, std::max (0.0, strip.posOf (x2)));
        const auto exact = proc.getInstState();
        const double rs = exact.regionStartExact, re = exact.regionEndExact;
        pressStrip (strip, x2, 50000, 50);
        model::SamplePatch patch2;
        model::StartPatch sp2;
        sp2.mode = model::StartMode::region;
        sp2.pos = std::min (1.0, std::max (0.0, (p2 - rs) / std::max (1.0e-9, re - rs)));
        patch2.start = sp2;
        check (model::encodeClip (*proc.getDocument().current()) == model::encodeClip (model::setNoteSample (*before2, ids, patch2))
                   && proc.undoLabel().value_or ("") == "Set start" && view.getToastText() == "Start set for 3 notes at 50% of the Region",
               fmt ("at 70 %% of the sound, inside the Region 50 .. 90 %%: Start %.4f of the Region, \"%s\"", *sp2.pos, view.getToastText().toRawUTF8()));
    }

    // nothing selected: a click changes nothing
    {
        proc.getDocument().setSelection ({}, std::string());
        view.getRoll().edited();
        editor.refreshNow();
        const auto bytesBefore = model::encodeClip (*proc.getDocument().current());
        const auto labelBefore = proc.undoLabel();
        pressStrip (strip, strip.xOf (0.6), 60000, 60);
        check (model::encodeClip (*proc.getDocument().current()) == bytesBefore && proc.undoLabel() == labelBefore
                   && strip.getOverlay().getStarts().empty(),
               "with nothing selected a click sets no Start and the strip shows no Start marker");
    }
    drainCommands (proc);
    resetUserSettings (proc);
}

//==================================================================================================
// Pictures for review: the strip with the live heads and grains of a fed frame, and two selected notes' Start markers
// (one at a spot, one random with its range).

namespace
{
    juce::Image shootHeads (ThemeId theme)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        juce::Image image;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (1100, 680);
            const auto clip = proc.getDocument().current();
            std::vector<std::string> ids;
            for (const auto& n : model::timeOrder (clip->notes))
                if (ids.size() < 2)
                    ids.push_back (n->id);
            model::SamplePatch a, b;
            model::StartPatch sa, sb;
            sa.mode = model::StartMode::region;
            sa.pos = 0.25;
            sb.mode = model::StartMode::random;
            sb.pos = 0.55;
            sb.range = 0.2;
            a.start = sa;
            b.start = sb;
            auto edited = model::setNoteSample (*clip, std::vector<std::string> { ids[0] }, a);
            edited = model::setNoteSample (edited, std::vector<std::string> { ids[1] }, b);
            proc.editClip ("Starts", edited);
            proc.getDocument().setSelection (ids, ids[0]);
            editor.getView().getRoll().edited();
            editor.refreshNow();
            const auto frame = fedFrame();
            editor.getView().getStrip().setDisplayFrame (frame.get());
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        resetUserSettings (proc);
        return image;
    }
}

SL_SNAPSHOT (m6_heads_overlay_dark)  { return shootHeads (ThemeId::dark); }
SL_SNAPSHOT (m6_heads_overlay_light) { return shootHeads (ThemeId::light); }
