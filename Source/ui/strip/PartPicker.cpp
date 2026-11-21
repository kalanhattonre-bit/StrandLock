#include "PartPicker.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace sl::ui
{
    namespace
    {
        /** Minutes and whole seconds ("13:15"). */
        juce::String minutes (double seconds)
        {
            const auto s = (juce::int64) std::floor (std::max (0.0, seconds) + 1e-9);
            return juce::String (s / 60) + ":" + (s % 60 < 10 ? "0" : "") + juce::String (s % 60);
        }
    }

    PartPicker::PartPicker (const Theme& t, StrandLockProcessor& p)
        : theme (t), processor (p), loadButton (t, "Load this part"), cancelButton (t, "Cancel")
    {
        loadButton.setTooltip ("Load these 10 minutes of the song (one undo step)");
        loadButton.onClick = [this] { loadChosen(); };
        cancelButton.setTooltip ("Keep the part that is loaded now");
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (loadButton);
        addAndMakeVisible (cancelButton);
        setVisible (false);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    bool PartPicker::open()
    {
        const auto sound = processor.getCurrentSound();
        if (sound == nullptr || sound->builtIn || sound->missing || ! sound->cut || sound->ref.kind != state::kSampleFile)
            return false;
        const auto& f = sound->ref.file;
        path = juce::String::fromUTF8 (f.path.c_str());
        name = juce::String::fromUTF8 (sound->name.c_str());
        fileFrames = f.fileFrames;
        rate = f.fileSampleRate;
        // the worker's rule: at most floor (600 x rate) frames, and never past the end of the file
        maxLength = std::min<std::uint64_t> (fileFrames, (std::uint64_t) std::floor (doc::SampleManager::kMaxFileSeconds * rate));
        start = f.loadedStartFrame;
        length = std::min (maxLength, fileFrames - std::min (start, fileFrames));
        overview = nullptr;
        processor.requestOverview();
        setVisible (true);
        toFront (false);
        refresh();
        repaint();
        return true;
    }

    void PartPicker::close()
    {
        setVisible (false);
        overview = nullptr;
        if (onClose)
            onClose();
    }

    void PartPicker::refresh()
    {
        if (! isVisible() || overview != nullptr)
            return;
        auto now = processor.getOverview();
        if (now != nullptr && now->ok && juce::String::fromUTF8 (now->path.c_str()) == path)
        {
            overview = std::move (now);
            repaint();
        }
    }

    void PartPicker::setStartFrame (std::uint64_t frame)
    {
        // from anywhere but the very end (the worker's rule: a part keeps at least 64 frames); 10 minutes or up to the end
        const std::uint64_t minFrames = (std::uint64_t) doc::SampleManager::kMinFileFrames;
        const std::uint64_t last = fileFrames > minFrames ? fileFrames - minFrames : 0;
        const auto f = std::min (frame, last);
        const auto len = std::min (maxLength, fileFrames - f);
        if (f != start || len != length)
        {
            start = f;
            length = len;
            repaint();
        }
    }

    void PartPicker::loadChosen()
    {
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return;
        const auto file = juce::File (path);
        if (onToast)
            onToast ("Loading " + timesText() + " of " + name + "...", 2600);
        processor.loadSoundFilePart (file, start);
        close();
    }

    juce::String PartPicker::timesText() const
    {
        if (! (rate > 0.0))
            return {};
        return minutes ((double) start / rate) + " to " + minutes ((double) (start + length) / rate) + " of " + minutes ((double) fileFrames / rate);
    }

    float PartPicker::xOfFrame (double frame) const noexcept
    {
        return (float) area.getX() + (float) (frame / (double) std::max<std::uint64_t> (1, fileFrames) * (double) area.getWidth());
    }

    void PartPicker::resized()
    {
        auto r = getLocalBounds();
        auto row = r.removeFromTop (rowHeight).reduced (4, 1);
        const int cw = cancelButton.preferredWidth(), lw = loadButton.preferredWidth();
        cancelButton.setBounds (row.removeFromRight (cw).withSizeKeepingCentre (cw, 22));
        row.removeFromRight (4);
        loadButton.setBounds (row.removeFromRight (lw).withSizeKeepingCentre (lw, 22));
        area = r.reduced (4, 2);
    }

    void PartPicker::paint (juce::Graphics& g)
    {
        g.setColour (theme (Tok::stripBg));
        g.fillAll();

        // The line: what this is, and the times the box holds.
        {
            auto row = juce::Rectangle<int> (8, 0, juce::jmax (0, loadButton.getX() - 16), rowHeight);
            g.setColour (theme (Tok::txt));
            g.setFont (Theme::px (11.5f, true));
            const juce::String head = "Pick 10 minutes to load: ";
            const int hw = (int) std::ceil (Theme::textWidth (Theme::px (11.5f, true), head)) + 1;
            g.drawText (head, row.removeFromLeft (juce::jmin (hw, row.getWidth())), juce::Justification::centredLeft, true);
            g.setColour (theme (Tok::dim));
            g.setFont (Theme::px (11.5f));
            g.drawText (timesText() + "  (drag the box along the song)", row, juce::Justification::centredLeft, true);
        }

        if (area.isEmpty())
            return;
        const auto a = area.toFloat();
        g.setColour (theme (Tok::panel));
        g.fillRect (a);
        if (overview != nullptr && ! overview->lo.empty())
        {
            // the whole file, one column per pixel from the overview's bins
            const auto bins = (int) overview->lo.size();
            const int W = juce::jmax (1, area.getWidth());
            const float mid = a.getCentreY(), amp = a.getHeight() * 0.46f;
            juce::RectangleList<float> rects;
            for (int x = 0; x < W; ++x)
            {
                const int b0 = x * bins / W, b1 = juce::jmax (b0 + 1, (x + 1) * bins / W);
                float lo = 1.0f, hi = -1.0f;
                for (int b = b0; b < juce::jmin (bins, b1); ++b)
                {
                    lo = juce::jmin (lo, overview->lo[(std::size_t) b]);
                    hi = juce::jmax (hi, overview->hi[(std::size_t) b]);
                }
                if (hi >= lo)
                    rects.addWithoutMerging ({ a.getX() + (float) x, mid - hi * amp, 1.0f, juce::jmax (1.0f, (hi - lo) * amp) });
            }
            g.setColour (theme (Tok::wave).withMultipliedAlpha (0.8f));
            g.fillRectList (rects);
        }
        else
        {
            g.setColour (theme (Tok::stripTxt));
            g.setFont (Theme::px (12.0f));
            g.drawText ("Reading the whole song...", a.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
        }

        // The box: the 10 minutes that would load, with the Region's colours.
        const float x0 = xOfFrame ((double) start), x1 = xOfFrame ((double) (start + length));
        g.setColour (theme (Tok::stripShade));
        g.fillRect (juce::Rectangle<float> (a.getX(), a.getY(), juce::jmax (0.0f, x0 - a.getX()), a.getHeight()));
        g.fillRect (juce::Rectangle<float> (x1, a.getY(), juce::jmax (0.0f, a.getRight() - x1), a.getHeight()));
        g.setColour (theme (Tok::regionBand).withMultipliedAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x0, a.getY(), juce::jmax (1.0f, x1 - x0), a.getHeight()));
        g.setColour (theme (Tok::regionEdge));
        g.drawRect (juce::Rectangle<float> (x0, a.getY(), juce::jmax (2.0f, x1 - x0), a.getHeight()), 2.0f);
    }

    void PartPicker::moveBoxTo (float x)
    {
        if (area.getWidth() <= 0 || fileFrames == 0)
            return;
        const double frame = (double) (x - grabOffset - (float) area.getX()) / (double) area.getWidth() * (double) fileFrames;
        setStartFrame ((std::uint64_t) std::max (0.0, std::floor (frame)));
    }

    void PartPicker::mouseDown (const juce::MouseEvent& e)
    {
        if (! area.contains (e.getPosition()))
            return;
        // inside the box: it moves with the mouse from where it was taken; elsewhere its middle jumps to the mouse
        const float x0 = xOfFrame ((double) start), x1 = xOfFrame ((double) (start + maxLength));
        grabOffset = e.position.x >= x0 && e.position.x <= x1 ? e.position.x - x0 : (x1 - x0) * 0.5f;
        grabbing = true;
        moveBoxTo (e.position.x);
    }

    void PartPicker::mouseDrag (const juce::MouseEvent& e)
    {
        if (grabbing)
            moveBoxTo (e.position.x);
    }
}
