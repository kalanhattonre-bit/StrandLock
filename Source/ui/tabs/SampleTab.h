#pragma once

// The Sample tab (PLUGIN-PLAN 4 M6, 7.8 T1 and T3; sl-ui-sample.js buildSampleTab and buildRecipes), in the prototype's
// rows:
//   Engine (Tape, Voice, Cloud) and Lock; Motion; Jump every; Re-roll; Preview (T3)
//   Speed, Heads, Spread, Grain, Spray, Drift, Pitch | Refresh time, Skip | Region start, Region end, Loop fade
//   Refresh (Smooth, Free, Synced), Every and Lock to the beat; Tape; Each grain backwards; Head tune; At the end
//   a line, then Recipes 1 to 8 and what they act on; Dice, what it keeps, Unpin all (T3)
// Every control does what the prototype's does, with its words (labels, tooltips, the hint line, the toasts). A control
// that does nothing with the settings now is faded and says why (rules::fades: the instrument and every note's own mix of
// settings). The Grain knob shows the engine's grain setting (Voice cycles, Cloud ms, Lock waves) and is off in Tape
// when no note plays grains.
//
// Decision 1: each control changes exactly its own parameter (Lock from another engine also picks Cloud, and Mirror with
// too few heads also sets Heads, as the prototype's do), and each finished gesture is one undo step with the prototype's
// label. Decision 2: drags snap as the q functions do; typed values are kept as typed. Controls are bound the M2 way (the
// editor's 30 Hz timer, no attachment). The Region knobs and Loop fade are M0's, kept so the Region and the loop
// crossfade have a control here (the prototype has the Region on the waveform only, and no crossfade control); each of
// their gestures is one undo step too.
//
// T3: a recipe (model::applyRecipe) sets every Sample setting and, for some, the Sample lines of the selected notes (every
// note when none is selected), in one undo step; the mouse on a recipe draws its notes dashed on the roll (the roll's
// ghosts) and says what it does in the hint line. Dice (model::dice) rolls the Sample settings, one undo step, and leaves
// the pinned ones alone: a small pin beside each setting's name, kept per person in UserSettings (decision 4). Preview
// (decision 3) is a switch: while Cubase is stopped StrandLock plays its clip at Cubase's tempo; if no block arrives for
// 250 ms while it is on, a notice at the top of the tab says that Cubase has paused StrandLock and names the Cubase
// option that does it. Message thread only.

#include "SampleControls.h"
#include "TabPanel.h"

#include "core/Inst.h"
#include "model/Clip.h"
#include "ui/SampleTabRules.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class StrandLockProcessor;

namespace sl::ui
{
    namespace roll
    {
        class RollView;
        class HoverHooks;
    }

    /** What a control of the Sample or Macros tab shows now (for the tests: the fixture's controls cases). */
    struct ControlState
    {
        juce::String name;                      // the prototype's: "Engine: Tape", "Lock to the beat", "Speed", "Macro Speed" ...
        bool faded = false;                     // drawn faded (it, or the group around it)
        bool off = false;                       // a knob with nothing to do at all
        juce::String title;                     // its tooltip
        std::optional<juce::String> hint;       // the hint line while the mouse is on it (none: it has no hint)
        std::optional<juce::String> readout;    // a knob's value
    };

    /** The settings the tabs fade by (the instrument and the clip), read again only when they changed: the parameters'
        floats, the INST state the rules read (the seed, the wheels' targets) and the clip version. */
    class SettingsWatch
    {
    public:
        explicit SettingsWatch (StrandLockProcessor& p) : processor (p) {}

        /** Reads the settings; true when they changed since the last call (always with force). */
        bool update (bool force);

        const core::Inst& inst() const noexcept { return current; }
        const model::Clip* clip() const noexcept { return clipNow.get(); }
        const rules::Fades& fades() const noexcept { return fadesNow; }
        std::optional<rules::Kind> grainKind() const noexcept { return grain; }

    private:
        StrandLockProcessor& processor;
        std::array<float, params::kNumParams> values {};
        std::int32_t seed = 0;
        std::int8_t mod1 = 0, mod2 = 0;
        model::ClipPtr clipNow;
        bool read = false;
        core::Inst current;
        rules::Fades fadesNow;
        std::optional<rules::Kind> grain;
    };

    /** A control that can be drawn faded (.42, and .6 under the mouse: sl-ui-sample.css .sls-faded and :hover), and puts
        its text in the hint line while the mouse is on it (sl-ui-sample.js helpOn). */
    class Fader
    {
    public:
        Fader (juce::Component& c, std::function<juce::String()> hintText, std::function<void (const juce::String&)> showHint);

        void setFaded (bool shouldFade);
        bool isFaded() const noexcept { return faded; }

        /** The hint line's text for the control now (empty: it has none). */
        juce::String hint() const { return hintText ? hintText() : juce::String(); }

    private:
        void hoverChanged (bool over);
        void apply();

        juce::Component& target;
        std::function<juce::String()> hintText;
        std::function<void (const juce::String&)> showHint;
        bool faded = false, hovering = false;
        HoverWatch watch;
    };

    /** Preview's paused-host check (plan 7.8 decision 3), on the editor's timer: paused once no block has arrived for
        250 ms while Preview is on. The 250 ms count from the last block seen, or from when Preview went on. */
    class PreviewWatch
    {
    public:
        static constexpr double pausedAfterMs = 250.0;

        /** One look: the time now (ms), the blocks the processor has played so far, Preview's switch. True: paused. */
        bool update (double nowMs, std::uint64_t blocks, bool previewOn) noexcept;
        bool isPaused() const noexcept { return paused; }

    private:
        bool on = false, paused = false;
        std::uint64_t lastBlocks = 0;
        double since = 0.0;
    };

    class SampleTab final : public TabPanel, private juce::ChangeListener
    {
    public:
        SampleTab (const Theme&, juce::AudioProcessorValueTreeState&, StrandLockProcessor&, roll::RollView&);
        ~SampleTab() override;

        /** Shows what each control does with the settings now: faded or not, its tooltip, the Grain knob's engine; and
            Preview's switch, the paused notice and what the recipes act on (MainView: on the editor's timer and after
            every edit; cheap when nothing changed). */
        void sync (bool force = false);

        /** Where the hint line and the toasts go (MainView). */
        std::function<void (const juce::String&)> onHint;
        std::function<void (const juce::String&, int)> onToast;

        /** Lets go of the roll's ghosts (MainView's destructor, while the roll is still there: the tab outlives it). */
        void releaseRoll();

        void resized() override;

        //==========================================================================================
        // Recipes, Dice and the pins (T3), as their buttons do them.

        /** Recipe n (1 .. 8): model::applyRecipe on the selected notes (every note with none selected) and the
            instrument now, as one undo step "Recipe 3 Frozen choir", and the prototype's toast. */
        void applyRecipe (int n);

        /** Dice: model::dice with the pins and a new seed (the prototype's: the time and the count of rolls), one undo
            step "Dice", and the prototype's toast. */
        void rollDice();

        /** A pin's click (a model::kDiceFields name): pinned or not, kept in UserSettings at once. */
        void setPinned (const std::string& field, bool pinned);
        void unpinAll();
        const std::vector<std::string>& getPins() const noexcept { return pins; }

        /** Preview's click: the switch on or off (StrandLockProcessor::setPreview). */
        void togglePreview();

        //==========================================================================================
        // For the tests.
        SampleKnob& getKnob (rules::KnobId id) const;
        SegmentedControl& getEngine() const noexcept { return *engine; }
        LockToggle& getLock() const noexcept { return *lock; }
        SegmentedControl& getMotion() const noexcept { return *motion; }
        ChoiceBox& getJump() const noexcept { return *jump; }
        FlatButton& getReroll() const noexcept { return *reroll; }
        SegmentedControl& getRefreshMode() const noexcept { return *refreshMode; }
        ChoiceBox& getRefreshRate() const noexcept { return *refreshRate; }
        ParamToggle& getBeatLock() const noexcept { return *beatLock; }
        ParamToggle& getTape() const noexcept { return *tape; }
        ParamToggle& getRev() const noexcept { return *rev; }
        SegmentedControl& getHeadTune() const noexcept { return *headTune; }
        SegmentedControl& getAtEnd() const noexcept { return *atEnd; }
        Knob& getRegionKnob (int i) const noexcept { return *regionKnobs[(std::size_t) i]; }   // 0 start, 1 end, 2 loop fade
        const SettingsWatch& getSettings() const noexcept { return settings; }

        RecipeButton& getRecipe (int n) const noexcept { return *recipes[(std::size_t) juce::jlimit (1, 8, n) - 1]; }
        FlatButton& getDice() const noexcept { return *dice; }
        FlatButton& getUnpinAll() const noexcept { return *unpin; }
        FlatButton& getPreview() const noexcept { return *preview; }
        const juce::String& getDiceInfo() const noexcept { return diceInfo->getText(); }
        const juce::String& getRecipeScope() const noexcept { return recipeScope->getText(); }
        const std::vector<std::unique_ptr<PinButton>>& getPinButtons() const noexcept { return pinButtons; }
        PinButton* getPin (const std::string& field) const noexcept;
        roll::HoverHooks* getRecipeHooks() const noexcept { return recipeHooks.get(); }
        std::int32_t getLastDiceSeed() const noexcept { return lastSeed; }
        const NoticeBox& getNotice() const noexcept { return notice; }
        bool isNoticeShown() const noexcept { return notice.isVisible(); }

        /** The clock the paused-host check reads, in ms (the tests set their own). */
        std::function<double()> clock;

        /** Every control as the prototype's fixture names it (ui/readouts.json controls). */
        std::vector<ControlState> controlStates() const;

        /** The undo steps the tab's controls made, in order (their labels; the tests count them). */
        const std::vector<std::string>& getSteps() const noexcept { return steps; }

    private:
        /** A row of segments: its words, each option's sentence and fade rule, the group's rule. */
        struct Seg
        {
            SegmentedControl* control = nullptr;
            juce::String caption;
            std::vector<juce::String> texts, titles;
            std::optional<rules::Rule> groupRule;
            int ruleIndex = -1;                        // the option with a rule of its own (Freeze), or -1
            std::optional<rules::Rule> optionRule;
            std::unique_ptr<Fader> fader;              // the group (its hint is the segments' own)
        };

        Seg& addSeg (params::P p, const juce::String& caption, std::vector<std::array<const char*, 2>> options,
                     std::optional<rules::Rule> groupRule, int ruleIndex, std::optional<rules::Rule> optionRule,
                     const char* pinField = nullptr);
        void segChosen (const Seg&, params::P p, int index);
        void segHovered (Seg&, int index);
        void paintSegFades (Seg&);
        void motionChosen (int index);
        void rerollClicked();
        void hint (const juce::String& text);
        void toast (const juce::String& text, int ms);
        juce::String segHint (const Seg&, int index) const;
        juce::String segTitle (const Seg&, int index) const;
        juce::String rerollTitle() const;
        const rules::Fade* fadeOf (std::optional<rules::Rule> r) const;
        void applyFades();

        // T3
        PinButton& makePin (const char* field);
        ParamToggle& pinnedToggle (params::P p, const juce::String& text, const char* pinField);
        void buildRecipes();
        juce::String recipeHint (int n) const;
        void syncPins();
        void syncRecipeScope();
        void syncPreview();
        void showNotice (bool show);
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        StrandLockProcessor& processor;
        roll::RollView& roll;
        TabActions actions;
        SettingsWatch settings;
        std::vector<std::string> steps;

        SegmentedControl* engine = nullptr;
        LockToggle* lock = nullptr;
        SegmentedControl* motion = nullptr;
        ChoiceBox* jump = nullptr;
        FlatButton* reroll = nullptr;
        std::array<SampleKnob*, 9> knobs {};
        std::array<Knob*, 3> regionKnobs {};
        SegmentedControl* refreshMode = nullptr;
        ChoiceBox* refreshRate = nullptr;
        ParamToggle* beatLock = nullptr;
        ParamToggle* tape = nullptr;
        ParamToggle* rev = nullptr;
        SegmentedControl* headTune = nullptr;
        SegmentedControl* atEnd = nullptr;

        std::vector<std::unique_ptr<Seg>> segs;
        std::unique_ptr<Fader> lockFader, jumpFader, rerollFader, rateFader, beatFader, tapeFader, revFader;

        // T3: the pins, the recipes, Dice, Preview and its notice
        std::vector<std::unique_ptr<PinButton>> pinButtons;
        std::vector<std::string> pins;
        std::array<RecipeButton*, 8> recipes {};
        SubText* recipeScope = nullptr;
        FlatButton* dice = nullptr;
        SubText* diceInfo = nullptr;
        FlatButton* unpin = nullptr;
        FlatButton* preview = nullptr;
        NoticeBox notice;
        std::unique_ptr<roll::HoverHooks> recipeHooks;
        std::unique_ptr<HoverWatch> diceHover, previewHover;
        PreviewWatch previewWatch;
        int previewShown = -1;            // what the button and the notice show: 0 off, 1 on, 2 on but Cubase paused us
        std::uint32_t seenStops = 0;      // the processor's count of Preview stops by Cubase's Play, as last seen
        std::size_t scopeShown = static_cast<std::size_t> (-1);   // the selection size the recipes' scope text says
        int rolls = 0;
        std::int32_t lastSeed = 0;
        int noticeWidth = -1, noticeHeight = 0;   // the width the notice was measured at, and its height there
    };
}
