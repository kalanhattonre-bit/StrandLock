#pragma once

// The waveform strip (PLUGIN-PLAN 4 M2; sl-ui.js "strip (Sample editor)", shell.html #stripRow): on the left the
// sound column (the starter menu, Browse..., Sound presets for M8), on the right the file line (the sound's name,
// length and root) with At the end and the zoom buttons, and under it the sound's waveform with the Region box.
//
// What the mouse does on the waveform, as in the prototype:
// - drag across it: a new Region there;
// - drag an orange edge: move that edge; drag the band along the top of the Region: move the whole Region;
// - hold the mouse down inside the Region: hear that spot (an audition at the root, from there; the integration
//   step plays the command queue); outside the Region the readout says where you are;
// - the wheel zooms around the pointer, Shift + wheel (or a sideways wheel) moves the view; when zoomed in, the
//   bar along the bottom shows the view and dragging it moves the view.
// A Region drag is one host gesture per edge and one undo step ("Region") when the mouse lets go; StrandLock keeps
// the exact f64 edges it set (INST, plan 2.2) while the parameters hold their floats.
//
// The heads' playheads (numbered lines with direction arrows) and the grains (dots) come from the engine's
// display frames, when the processor has a DisplaySlot (the integration step makes it); until then none are drawn.

#include "WavePeaks.h"

#include "engine/DisplayFrame.h"
#include "ui/Controls.h"

#include <functional>
#include <memory>
#include <optional>

class StrandLockProcessor;

namespace sl::ui
{
    class WaveStrip final : public juce::Component
    {
    public:
        WaveStrip (const Theme&, StrandLockProcessor&);
        ~WaveStrip() override;

        /** The editor's timer: a new sound (its peaks), the Region, the root, the sound menu. */
        void refresh();

        /** The engine's newest display frame (copied), or null: no heads drawn. */
        void setDisplayFrame (const engine::DisplayFrame* frame);

        //==========================================================================================
        // The view: the part a .. b (fractions of the sound) shown, kept in the project (UIST strip view).

        /** sl-ui.js zoomStrip: the view's width times factor (at most the whole sound, at least 256 frames),
            around centre (the middle of the view when not given). */
        void zoom (double factor, std::optional<double> centre = std::nullopt);
        /** Show all: the whole sound (the Region stays as it is). */
        void showAll();
        /** sl-ui.js setStripView: a tiny or reversed range widens to the zoom limit. */
        void setView (double a, double b);
        double viewStart() const noexcept { return viewA; }
        double viewEnd() const noexcept { return viewB; }

        //==========================================================================================
        // Loading a sound.

        /** The starter menu picked starter i (0 .. 7, the menu's order: sawopen first). */
        void loadStarter (int index);
        /** Browse...: an async file chooser for audio files; the file picked loads. */
        void browse();
        /** A file from Browse or a drop: the toast says it is loading, and the worker loads it (rooted at C3). */
        void loadFile (const juce::File& file);

        /** Where the strip's toasts go (MainView shows them): the text and how long, in ms. */
        std::function<void (const juce::String&, int)> onToast;

        //==========================================================================================
        // For the tests.

        const doc::SoundView* shownSound() const noexcept { return sound.get(); }
        const juce::String& headerText() const noexcept { return header; }
        juce::ComboBox& getSoundMenu() noexcept { return soundMenu; }
        FlatButton& getBrowseButton() noexcept { return browseButton; }
        FlatButton& getZoomInButton() noexcept { return zoomIn; }
        FlatButton& getZoomOutButton() noexcept { return zoomOut; }
        FlatButton& getShowAllButton() noexcept { return showAllButton; }
        ChoiceBox& getAtEnd() noexcept { return *atEnd; }
        juce::Rectangle<int> getWaveArea() const noexcept { return waveArea; }
        const juce::String& getReadout() const noexcept { return readout; }
        /** The x (in this component) of a position in the sound (0..1), and back. */
        float xOf (double pos) const noexcept;
        double posOf (float x) const noexcept;
        /** How many heads and grains the last paint drew. */
        int headsDrawn() const noexcept { return lastHeads; }
        int grainsDrawn() const noexcept { return lastGrains; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** The strip's height for a window this many logical pixels high (shell.html's --wave-h: 56 px at most
            720 px high, 72 at most 820, else 92; StrandLock's are a little taller, so the Region is easy to grab). */
        static int heightFor (int windowHeight) noexcept;

        static constexpr int columnWidth = 172, headerHeight = 28;
        static constexpr int band = 12;              // sl-ui.js BAND: the Region band along the top
        static constexpr float waveAlpha = 0.8f;     // renderWave's globalAlpha
        static constexpr int clickMs = 300;          // STRIP_CLICK_MS: a press held longer is listening
        static constexpr float edgeGrab = 7.0f;      // how near an edge the mouse takes it (stripDown)

    private:
        enum class Drag { none, pan, startEdge, endEdge, box, click };

        struct Region
        {
            double start = 0.0, end = 1.0;
        };

        Region regionNow() const;
        void setRegion (double start, double end);
        void beginRegionGesture (bool start, bool end);
        void endRegionGesture();
        double soundSeconds() const noexcept;
        double minRegionWidth() const noexcept;
        juce::String regionText (const Region& r) const;
        void fillSoundMenu();
        void makeHeader();
        void saveView();
        void auditionStop();
        void toast (const juce::String& text, int ms = 2600);
        void paintWave (juce::Graphics&, juce::Rectangle<float> area);
        void paintHeads (juce::Graphics&, juce::Rectangle<float> area);

        const Theme& theme;
        StrandLockProcessor& processor;

        std::shared_ptr<const doc::SoundView> sound;
        std::shared_ptr<const WavePeaks> peaks;
        bool firstSound = true;

        // The view and the wave columns drawn for it (made again when the sound, the view or the width changes).
        double viewA = 0.0, viewB = 1.0;
        std::vector<WavePeaks::Column> columns;
        double columnsA = -1.0, columnsB = -1.0;
        int columnsWidth = -1;
        const doc::SoundView* columnsSound = nullptr;

        // What the file line and the Region show now.
        float paramStart = 0.0f, paramEnd = 1.0f;
        double exactStart = 0.0, exactEnd = 1.0;
        int root = 60;
        float cents = 0.0f;
        juce::String header, headerName, headerRest;

        // The mouse.
        Drag drag = Drag::none;
        double dragPos0 = 0.0, dragMinWidth = 0.002, panWidth = 1.0;
        Region dragRegion0;
        float dragX0 = 0.0f;
        bool dragMoved = false, dragInside = false;
        double dragAt = 0.0;
        int auditionNote = -1;
        juce::String readout;
        std::unique_ptr<ParamBinding> startBinding, endBinding;

        // The heads from the engine (a copy of the newest frame; null until the integration step sends one).
        std::unique_ptr<engine::DisplayFrame> frame;
        int lastHeads = 0, lastGrains = 0;

        juce::Rectangle<int> waveArea;
        juce::ComboBox soundMenu;
        FlatButton browseButton, presetsButton, zoomOut, zoomIn, showAllButton;
        std::unique_ptr<ChoiceBox> atEnd;
        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveStrip)
    };
}
