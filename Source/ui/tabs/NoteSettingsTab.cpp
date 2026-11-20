#include "NoteSettingsTab.h"

#include "PluginProcessor.h"
#include "SampleControls.h"
#include "core/JsCompat.h"
#include "core/Rates.h"
#include "model/ClipEdit.h"
#include "model/Node.h"
#include "model/Shapes.h"
#include "ui/roll/Drawers.h"
#include "ui/roll/NoteDecor.h"
#include "ui/roll/RollGeometry.h"
#include "ui/roll/RollView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace sl::ui
{
    namespace
    {
        constexpr int kControlHeight = 26;   // a button's height on the tabs (TabPanel)
        constexpr int kPartGap = 6;          // .slp-nsg: 6 px between a group's parts
        constexpr int kGroupGap = 10;        // .slp-nsbody: 16 px between groups (the flow's 6, and these 10)
        constexpr int kJumpBoxWidth = 150;
        constexpr float kAsleep = 0.45f;     // nothing selected: the controls are faded and take no clicks (.slp-idle)

        // The prototype's words (sl-ui-panels.js, and SLModel.ENGINE_NAMES).
        const char* const kEngineNames[] = { "Tape", "Voice", "Cloud" };
        const char* const kMotionNames[] = { "Forward", "Backward", "Bounce", "Mirror", "Freeze", "Scatter" };
        const char* const kStartNames[] = { "a spot in the part", "carry on", "with the song", "random" };
        constexpr const char* kEmptyText = "Select notes on the roll to give them their own settings. Notes without their own settings follow "
                                           "the Sample tab.";
        constexpr const char* kClearTitle = "Clear the own settings of the selected notes (engine, lock, motion, tape, each grain backwards, jump)";
        constexpr const char* kPathTitle = "The line is where in the Region the note is: flat freezes, rising plays forwards, a step jumps";
        constexpr const char* kRerollTitle = "New random start spots (they stay until you re-roll again)";
        constexpr const char* kRestartTitle = "This note starts its sound again even when a slide joins it to the note before";
        constexpr const char* kSpotTitle = "Where in the Region the notes start";
        constexpr const char* kRangeTitle = "How far past the spot a random start may land";
        constexpr const char* kJumpTitle = "How often Scatter jumps to a new spot";
        constexpr const char* kRandomOnly = "Works with a Random start";
        constexpr const char* kSpotOnly = "Works with a start at a spot in the part, or a Random one";

        juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
        juce::String dot() { return juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " "; }
        std::string onOff (bool on) { return on ? "on" : "off"; }
        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        /** A setting that follows the instrument (a null), and one the notes hold themselves. */
        template <class T>
        model::Choice<T> follow()
        {
            model::Choice<T> c;
            c.emplace();
            return c;
        }

        template <class T>
        model::Choice<T> own (T v)
        {
            model::Choice<T> c;
            c.emplace (v);
            return c;
        }

        /** sl-ui-panels.js triIn: 0 Follow, 1 On, 2 Off. */
        model::Choice<bool> triIn (int index)
        {
            return index == 0 ? follow<bool>() : own (index == 1);
        }

        const char* triWord (int index) { return index == 0 ? "follow" : index == 1 ? "on" : "off"; }

        /** sl-ui-panels.js triOut as a segment: -1 mixed, 0 Follow, 1 On, 2 Off. */
        int triIndex (const model::Mixed<std::optional<bool>>& m)
        {
            if (m.mixed)
                return -1;
            return ! m.value.has_value() ? 0 : *m.value ? 1 : 2;
        }

        /** sl-ui-panels.js fmtTime: "0:15.15". */
        juce::String fmtTime (double s)
        {
            s = std::max (0.0, s);
            const double m = std::floor (s / 60.0), r = s - m * 60.0;
            return str (core::jsNumberToString (m)) + ":" + (r < 10.0 ? "0" : "") + str (core::jsToFixed (r, 2));
        }

        juce::String rounded (double v) { return str (core::jsNumberToString (core::jsRound (v))); }
    }

    //==============================================================================================
    /** The "mixed" tag (.slp-mixed): 10.5 px bold in the warning chip's colours, a round pill. */
    class NoteSettingsTab::MixedTag final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MixedTag (const Theme& t) : theme (t)
        {
            setTooltip ("The selected notes differ here");
        }

        int preferredWidth() const { return (int) std::ceil (Theme::textWidth (font(), "mixed")) + 12; }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 16.0f);
            g.setColour (theme (Tok::chipWarnBg));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (theme (Tok::chipWarnTxt));
            g.setFont (font());
            g.drawText ("mixed", r, juce::Justification::centred, false);
        }

    private:
        static juce::Font font() { return Theme::px (10.5f, true); }
        const Theme& theme;
    };

    /** One group (.slp-nsg): its caption in small capitals, its controls, and the "mixed" tag while the selected notes
        differ. Faded (half strength) when it does nothing for them, with the reason as its tooltip. */
    class NoteSettingsTab::GroupBox final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        GroupBox (const Theme& t, const juce::String& captionText)
            : caption (t, captionText, Caption::Style::label), tag (t)
        {
            if (captionText.isNotEmpty())
                addAndMakeVisible (caption);
            addChildComponent (tag);
        }

        template <class C>
        C& add (std::unique_ptr<C> c, std::function<int()> width)
        {
            auto& ref = *c;
            addAndMakeVisible (ref);
            parts.push_back ({ std::move (c), std::move (width) });
            return ref;
        }

        int preferredWidth() const
        {
            int w = 0;
            if (caption.getText().isNotEmpty())
                w += caption.preferredWidth() + kPartGap;
            for (const auto& p : parts)
                w += p.width() + kPartGap;
            if (tag.isVisible())
                w += tag.preferredWidth() + kPartGap;
            return w - kPartGap + kGroupGap;
        }

        void setMixed (bool mixed)
        {
            if (mixed != tag.isVisible())
                tag.setVisible (mixed);
        }

        bool isMixed() const noexcept { return tag.isVisible(); }

        void setFaded (bool shouldFade, const juce::String& why)
        {
            faded = shouldFade;
            reason = shouldFade ? why : juce::String();
            setTooltip (reason);
            updateAlpha();
        }

        bool isFaded() const noexcept { return faded; }
        const juce::String& getReason() const noexcept { return reason; }

        void setAsleep (bool shouldSleep)
        {
            asleep = shouldSleep;
            setEnabled (! shouldSleep);
            updateAlpha();
        }

        void resized() override
        {
            const int h = getHeight();
            int x = 0;
            if (caption.getText().isNotEmpty())
            {
                const int w = caption.preferredWidth();
                caption.setBounds (x, (h - 18) / 2, w, 18);
                x += w + kPartGap;
            }
            for (auto& p : parts)
            {
                const int w = p.width();
                p.component->setBounds (x, 0, w, h);
                x += w + kPartGap;
            }
            if (tag.isVisible())
                tag.setBounds (x, 0, tag.preferredWidth(), h);
        }

    private:
        void updateAlpha() { setAlpha (asleep ? kAsleep : faded ? 0.5f : 1.0f); }

        struct Part
        {
            std::unique_ptr<juce::Component> component;
            std::function<int()> width;
        };

        Caption caption;
        MixedTag tag;
        std::vector<Part> parts;
        bool faded = false, asleep = false;
        juce::String reason;
    };

    /** The first line (.slp-nshead): how many notes are selected, in bold, then whether they follow the Sample tab; or
        how to use the tab. */
    class NoteSettingsTab::HeadLine final : public juce::Component
    {
    public:
        explicit HeadLine (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }

        void setTexts (const juce::String& boldPart, const juce::String& restPart, bool asNote)
        {
            if (boldPart == bold && restPart == rest && asNote == note)
                return;
            bold = boldPart;
            rest = restPart;
            note = asNote;
            repaint();
        }

        juce::String text() const { return bold + rest; }

        int preferredWidth() const
        {
            return (int) std::ceil (Theme::textWidth (boldFont(), bold) + Theme::textWidth (restFont(), rest)) + 2;
        }

        void paint (juce::Graphics& g) override
        {
            const float h = (float) getHeight(), bw = Theme::textWidth (boldFont(), bold);
            g.setColour (theme (Tok::txt));
            g.setFont (boldFont());
            g.drawText (bold, juce::Rectangle<float> (0.0f, 0.0f, bw + 1.0f, h), juce::Justification::centredLeft, false);
            g.setColour (theme (Tok::dim));
            g.setFont (restFont());
            g.drawText (rest, juce::Rectangle<float> (bw, 0.0f, juce::jmax (0.0f, (float) getWidth() - bw), h), juce::Justification::centredLeft, true);
        }

    private:
        juce::Font boldFont() const { return Theme::px (12.0f, true); }
        juce::Font restFont() const { return Theme::px (note ? 11.5f : 12.0f); }   // the empty tab's sentence is a .slp-note

        const Theme& theme;
        juce::String bold, rest;
        bool note = false;
    };

    //==============================================================================================
    NoteSettingsTab::NoteSettingsTab (const Theme& t, juce::AudioProcessorValueTreeState& s, roll::RollView& r, StrandLockProcessor& p)
        : TabPanel (t, s), roll (r), processor (p)
    {
        // the first line: how many notes, whether they follow the Sample tab, and Follow the Sample tab for all
        head = &add (std::make_unique<HeadLine> (t), 200, 18);
        {
            auto c = std::make_unique<FlatButton> (t, "Follow the Sample tab for all");
            c->onClick = [this]
            {
                model::SamplePatch patch;
                patch.engine = follow<core::Engine>();
                patch.lock = follow<bool>();
                patch.motion = follow<core::Motion>();
                patch.tape = follow<bool>();
                patch.rev = follow<bool>();
                patch.jump = follow<core::RateId>();
                setNS (patch, "Note settings: follow the Sample tab");
            };
            const int w = c->preferredWidth();
            clear = &add (std::move (c), w, kControlHeight);
            setTitle (*clear, kClearTitle);
        }

        // Engine, Lock, Motion, Travel
        newRow();
        engine = &segGroup (Group::engine, "Engine", { "Follow", "Tape", "Voice", "Cloud" },
                            { {}, "Speed and pitch move together, like tape", "Slowed, frozen or backwards, it stays in tune and keeps its character",
                              "Clouds of grains that work on anything" },
                            [this] (int i)
                            {
                                model::SamplePatch patch;
                                patch.engine = i == 0 ? follow<core::Engine>() : own ((core::Engine) (i - 1));
                                setNS (patch, std::string ("Note engine: ") + (i == 0 ? "follow" : kEngineNames[i - 1]));
                            });
        lock = &segGroup (Group::lock, "Lock", { "Follow", "On", "Off" }, { {}, "Every grab loops at exactly the note you play", {} },
                          [this] (int i)
                          {
                              model::SamplePatch patch;
                              patch.lock = triIn (i);
                              setNS (patch, std::string ("Note lock: ") + triWord (i));
                          });
        motion = &segGroup (Group::motion, "Motion", { "Follow", "Forward", "Backward", "Bounce", "Mirror", "Freeze", "Scatter" }, {},
                            [this] (int i) { motionPicked (i); });
        travel = &segGroup (Group::travel, "Travel", { "Speed", "Path" },
                            { "Above the middle line plays forwards, below plays backwards, on the line stops", kPathTitle },
                            [this] (int i) { travelPicked (i); });

        // Start: where the notes start, the spot, the random range and Re-roll
        start = &segGroup (Group::start, "Start", { "A spot in the part", "Carry on", "With the song", "Random" },
                           { "Starts at the spot below (or click the waveform)", "Carries on from where the note before it was",
                             "The sound runs under the song like a tape: the note plays what is there at its time",
                             "A random spot within the range below. It stays the same every time until you Re-roll." },
                           [this] (int i)
                           {
                               model::StartPatch sp;
                               sp.mode = (model::StartMode) i;
                               model::SamplePatch patch;
                               patch.start = sp;
                               setNS (patch, std::string ("Start: ") + kStartNames[i]);
                           });
        {
            auto sl = std::make_unique<roll::ToolSlider> (t, "Spot", 0.0, 1.0, 0.001, 0.0,
                                                          [this] (double v) { return spotMixed ? juce::String ("mixed") : spotText (v); });
            const auto patchOf = [] (double v)
            {
                model::StartPatch sp;
                sp.pos = v;
                model::SamplePatch patch;
                patch.start = sp;
                return patch;
            };
            sl->onInput = [this, patchOf] (double v) { slide ("ns:pos", "Start spot", patchOf (v), false); };
            sl->onChange = [this, patchOf] (double v) { slide ("ns:pos", "Start spot", patchOf (v), true); };
            const int w = sl->preferredWidthFor ("100% (10:00.00)");
            spot = &add (std::move (sl), w, kControlHeight);
            setTitle (*spot, kSpotTitle);
        }
        {
            auto sl = std::make_unique<roll::ToolSlider> (t, "Range", 0.0, 1.0, 0.005, 0.25,
                                                          [this] (double v) { return rangeMixed ? juce::String ("mixed") : rounded (v * 100.0) + "% of the Region"; });
            const auto patchOf = [] (double v)
            {
                model::StartPatch sp;
                sp.range = v;
                model::SamplePatch patch;
                patch.start = sp;
                return patch;
            };
            sl->onInput = [this, patchOf] (double v) { slide ("ns:range", "Random range", patchOf (v), false); };
            sl->onChange = [this, patchOf] (double v) { slide ("ns:range", "Random range", patchOf (v), true); };
            const int w = sl->preferredWidthFor ("100% of the Region");
            range = &add (std::move (sl), w, kControlHeight);
            setTitle (*range, kRangeTitle);
        }
        {
            auto b = std::make_unique<FlatButton> (t, "Re-roll");
            b->onClick = [this] { rerollClicked(); };
            const int w = b->preferredWidth();
            reroll = &add (std::move (b), w, kControlHeight);
            setTitle (*reroll, kRerollTitle);
        }

        // Tape, Each grain backwards, Jump every, Restart sample here, Velocity
        tape = &segGroup (Group::tape, "Tape", { "Follow", "On", "Off" }, {},
                          [this] (int i)
                          {
                              model::SamplePatch patch;
                              patch.tape = triIn (i);
                              setNS (patch, std::string ("Note tape: ") + triWord (i));
                          });
        rev = &segGroup (Group::rev, "Each grain backwards", { "Follow", "On", "Off" }, {},
                         [this] (int i)
                         {
                             model::SamplePatch patch;
                             patch.rev = triIn (i);
                             setNS (patch, std::string ("Each grain backwards: ") + triWord (i));
                         });
        {
            auto box = std::make_unique<GroupBox> (t, "Jump every");
            jump = &box->add (std::make_unique<juce::ComboBox>(), [] { return kJumpBoxWidth; });
            jump->setTooltip (kJumpTitle);
            jump->setTitle ("Jump every");
            fillJump (false);
            jump->onChange = [this] { jumpPicked(); };
            const int w = box->preferredWidth();
            groups[(std::size_t) Group::jump] = &add (std::move (box), w, kControlHeight);
        }
        {
            auto box = std::make_unique<GroupBox> (t, juce::String());
            restart = &box->add (std::make_unique<FlatButton> (t, "Restart sample here"), [this] { return restart->preferredWidth(); });
            restart->onClick = [this] { restartClicked(); };
            setTitle (*restart, kRestartTitle);
            const int w = box->preferredWidth();
            groups[(std::size_t) Group::restart] = &add (std::move (box), w, kControlHeight);
        }
        {
            auto box = std::make_unique<GroupBox> (t, juce::String());
            auto sl = std::make_unique<roll::ToolSlider> (t, "Velocity", 1.0, 127.0, 1.0, 102.0,
                                                          [this] (double v) { return velMixed ? juce::String ("mixed") : rounded (v); });
            const int sw = sl->preferredWidthFor ("127");
            velocity = &box->add (std::move (sl), [sw] { return sw; });
            velocity->onInput = [this] (double v)
            {
                model::SamplePatch patch;
                patch.vel = v / 127.0;
                slide ("ns:vel", "Velocity", patch, false);
            };
            velocity->onChange = [this] (double v)
            {
                model::SamplePatch patch;
                patch.vel = v / 127.0;
                slide ("ns:vel", "Velocity", patch, true);
            };
            const int w = box->preferredWidth();
            groups[(std::size_t) Group::velocity] = &add (std::move (box), w, kControlHeight);
        }

        // No control takes the keyboard (M0's rule: the keys are Cubase's unless Keys is on).
        roll::noKeyboardFocus (*this);
        sync (true);
    }

    NoteSettingsTab::~NoteSettingsTab()
    {
        hovers.clear();
    }

    roll::SegButtons& NoteSettingsTab::segGroup (Group g, const juce::String& captionText, const juce::StringArray& labels,
                                                 const std::vector<juce::String>& buttonTitles, std::function<void (int)> picked)
    {
        auto box = std::make_unique<GroupBox> (theme, captionText);
        auto seg = std::make_unique<roll::SegButtons> (theme, labels, 0);
        auto* raw = seg.get();
        box->add (std::move (seg), [raw] { return raw->preferredWidth(); });
        for (int i = 0; i < labels.size(); ++i)
            setTitle (raw->getButton (i), (std::size_t) i < buttonTitles.size() ? buttonTitles[(std::size_t) i] : juce::String());
        raw->onPick = std::move (picked);
        const int w = box->preferredWidth();
        groups[(std::size_t) g] = &add (std::move (box), w, kControlHeight);
        return *raw;
    }

    void NoteSettingsTab::setTitle (juce::Component& c, const juce::String& title)
    {
        // sl-ui-panels.js button (): a control's title is its tooltip, and the hint line while the mouse is on it
        titles[&c] = title;
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (&c))
            tip->setTooltip (title);
        auto* target = &c;
        hovers.push_back (std::make_unique<HoverWatch> (c, [this, target] (bool over)
        {
            const auto text = titleOf (*target);
            if (text.isEmpty())
                return;
            roll.showHint (over ? text : juce::String());
        }));
    }

    juce::String NoteSettingsTab::titleOf (const juce::Component& c) const
    {
        const auto it = titles.find (&c);
        return it != titles.end() ? it->second : juce::String();
    }

    void NoteSettingsTab::fillJump (bool withMixed)
    {
        // sl-ui-panels.js NS.jump: Follow, the 21 rates, and "mixed" (greyed) while the selected notes differ
        jump->clear (juce::dontSendNotification);
        jump->addItem ("Follow", 1);
        for (int i = 0; i < core::kNumRates; ++i)
            jump->addItem (core::rateLabel ((core::RateId) i), i + 2);
        if (withMixed)
        {
            jump->addItem ("mixed", jumpMixedId);
            jump->setItemEnabled (jumpMixedId, false);
        }
        jumpHasMixed = withMixed;
    }

    double NoteSettingsTab::soundSeconds() const
    {
        const auto sound = processor.getCurrentSound();
        return sound != nullptr && sound->seconds() > 0.0 ? sound->seconds() : 6.0;   // sl-ui.js sampleSeconds
    }

    juce::String NoteSettingsTab::spotText (double v) const
    {
        // sl-ui-panels.js posFmt: "40% (0:02.40)", the time in the sound
        const auto I = processor.readInst();
        const auto& R = I.region;
        return rounded (v * 100.0) + "% (" + fmtTime ((R.start + v * (R.end - R.start)) * soundSeconds()) + ")";
    }

    //==============================================================================================
    // What the controls do (sl-ui-panels.js setNS, setMotion, setTravelKind and the sliders' liveRun).

    void NoteSettingsTab::setNS (const model::SamplePatch& patch, const std::string& label)
    {
        auto& tools = roll.getTools();
        const auto clip = tools.clip();
        const auto ids = tools.ids();
        if (clip == nullptr || ids.empty())
            return;
        tools.act (model::setNoteSample (*clip, ids, patch), label);
        edited();
    }

    void NoteSettingsTab::edited()
    {
        roll.edited();   // the roll refreshes, and MainView shows the result in the rows around it (this tab too)
        sync (true);
    }

    void NoteSettingsTab::motionPicked (int i)
    {
        auto& tools = roll.getTools();
        const auto clip = tools.clip();
        const auto ids = tools.ids();
        if (clip == nullptr || ids.empty())
            return;
        model::SamplePatch patch;
        patch.motion = i == 0 ? follow<core::Motion>() : own ((core::Motion) (i - 1));
        model::Clip next = model::setNoteSample (*clip, ids, patch);

        // Mirror with 1 head gets 2 heads in the same click (S2): every selected note with no Heads generator whose
        // Heads line never reaches 1.5 heads
        int extra = 0;
        if (i == 1 + (int) core::Motion::mirror)
        {
            const auto I = processor.readInst();
            std::map<std::string, model::NodeList> heads;
            for (const auto& n : roll::RollGeometry::selectedNotes (*clip, tools.rollState()))
            {
                double most = -std::numeric_limits<double>::infinity();
                for (const auto& q : n->lane (core::LaneId::heads))
                    most = std::max (most, q.v);
                if (! n->genOf (core::LaneId::heads).has_value() && I.heads + (most - 0.5) * 14.0 < 1.5)
                {
                    heads[n->id] = model::flat (clampD (0.5 + (2.0 - I.heads) / 14.0, 0.0, 1.0));
                    ++extra;
                }
            }
            if (extra > 0)
                next = model::setLaneNodesMany (next, core::LaneId::heads, heads);
        }
        tools.act (std::move (next), std::string ("Note motion: ") + (i == 0 ? "follow" : kMotionNames[i - 1]));
        edited();
        if (extra > 0)
            tools.toast ("Mirror needs 2 heads: Heads set to 2 on " + roll::nNotes (extra), 2400);
    }

    void NoteSettingsTab::travelPicked (int i)
    {
        // The Travel line means a speed in one kind and a place in the Region in the other, so a flat line that switches
        // kind starts as the new kind's playing line: Path = Fit (1x through the Region), Speed = 1x forwards. A line the
        // person shaped stays as it is (the toast says how it now reads). One undo step with the switch.
        auto& tools = roll.getTools();
        const auto clip = tools.clip();
        const auto ids = tools.ids();
        if (clip == nullptr || ids.empty())
            return;
        const auto v = i == 1 ? model::TravelKind::path : model::TravelKind::speed;
        const auto flatLine = [] (const model::Note& n)
        {
            if (n.genOf (core::LaneId::travel).has_value())
                return false;
            const auto& line = n.lane (core::LaneId::travel);
            for (const auto& q : line)
                if (std::abs (q.v - line.front().v) >= 1e-6)
                    return false;
            return true;
        };
        std::vector<std::string> reset;
        int changing = 0;
        for (const auto& n : roll::RollGeometry::selectedNotes (*clip, tools.rollState()))
            if (n->sample.travel != v)
            {
                ++changing;
                if (flatLine (*n))
                    reset.push_back (n->id);
            }
        model::SamplePatch patch;
        patch.travel = v;
        model::Clip next = model::setNoteSample (*clip, ids, patch);
        if (! reset.empty())
            if (auto shaped = model::applyShape (next, reset, core::LaneId::travel,
                                                 v == model::TravelKind::path ? model::ShapeKey::fit : model::ShapeKey::reset,
                                                 model::ShapeScope::each, tools.shapeOpts()))
                next = std::move (*shaped);
        tools.act (std::move (next), std::string ("Travel: ") + (v == model::TravelKind::path ? "Path" : "Speed"));
        edited();
        if (changing == 0)
            return;
        const int kept = changing - (int) reset.size();
        const bool any = ! reset.empty();
        std::string text;
        if (v == model::TravelKind::path)
            text = std::string (any ? "Path: the Travel line now plays through the Region at 1x (Fit). Flat = hold still, a step = jump." : "")
                 + (kept > 0 ? std::string (any ? " " : "") + "Path: the line you drew now says where in the Region "
                                   + (kept == 1 ? "the note is" : "the notes are") + " (top = the end)."
                             : std::string());
        else
            text = std::string (any ? "Speed: the Travel line is back to 1x forwards." : "")
                 + (kept > 0 ? std::string (any ? " " : "") + "Speed: above the middle line plays forwards, below plays backwards." : std::string());
        tools.toast (text, 4500);
    }

    void NoteSettingsTab::rerollClicked()
    {
        auto& tools = roll.getTools();
        const auto clip = tools.clip();
        const auto ids = tools.ids();
        if (clip == nullptr || ids.empty())
            return;
        tools.act (model::editNotes (*clip, ids, [] (model::Note& n, const model::Note&)
                                     {
                                         if (n.sample.start.mode == model::StartMode::random)
                                             n.sample.start.seed = core::toInt32 ((double) n.sample.start.seed + 1.0);
                                     }),
                   "Re-roll random start");
        edited();
    }

    void NoteSettingsTab::restartClicked()
    {
        const auto clip = roll.getTools().clip();
        const auto ids = roll.getTools().ids();
        if (clip == nullptr || ids.empty())
            return;
        const auto mx = model::getMixed (*clip, ids);
        const bool on = ! (mx.has_value() && ! mx->restart.mixed && mx->restart.value);
        model::SamplePatch patch;
        patch.restart = on;
        setNS (patch, "Restart sample here: " + onOff (on));
    }

    void NoteSettingsTab::jumpPicked()
    {
        const int id = jump->getSelectedId();
        if (id == jumpMixedId || id < 1)
            return;
        model::SamplePatch patch;
        if (id == 1)
        {
            patch.jump = follow<core::RateId>();
            setNS (patch, "Jump every : follow");
            return;
        }
        const auto rate = (core::RateId) (id - 2);
        patch.jump = own (rate);
        setNS (patch, std::string ("Jump every ") + core::rateLabel (rate));
    }

    void NoteSettingsTab::slide (const std::string& key, const std::string& label, const model::SamplePatch& patch, bool final)
    {
        auto& tools = roll.getTools();
        const auto ids = tools.ids();
        if (ids.empty())
            return;
        tools.liveSession (key, label, [ids, patch] (const model::Clip& base) { return model::setNoteSample (base, ids, patch); }, final);
        edited();
    }

    //==============================================================================================
    // What the tab shows (sl-ui-panels.js syncNoteSettings).

    juce::String NoteSettingsTab::getHeadText() const
    {
        return head->text();
    }

    bool NoteSettingsTab::isMixed (Group g) const noexcept
    {
        return group (g).isMixed();
    }

    bool NoteSettingsTab::isFaded (Group g) const noexcept
    {
        return group (g).isFaded();
    }

    juce::String NoteSettingsTab::getFadeReason (Group g) const
    {
        return group (g).getReason();
    }

    roll::SegButtons& NoteSettingsTab::getSegments (Group g) const noexcept
    {
        switch (g)
        {
            case Group::lock:   return *lock;
            case Group::motion: return *motion;
            case Group::travel: return *travel;
            case Group::start:  return *start;
            case Group::tape:   return *tape;
            case Group::rev:    return *rev;
            case Group::engine:
            case Group::jump:
            case Group::restart:
            case Group::velocity:
                break;
        }
        return *engine;
    }

    void NoteSettingsTab::fadeGroup (Group g, bool faded, const juce::String& why)
    {
        auto& box = group (g);
        box.setFaded (faded, why);
        // a button with no text of its own shows the group's reason (an HTML title on the group does)
        if (g == Group::engine || g == Group::lock || g == Group::motion || g == Group::travel || g == Group::start || g == Group::tape
            || g == Group::rev)
        {
            auto& seg = getSegments (g);
            for (int i = 0; i < seg.getNumChildComponents(); ++i)   // its children are its buttons
            {
                auto& b = seg.getButton (i);
                const auto own = titleOf (b);
                b.setTooltip (own.isNotEmpty() ? own : faded ? why : juce::String());
            }
        }
    }

    bool NoteSettingsTab::fitGroup (Group g)
    {
        auto& box = group (g);
        box.resized();
        return resizeItem (box, box.preferredWidth());
    }

    void NoteSettingsTab::sync (bool force)
    {
        auto& tools = roll.getTools();
        const auto clip = tools.clip();
        const auto ids = tools.ids();
        const auto st = tools.rollState();
        const auto I = processor.readInst();
        const double secs = soundSeconds();
        const auto sameInst = [&I] (const core::Inst& o)
        {
            return o.engine == I.engine && o.lock == I.lock && o.motion == I.motion && o.tape == I.tape && o.rev == I.rev && o.jump == I.jump
                && juce::exactlyEqual (o.region.start, I.region.start) && juce::exactlyEqual (o.region.end, I.region.end);
        };
        if (! force && seen.valid && seen.clip == clip && seen.ids == ids && seen.primary == st.primary && sameInst (seen.inst, I)
            && juce::exactlyEqual (seen.seconds, secs))
            return;
        seen.clip = clip;
        seen.ids = ids;
        seen.primary = st.primary;
        seen.inst = I;
        seen.seconds = secs;
        seen.valid = true;

        bool layout = false;
        const auto mx = clip != nullptr && ! ids.empty() ? model::getMixed (*clip, ids) : std::nullopt;
        asleep = ! mx.has_value();

        // nothing selected: the sentence, and every control asleep where it is
        if (! mx)
        {
            head->setTexts ({}, kEmptyText, true);
            layout = resizeItem (*head, head->preferredWidth()) || layout;
            clear->setEnabled (false);
            for (auto* g : groups)
            {
                g->setAsleep (true);
                g->setMixed (false);
            }
            for (auto* seg : { engine, lock, motion, travel, start, tape, rev })
                seg->setSelected (-1);
            for (juce::Component* c : { (juce::Component*) spot, (juce::Component*) range, (juce::Component*) reroll })
            {
                c->setEnabled (false);
                c->setAlpha (kAsleep);
            }
            spotMixed = rangeMixed = velMixed = false;
            if (jumpHasMixed)
                fillJump (false);
            jump->setSelectedId (1, juce::dontSendNotification);
            restart->setToggleState (false, juce::dontSendNotification);
            for (int g = 0; g < numGroups; ++g)
                layout = fitGroup ((Group) g) || layout;
            if (layout)
                layoutChanged();
            repaint();
            return;
        }

        // the selected notes in time order, how many have settings of their own, the primary one
        const auto notes = roll::RollGeometry::selectedNotes (*clip, st);
        int ownCount = 0;
        model::NotePtr P;
        for (const auto& n : notes)
        {
            ownCount += roll::hasOwnSettings (*n) ? 1 : 0;
            if (n->id == st.primary)
                P = n;
        }
        if (P == nullptr && ! notes.empty())
            P = notes.front();
        const int count = (int) ids.size();
        head->setTexts (str (roll::nNotes (count)) + " selected",
                        ownCount > 0 ? dot() + juce::String (ownCount) + (ownCount == 1 ? " has its" : " have their")
                                           + " own settings (a dot on the roll marks them)"
                                     : dot() + "following the Sample tab",
                        false);
        layout = resizeItem (*head, head->preferredWidth()) || layout;
        clear->setEnabled (ownCount > 0);
        for (auto* g : groups)
            g->setAsleep (false);

        std::set<int> engines, motions;
        for (const auto& n : notes)
        {
            engines.insert ((int) n->sample.engine.value_or (I.engine));
            motions.insert ((int) n->sample.motion.value_or (I.motion));
        }
        const bool allTape = engines.size() == 1 && *engines.begin() == (int) core::Engine::tape;

        // the segments: the shared setting lit, none while the notes differ (and their "mixed" tag), Follow naming the
        // Sample tab's own setting
        const auto show = [this] (Group g, roll::SegButtons& seg, int index, const juce::String& followLabel)
        {
            seg.setSelected (index);
            if (followLabel.isNotEmpty() && seg.getButton (0).getButtonText() != followLabel)
            {
                seg.getButton (0).setButtonText (followLabel);
                seg.resized();
            }
            group (g).setMixed (index < 0);
        };
        const auto onOffWord = [] (bool b) { return juce::String (b ? "on" : "off"); };
        show (Group::engine, *engine, mx->engine.mixed ? -1 : mx->engine.value ? 1 + (int) *mx->engine.value : 0,
              "Follow (" + juce::String (kEngineNames[(int) I.engine]) + ")");
        show (Group::lock, *lock, triIndex (mx->lock), "Follow (" + onOffWord (I.lock) + ")");
        show (Group::motion, *motion, mx->motion.mixed ? -1 : mx->motion.value ? 1 + (int) *mx->motion.value : 0,
              "Follow (" + juce::String (kMotionNames[(int) I.motion]) + ")");
        show (Group::travel, *travel, mx->travel.mixed ? -1 : mx->travel.value == model::TravelKind::path ? 1 : 0, {});
        show (Group::start, *start, mx->startMode.mixed ? -1 : (int) mx->startMode.value, {});
        show (Group::tape, *tape, triIndex (mx->tape), "Follow (" + onOffWord (I.tape) + ")");
        show (Group::rev, *rev, triIndex (mx->rev), "Follow (" + onOffWord (I.rev) + ")");

        // what does nothing for these notes is faded and says why
        fadeGroup (Group::lock, engines.count ((int) core::Engine::cloud) == 0, "Lock only works in the Cloud engine");
        fadeGroup (Group::tape, allTape, "The Tape engine always ties pitch to speed");
        fadeGroup (Group::rev, allTape, "The Tape engine has no grains");
        fadeGroup (Group::jump, motions.count ((int) core::Motion::scatter) == 0, "Jump every only matters with Motion Scatter");
        {
            auto& freeze = motion->getButton (1 + (int) core::Motion::freeze);
            freeze.setAlpha (allTape ? 0.5f : 1.0f);
            titles[&freeze] = allTape ? "Tape cannot freeze: it plays Forward" : "";
            freeze.setTooltip (titles[&freeze]);
            auto& path = travel->getButton (1);
            path.setAlpha (allTape ? 0.5f : 1.0f);
            titles[&path] = allTape ? "Tape plays Path as Speed at 1x" : kPathTitle;
            path.setTooltip (titles[&path]);
        }

        // Start: the spot (a spot in the part, random, or mixed), the random range and Re-roll (random)
        {
            const bool modeMixed = mx->startMode.mixed;
            const auto mode = mx->startMode.value;
            const bool spotWorks = modeMixed || mode == model::StartMode::region || mode == model::StartMode::random;
            const bool randomOnly = ! modeMixed && mode == model::StartMode::random;
            spot->setEnabled (spotWorks);
            spot->setAlpha (spotWorks ? 1.0f : 0.5f);
            titles[spot] = spotWorks ? kSpotTitle : kSpotOnly;
            for (juce::Component* c : { (juce::Component*) range, (juce::Component*) reroll })
            {
                c->setEnabled (randomOnly);
                c->setAlpha (randomOnly ? 1.0f : 0.5f);
            }
            titles[range] = randomOnly ? kRangeTitle : kRandomOnly;
            titles[reroll] = randomOnly ? kRerollTitle : kRandomOnly;
            reroll->setTooltip (titles[reroll]);
            spotMixed = mx->startPos.mixed;
            rangeMixed = mx->startRange.mixed;
            spot->setValue (mx->startPos.mixed ? P->sample.start.pos : mx->startPos.value);
            range->setValue (mx->startRange.mixed ? P->sample.start.range : mx->startRange.value);
            spot->repaint();
            range->repaint();
        }

        // Jump every: Follow names the Sample tab's rate; "mixed" while the notes differ
        {
            jump->changeItemText (1, "Follow (" + juce::String (core::rateLabel (I.jump)) + ")");
            const bool jumpMixed = mx->jump.mixed;
            if (jumpMixed != jumpHasMixed)
            {
                fillJump (jumpMixed);
                jump->changeItemText (1, "Follow (" + juce::String (core::rateLabel (I.jump)) + ")");
            }
            jump->setSelectedId (jumpMixed ? jumpMixedId : mx->jump.value ? 2 + (int) *mx->jump.value : 1, juce::dontSendNotification);
            group (Group::jump).setMixed (jumpMixed);
        }

        // Restart sample here, and Velocity (its slider reads "mixed"; the group shows no tag)
        restart->setToggleState (! mx->restart.mixed && mx->restart.value, juce::dontSendNotification);
        {
            const juce::String text = juce::String ("Restart sample here") + (mx->restart.mixed ? " (some)" : "");
            if (restart->getButtonText() != text)
                restart->setButtonText (text);
        }
        velMixed = mx->vel.mixed;
        velocity->setValue ((double) core::jsRound ((mx->vel.mixed ? P->vel : mx->vel.value) * 127.0));
        velocity->repaint();

        for (int g = 0; g < numGroups; ++g)
            layout = fitGroup ((Group) g) || layout;
        if (layout)
            layoutChanged();
        repaint();
    }
}
