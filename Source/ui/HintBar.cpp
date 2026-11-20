#include "HintBar.h"

#include <cmath>
#include <string>

namespace sl::ui
{
    namespace
    {
        // The prototype's signs, as UTF-8 (the source stays ASCII): the middle dot between the parts, the filled
        // and the hollow diamond (a slide handle, a segment's bend diamond) and the node dot.
        const std::string kDot = " \xc2\xb7 ";
        const std::string kFilledDiamond = "\xe2\x97\x86";
        const std::string kHollowDiamond = "\xe2\x97\x87";
        const std::string kNodeDot = "\xe2\x97\x8f";
        const std::string kSmallDown = "\xe2\x96\xbe";   // the lane button's menu arrow

        // sl-ui.js SUB_HINT: what each Sample lane's line does (sampleLaneHint)
        const char* subHint (core::LaneId lane) noexcept
        {
            switch (lane)
            {
                case core::LaneId::travel:
                    return "Travel: above the middle line plays forwards, below plays backwards, on the line stops (1/8x to 8x). "
                           "Path notes: the line is where in the Region the note is.";
                case core::LaneId::heads:
                    return "Heads: raise the line for more playheads (1 to 8), lower it for fewer. Flat = the Heads knob.";
                case core::LaneId::grain:
                    return "Grain: raise the line for longer grains, lower it for shorter ones. Flat = the Grain knob.";
                case core::LaneId::refresh:
                    return "Refresh: raise the line for faster fresh grabs, lower it for slower; the very bottom = Smooth. "
                           "The line works once Refresh on the Sample tab is Free or Synced (it looks dim until then).";
                case core::LaneId::pitch:
                case core::LaneId::amp:
                case core::LaneId::vib:
                case core::LaneId::tone:
                case core::LaneId::formant:
                case core::LaneId::hollow:
                    break;
            }
            return "";
        }

        juce::Font hintFont() { return Theme::px (11.5f); }
        juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
        juce::Font tryFont() { return Theme::px (11.5f, true); }
        constexpr const char* kTryLabel = "Try this: ";
    }

    juce::String modeHint (int mode, std::optional<core::LaneId> lane, bool freehand, bool linkArmed, bool nodes)
    {
        std::string t;
        if (linkArmed)
        {
            t = "SLIDE INTO: click the note the selected note should slide into.";
        }
        else if (mode <= 1 || ! lane.has_value())
        {
            t = "BUILD (1): double-click empty space = new note" + kDot + "drag a note to move it" + kDot
              + "drag its end dots sideways (length) or up/down (bend)" + kDot + kFilledDiamond + " slide handles" + kDot
              + "Shift+click a 2nd note = slide into it" + kDot + "drag on empty space = box select (Shift adds, Ctrl toggles)" + kDot
              + "right-click = menu";
        }
        else
        {
            const std::string name = core::laneInfo (*lane).name;
            const std::string upper = juce::String (name).toUpperCase().toStdString();
            const std::string title = mode >= 8 ? "SAMPLE (8)" + kDot + upper : upper + " (" + std::to_string (mode) + ")";
            if (freehand)
            {
                t = "FREEHAND (" + name + "): paint across the selected note; it turns into nodes when you let go.";
            }
            else if (nodes)
            {
                t = title + kDot + "NODES: drag a box around nodes of the selected notes, then drag one of them to move them together "
                                   "(Shift adds, Ctrl toggles).";
            }
            else
            {
                std::string extra;
                if (mode >= 8)
                    extra = kDot + subHint (*lane);
                else if (*lane == core::LaneId::pitch)
                    extra = kDot + "nodes snap to semitones (hold Shift for free)" + kDot + kFilledDiamond + " slide handles";
                t = title + ": notes are locked, only " + name + " changes" + kDot + "click a note to select it" + kDot + "drag "
                  + kNodeDot + " nodes" + kDot + "double-click the line = add a node" + kDot + "double-click a node = delete" + kDot
                  + "drag the small " + kHollowDiamond + " in a segment = curve it (it fills in), double-click a filled " + kFilledDiamond
                  + " = straighten" + kDot + kSmallDown + " on the lane button = shapes for every selected note" + extra;
            }
        }
        return juce::String::fromUTF8 (t.c_str());
    }

    juce::String sampleLaneHint (core::LaneId lane)
    {
        return juce::String::fromUTF8 (subHint (lane));
    }

    //==============================================================================================
    // sl-ui-panels.js "Try this (U3)": MODE_TIPS, SUB_TIPS, MOTION_TIPS, ENGINE_TIPS and THEN_TIPS, and updateTry.

    namespace
    {
        const std::string kNext = " \xe2\x96\xb8 ";   // the menus' small arrow between a button and what it opens
        const std::string kHollow = "\xe2\x97\x87";   // a segment's hollow bend diamond

        /** The engine a note plays, as the tips tell them apart: Cloud with Lock is a kind of its own. */
        enum class TipKind { tape, voice, cloud, lock };

        std::string modeTip (int mode)
        {
            switch (mode)
            {
                case 1: return "draw a chord, drag a box around it, then Tools" + kNext + "Strum" + kNext + "Strum up.";
                case 2: return "select a held note, choose Shapes" + kNext + "Ramp up, then drag the small " + kHollow + " to curve the rise.";
                case 3: return "Pattern" + kNext + "Square at 1/16 Triplet with both depths near 80%: an instant rhythmic gate.";
                case 4: return "Shapes" + kNext + "Ramp up, so the vibrato grows across each note like a singer's.";
                case 5: return "Pattern" + kNext + "Squiggle at 1/16 Triplet with Lock to the beat: a wah that grooves with the bar.";
                case 6: return "select a phrase, choose Across, then Shapes" + kNext + "Ramp down: one long aah to ooh over all the notes.";
                case 7: return "Shapes" + kNext + "Swell: each note goes hollow in the middle, then opens again.";
                default: return {};
            }
        }

        std::string subTip (core::LaneId lane, TipKind kind)
        {
            switch (lane)
            {
                case core::LaneId::heads:
                    return "Shapes" + kNext + "Ramp up on Heads: each note blooms from one playhead to many.";
                case core::LaneId::grain:
                    if (kind == TipKind::tape)   // ENGINE_TIPS.grain.tape
                        return "Grain does nothing in Tape: pick Voice or Cloud on the Sample tab, then try Random at 1/16 on Grain.";
                    return "Random at 1/16 with Smooth near 50% on Grain: grains that breathe.";
                case core::LaneId::refresh:
                    return "set Refresh to Synced on the Sample tab, then Shapes" + kNext + "Ramp up here for a stutter that speeds up.";
                case core::LaneId::pitch:
                case core::LaneId::amp:
                case core::LaneId::vib:
                case core::LaneId::tone:
                case core::LaneId::formant:
                case core::LaneId::hollow:
                case core::LaneId::travel:
                    break;
            }
            return {};
        }

        /** MOTION_TIPS, with ENGINE_TIPS' freeze (tape, voice) and scatter (tape) in their place. */
        std::string motionTip (core::Motion motion, TipKind kind)
        {
            switch (motion)
            {
                case core::Motion::forward:
                    return "Forward: draw the Travel line below the middle on one note, and that note plays backwards.";
                case core::Motion::backward:
                    return "Backward: add Shapes" + kNext + "Swell on Volume, so each note fades in like reversed tape.";
                case core::Motion::bounce:
                    return "Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other.";
                case core::Motion::mirror:
                    return "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways.";
                case core::Motion::freeze:
                    if (kind == TipKind::tape)
                        return "Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud on the Sample tab, then raise Heads to 4.";
                    if (kind == TipKind::voice)
                        return "Freeze: raise Heads to 4 and set Head tune to Detune for a frozen choir (pick Cloud to add Spray too).";
                    return "Freeze: raise Heads to 4, then add Spray.";
                case core::Motion::scatter:
                    if (kind == TipKind::tape)
                        return "Scatter: set Jump every to 1/8 Triplet, then raise Heads to 3.";
                    return "Scatter: set Jump every to 1/8 Triplet, then Random on Grain.";
            }
            return {};
        }

        /** THEN_TIPS: once a Motion's tip is done (by a recipe, or by hand), its next step. Empty while it is not done. */
        std::string thenTip (core::Motion motion, const core::Inst& I, TipKind kind)
        {
            switch (motion)
            {
                case core::Motion::freeze:
                    if (kind != TipKind::tape && I.heads >= 4.0 && (kind == TipKind::cloud ? I.spray > 0.0 : I.headTune != core::HeadTune::same))
                        return "Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment "
                               "of the sound.";
                    break;
                case core::Motion::bounce:
                    if (kind != TipKind::tape && I.heads >= 3.0 && I.drift > 0.0)
                        return "Bounce: drag the Region smaller on the waveform, so the heads turn round more often.";
                    break;
                case core::Motion::mirror:
                    if (kind != TipKind::tape && I.heads >= 2.0)
                        return "Mirror: raise Spread on the Sample tab, so the forward and backward heads read different spots.";
                    break;
                case core::Motion::scatter:
                    if (I.jump == core::RateId::r1_8T)
                        return "Scatter: press Re-roll on the Sample tab for new jump spots (the rhythm of the notes stays).";
                    break;
                case core::Motion::forward:
                case core::Motion::backward:
                    break;
            }
            return {};
        }
    }

    juce::String tryThis (int mode, core::LaneId subLane, const model::Note* P, const core::Inst& I)
    {
        // updateTry: the engine the primary note plays (its own, else the Sample tab's), Cloud with Lock as Lock
        const auto eng = P != nullptr && P->sample.engine.has_value() ? *P->sample.engine : I.engine;
        const bool lockOn = eng == core::Engine::cloud && (P != nullptr && P->sample.lock.has_value() ? *P->sample.lock : I.lock);
        const TipKind kind = eng == core::Engine::tape    ? TipKind::tape
                           : eng == core::Engine::voice   ? TipKind::voice
                           : lockOn                       ? TipKind::lock
                                                          : TipKind::cloud;
        std::string text;
        if (mode == 8)
        {
            if (subLane == core::LaneId::travel)
            {
                const auto mo = P != nullptr && P->sample.motion.has_value() ? *P->sample.motion : I.motion;
                text = thenTip (mo, I, kind);
                if (text.empty())
                    text = motionTip (mo, kind);
            }
            else
            {
                text = subTip (subLane, kind);
            }
        }
        else
        {
            text = modeTip (mode);
        }
        return fromUtf8 (text);
    }

    //==============================================================================================
    HintBar::HintBar (const Theme& t)
        : theme (t), hover (t)
    {
        hover.setVisible (false);
    }

    void HintBar::setHint (const juce::String& newHint)
    {
        if (newHint == hint)
            return;
        hint = newHint;
        hintShown = hintShown && isHintCut();
        updateHover();
        repaint();
    }

    void HintBar::setTryThis (const juce::String& text)
    {
        if (text == tryThis)
            return;
        tryThis = text;
        repaint();
    }

    juce::Rectangle<int> HintBar::tryArea() const
    {
        const auto r = getLocalBounds().reduced (padX, 0);
        if (tryThis.isEmpty())
            return r.withWidth (0).withX (r.getRight());
        const float want = Theme::textWidth (tryFont(), kTryLabel) + Theme::textWidth (hintFont(), tryThis);
        const int w = juce::jmin ((int) std::ceil (want) + 2, r.getWidth() / 2);
        return r.withLeft (r.getRight() - w);
    }

    juce::Rectangle<int> HintBar::hintArea() const
    {
        const auto r = getLocalBounds().reduced (padX, 0);
        const auto t = tryArea();
        return r.withRight (t.getWidth() > 0 ? t.getX() - 12 : r.getRight());
    }

    bool HintBar::isHintCut() const
    {
        return Theme::textWidth (hintFont(), hint) > (float) hintArea().getWidth();
    }

    void HintBar::paint (juce::Graphics& g)
    {
        // the mode's hint, dim, cut with an ellipsis when it does not fit (shell.html #hint)
        g.setColour (theme (Tok::dim));
        g.setFont (hintFont());
        g.drawText (hint, hintArea(), juce::Justification::centredLeft, true);

        // "Try this:" in the accent, then its text (shell.html #tryThis)
        auto t = tryArea();
        if (t.getWidth() > 0)
        {
            const int tw = (int) std::ceil (Theme::textWidth (tryFont(), kTryLabel));
            g.setColour (theme (Tok::acc));
            g.setFont (tryFont());
            g.drawText (kTryLabel, t.removeFromLeft (tw), juce::Justification::centredLeft, false);
            g.setColour (theme (Tok::txt));
            g.setFont (hintFont());
            g.drawText (tryThis, t, juce::Justification::centredLeft, true);
        }
    }

    void HintBar::resized()
    {
        hintShown = hintShown && isHintCut();
        updateHover();
    }

    bool HintBar::isTryCut() const
    {
        if (tryThis.isEmpty())
            return false;
        const float want = Theme::textWidth (tryFont(), kTryLabel) + Theme::textWidth (hintFont(), tryThis);
        return want > (float) tryArea().getWidth() - 1.0f;
    }

    void HintBar::mouseEnter (const juce::MouseEvent& e)
    {
        mouseMove (e);
    }

    void HintBar::mouseMove (const juce::MouseEvent& e)
    {
        // the mouse on a cut suggestion shows all of it over the right half; on a cut mode line, all of that
        const bool overTry = tryArea().contains (e.getPosition());
        const bool wantHint = ! overTry && isHintCut(), wantTry = overTry && isTryCut();
        if (wantHint == hintShown && wantTry == tryShown)
            return;
        hintShown = wantHint;
        tryShown = wantTry;
        updateHover();
    }

    void HintBar::mouseExit (const juce::MouseEvent&)
    {
        hintShown = false;
        tryShown = false;
        updateHover();
    }

    void HintBar::showHover (bool show)
    {
        hintShown = show;
        if (show)
            tryShown = false;
        updateHover();
    }

    void HintBar::showTryHover (bool show)
    {
        tryShown = show;
        if (show)
            hintShown = false;
        updateHover();
    }

    void HintBar::setHelp (const juce::String& text)
    {
        if (text == help)
            return;
        help = text;
        updateHover();
    }

    void HintBar::updateHover()
    {
        // a control's text wins over the whole hint (sl-ui.js updateHint: the override covers the bar); a whole suggestion
        // covers only the right half, where the suggestion is (never the mode's line)
        const bool trying = help.isEmpty() && ! hintShown && tryShown && tryThis.isNotEmpty();
        const bool show = help.isNotEmpty() || hintShown || trying;
        const juce::String& shown = help.isNotEmpty() ? help : trying ? tryThis : hint;
        if (! show || getParentComponent() == nullptr || hover.getParentComponent() != getParentComponent())
        {
            hover.setVisible (false);
            return;
        }
        // from the bar's bottom edge up, tall enough for the whole text, wrapped: as wide as the bar, or over the right
        // half from where the suggestion starts
        const int left = trying ? tryArea().getX() - padX : 0;
        const int width = getWidth() - left;
        const int innerWidth = juce::jmax (40, width - 2 * padX);
        juce::AttributedString text;
        if (trying)
            text.append (kTryLabel, tryFont());
        text.append (shown, hintFont());
        text.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (text, (float) innerWidth);
        const int height = juce::jmax (getHeight() + 2, (int) std::ceil (layout.getHeight()) + 12);
        hover.prefix = trying ? juce::String (kTryLabel) : juce::String();
        hover.text = shown;
        hover.setBounds (getX() + left - 1, getBottom() + 1 - height, width + 2, height);
        hover.setVisible (true);
        hover.toFront (false);
        hover.repaint();
    }

    void HintBar::Hover::paint (juce::Graphics& g)
    {
        // shell.html #hintHover: the panel colour, a 1 px accent outline, the card's round bottom corners
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        juce::Path box;
        box.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), Theme::frameCorner, Theme::frameCorner,
                                 false, false, true, true);
        g.setColour (theme (Tok::panel));
        g.fillPath (box);
        g.setColour (theme (Tok::acc));
        g.strokePath (box, juce::PathStrokeType (1.0f));

        juce::AttributedString as;
        if (prefix.isNotEmpty())
            as.append (prefix, tryFont(), theme (Tok::acc));
        as.append (text, hintFont(), theme (Tok::txt));
        as.setWordWrap (juce::AttributedString::byWord);
        as.setJustification (juce::Justification::centredLeft);
        juce::TextLayout layout;
        const auto inner = getLocalBounds().toFloat().reduced ((float) HintBar::padX, 5.0f);
        layout.createLayout (as, inner.getWidth());
        layout.draw (g, inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (inner.getHeight(), layout.getHeight())));
    }
}
