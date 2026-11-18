#include "Inspector.h"

#include "InspectorText.h"
#include "RollGeometry.h"

#include "PluginProcessor.h"
#include "core/JsCompat.h"
#include "core/Music.h"
#include "model/ClipEdit.h"
#include "model/LaneFormat.h"
#include "model/NoteSettings.h"
#include "ui/StrandLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        juce::Font captionFont() { return Theme::px (10.5f, false, 0.06f); }
        juce::Font unitFont() { return Theme::px (11.0f); }
        juce::Font valueFont() { return Theme::px (12.0f); }

        constexpr int kInnerGap = 4;          // caption, box and unit (sl-ui-panels.css .slp-field gap)
        constexpr int kMinValueBox = 66;      // input.slp-in width
        constexpr int kMaxValueBox = 190;     // fitVal's widest
        constexpr float kDisabledAlpha = 0.55f;   // input.slp-in:disabled

        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    }

    //==============================================================================================
    /** The value box: a label that becomes a text box on a click (Enter keeps, Escape puts the old value back,
        a click elsewhere keeps). It draws only its text: the field draws the box around it. */
    class InspectorField::Box final : public juce::Label
    {
    public:
        explicit Box (InspectorField& f) : owner (f)
        {
            setEditable (true, false, false);
            setFont (valueFont());
            setJustificationType (juce::Justification::centredLeft);
            setBorderSize (juce::BorderSize<int> (0, 6, 0, 4));
            setMinimumHorizontalScale (1.0f);
            setMouseCursor (juce::MouseCursor::IBeamCursor);
        }

        void applyColours (const Theme& theme)
        {
            const auto clear = juce::Colours::transparentBlack;
            setColour (juce::Label::textColourId, theme (Tok::txt));
            setColour (juce::Label::backgroundColourId, clear);
            setColour (juce::Label::outlineColourId, clear);
            setColour (juce::Label::textWhenEditingColourId, theme (Tok::txt));
            setColour (juce::Label::backgroundWhenEditingColourId, clear);
            setColour (juce::Label::outlineWhenEditingColourId, clear);
            // the text box the label opens takes every colour set here (Label::createEditorComponent)
            setColour (juce::TextEditor::textColourId, theme (Tok::txt));
            setColour (juce::TextEditor::backgroundColourId, clear);
            setColour (juce::TextEditor::outlineColourId, clear);
            setColour (juce::TextEditor::focusedOutlineColourId, clear);
            setColour (juce::TextEditor::highlightColourId, theme (Tok::acc).withAlpha (0.35f));
            setColour (juce::TextEditor::highlightedTextColourId, theme (Tok::txt));
            setColour (juce::CaretComponent::caretColourId, theme (Tok::txt));
        }

    protected:
        void editorShown (juce::TextEditor* ed) override
        {
            if (ed == nullptr)
                return;
            ed->setJustification (juce::Justification::centredLeft);
            ed->setBorder (juce::BorderSize<int> (0));
            ed->setIndents (6, 0);
            ed->setSelectAllWhenFocused (true);
            ed->selectAll();
            owner.repaint();
        }

        void editorAboutToBeHidden (juce::TextEditor*) override { owner.repaint(); }

        void textWasEdited() override { owner.edited(); }

        void lookAndFeelChanged() override
        {
            juce::Label::lookAndFeelChanged();
            if (auto* laf = dynamic_cast<StrandLookAndFeel*> (&getLookAndFeel()))
                applyColours (laf->getTheme());
        }

    private:
        InspectorField& owner;
    };

    //==============================================================================================
    InspectorField::InspectorField (const Theme& t, const juce::String& captionText, const juce::String& unitText, const juce::String& tooltip)
        : theme (t), caption (captionText), unit (unitText), box (std::make_unique<Box> (*this))
    {
        box->applyColours (theme);
        box->setTooltip (tooltip);
        addAndMakeVisible (*box);
    }

    InspectorField::~InspectorField()
    {
        if (box != nullptr && box->isBeingEdited())
            box->hideEditor (true);
    }

    void InspectorField::setValue (const juce::String& text)
    {
        if (box->isBeingEdited())
            return;
        shown = text;
        if (box->getText() != text)
            box->setText (text, juce::dontSendNotification);
    }

    juce::String InspectorField::getValue() const
    {
        return box->getText();
    }

    void InspectorField::setCaption (const juce::String& text)
    {
        if (text == caption)
            return;
        caption = text;
        resized();
        repaint();
    }

    void InspectorField::setTooltipText (const juce::String& text)
    {
        box->setTooltip (text);
    }

    void InspectorField::setBoxWidth (int width)
    {
        boxWidth = juce::jmax (24, width);
    }

    int InspectorField::captionWidth() const
    {
        return caption.isEmpty() ? 0 : (int) std::ceil (Theme::textWidth (captionFont(), caption.toUpperCase())) + 2;
    }

    int InspectorField::unitWidth() const
    {
        return unit.isEmpty() ? 0 : (int) std::ceil (Theme::textWidth (unitFont(), unit)) + 2;
    }

    int InspectorField::preferredWidth() const
    {
        const int c = captionWidth(), u = unitWidth();
        return c + (c > 0 ? kInnerGap : 0) + boxWidth + (u > 0 ? kInnerGap + u : 0);
    }

    bool InspectorField::isBeingEdited() const
    {
        return box->isBeingEdited();
    }

    void InspectorField::finishEditing()
    {
        if (box->isBeingEdited())
            box->hideEditor (false);
    }

    void InspectorField::type (const juce::String& text)
    {
        if (! isEnabled())
            return;
        box->showEditor();
        if (auto* ed = box->getCurrentTextEditor())
            ed->setText (text, false);
        box->hideEditor (false);
    }

    void InspectorField::edited()
    {
        // sl-ui-panels.js field (): a value typed back as it was shown changes nothing
        const auto typed = box->getText();
        if (typed.trim() == shown.trim())
            return;
        if (onCommit)
            onCommit (typed);
    }

    void InspectorField::resized()
    {
        const int c = captionWidth();
        const int x = c + (c > 0 ? kInnerGap : 0);
        box->setBounds (x, (getHeight() - boxHeight) / 2, boxWidth, boxHeight);
    }

    void InspectorField::enablementChanged()
    {
        repaint();
    }

    void InspectorField::paint (juce::Graphics& g)
    {
        const float alpha = isEnabled() ? 1.0f : kDisabledAlpha;
        const int c = captionWidth();
        if (c > 0)
        {
            g.setColour (theme (Tok::dim));
            g.setFont (captionFont());
            g.drawText (caption.toUpperCase(), juce::Rectangle<int> (0, 0, c, getHeight()), juce::Justification::centredLeft, false);
        }

        // the box (shell.html input[type=text]: the button colours, a 7 px corner; the accent while typed in)
        const auto r = box->getBounds().toFloat().reduced (0.5f);
        g.setColour (theme (Tok::btn).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour ((box->isBeingEdited() ? theme (Tok::acc) : theme (Tok::btnb)).withMultipliedAlpha (alpha));
        juce::Path outline;
        outline.addRoundedRectangle (r, Theme::corner);
        g.strokePath (outline, juce::PathStrokeType (box->isBeingEdited() ? 1.5f : 1.0f));

        if (unit.isNotEmpty())
        {
            g.setColour (theme (Tok::dim).withMultipliedAlpha (alpha));
            g.setFont (unitFont());
            const int x = box->getRight() + kInnerGap;
            g.drawText (unit, juce::Rectangle<int> (x, 0, juce::jmax (0, getWidth() - x), getHeight()), juce::Justification::centredLeft, false);
        }
    }

    //==============================================================================================
    Inspector::Inspector (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          start (t, "Start", {}, "Where the note starts, Cubase style bar.beat.sixteenth (e.g. 2.1.3). With several notes selected they all move by the same amount."),
          length (t, "Length", "beats", "In beats (0.5) or as a note value (1/8, 1/16T, 1 bar). With several notes selected they all get this length."),
          pitch (t, "Pitch", {}, "A note name such as C3 or F#2 (C3 = MIDI 60, as in Cubase) or a MIDI number. Several selected notes all move by the same amount."),
          velocity (t, "Velocity", {}, "1 to 127. With several notes selected they all get this velocity."),
          nodeTime (t, "at", "beats in", "How far into the note the node sits, in beats (or a note value such as 1/16)"),
          nodeValue (t, "value", {}, "Type the value in real units, e.g. +3 st, -6 dB, 2.4 kHz, 0.5x backwards, 3 heads"),
          emptyText ("Select a note: its start, length, pitch and velocity show here, and you can type new ones.")
    {
        start.onCommit = [this] (const juce::String& s) { commitStart (s); };
        length.onCommit = [this] (const juce::String& s) { commitLength (s); };
        pitch.onCommit = [this] (const juce::String& s) { commitPitch (s); };
        velocity.onCommit = [this] (const juce::String& s) { commitVelocity (s); };
        nodeTime.onCommit = [this] (const juce::String& s) { commitNodeTime (s); };
        nodeValue.onCommit = [this] (const juce::String& s) { commitNodeValue (s); };
        for (auto* f : { &start, &length, &pitch, &velocity, &nodeTime, &nodeValue })
            addChildComponent (*f);
        sync (bpm);
    }

    Inspector::~Inspector() = default;

    model::ClipPtr Inspector::clip() const
    {
        return processor.getDocument().current();
    }

    model::NotePtr Inspector::primary() const
    {
        const auto c = clip();
        const auto st = processor.getDocument().rollState();
        if (c == nullptr || st.primary.empty())
            return nullptr;
        for (const auto& n : c->notes)
            if (n->id == st.primary)
                return n;
        return nullptr;
    }

    std::vector<std::string> Inspector::selectedIds() const
    {
        std::vector<std::string> ids;
        const auto c = clip();
        if (c == nullptr)
            return ids;
        for (const auto& n : RollGeometry::selectedNotes (*c, processor.getDocument().rollState()))
            ids.push_back (n->id);
        return ids;
    }

    double Inspector::total() const
    {
        const auto c = clip();
        return c != nullptr ? RollGeometry::totalBeats (*c) : 16.0;
    }

    std::optional<Inspector::NodeCtx> Inspector::nodeCtx() const
    {
        // sl-ui-panels.js activeNodeCtx: the inspector's node, on the lane the mode shows, of a selected note
        const auto st = processor.getDocument().rollState();
        const auto lane = st.activeLane();
        const auto& nd = st.activeNode;
        if (! nd || ! lane || nd->lane != *lane || ! st.isSelected (nd->id))
            return std::nullopt;
        const auto c = clip();
        if (c == nullptr)
            return std::nullopt;
        for (const auto& n : c->notes)
        {
            if (n->id != nd->id)
                continue;
            const auto& nodes = model::noteLane (*n, *lane);
            if (nd->k < 0 || nd->k >= (int) nodes.size())
                return std::nullopt;
            return NodeCtx { n, *lane, nd->k };
        }
        return std::nullopt;
    }

    //==============================================================================================
    void Inspector::sync (double newBpm)
    {
        bpm = std::isfinite (newBpm) && newBpm > 0.0 ? newBpm : 120.0;
        const auto P = primary();
        const bool note = P != nullptr;
        const auto c = note ? nodeCtx() : std::nullopt;
        const bool node = c.has_value();
        const bool layoutChanged = note != hasNote || node != hasNode;
        hasNote = note;
        hasNode = node;

        for (auto* f : { &start, &length, &pitch, &velocity })
            f->setVisible (note);
        nodeTime.setVisible (node);
        nodeValue.setVisible (node);

        if (note)
        {
            // sl-ui-panels.js syncInspector
            start.setValue (str (fmtPos (P->s)));
            length.setValue (str (fmtNum (P->l, 4)));
            pitch.setValue (str (core::noteName (P->p)));
            velocity.setValue (juce::String ((int) std::max (1.0, core::jsRound (P->vel * 127.0))));
        }

        juce::String caption;
        if (node)
        {
            const auto& n = *c->note;
            const auto& nodes = model::noteLane (n, c->lane);
            const int last = (int) nodes.size() - 1;
            const bool ends = c->k == 0 || c->k == last;
            const auto& q = nodes[(std::size_t) c->k];
            caption = "Node (" + str (core::laneInfo (c->lane).name) + ")";
            nodeTime.setEnabled (! ends);
            nodeTime.setTooltipText (ends ? "The first and last nodes stay at the start and end of the note"
                                          : "How far into the note the node sits, in beats (or a note value such as 1/16)");
            nodeTime.setValue (c->k == 0 ? juce::String ("start") : c->k == last ? juce::String ("end") : str (fmtNum (q.t * n.l, 3)));
            const auto ctx = model::formatCtx (processor.readInst(), bpm, &n);
            nodeValue.setValue (str (laneText (c->lane, q.v, ctx)));
            fitValueBox();
        }
        if (caption != nodeCaption)
        {
            nodeCaption = caption;
            repaint();
        }
        if (layoutChanged)
            repaint();
        layoutFields();
    }

    void Inspector::fitValueBox()
    {
        // sl-ui-panels.js fitVal: the value box grows with its text ("25% of the Region", "0.5x backwards")
        if (nodeValue.isBeingEdited())
            return;
        const double w = std::min ((double) kMaxValueBox, std::max ((double) kMinValueBox, 14.0 + 6.6 * (double) nodeValue.getValue().length()));
        nodeValue.setBoxWidth ((int) core::jsRound (w));
    }

    int Inspector::preferredWidth() const
    {
        if (! hasNote)
            return (int) std::ceil (Theme::textWidth (Theme::px (12.0f), emptyText)) + 4;
        int w = start.preferredWidth() + length.preferredWidth() + pitch.preferredWidth() + velocity.preferredWidth() + 3 * fieldGap;
        if (hasNode)
            w += fieldGap + 1 + 8 + (int) std::ceil (Theme::textWidth (captionFont(), nodeCaption.toUpperCase())) + 2 + fieldGap
               + nodeTime.preferredWidth() + fieldGap + nodeValue.preferredWidth();
        return w;
    }

    void Inspector::layoutFields()
    {
        const int h = getHeight();
        int x = 0;
        const auto place = [&] (InspectorField& f)
        {
            const int w = f.preferredWidth();
            f.setBounds (x, 0, w, h);
            x += w + fieldGap;
        };
        nodeLineX = -1;
        if (! hasNote)
            return;
        for (auto* f : { &start, &length, &pitch, &velocity })
            place (*f);
        if (! hasNode)
            return;
        // the node's part after a 1 px line (sl-ui-panels.css .slp-node): its caption, at, value; the value box
        // gives way when the line is short
        nodeLineX = x;
        x += 1 + 8;
        nodeCaptionX = x;
        nodeCaptionWidth = (int) std::ceil (Theme::textWidth (captionFont(), nodeCaption.toUpperCase())) + 2;
        x += nodeCaptionWidth + fieldGap;
        place (nodeTime);
        const int room = getWidth() - x - (nodeValue.preferredWidth() - nodeValue.getBoxWidth());
        if (room < nodeValue.getBoxWidth())
            nodeValue.setBoxWidth (juce::jmax (40, room));
        place (nodeValue);
    }

    void Inspector::resized()
    {
        layoutFields();
    }

    void Inspector::paint (juce::Graphics& g)
    {
        if (! hasNote)
        {
            g.setColour (theme (Tok::dim));
            g.setFont (Theme::px (12.0f));
            g.drawText (emptyText, getLocalBounds(), juce::Justification::centredLeft, true);
            return;
        }
        if (hasNode && nodeLineX >= 0)
        {
            g.setColour (theme (Tok::line));
            g.fillRect (nodeLineX, (getHeight() - InspectorField::boxHeight) / 2, 1, InspectorField::boxHeight);
            g.setColour (theme (Tok::dim));
            g.setFont (captionFont());
            g.drawText (nodeCaption.toUpperCase(), juce::Rectangle<int> (nodeCaptionX, 0, nodeCaptionWidth, getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }

    void Inspector::finishEditing()
    {
        for (auto* f : { &start, &length, &pitch, &velocity, &nodeTime, &nodeValue })
            f->finishEditing();
    }

    bool Inspector::isEditing() const
    {
        for (const auto* f : { &start, &length, &pitch, &velocity, &nodeTime, &nodeValue })
            if (f->isBeingEdited())
                return true;
        return false;
    }

    //==============================================================================================
    // What a typed value does (sl-ui-panels.js commitStart ... commitNodeVal): one undo step each.

    void Inspector::toast (const juce::String& text, int ms)
    {
        if (onToast)
            onToast (text, ms);
    }

    void Inspector::act (model::Clip proposed, const std::string& label)
    {
        const auto out = processor.editClip (label, std::move (proposed));
        if (out.refused)
            toast (str (out.message));
        sync (bpm);
        if (onEdit)
            onEdit();
    }

    void Inspector::commitStart (const juce::String& text)
    {
        const auto b = parsePos (text.toStdString());
        const auto P = primary();
        if (! b || P == nullptr)
        {
            toast ("Type a position like 2.1.3 (bar.beat.sixteenth)");
            sync (bpm);
            return;
        }
        const double d = *b - P->s, T = total();
        act (model::editNotes (*clip(), model::Ids (selectedIds()),
                               [d, T] (model::Note& n, const model::Note&) { n.s = clampD (n.s + d, 0.0, std::max (0.0, T - n.l)); }),
             "Type start");
    }

    void Inspector::commitLength (const juce::String& text)
    {
        const auto l = parseBeats (text.toStdString());
        if (! l || ! (*l > 0.0))
        {
            toast ("Type a length in beats (0.5) or a note value (1/8)");
            sync (bpm);
            return;
        }
        const double len = *l, T = total();
        act (model::editNotes (*clip(), model::Ids (selectedIds()),
                               [len, T] (model::Note& n, const model::Note&)
                               { n.l = clampD (len, model::kMinLen, std::max (model::kMinLen, T - n.s)); }),
             "Type length");
    }

    void Inspector::commitPitch (const juce::String& text)
    {
        const auto p = parsePitch (text.toStdString());
        const auto P = primary();
        if (! p || P == nullptr)
        {
            toast ("Type a note such as C3 or F#2, or a MIDI number");
            sync (bpm);
            return;
        }
        const double d = *p - P->p;
        act (model::editNotes (*clip(), model::Ids (selectedIds()),
                               [d] (model::Note& n, const model::Note&) { n.p = clampD (n.p + d, (double) kMinPitch, (double) kMaxPitch); }),
             "Type pitch");
    }

    void Inspector::commitVelocity (const juce::String& text)
    {
        const auto v = parseVelocity (text.toStdString());
        if (! v)
        {
            toast ("Type a velocity from 1 to 127");
            sync (bpm);
            return;
        }
        model::NotePatch patch;
        patch.vel = (double) *v / 127.0;
        act (model::updateNotes (*clip(), model::Ids (selectedIds()), patch), "Type velocity");
    }

    void Inspector::commitNodeTime (const juce::String& text)
    {
        const auto c = nodeCtx();
        if (! c)
            return;
        const auto& nodes = model::noteLane (*c->note, c->lane);
        const int last = (int) nodes.size() - 1;
        if (c->k == 0 || c->k == last)
            return;
        const auto b = parseBeats (text.toStdString());
        if (! b)
        {
            toast ("Type how many beats into the note, e.g. 0.5 or 1/16");
            sync (bpm);
            return;
        }
        model::NodeList nd = nodes;
        const auto k = (std::size_t) c->k;
        nd[k].t = clampD (*b / c->note->l, nd[k - 1].t + 0.004, nd[k + 1].t - 0.004);
        act (model::setLaneNodes (*clip(), c->note->id, c->lane, nd), "Type node time");
    }

    void Inspector::commitNodeValue (const juce::String& text)
    {
        const auto c = nodeCtx();
        if (! c)
            return;
        const auto ctx = model::formatCtx (processor.readInst(), bpm, c->note.get());
        const auto r = laneValueFromText (c->lane, text.toStdString(), ctx);
        if (! r.ok)
        {
            toast (str (r.text), 3500);
            sync (bpm);
            return;
        }
        model::NodeList nd = model::noteLane (*c->note, c->lane);
        nd[(std::size_t) c->k].v = r.v;
        act (model::setLaneNodes (*clip(), c->note->id, c->lane, nd), "Type node value");
    }
}
