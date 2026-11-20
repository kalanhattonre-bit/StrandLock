// Copy / Paste and Slides (PLUGIN-PLAN 4 M5 "copy/paste" and "snapshots", 7.7 S3 and decisions 4, 5 and 6).
//
// - Lanes: Copy takes the chosen lanes of the primary note, Paste puts them on every selected note with the same node
//   times (fractions of the note), so they stretch to each note's length; one undo step, the bytes of the direct model
//   call (model::copyLanes and pasteLanes); only the lanes the chips chose.
// - Notes: Duplicate and Paste notes keep the links inside the group and drop the ones out of it; copies that would
//   start after the clip's end are left out and the rest end at it (sl-ui.js duplicateSel); one undo step each; the right-
//   click menu's Paste notes here puts them on the grid line at the click.
// - Caps: a Duplicate or Paste notes past 4096 notes is refused with the cap's toast and changes nothing (0 bytes, 0
//   undo steps, 0 clip versions); one that reaches exactly 4096 is done (decision 5).
// - The Copy / Paste tab in the editor: its chips (kept in UIST), Copy, Paste, Copy notes, Paste notes and Duplicate.
// - The Slides tab: Auto slide and Slide time bound the M2 way (decision 4: a pick is one host gesture and one undo
//   step; the host's automation shows on the 30 Hz timer only); Selected notes, live while it moves and one undo step
//   when let go, Auto and Jump, no slide.
// - Decision 6: a new instance opens folded; an open tab shows whole rows above the roll's floor or the tabs fold; an
//   open drawer on a short window takes the inspector's line; the zoom buttons sit in the tab row, clear of the ruler.
// - The M5 table's pictures (dark and light at 100 %: the Shapes drawer, a hover ghost, a box select, and the two tabs),
//   checked for what each must show.

#include <juce_audio_processors/juce_audio_processors.h>

#include "ToolRig.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Shapes.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/Drawers.h"
#include "ui/roll/RollView.h"
#include "ui/roll/SelectionBar.h"
#include "ui/tabs/CopyPasteTab.h"
#include "ui/tabs/SlidesTab.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::ThemeId;
    using ui::Tok;
    using ui::roll::ToolSettings;
    using Bytes = std::vector<std::uint8_t>;
    using core::LaneId;

    model::NotePtr ptrOf (model::Note n)
    {
        return std::make_shared<const model::Note> (std::move (n));
    }

    const model::Note* noteIn (const model::Clip& c, const std::string& id)
    {
        for (const auto& n : c.notes)
            if (n->id == id)
                return n.get();
        return nullptr;
    }

    bool sameBits (double a, double b)
    {
        std::uint64_t x = 0, y = 0;
        std::memcpy (&x, &a, sizeof x);
        std::memcpy (&y, &b, sizeof y);
        return x == y;
    }

    /** The same nodes, bit for bit (t, v, shape, bend). */
    bool sameNodeBits (const model::NodeList& a, const model::NodeList& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (! sameBits (a[i].t, b[i].t) || ! sameBits (a[i].v, b[i].v) || a[i].shape != b[i].shape || ! sameBits (a[i].bend, b[i].bend))
                return false;
        return true;
    }

    bool sameGenOpt (const std::optional<model::Gen>& a, const std::optional<model::Gen>& b)
    {
        return a.has_value() == b.has_value() && (! a || model::sameGen (*a, *b));
    }

    /** One lane of two notes holds the same drawing and generator. */
    bool sameLane (const model::Note& a, const model::Note& b, LaneId lane)
    {
        return sameNodeBits (a.lane (lane), b.lane (lane)) && sameGenOpt (a.genOf (lane), b.genOf (lane));
    }

    /** The source of the lane copies: one beat, a drawn Pitch line (a bend in the middle), a Volume line and a pattern
        on Tone. */
    model::Note sourceNote()
    {
        auto n = model::mkNote (60.0, 0.0, 1.0, "src");
        n.lane (LaneId::pitch) = { model::mkNode (0.0, 0.5), model::mkNode (0.3, 0.7, model::NodeShape::bend, 0.4),
                                   model::mkNode (1.0, 0.45, model::NodeShape::linear) };
        n.lane (LaneId::amp) = { model::mkNode (0.0, 0.2, model::NodeShape::linear), model::mkNode (0.75, 0.9), model::mkNode (1.0, 0.6) };
        n.genOf (LaneId::tone) = model::makeGen (model::GenKind::pattern, model::GenPatch {});
        return n;
    }

    /** The source and three targets of 4, 0.5 and 2 beats with lanes of their own (a Formant line, a random on
        Vibrato), in 8 bars. */
    model::Clip lanesClip()
    {
        model::Clip c = model::newClip (8.0);
        c.notes.push_back (ptrOf (sourceNote()));
        const double lens[] = { 4.0, 0.5, 2.0 };
        for (int i = 0; i < 3; ++i)
        {
            auto n = model::mkNote (62.0 + i, 2.0 + 5.0 * i, lens[i], "t" + std::to_string (i));
            n.lane (LaneId::formant) = { model::mkNode (0.0, 0.1), model::mkNode (0.5, 0.8), model::mkNode (1.0, 0.3) };
            n.genOf (LaneId::vib) = model::makeGen (model::GenKind::random, model::GenPatch {});
            c.notes.push_back (ptrOf (std::move (n)));
        }
        return c;
    }

    /** A -> B and C -> D (links), E alone, in 8 bars. */
    model::Clip linksClip()
    {
        model::Clip c = model::newClip (8.0);
        const auto add = [&c] (const char* id, double p, double s, double l, const char* to)
        {
            auto n = model::mkNote (p, s, l, id);
            if (to != nullptr)
                n.glideTo = to;
            c.notes.push_back (ptrOf (std::move (n)));
        };
        add ("A", 60.0, 0.0, 1.0, "B");
        add ("B", 64.0, 1.0, 1.0, nullptr);
        add ("C", 67.0, 2.0, 1.0, "D");
        add ("D", 69.0, 3.0, 1.0, nullptr);
        add ("E", 72.0, 1.5, 0.5, nullptr);
        return c;
    }

    /** n notes c0 .. c(n-1), one beat each, in 64 bars (test_limits' clip). */
    model::Clip manyNotes (int n)
    {
        model::Clip c = model::newClip (64.0);
        c.notes.reserve ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            c.notes.push_back (ptrOf (model::mkNote (48.0 + (double) (i % 24), (double) (i % 200), 1.0, "c" + std::to_string (i))));
        return c;
    }

    std::vector<std::string> firstIds (int n)
    {
        std::vector<std::string> ids;
        for (int i = 0; i < n; ++i)
            ids.push_back ("c" + std::to_string (i));
        return ids;
    }

    /** The direct model call of a Duplicate or Paste notes (the copies, then sl-ui.js duplicateSel's rule: a copy that
        starts at or after the clip's end goes, the others end at it at the latest). */
    model::Clip placedCopies (const model::EditResult& r)
    {
        const double T = (double) r.clip.bars * (double) r.clip.beatsPerBar;
        std::vector<std::string> keep, gone;
        for (const auto& id : r.ids)
        {
            const auto* n = noteIn (r.clip, id);
            (n != nullptr && n->s < T ? keep : gone).push_back (id);
        }
        auto out = model::removeNotes (r.clip, gone);
        return model::editNotes (out, keep, [T] (model::Note& n, const model::Note&) { n.l = std::min (n.l, T - n.s); });
    }

    /** The copy of the note with pitch p among ids (each test note has its own pitch). */
    const model::Note* copyWithPitch (const model::Clip& c, const std::vector<std::string>& ids, double p)
    {
        for (const auto& id : ids)
            if (const auto* n = noteIn (c, id))
                if (std::abs (n->p - p) < 1e-9)
                    return n;
        return nullptr;
    }

    /** The last undo step's label, or "" when there is none. */
    std::string lastLabel (const RollRig& r)
    {
        return r.labels.empty() ? std::string() : r.labels.back();
    }

    std::array<bool, core::kNumLanes> allLanes()
    {
        std::array<bool, core::kNumLanes> m {};
        m.fill (true);
        return m;
    }

    //==============================================================================================
    // The editor.

    struct EditorRig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;

        explicit EditorRig (ThemeId theme = ThemeId::dark, int width = 1100, int height = 680)
        {
            proc.waitForSounds();
            auto& s = proc.getUserSettings();
            s.setTheme (theme);
            s.setUiScalePercent (100);
            s.setKeysDefault (false);
            editor = std::make_unique<StrandLockEditor> (proc);
            editor->setLogicalSize (width, height);
            view().getRoll().setMenusShown (false);
            editor->refreshNow();
        }

        ~EditorRig()
        {
            editor.reset();
            proc.getUserSettings().setTheme (state::Theme::dark);
        }

        EditorRig (const EditorRig&) = delete;
        EditorRig& operator= (const EditorRig&) = delete;

        MainView& view() { return editor->getView(); }
        ui::roll::RollView& roll() { return view().getRoll(); }
        ui::roll::SelectionBar& bar() { return view().getSelectionBar(); }

        void use (const model::Clip& clip)
        {
            proc.editClip ("Start", clip);
            proc.getDocument().setSelection ({}, std::string());
            roll().edited();
            editor->refreshNow();
        }

        void select (const std::vector<std::string>& ids, const std::string& primary)
        {
            proc.getDocument().setSelection (ids, primary);
            roll().edited();
        }

        juce::Image picture() { return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); }
        std::uint64_t submits() { return proc.getSnapshotPublisher().stats().submitted; }
        Bytes bytes() { return model::encodeClip (*proc.getDocument().current()); }
    };

    struct ParamWatch final : juce::AudioProcessorParameter::Listener
    {
        explicit ParamWatch (juce::AudioProcessorParameter& p) : param (p) { param.addListener (this); }
        ~ParamWatch() override { param.removeListener (this); }
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends) += 1; }

        juce::AudioProcessorParameter& param;
        int begins = 0, ends = 0, changes = 0;
    };

    void hostSets (juce::RangedAudioParameter& p, float plain)
    {
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
    }

    float plainOf (StrandLockProcessor& proc, params::P p)
    {
        return params::plainValueOf (proc.getParam (p));
    }

    int channelDistance (juce::Colour a, juce::Colour b)
    {
        return std::max ({ std::abs ((int) a.getRed() - (int) b.getRed()), std::abs ((int) a.getGreen() - (int) b.getGreen()),
                           std::abs ((int) a.getBlue() - (int) b.getBlue()) });
    }

    int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour, int tolerance)
    {
        int n = 0;
        area = area.getIntersection (image.getBounds());
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pixels.getPixelColour (x, y), colour) <= tolerance)
                    ++n;
        return n;
    }

    /** Pixels of area that differ between two pictures of the same size. */
    int countDifferent (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area)
    {
        if (a.getBounds() != b.getBounds())
            return -1;
        int n = 0;
        area = area.getIntersection (a.getBounds());
        const juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly), pb (b, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pa.getPixelColour (x, y), pb.getPixelColour (x, y)) > 8)
                    ++n;
        return n;
    }

    double meanLuminance (const juce::Image& image)
    {
        double sum = 0.0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                sum += (0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue()) / 255.0;
            }
        return sum / std::max (1.0, (double) image.getWidth() * (double) image.getHeight());
    }

    /** The x on a slider's track where it reads v (its steps), or -1. */
    float sliderX (ui::roll::ToolSlider& s, double v)
    {
        for (float x = 0.0f; x < (float) s.getWidth(); x += 0.25f)
            if (std::abs (s.valueAt (x) - v) < 1e-9)
                return x;
        return -1.0f;
    }
}

//==================================================================================================
SL_TEST ("ui", copyPasteLanesStretch)
{
    section ("Copy / Paste lanes: the copied lanes go onto every selected note with their node times as they were, so each lane "
             "stretches to its note's length; only the lanes chosen; one undo step, the bytes of the direct model call");

    const model::Clip clip = lanesClip();
    const model::Note& src = *clip.notes[0];
    const std::vector<std::string> targets { "t0", "t1", "t2" };

    // every lane (the default)
    {
        RollRig r (clip);
        auto& g = r.gestures();
        r.doc.setSelection ({ "src" }, std::string ("src"));
        g.copyLanes();
        const bool held = g.laneClipboard().has_value() && r.lastToast().value_or ("") == "Copied 10 lanes";
        r.doc.setSelection (targets, std::string ("t0"));
        g.pasteLanes();
        const auto after = r.doc.current();
        const auto direct = model::pasteLanes (clip, targets, model::copyLanes (src, allLanes()));
        const bool bytes = model::encodeClip (*after) == model::encodeClip (direct);

        int lanesSame = 0, notesKept = 0, timesSame = 0;
        juce::String stretch;
        for (const auto& id : targets)
        {
            const auto* n = noteIn (*after, id);
            const auto* was = noteIn (clip, id);
            if (n == nullptr || was == nullptr)
                continue;
            for (int i = 0; i < core::kNumLanes; ++i)
                lanesSame += sameLane (*n, src, static_cast<LaneId> (i)) ? 1 : 0;
            // the node times are the source's, bit for bit: the lanes are fractions of the note, so they stretch
            bool times = n->lane (LaneId::pitch).size() == src.lane (LaneId::pitch).size();
            for (std::size_t k = 0; times && k < src.lane (LaneId::pitch).size(); ++k)
                times = sameBits (n->lane (LaneId::pitch)[k].t, src.lane (LaneId::pitch)[k].t);
            timesSame += times ? 1 : 0;
            notesKept += sameBits (n->s, was->s) && sameBits (n->l, was->l) && sameBits (n->p, was->p) && sameBits (n->vel, was->vel) ? 1 : 0;
            stretch << juce::String (n->l, 1) << " beats: the bend node at beat " << juce::String (n->s + n->lane (LaneId::pitch)[1].t * n->l, 2)
                    << " of the note's " << juce::String (n->s, 1) << " to " << juce::String (n->s + n->l, 2) << "; ";
        }
        note (stretch.toStdString());
        check (held && bytes && r.steps == 1 && r.lastToast().value_or ("") == "Pasted onto 3 notes" && lastLabel (r) == "Paste lanes",
               fmt ("Copy lanes from the 1-beat source, Paste onto 3 notes: one undo step \"%s\", the bytes of model::pasteLanes (%s)",
                    lastLabel (r).c_str(), bytes ? "same" : "DIFFERENT"));
        check (lanesSame == 30 && timesSame == 3 && notesKept == 3,
               fmt ("on the 4, 0.5 and 2 beat notes all 10 lanes are the source's (%d of 30), node times bit for bit (%d of 3), and each note keeps "
                    "its start, length, pitch and velocity (%d of 3)", lanesSame, timesSame, notesKept));
        r.undo();
        check (model::encodeClip (*r.doc.current()) == model::encodeClip (clip), "Undo puts the targets' own lanes back");
    }

    // only Pitch and Tone (the chips)
    {
        RollRig r (clip);
        auto& g = r.gestures();
        g.copyMask.fill (false);
        g.copyMask[(std::size_t) core::laneIndex (LaneId::pitch)] = true;
        g.copyMask[(std::size_t) core::laneIndex (LaneId::tone)] = true;
        r.doc.setSelection ({ "src" }, std::string ("src"));
        g.copyLanes();
        const auto heldText = ui::CopyPasteTab::lanesHeld (g.laneClipboard() ? &g.laneClipboard()->has : nullptr);
        r.doc.setSelection ({ "t0" }, std::string ("t0"));
        g.pasteLanes();
        const auto* n = noteIn (*r.doc.current(), "t0");
        const auto* was = noteIn (clip, "t0");
        int chosen = 0, kept = 0;
        for (int i = 0; n != nullptr && was != nullptr && i < core::kNumLanes; ++i)
        {
            const auto lane = static_cast<LaneId> (i);
            if (lane == LaneId::pitch || lane == LaneId::tone)
                chosen += sameLane (*n, src, lane) ? 1 : 0;
            else
                kept += sameLane (*n, *was, lane) ? 1 : 0;
        }
        const auto direct = model::pasteLanes (clip, { "t0" }, model::copyLanes (src, g.copyMask));
        check (r.lastToast().value_or ("") == "Pasted onto 1 note" && chosen == 2 && kept == 8 && heldText == "holding: Pitch, Tone"
                   && model::encodeClip (*r.doc.current()) == model::encodeClip (direct),
               fmt ("with only Pitch and Tone chosen (\"%s\"): those 2 lanes are the source's, the other 8 the note's own (%d), the bytes of the "
                    "direct call", heldText.toRawUTF8(), kept));
    }

    // what the tab says it holds (sl-ui.js clipInfo)
    {
        auto five = allLanes();
        for (int i = 5; i < core::kNumLanes; ++i)
            five[(std::size_t) i] = false;
        const auto all = allLanes();
        check (ui::CopyPasteTab::lanesHeld (nullptr) == "nothing copied" && ui::CopyPasteTab::lanesHeld (&all) == "holding: 10 lanes"
                   && ui::CopyPasteTab::lanesHeld (&five) == "holding: 5 lanes",
               "the tab's text: \"nothing copied\", \"holding: Pitch, Tone\", and past four lanes \"holding: 10 lanes\"");
    }
}

SL_TEST ("ui", copyPasteNotesKeepLinks)
{
    section ("Duplicate and Paste notes: the links inside the group are kept, those out of it dropped; copies past the clip's end are left "
             "out; one undo step each, the bytes of the direct model call; the right-click menu's Paste notes here");

    const model::Clip clip = linksClip();

    // Duplicate A, B, C and E: A -> B is inside the group, C -> D goes out of it
    {
        RollRig r (clip);
        auto& g = r.gestures();
        const std::vector<std::string> sel { "A", "B", "C", "E" };
        r.doc.setSelection (sel, std::string ("A"));
        g.duplicateSelection();
        const auto c = r.doc.current();
        const auto copies = r.doc.rollState().selected;
        const auto* a = copyWithPitch (*c, copies, 60.0);
        const auto* b = copyWithPitch (*c, copies, 64.0);
        const auto* cc = copyWithPitch (*c, copies, 67.0);
        const bool linked = a != nullptr && b != nullptr && a->glideTo == b->id;
        const bool dropped = cc != nullptr && cc->glideTo.empty();
        const auto* origA = noteIn (*c, "A");
        const auto* origC = noteIn (*c, "C");
        const bool originals = origA != nullptr && origC != nullptr && origA->glideTo == "B" && origC->glideTo == "D";
        const auto direct = placedCopies (model::duplicateNotes (clip, inTimeOrder (clip, sel)));
        const bool bytes = model::encodeClip (*c) == model::encodeClip (direct);
        check (linked && dropped && originals && copies.size() == 4 && a != nullptr && sameBits (a->s, 3.0),
               fmt ("Duplicate: the copy of A slides into the copy of B (%s), the copy of C slides nowhere (D was not copied: %s), the "
                    "originals keep theirs (%s); the 4 copies start where the group ends (beat %s) and are selected",
                    linked ? "yes" : "NO", dropped ? "yes" : "NO", originals ? "yes" : "NO", a != nullptr ? juce::String (a->s).toRawUTF8() : "-"));
        check (bytes && r.steps == 1 && lastLabel (r) == "Duplicate", fmt ("one undo step \"%s\", the bytes of model::duplicateNotes (%s)",
                                                                           lastLabel (r).c_str(), bytes ? "same" : "DIFFERENT"));
    }

    // Copy notes A, B, C; Paste notes after E (it ends at beat 2); then near the clip's end
    {
        RollRig r (clip);
        auto& g = r.gestures();
        g.pasteNotes();
        const bool nothing = r.lastToast().value_or ("") == "Copy some notes first" && r.steps == 0;
        r.doc.setSelection ({ "A", "B", "C" }, std::string ("A"));
        g.copyNotes();
        const bool copied = g.noteClipboard().size() == 3 && r.lastToast().value_or ("") == "Copied 3 notes";
        r.doc.setSelection ({ "E" }, std::string ("E"));
        const auto before = *r.doc.current();
        g.pasteNotes();
        const auto c = r.doc.current();
        const auto pasted = r.doc.rollState().selected;
        const auto* a = copyWithPitch (*c, pasted, 60.0);
        const auto* b = copyWithPitch (*c, pasted, 64.0);
        const auto* cc = copyWithPitch (*c, pasted, 67.0);
        const auto direct = placedCopies (model::pasteNotes (before, g.noteClipboard(), 2.0));
        const bool bytes = model::encodeClip (*c) == model::encodeClip (direct);
        check (nothing && copied && pasted.size() == 3 && a != nullptr && b != nullptr && cc != nullptr && a->glideTo == b->id && cc->glideTo.empty()
                   && sameBits (a->s, 2.0) && bytes && r.steps == 1 && lastLabel (r) == "Paste notes" && r.lastToast().value_or ("") == "Pasted 3 notes",
               fmt ("Paste notes with E selected: the 3 copies start where E ends (beat %s), A's copy slides into B's, C's into none; one undo step "
                    "\"%s\", the bytes of model::pasteNotes (%s); \"%s\"", a != nullptr ? juce::String (a->s).toRawUTF8() : "-",
                    lastLabel (r).c_str(), bytes ? "same" : "DIFFERENT", r.lastToast().value_or ("").c_str()));

        // at beat 30.5 of 32: A's copy fits, B's is cut to end at 32, C's would start at 32.5 and is left out
        const auto before2 = *r.doc.current();
        g.pasteNotesAt (30.5);
        const auto c2 = r.doc.current();
        const auto pasted2 = r.doc.rollState().selected;
        const auto* a2 = copyWithPitch (*c2, pasted2, 60.0);
        const auto* b2 = copyWithPitch (*c2, pasted2, 64.0);
        const auto direct2 = placedCopies (model::pasteNotes (before2, g.noteClipboard(), 30.5));
        check (pasted2.size() == 2 && a2 != nullptr && b2 != nullptr && a2->glideTo == b2->id && sameBits (b2->l, 0.5)
                   && model::encodeClip (*c2) == model::encodeClip (direct2) && r.steps == 2
                   && r.lastToast().value_or ("") == "Pasted 2 of 3 notes: the rest would start after the clip's end",
               fmt ("Paste notes at beat 30.5 of 32: 2 of 3 copies, B's cut to %s beat, the link kept; \"%s\"", b2 != nullptr ? juce::String (b2->l).toRawUTF8() : "-",
                    r.lastToast().value_or ("").c_str()));
        g.pasteNotesAt (40.0);
        check (r.steps == 2 && r.lastToast().value_or ("") == "No room there: add bars first", "past the clip's end nothing is pasted, with a toast");
    }

    // the right-click menu: Paste notes here (Build, empty space) only once notes are copied; on the grid line at the click
    {
        RollRig r (clip);
        auto& g = r.gestures();
        g.setMode (1);
        g.sync();
        const auto& geo = g.geometry();
        const double x = 20.6 * geo.beatWidth(), y = geo.cy (100.0);   // beat 20.6, far above the notes
        const auto findItem = [] (const std::vector<ui::roll::MenuItem>& items) -> const ui::roll::MenuItem*
        {
            for (const auto& m : items)
                if (m.label == "Paste notes here")
                    return &m;
            return nullptr;
        };
        const auto first = g.contextMenu (x, y, 1000.0);
        const bool absent = findItem (first) == nullptr;
        r.doc.setSelection ({ "A", "B" }, std::string ("A"));
        g.copyNotes();
        r.doc.setSelection ({}, std::string());
        const auto menu = g.contextMenu (x, y, 2000.0);
        const auto* item = findItem (menu);
        if (item != nullptr)
            g.runMenuItem (*item);
        const auto c = r.doc.current();
        const auto pasted = r.doc.rollState().selected;
        const auto* a = copyWithPitch (*c, pasted, 60.0);
        check (absent && item != nullptr && pasted.size() == 2 && a != nullptr && sameBits (a->s, 20.5) && r.steps == 1,
               fmt ("the empty-space menu has no Paste notes here until notes are copied (%s); then its copies start on the 1/16 line at the "
                    "click, beat %s; one undo step", absent ? "yes" : "NO", a != nullptr ? juce::String (a->s).toRawUTF8() : "-"));
    }
}

SL_TEST ("ui", copyPasteRefusedPast4096)
{
    section ("Caps (decision 5): a Duplicate or Paste notes past 4096 notes is refused with a toast and changes nothing (0 bytes, 0 undo "
             "steps, 0 clip versions); one that makes exactly 4096 is done");

    const model::Clip big = manyNotes (4000);

    // Duplicate
    {
        ToolRig t (big);
        auto& r = t.rig;
        auto& g = r.gestures();
        const auto bytes0 = t.bytes();
        const auto version0 = r.doc.version();
        r.doc.setSelection (firstIds (97), std::string ("c0"));
        const int versions0 = t.versions;
        g.duplicateSelection();
        const bool refused = t.bytes() == bytes0 && r.doc.version() == version0 && t.versions == versions0 && r.steps == 0
                          && r.doc.rollState().selected.size() == 97;
        check (refused && r.lastToast().value_or ("") == "That would make 4097 notes; the limit is 4096.",
               fmt ("Duplicate of 97 notes on 4000: refused, \"%s\"; the bytes, the version and the selection unchanged, %d undo steps",
                    r.lastToast().value_or ("").c_str(), r.steps));
        r.doc.setSelection (firstIds (96), std::string ("c0"));
        g.duplicateSelection();
        check (r.doc.current()->notes.size() == 4096 && r.steps == 1 && t.versions == versions0 + 1,
               fmt ("Duplicate of 96: done, exactly %d notes, one undo step", (int) r.doc.current()->notes.size()));
    }

    // Paste notes
    {
        ToolRig t (big);
        auto& r = t.rig;
        auto& g = r.gestures();
        r.doc.setSelection (firstIds (97), std::string ("c0"));
        g.copyNotes();
        const auto bytes0 = t.bytes();
        const auto version0 = r.doc.version();
        const int versions0 = t.versions;
        g.pasteNotes();
        check (t.bytes() == bytes0 && r.doc.version() == version0 && t.versions == versions0 && r.steps == 0
                   && r.lastToast().value_or ("") == "That would make 4097 notes; the limit is 4096.",
               fmt ("Paste notes of 97 on 4000: refused, \"%s\", nothing changed", r.lastToast().value_or ("").c_str()));
        r.doc.setSelection (firstIds (96), std::string ("c0"));
        g.copyNotes();
        g.pasteNotes();
        check (r.doc.current()->notes.size() == 4096 && r.steps == 1, fmt ("Paste notes of 96: done, exactly %d notes", (int) r.doc.current()->notes.size()));
    }

    // through the editor: the tab's Duplicate, the toast in the window, nothing handed to the SnapshotPublisher
    {
        EditorRig e;
        e.use (big);
        e.view().openTab (MainView::copyTab);
        e.select (firstIds (97), "c0");
        const auto bytes0 = e.bytes();
        const auto submits0 = e.submits();
        const auto undo0 = e.proc.undoLabel();
        e.view().getCopyPasteTab().getDuplicateButton().onClick();
        e.view().getCopyPasteTab().getPasteNotesButton().onClick();   // nothing copied yet
        const auto toastAfterPaste = e.view().getToastText();
        e.view().getCopyPasteTab().getCopyNotesButton().onClick();
        e.view().getCopyPasteTab().getPasteNotesButton().onClick();
        check (e.bytes() == bytes0 && e.submits() == submits0 && e.proc.undoLabel() == undo0 && toastAfterPaste == "Copy some notes first"
                   && e.view().getToastText() == "That would make 4097 notes; the limit is 4096." && e.view().isToastShown(),
               fmt ("in the editor, the tab's Duplicate and Paste notes past 4096: \"%s\" in the window, the clip unchanged, nothing submitted",
                    e.view().getToastText().toRawUTF8()));
    }
}

SL_TEST ("ui", copyPasteTabInTheEditor)
{
    section ("The Copy / Paste tab: the chips choose the lanes (the project keeps them), Copy and Paste, what is held, Copy notes, Paste "
             "notes and Duplicate, each one undo step");

    const model::Clip clip = lanesClip();
    EditorRig e;
    e.use (clip);
    auto& view = e.view();
    view.openTab (MainView::copyTab);
    auto& tab = view.getCopyPasteTab();
    e.editor->refreshNow();

    int lit = 0;
    for (int i = 0; i < core::kNumLanes; ++i)
        lit += tab.getChip (static_cast<LaneId> (i)).getToggleState() ? 1 : 0;
    check (lit == 10 && tab.getLanesText() == "nothing copied" && tab.getNotesText() == "no notes copied" && view.getPaneArea().getHeight() > 0,
           fmt ("the tab opens with all 10 chips lit (%d), \"%s\", \"%s\"", lit, tab.getLanesText().toRawUTF8(), tab.getNotesText().toRawUTF8()));

    // the Pitch chip off: Copy takes the other 9; the project keeps the choice for the next window
    tab.getChip (LaneId::pitch).onClick();
    const bool pitchOff = ! tab.getChip (LaneId::pitch).getToggleState() && e.proc.getUiState().copyMask == 0x3fe
                       && ! view.getRoll().getGestures().copyMask[(std::size_t) core::laneIndex (LaneId::pitch)];
    {
        StrandLockEditor again (e.proc);
        check (pitchOff && ! again.getView().getCopyPasteTab().getChip (LaneId::pitch).getToggleState()
                   && again.getView().getCopyPasteTab().getChip (LaneId::amp).getToggleState(),
               fmt ("Pitch off: UIST copyMask 0x%x, and a new window shows Pitch off, Volume on", (unsigned) e.proc.getUiState().copyMask));
    }

    e.select ({ "src" }, "src");
    tab.getCopyButton().onClick();
    e.editor->refreshNow();
    const auto heldText = tab.getLanesText();
    e.select ({ "t0", "t2" }, "t0");
    tab.getPasteButton().onClick();
    auto mask = allLanes();
    mask[(std::size_t) core::laneIndex (LaneId::pitch)] = false;
    const auto direct = model::pasteLanes (clip, { "t0", "t2" }, model::copyLanes (*clip.notes[0], mask));
    check (heldText == "holding: 9 lanes" && e.bytes() == model::encodeClip (direct) && e.proc.undoLabel().value_or ("") == "Paste lanes",
           fmt ("Copy (\"%s\") and Paste onto 2 notes: the bytes of the direct call with Pitch left out, one undo step \"%s\"",
                heldText.toRawUTF8(), e.proc.undoLabel().value_or ("").c_str()));

    // notes
    e.select ({ "t0", "t1" }, "t0");
    tab.getCopyNotesButton().onClick();
    e.editor->refreshNow();
    const auto notesText = tab.getNotesText();
    const auto before = *e.proc.getDocument().current();
    tab.getPasteNotesButton().onClick();
    const auto pasted = e.proc.getDocument().rollState().selected;
    const auto label1 = e.proc.undoLabel().value_or ("");
    tab.getDuplicateButton().onClick();
    const auto label2 = e.proc.undoLabel().value_or ("");
    const auto duplicated = e.proc.getDocument().rollState().selected;
    const auto directPaste = placedCopies (model::pasteNotes (before, model::selected (before, { "t0", "t1" }), 7.5));
    check (notesText == "holding: 2 notes" && pasted.size() == 2 && label1 == "Paste notes" && label2 == "Duplicate" && duplicated.size() == 2,
           fmt ("Copy notes (\"%s\"), Paste notes (%d pasted, \"%s\"), Duplicate (%d, \"%s\")", notesText.toRawUTF8(), (int) pasted.size(),
                label1.c_str(), (int) duplicated.size(), label2.c_str()));
    e.proc.undo();
    check (e.bytes() == model::encodeClip (directPaste), "the paste went after the selected notes (t1 ends at beat 7.5): the bytes of the direct call");
}

SL_TEST ("ui", slidesTabAutoSlideAndTime)
{
    section ("The Slides tab (decision 4): Auto slide and Slide time bound the M2 way: a pick is one host gesture and one undo step; the "
             "host's automation shows on the 30 Hz timer, not at once; a time the list does not hold shows as its text");

    EditorRig e;
    auto& view = e.view();
    view.openTab (MainView::slidesTab);
    auto& tab = view.getSlidesTab();
    auto& mode = tab.getAutoSlide().getBox();
    auto& time = tab.getSlideTime().getBox();
    auto& modeParam = e.proc.getParam (params::P::glideMode);
    auto& timeParam = e.proc.getParam (params::P::glideTime);

    check (mode.getNumItems() == 3 && mode.getItemText (0) == "Off" && mode.getItemText (1) == "Legato (touching notes)"
               && mode.getItemText (2) == "Always (next note)" && mode.getSelectedItemIndex() == 1 && time.getNumItems() == 5
               && time.getItemText (0) == "1/32" && time.getItemText (4) == "1/2" && time.getText() == "1/8",
           fmt ("Auto slide offers Off, Legato (touching notes), Always (next note) and shows \"%s\"; Slide time 1/32 to 1/2 shows \"%s\"",
                mode.getText().toRawUTF8(), time.getText().toRawUTF8()));

    // a pick: one gesture, the parameter, one undo step; Undo puts it back
    {
        ParamWatch w (modeParam);
        mode.setSelectedItemIndex (2, juce::sendNotificationSync);
        const int begins = w.begins, ends = w.ends;   // the pick alone (Undo writes the parameter in a gesture too)
        const bool picked = juce::exactlyEqual (plainOf (e.proc, params::P::glideMode), 2.0f) && begins == 1 && ends == 1
                         && e.proc.undoLabel().value_or ("") == "Auto slide";
        e.view().getRoll().undo();
        check (picked && juce::exactlyEqual (plainOf (e.proc, params::P::glideMode), 1.0f),
               fmt ("Always picked: one gesture (%d / %d), the parameter 2, one undo step; Undo: Legato again (%g)", begins, ends,
                    (double) plainOf (e.proc, params::P::glideMode)));
    }
    {
        ParamWatch w (timeParam);
        time.setSelectedItemIndex (3, juce::sendNotificationSync);
        check (juce::exactlyEqual (plainOf (e.proc, params::P::glideTime), 1.0f) && w.begins == 1 && w.ends == 1
                   && e.proc.undoLabel().value_or ("") == "Slide time",
               fmt ("Slide time 1/4: 1 beat in one gesture (%d / %d), one undo step \"%s\"", w.begins, w.ends, e.proc.undoLabel().value_or ("").c_str()));
    }

    // the host: nothing moves until the editor's timer; never an attachment
    view.syncControls();
    hostSets (modeParam, 0.0f);
    hostSets (timeParam, 0.7f);
    const bool waited = mode.getSelectedItemIndex() == 1 && time.getText() == "1/4";
    const int moved = view.syncControls();
    check (waited && moved == 2 && mode.getSelectedItemIndex() == 0 && time.getText() == "0.700 beats" && time.getSelectedId() == 0,
           fmt ("host Off and 0.7 beats wait for the timer, then %d move: \"%s\", \"%s\"", moved, mode.getText().toRawUTF8(), time.getText().toRawUTF8()));
    check (view.syncControls() == 0, "and the next tick moves nothing");
}

SL_TEST ("ui", slidesTabSelectedNotes)
{
    section ("The Slides tab's Selected notes: the primary note's own slide time; a drag changes the selected notes live (shown and played, "
             "no undo step) and is one undo step when let go; Auto and Jump, no slide; greyed with nothing selected");

    model::Clip clip = model::newClip (4.0);
    clip.notes.push_back (ptrOf (model::mkNote (60.0, 0.0, 1.0, "n1")));
    clip.notes.push_back (ptrOf (model::mkNote (62.0, 1.0, 1.0, "n2")));
    clip.notes.push_back (ptrOf (model::mkNote (64.0, 2.0, 1.0, "n3")));
    EditorRig e;
    e.use (clip);
    auto& view = e.view();
    view.openTab (MainView::slidesTab);
    e.editor->refreshNow();
    auto& tab = view.getSlidesTab();
    auto& slider = tab.getNotesSlider();

    const bool greyed = ! slider.isEnabled() && tab.notesText() == juce::String::fromUTF8 ("\xe2\x80\x94");
    slider.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 60.0f, 10.0f }, {}, juce::MouseInputSource::defaultPressure,
                                        juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                        juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &slider, &slider,
                                        juce::Time (1000), { 60.0f, 10.0f }, juce::Time (1000), 1, false));
    check (greyed && e.bytes() == model::encodeClip (clip), fmt ("with nothing selected the slider is greyed and reads \"%s\"; a press on it changes nothing",
                                                                 tab.notesText().toRawUTF8()));

    e.select ({ "n1", "n2" }, "n1");
    e.editor->refreshNow();
    const bool auto0 = slider.isEnabled() && tab.notesText() == "auto" && std::abs (slider.getValue() - 0.5) < 1e-12;

    // a drag: three moves live, then the release
    const auto base = *e.proc.getDocument().current();
    const auto submits0 = e.submits();
    const auto undo0 = e.proc.undoLabel();
    int live = 0, liveBytes = 0;
    for (const double v : { 0.25, 1.25, 0.75 })
    {
        const float x = sliderX (slider, v);
        if (x < 0.0f)
            continue;
        slider.dragTo (x);
        ++live;
        model::NotePatch patch;
        patch.glide = std::optional<double> (v);
        liveBytes += e.bytes() == model::encodeClip (model::updateNotes (base, std::vector<std::string> { "n1", "n2" }, patch)) ? 1 : 0;
    }
    const bool noStep = e.proc.undoLabel() == undo0;
    const auto liveSubmits = e.submits() - submits0;
    slider.release();
    e.editor->refreshNow();
    check (auto0 && live == 3 && liveBytes == 3 && noStep && liveSubmits >= 3 && e.proc.undoLabel().value_or ("") == "Slide time (notes)"
               && tab.notesText() == "0.75 beats",
           fmt ("n1 follows Slide time (\"auto\", at 0.5); 3 moves are %d of 3 the bytes of model::updateNotes on the clip the drag started from, "
                "%d clips handed to the SnapshotPublisher, no undo step; the release is one step \"%s\" and the slider reads \"%s\"",
                liveBytes, (int) liveSubmits, e.proc.undoLabel().value_or ("").c_str(), tab.notesText().toRawUTF8()));
    const auto* n3 = noteIn (*e.proc.getDocument().current(), "n3");
    check (n3 != nullptr && ! n3->glide.has_value(), "n3, not selected, keeps following Slide time");

    // Jump, no slide and Auto
    tab.getJumpButton().onClick();
    e.editor->refreshNow();
    const auto* j = noteIn (*e.proc.getDocument().current(), "n2");
    const bool jumped = j != nullptr && j->glide.has_value() && juce::exactlyEqual (*j->glide, 0.0) && tab.notesText() == "jump"
                     && e.proc.undoLabel().value_or ("") == "Slide: jump";
    tab.getAutoButton().onClick();
    e.editor->refreshNow();
    const auto* a = noteIn (*e.proc.getDocument().current(), "n2");
    check (jumped && a != nullptr && ! a->glide.has_value() && juce::exactlyEqual (a->gc, 0.0) && tab.notesText() == "auto"
               && e.proc.undoLabel().value_or ("") == "Slide: auto",
           fmt ("Jump, no slide: glide 0 (\"jump\"), one step; Auto: back to Slide time (\"%s\"), one step \"%s\"", tab.notesText().toRawUTF8(),
                e.proc.undoLabel().value_or ("").c_str()));

    // Undo twice: the slide time the drag left; once more: the clip before the drag
    view.getRoll().undo();
    view.getRoll().undo();
    const auto* u = noteIn (*e.proc.getDocument().current(), "n1");
    const bool backToDrag = u != nullptr && u->glide.has_value() && std::abs (*u->glide - 0.75) < 1e-12;
    view.getRoll().undo();
    check (backToDrag && e.bytes() == model::encodeClip (base), "Undo walks back through Auto, Jump and the drag to the clip before it");
}

SL_TEST ("ui", decisionSixLayout)
{
    section ("Decision 6: a new instance opens with the tabs folded (a project keeps its own fold); an open tab shows whole rows above the "
             "roll's floor or the tabs fold; an open drawer on a short window takes the inspector's line; the zoom buttons sit in the tab row");

    // a new instance, and a project saved with the tab open
    {
        EditorRig e;
        auto& view = e.view();
        const bool fresh = view.isFolded() && e.proc.getUiState().folded == 1 && view.getPaneArea().getHeight() == 0;
        const int foldedRoll = view.getRollArea().getHeight();
        view.openTab (MainView::macrosTab);
        juce::MemoryBlock saved;
        e.proc.getStateInformation (saved);
        StrandLockProcessor other;
        other.setStateInformation (saved.getData(), (int) saved.getSize());
        StrandLockEditor reopened (other);
        check (fresh && foldedRoll >= 300 && ! reopened.getView().isFolded() && reopened.getView().getOpenTab() == MainView::macrosTab,
               fmt ("a new instance: folded, the roll %d px; a project saved with Macros open opens with Macros open", foldedRoll));
    }

    // every tab at every height from 560 to 900 (and two widths): whole rows or folded, never a row cut, and never a fold
    // when a whole row would have fitted (the fonts differ between platforms, so which tabs fold where is worked out
    // from each panel's own rows, not written down)
    {
        EditorRig e;
        auto& view = e.view();
        int shown = 0, foldedCount = 0, cut = 0, squeezed = 0, needless = 0, cases = 0;
        juce::String firstCut, firstNeedless, foldList;
        bool sampleFoldsSmall = false, sampleShowsDefault = false;
        for (const int w : { 900, 1100 })
            for (int h = 560; h <= 900; h += 20)
            {
                e.editor->setLogicalSize (w, h);
                for (const int tab : { MainView::sampleTab, MainView::macrosTab, MainView::slidesTab, MainView::copyTab, MainView::soundTab })
                {
                    view.openTab (tab);
                    e.editor->refreshNow();
                    ++cases;
                    const int pane = view.getPaneArea().getHeight();
                    const int roll = view.getRollArea().getHeight();
                    const int width = view.getPaneArea().getWidth();
                    const int bar = view.getPaneViewport().getScrollBarThickness();
                    auto* panel = view.getTabPanel (tab);
                    if (pane == 0)
                    {
                        foldedCount += view.isAutoFolded() ? 1 : 0;
                        foldList << (foldList.isEmpty() ? "" : ", ") << w << " x " << h << " " << view.getTabStrip().getSelected();
                        if (tab == MainView::sampleTab && w == 900 && h == 560)
                            sampleFoldsSmall = true;
                        // the fold was needed: with the tabs folded the roll has all the room, and no whole panel within
                        // 2/5 of it, nor a scrolling pane of whole rows (at least 64 px), leaves the roll its floor
                        bool fits = false;
                        if (panel != nullptr)
                        {
                            const auto whole = panel->rowEnds (width);
                            fits = ! whole.empty() && whole.back() <= std::max (80, roll * 2 / 5) && roll - whole.back() >= MainView::rollFloor;
                            for (const int end : panel->rowEnds (width - bar))
                                fits = fits || (end >= 64 && roll - end >= MainView::rollFloor);
                        }
                        if (fits)
                        {
                            ++needless;
                            if (firstNeedless.isEmpty())
                                firstNeedless << w << " x " << h << " tab " << tab << " (roll " << roll << " px)";
                        }
                        continue;
                    }
                    ++shown;
                    if (tab == MainView::sampleTab && w == 1100 && h == 680)
                        sampleShowsDefault = true;
                    bool whole = false;
                    for (const int b : { 0, bar })
                        if (panel != nullptr)
                            for (const int end : panel->rowEnds (width - b))
                                whole = whole || end == pane;
                    if (! whole)
                    {
                        ++cut;
                        if (firstCut.isEmpty())
                            firstCut << w << " x " << h << " tab " << tab << ": " << pane << " px";
                    }
                    squeezed += roll < MainView::rollFloor ? 1 : 0;
                }
            }
        note (fmt ("the Sample tab at 900 x 560 (M4 review UI4-7: its pane cut the knob row): %s; folded (window, tab 0 Sample .. 6 Sound): %s",
                   sampleFoldsSmall ? "folded" : "shown whole", foldList.toRawUTF8()));
        check (cut == 0 && squeezed == 0 && needless == 0 && shown + foldedCount == cases && shown > 0 && foldedCount > 0 && sampleShowsDefault,
               fmt ("%d tab and window cases: %d show whole rows, %d fold (%d cut a row%s%s, %d leave the roll under %d px, %d folded where a whole "
                    "row fitted%s%s); the Sample tab shows at 1100 x 680 (%s)", cases, shown, foldedCount, cut, firstCut.isEmpty() ? "" : ", first ",
                    firstCut.toRawUTF8(), squeezed, MainView::rollFloor, needless, firstNeedless.isEmpty() ? "" : ", first ", firstNeedless.toRawUTF8(),
                    sampleShowsDefault ? "yes" : "NO"));

        // folded for want of room: a click on the tab or Show says why, and the person's choice stays. The first tab and
        // height at 900 px wide that folds (which one depends on the fonts).
        int foundTab = -1, foundHeight = 0;
        for (int h = 560; h <= 900 && foundTab < 0; h += 20)
        {
            e.editor->setLogicalSize (900, h);
            for (const int tab : { MainView::sampleTab, MainView::macrosTab, MainView::soundTab, MainView::copyTab, MainView::slidesTab })
            {
                view.openTab (tab);
                e.editor->refreshNow();
                if (view.isAutoFolded())
                {
                    foundTab = tab;
                    foundHeight = h;
                    break;
                }
            }
        }
        bool told = false, stillOpen = false;
        if (foundTab >= 0)
        {
            view.getTabStrip().onClick (foundTab);
            told = view.isToastShown() && view.getToastText().startsWith ("This window is too short") && view.isAutoFolded() && ! view.isFolded()
                && e.proc.getUiState().folded == 0 && view.getPaneArea().getHeight() == 0;
            view.getFoldButton().onClick();
            stillOpen = ! view.isFolded() && view.isAutoFolded() && e.proc.getUiState().folded == 0;
        }
        e.editor->setLogicalSize (900, 900);
        e.editor->refreshNow();
        check (foundTab >= 0 && told && stillOpen && ! view.isAutoFolded() && view.getPaneArea().getHeight() > 0,
               fmt ("folded at 900 x %d (tab %d): a click on the tab and on Show say \"%s\" and keep the choice; at 900 x 900 the tab opens by "
                    "itself (%d px)", foundHeight, foundTab, view.getToastText().toRawUTF8(), view.getPaneArea().getHeight()));
    }

    // an open drawer on a window too short for the roll's floor takes the inspector's line, and only then. Walk's panel
    // makes the Tools drawer take its least room (62 px).
    {
        EditorRig e (ThemeId::dark, 900, 560);
        auto& view = e.view();
        view.setFolded (true);
        e.roll().selectAll();
        view.setMode (5);
        e.bar().getDrawerButton (ToolSettings::Drawer::tools).onClick();
        if (auto* tools = dynamic_cast<ui::roll::ToolsDrawer*> (e.bar().getOpenDrawer()))
            tools->getButton ("Walk" + juce::String::charToString ((juce::juce_wchar) 0x2026)).onClick();
        constexpr int inspectorLine = 32;   // MainView's line for the inspector
        int folds = 0, kept = 0, wrong = 0, smallRoll = 0;
        juce::String firstWrong;
        for (int h = 560; h <= 760; h += 8)
        {
            e.editor->setLogicalSize (900, h);
            e.editor->refreshNow();
            const bool folded = view.isInspectorFolded();
            const int roll = view.getRollArea().getHeight();
            const int withLine = roll - (folded ? inspectorLine : 0);   // what the roll would have kept with the line
            const bool lineGone = ! view.getInspector().isVisible()
                               && view.getSelectionRowArea().getHeight() == 36 + view.getSelectionBar().getHeight();
            const bool right = folded ? (lineGone && withLine < MainView::rollFloor) : (view.getInspector().isVisible() && withLine >= MainView::rollFloor);
            (folded ? folds : kept) += 1;
            if (h == 560)
                smallRoll = roll;
            if (! right)
            {
                ++wrong;
                if (firstWrong.isEmpty())
                    firstWrong << "900 x " << h << ": " << (folded ? "folded" : "kept") << ", roll " << roll;
            }
        }
        e.bar().getDrawerButton (ToolSettings::Drawer::tools).onClick();   // closed
        e.editor->setLogicalSize (900, 560);
        e.editor->refreshNow();
        check (wrong == 0 && folds > 0 && kept > 0 && ! view.isInspectorFolded() && view.getInspector().isVisible(),
               fmt ("900 x 560 to 760, the tabs folded, the Tools drawer with Walk's panel open: the inspector's line goes to the roll exactly when "
                    "the roll would keep less than %d px (%d heights fold, %d keep it, %d wrong%s%s; at 560 the roll has %d px); with the drawer "
                    "closed the inspector is back", MainView::rollFloor, folds, kept, wrong, firstWrong.isEmpty() ? "" : ": ", firstWrong.toRawUTF8(),
                    smallRoll));
    }

    // the zoom buttons in the tab row, clear of the ruler, which runs the whole width; they still zoom
    {
        EditorRig e (ThemeId::dark, 900, 560);
        auto& view = e.view();
        auto& roll = e.roll();
        const auto ruler = roll.getRuler().getBounds() + roll.getPosition();
        int inRow = 0, overRuler = 0;
        for (int i = 0; i < 4; ++i)
        {
            auto& b = roll.getZoomButton (i);
            inRow += b.getParentComponent() == &view && view.getTabsArea().contains (b.getBounds()) && b.isVisible() ? 1 : 0;
            overRuler += b.getBounds().intersects (ruler) ? 1 : 0;
        }
        const bool fullRuler = roll.getRuler().getRight() >= roll.getCanvasArea().getRight();
        const double bw = roll.getGeometry().beatWidth();
        roll.getZoomButton (1).onClick();   // H+
        check (inRow == 4 && overRuler == 0 && fullRuler && std::abs (roll.getGeometry().beatWidth() - bw * 1.3) < 1e-9,
               fmt ("900 x 560: %d of 4 zoom buttons in the tab row, %d over the ruler; the ruler reaches the canvas's right edge (%d >= %d); "
                    "H+ makes beats 1.3 times wider", inRow, overRuler, roll.getRuler().getRight(), roll.getCanvasArea().getRight()));
    }
}

//==================================================================================================
// The M5 table's pictures (dark and light at 100 %): the selection bar with the Shapes drawer open, a hover ghost
// preview, a box select in progress; and the Copy / Paste and Slides tabs. Each is checked for what it must show.

namespace
{
    enum class M5Shot { shapesDrawer, hoverGhost, boxSelect, copyTab, slidesTab };

    struct M5Picture
    {
        juce::Image image;
        juce::Image without;            // hoverGhost and boxSelect: the same window without the ghost or the box
        juce::Rectangle<int> canvas;    // the roll's canvas, editor pixels
        juce::Rectangle<int> drawer;    // the open drawer's box
        juce::Rectangle<int> pane;      // the open tab's pane
        int shapeButtons = 0;
    };

    M5Picture shootM5 (ThemeId theme, M5Shot what)
    {
        M5Picture out;
        EditorRig e (theme);
        auto& view = e.view();
        auto& roll = e.roll();
        auto& bar = e.bar();
        if (what == M5Shot::copyTab || what == M5Shot::slidesTab)
        {
            // a tab open on the demo clip: Copy / Paste after Copy from a note with two chips off; Slides with a note
            // that has its own time
            const auto c = e.proc.getDocument().current();
            const std::string first = c->notes.front()->id;
            if (what == M5Shot::copyTab)
            {
                view.openTab (MainView::copyTab);
                auto& tab = view.getCopyPasteTab();
                tab.getChip (LaneId::vib).onClick();
                tab.getChip (LaneId::hollow).onClick();
                e.select ({ first }, first);
                tab.getCopyButton().onClick();
                tab.getCopyNotesButton().onClick();
            }
            else
            {
                view.openTab (MainView::slidesTab);
                e.select ({ first }, first);
                view.getSlidesTab().getNotesSlider().dragTo (sliderX (view.getSlidesTab().getNotesSlider(), 0.75));
                view.getSlidesTab().getNotesSlider().release();
            }
            e.editor->refreshNow();
            out.image = e.picture();
            out.pane = view.getPaneArea();
            out.canvas = roll.getCanvasArea() + roll.getPosition();
            return out;
        }

        view.setFolded (true);
        e.editor->refreshNow();
        if (what == M5Shot::boxSelect)
        {
            // Tone (box select works in every mode): two notes selected, then a Shift box from empty space down and right
            // across the notes, still held (S1's m5_box_select pictures show a plain one in Build)
            auto& g = roll.getGestures();
            const auto& geo = roll.getGeometry();
            view.setMode (5);
            roll.setScroll (0.0, geo.cy (66.0) - geo.viewHeight() / 2.0);
            const auto c = e.proc.getDocument().current();
            e.select ({ c->notes[0]->id, c->notes[1]->id }, c->notes[0]->id);
            e.editor->refreshNow();
            out.without = e.picture();
            const auto st = e.proc.getDocument().rollState();
            double x0 = geo.scrollX() + 30.0, y0 = geo.scrollY() + 10.0;
            for (int step = 0; step < 40; ++step)
            {
                const double x = geo.scrollX() + 30.0 + 13.0 * step, y = geo.scrollY() + 10.0 + 3.0 * step;
                const bool onNode = st.activeLane().has_value() && geo.hitNode (*c, st, x, y, *st.activeLane()).has_value();
                if (geo.hitNote (*c, st, x, y) == nullptr && ! onNode)
                {
                    x0 = x;
                    y0 = y;
                    break;
                }
            }
            g.pointerDown (x0, y0, { true, false }, 10000.0);   // Shift: the box adds to the selection
            g.pointerMove (x0 + 4.2 * geo.beatWidth(), y0 + 0.6 * geo.viewHeight(), { true, false });
            roll.edited();
            e.editor->refreshNow();
            out.image = e.picture();
            out.canvas = roll.getCanvasArea() + roll.getPosition();
            if (g.isDragging())
                g.pointerUp();
            return out;
        }

        // the Shapes drawer on Volume with every note selected; hoverGhost: the mouse on Ramp up
        roll.selectAll();
        view.setMode (3);
        bar.getDrawerButton (ToolSettings::Drawer::shapes).onClick();
        e.editor->refreshNow();
        auto* shapes = dynamic_cast<ui::roll::ShapesDrawer*> (bar.getOpenDrawer());
        if (shapes != nullptr)
            out.shapeButtons = (int) shapes->getShapeButtons().size();
        if (what == M5Shot::hoverGhost)
        {
            out.without = e.picture();
            if (shapes != nullptr)
                if (auto* up = shapes->findShape (model::ShapeKey::up))
                    shapes->getHooks().enter (*up);
            e.editor->refreshNow();
        }
        out.image = e.picture();
        out.canvas = roll.getCanvasArea() + roll.getPosition();
        out.drawer = bar.getDrawerArea() + bar.getPosition();
        return out;
    }
}

SL_TEST ("ui", m5SnapshotPictures)
{
    section ("The M5 pictures (dark and light at 100 %): the Shapes drawer open, a hover ghost, a box select in progress, the Copy / Paste "
             "and Slides tabs, each showing what it must");

    for (const auto theme : { ThemeId::dark, ThemeId::light })
    {
        const bool dark = theme == ThemeId::dark;
        const auto& P = ui::palette (theme);
        const char* name = dark ? "dark" : "light";

        const auto shapes = shootM5 (theme, M5Shot::shapesDrawer);
        const int drawerFill = countNear (shapes.image, shapes.drawer.reduced (10), P[Tok::panel2], 2);
        const double lum = meanLuminance (shapes.image);
        check (shapes.image.getWidth() == 1100 && shapes.image.getHeight() == 680 && (dark ? lum < 0.25 : lum > 0.75) && shapes.shapeButtons == 8
                   && shapes.drawer.getHeight() >= ui::roll::SelectionBar::drawerMinHeight && drawerFill > shapes.drawer.getWidth() * 10,
               fmt ("%s: the Shapes drawer, %d px high with Volume's %d shapes, filled in panel2 (%d pixels); mean luminance %.3f", name,
                    shapes.drawer.getHeight(), shapes.shapeButtons, drawerFill, lum));

        const auto ghost = shootM5 (theme, M5Shot::hoverGhost);
        const int ghostPixels = countDifferent (ghost.image, ghost.without, ghost.canvas);
        check (ghostPixels > 300, fmt ("%s: the mouse on Ramp up draws its ghost on the roll (%d pixels change in the canvas)", name, ghostPixels));

        const auto box = shootM5 (theme, M5Shot::boxSelect);
        const int boxPixels = countNear (box.image, box.canvas, P[Tok::boxLine], 24);
        const int changed = countDifferent (box.image, box.without, box.canvas);
        check (boxPixels > 100 && changed > 1000, fmt ("%s: a box select in progress, its dashed outline (%d pixels) and fill (%d pixels change)",
                                                       name, boxPixels, changed));

        const auto copy = shootM5 (theme, M5Shot::copyTab);
        const int toneChip = countNear (copy.image, copy.pane, P[Tok::laneTone], 6);
        const int pitchChip = countNear (copy.image, copy.pane, P[Tok::lanePitch], 6);
        check (copy.pane.getHeight() > 0 && toneChip > 150 && pitchChip > 150,
               fmt ("%s: the Copy / Paste tab, %d px, its chips in the lanes' colours (Tone %d, Pitch %d pixels)", name, copy.pane.getHeight(),
                    toneChip, pitchChip));

        const auto slides = shootM5 (theme, M5Shot::slidesTab);
        const int accent = countNear (slides.image, slides.pane, P[Tok::acc], 6);
        check (slides.pane.getHeight() > 0 && accent > 60, fmt ("%s: the Slides tab, %d px, the slider filled in the accent (%d pixels)", name,
                                                                slides.pane.getHeight(), accent));
    }
}

SL_SNAPSHOT (m5_table_shapes_drawer_dark)  { return shootM5 (ThemeId::dark, M5Shot::shapesDrawer).image; }
SL_SNAPSHOT (m5_table_shapes_drawer_light) { return shootM5 (ThemeId::light, M5Shot::shapesDrawer).image; }
SL_SNAPSHOT (m5_table_hover_ghost_dark)    { return shootM5 (ThemeId::dark, M5Shot::hoverGhost).image; }
SL_SNAPSHOT (m5_table_hover_ghost_light)   { return shootM5 (ThemeId::light, M5Shot::hoverGhost).image; }
SL_SNAPSHOT (m5_table_box_select_dark)     { return shootM5 (ThemeId::dark, M5Shot::boxSelect).image; }
SL_SNAPSHOT (m5_table_box_select_light)    { return shootM5 (ThemeId::light, M5Shot::boxSelect).image; }
SL_SNAPSHOT (m5_tab_copy_paste_dark)       { return shootM5 (ThemeId::dark, M5Shot::copyTab).image; }
SL_SNAPSHOT (m5_tab_copy_paste_light)      { return shootM5 (ThemeId::light, M5Shot::copyTab).image; }
SL_SNAPSHOT (m5_tab_slides_dark)           { return shootM5 (ThemeId::dark, M5Shot::slidesTab).image; }
SL_SNAPSHOT (m5_tab_slides_light)          { return shootM5 (ThemeId::light, M5Shot::slidesTab).image; }
