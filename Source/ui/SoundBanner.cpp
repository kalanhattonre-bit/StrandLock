#include "SoundBanner.h"

#include "PluginProcessor.h"

namespace sl::ui
{
    namespace
    {
        constexpr int kPadX = 8, kGap = 6, kButtonHeight = 24;
    }

    SoundBanner::SoundBanner (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          locateButton (t, "Locate sample...", FlatButton::Icon::folder),
          yesButton (t, "Yes"),
          noButton (t, "No"),
          regionButton (t, "Just the Region")
    {
        locateButton.setTooltip ("Choose the sound file: it plays with every setting as it was (one undo step)");
        locateButton.onClick = [this] { chooseFile(); };
        yesButton.setTooltip ("Keep the whole file inside the Cubase project (asked once for this file)");
        yesButton.onClick = [this] { processor.answerEmbedQuestion (StrandLockProcessor::EmbedAnswer::yes); refresh(); };
        noButton.setTooltip ("Keep nothing of it in the project: the file has to stay where it is");
        noButton.onClick = [this] { processor.answerEmbedQuestion (StrandLockProcessor::EmbedAnswer::no); refresh(); };
        regionButton.setTooltip ("Keep only the Region (and up to 1 s either side) in the project, as 24-bit FLAC");
        regionButton.onClick = [this] { processor.answerEmbedQuestion (StrandLockProcessor::EmbedAnswer::region); refresh(); };
        for (auto* b : { &locateButton, &yesButton, &noButton, &regionButton })
            addChildComponent (*b);
        setInterceptsMouseClicks (false, true);
        refresh();
    }

    SoundBanner::~SoundBanner()
    {
        chooser.reset();
    }

    bool SoundBanner::refresh()
    {
        const auto notice = processor.getSoundNotice();
        Kind now = Kind::none;
        switch (notice.kind)
        {
            case StrandLockProcessor::SoundNotice::Kind::none:     now = Kind::none; break;
            case StrandLockProcessor::SoundNotice::Kind::question: now = Kind::question; break;
            case StrandLockProcessor::SoundNotice::Kind::locate:   now = Kind::locate; break;
            case StrandLockProcessor::SoundNotice::Kind::partKept: now = Kind::partKept; break;
        }
        const auto words = juce::String::fromUTF8 (notice.text.c_str());
        const bool cameOrWent = (now == Kind::none) != (kind == Kind::none);
        if (now != kind || words != text)
        {
            kind = now;
            text = words;
            locateButton.setVisible (kind == Kind::locate || kind == Kind::partKept);
            for (auto* b : { &yesButton, &noButton, &regionButton })
                b->setVisible (kind == Kind::question);
            resized();
            repaint();
        }
        return cameOrWent;
    }

    void SoundBanner::chooseFile()
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto ref = processor.getSampleRef();
        auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        // The folder the project's file was in, when it is still there.
        const auto saved = juce::String::fromUTF8 (ref.file.path.c_str());
        if (saved.isNotEmpty() && juce::File::isAbsolutePath (saved) && juce::File (saved).getParentDirectory().isDirectory())
            start = juce::File (saved).getParentDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Locate the sound file", start, formats.getWildcardForAllFormats(), true);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SoundBanner> (this)] (const juce::FileChooser& fc)
                              {
                                  const auto picked = fc.getResult();
                                  if (safe != nullptr && picked != juce::File())
                                      safe->locate (picked);
                              });
    }

    void SoundBanner::locate (const juce::File& file)
    {
        if (processor.locateSound (file) != 0 && onToast)
            onToast ("Loading " + file.getFileName() + "...", 2600);
    }

    void SoundBanner::resized()
    {
        auto r = getLocalBounds().reduced (kPadX, 0);
        const int cy = r.getCentreY();
        int right = r.getRight();
        auto place = [&right, cy] (FlatButton& b)
        {
            if (! b.isVisible())
                return;
            const int w = b.preferredWidth();
            b.setBounds (right - w, cy - kButtonHeight / 2, w, kButtonHeight);
            right -= w + kGap;
        };
        // From the right: Locate, or the question's three answers (Yes first, reading from the left).
        place (locateButton);
        place (regionButton);
        place (noButton);
        place (yesButton);
        textArea = r.withRight (juce::jmax (r.getX(), right - kGap));
    }

    void SoundBanner::paint (juce::Graphics& g)
    {
        if (kind == Kind::none)
            return;
        g.setColour (theme (Tok::chipWarnBg));
        g.fillAll();
        g.setColour (theme (Tok::line));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        g.setColour (theme (Tok::chipWarnTxt));
        g.setFont (Theme::px (12.0f, true));
        g.drawFittedText (text, textArea, juce::Justification::centredLeft, 2, 0.8f);
    }
}
