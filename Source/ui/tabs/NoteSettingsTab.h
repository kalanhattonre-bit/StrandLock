#pragma once

// The Note settings tab (PLUGIN-PLAN 4 M6, 7.8 T2; S7; sl-ui-panels.js buildNoteSettings and syncNoteSettings): the
// selected notes' own Sample settings. Its first line says how many notes are selected and how many have settings of
// their own (a dot on the roll marks them), with "Follow the Sample tab for all"; with nothing selected it says how to
// use the tab. Then Engine, Lock, Motion, Travel (Speed or Path), Start (a spot in the part, carry on, with the song,
// random; the spot, the random range and Re-roll), Tape, Each grain backwards, Jump every, Restart sample here and
// Velocity. "Follow" (with the Sample tab's own setting in brackets) clears a note's own setting. Where the selected
// notes differ, no button is lit and a "mixed" tag says so; a slider reads "mixed". A group that does nothing for these
// notes is faded and says why in its tooltip (Lock outside Cloud, Tape and Each grain backwards on Tape notes, Jump
// every without Scatter).
//
// Every change is one model call (model::setNoteSample, through the roll's SelectionTools) and one undo step with the
// prototype's label ("Note engine: Tape", "Start: random"); the sliders change the notes while they move and are one
// step on release. Choosing Mirror for notes with one head also sets their Heads lane to 2 heads (in the same step, with
// a toast); switching a flat Travel line to Path makes it play through the Region (Fit), and back to Speed 1x forwards.
//
// Unlike the prototype, which hides the groups while nothing is selected and the random range while the Start is not
// Random, every control stays in its place (faded and asleep when it does nothing): the pane keeps its rows, so a click
// that selects a note never moves the roll under the mouse. Message thread only.

#include "TabPanel.h"

#include "core/Inst.h"
#include "model/Clip.h"
#include "model/NoteSettings.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class StrandLockProcessor;

namespace sl::ui
{
    namespace roll
    {
        class RollView;
        class SegButtons;
        class ToolSlider;
    }

    class HoverWatch;

    class NoteSettingsTab final : public TabPanel
    {
    public:
        NoteSettingsTab (const Theme&, juce::AudioProcessorValueTreeState&, roll::RollView&, StrandLockProcessor&);
        ~NoteSettingsTab() override;

        /** Shows the selected notes' settings now (MainView: on the editor's timer and after every edit; cheap when
            nothing it shows changed). */
        void sync (bool force = false);

        /** The groups, in the prototype's order. */
        enum class Group { engine, lock, motion, travel, start, tape, rev, jump, restart, velocity };
        static constexpr int numGroups = 10;

        //==========================================================================================
        // For the tests.

        /** No note is selected: the first line says how to use the tab and every control is asleep. */
        bool isAsleep() const noexcept { return asleep; }

        /** The first line ("3 notes selected · following the Sample tab"). */
        juce::String getHeadText() const;

        bool isMixed (Group g) const noexcept;
        bool isFaded (Group g) const noexcept;
        juce::String getFadeReason (Group g) const;

        /** The segments of Engine, Lock, Motion, Travel, Start, Tape and Each grain backwards (the lit one; -1 while
            mixed or asleep). Any other group: Engine's. */
        roll::SegButtons& getSegments (Group g) const noexcept;
        roll::ToolSlider& getSpot() const noexcept { return *spot; }
        roll::ToolSlider& getRange() const noexcept { return *range; }
        roll::ToolSlider& getVelocity() const noexcept { return *velocity; }
        juce::ComboBox& getJump() const noexcept { return *jump; }
        FlatButton& getRestart() const noexcept { return *restart; }
        FlatButton& getClear() const noexcept { return *clear; }
        FlatButton& getReroll() const noexcept { return *reroll; }

        /** A control's own text (its tooltip, and the hint line while the mouse is on it), or empty. */
        juce::String titleOf (const juce::Component& c) const;

        /** The id the Jump every list gives "mixed" (shown only while the selected notes differ). */
        static constexpr int jumpMixedId = 100;

        /** How many syncs did their work (something the tab shows had changed), and how many times a group's width was
            measured (only when its texts changed). */
        int getSyncs() const noexcept { return syncs; }
        int getFits() const noexcept { return fits; }

    private:
        class GroupBox;
        class HeadLine;
        class MixedTag;

        roll::SegButtons& segGroup (Group g, const juce::String& caption, const juce::StringArray& labels, const std::vector<juce::String>& titles,
                                    std::function<void (int)> picked);
        void setNS (const model::SamplePatch& patch, const std::string& label);
        void motionPicked (int index);
        void travelPicked (int index);
        void rerollClicked();
        void restartClicked();
        void jumpPicked();
        void slide (const std::string& key, const std::string& label, const model::SamplePatch& patch, bool final);
        void edited();

        GroupBox& group (Group g) const noexcept { return *groups[(std::size_t) g]; }
        void fadeGroup (Group g, bool faded, const juce::String& why);
        void setTitle (juce::Component& c, const juce::String& title);
        void fillJump (bool withMixed);
        bool fitGroup (Group g);
        juce::String spotText (double v) const;
        double soundSeconds() const;

        roll::RollView& roll;
        StrandLockProcessor& processor;

        HeadLine* head = nullptr;
        FlatButton* clear = nullptr;
        std::array<GroupBox*, numGroups> groups {};
        roll::SegButtons* engine = nullptr;
        roll::SegButtons* lock = nullptr;
        roll::SegButtons* motion = nullptr;
        roll::SegButtons* travel = nullptr;
        roll::SegButtons* start = nullptr;
        roll::SegButtons* tape = nullptr;
        roll::SegButtons* rev = nullptr;
        roll::ToolSlider* spot = nullptr;
        roll::ToolSlider* range = nullptr;
        FlatButton* reroll = nullptr;
        juce::ComboBox* jump = nullptr;
        FlatButton* restart = nullptr;
        roll::ToolSlider* velocity = nullptr;

        bool asleep = true;
        bool spotMixed = false, rangeMixed = false, velMixed = false;   // the sliders read "mixed"
        bool jumpHasMixed = false;
        int syncs = 0, fits = 0;
        std::map<const juce::Component*, juce::String> titles;          // each control's own text
        std::vector<std::unique_ptr<HoverWatch>> hovers;

        /** What the tab showed at its last sync: it syncs again only when one of these changed. */
        struct Seen
        {
            model::ClipPtr clip;
            std::vector<std::string> ids;
            std::string primary;
            core::Inst inst;
            double seconds = 0.0;
            bool valid = false;
        };
        Seen seen;
    };
}
