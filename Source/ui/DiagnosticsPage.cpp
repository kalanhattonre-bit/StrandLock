#include "DiagnosticsPage.h"

#include "PluginProcessor.h"
#include "core/Music.h"
#include "core/Version.h"

#include <cmath>

namespace sl::ui
{
    namespace
    {
        constexpr int kPad = 8;

        juce::Font labelFont() { return Theme::px (11.5f); }
        juce::Font valueFont() { return Theme::px (11.5f); }
        juce::Font titleFont() { return Theme::px (10.5f, true, 0.06f); }

        juce::String fixed (double v, int decimals) { return juce::String (v, decimals); }

        // PLUGIN-PLAN 4 M2: the Voice and Cloud grain engines come with M3.
        constexpr const char* kEnginesNote = "Tape plays now. Notes set to Voice or Cloud play as Tape until the next build (M3).";
        juce::String count (std::uint64_t n) { return juce::String (static_cast<juce::int64> (n)); }

        juce::String noteText (int note)
        {
            return note < 0 ? juce::String ("none yet") : juce::String (core::noteName (note)) + " (" + juce::String (note) + ")";
        }

        juce::String bucketText (int b)
        {
            if (b == 0)
                return "1";
            const int lo = 1 << b;
            if (b == diag::DiagModel::kBuckets - 1)
                return juce::String (lo) + "+";
            return juce::String (lo) + "-" + juce::String ((lo << 1) - 1);
        }

        template <class T>
        std::size_t lastN (const std::vector<T>& v, std::size_t n) { return v.size() > n ? v.size() - n : 0; }
    }

    DiagnosticsPage::DiagnosticsPage (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          copyButton (t, "Copy report", FlatButton::Icon::copy),
          title (t, "Diagnostics", Caption::Style::label),
          intro (t, "What Cubase tells StrandLock, live. Press Copy report and paste the text into your reply.", Caption::Style::sub)
    {
        copyText = [] (const juce::String& text) { juce::SystemClipboard::copyTextToClipboard (text); };
        copyButton.onClick = [this] { copyReport(); };
        copyButton.setTooltip ("Copy everything on this page as text, to paste into a message");
        addAndMakeVisible (copyButton);
        addAndMakeVisible (title);
        addAndMakeVisible (intro);
    }

    juce::String DiagnosticsPage::copyReport()
    {
        const auto text = processor.getDiag().reportText();
        if (copyText)
            copyText (text);
        if (onCopied)
            onCopied();
        return text;
    }

    void DiagnosticsPage::update (const diag::DiagModel::Snapshot& s, bool keysOn)
    {
        std::vector<Card> next;

        {
            Card c { "This computer", {} };
            const auto& host = processor.getHostInfo();
            c.rows.push_back ({ "StrandLock", juce::String (core::kPluginVersion) + " (probe build)" });
            c.rows.push_back ({ "Host", host.summary() });
            c.rows.push_back ({ "Engines", juce::String (kEnginesNote) });
            c.rows.push_back ({ "Sample rate", s.sampleRate > 0.0 ? fixed (s.sampleRate, 0) + " Hz" : juce::String ("not playing yet") });
            c.rows.push_back ({ "Block size at start", juce::String (s.preparedBlockSize) + " samples" });
            c.rows.push_back ({ "Display scale", "Windows " + fixed (s.desktopScale * 100.0, 0) + " %, StrandLock UI "
                                                 + juce::String (s.uiScalePercent) + " %" });
            next.push_back (std::move (c));
        }

        {
            Card c { "Blocks (checklist 8 and 9)", {} };
            c.rows.push_back ({ "Blocks", count (s.blocks) + " (playing " + count (s.playingBlocks) + ", stopped " + count (s.stoppedBlocks) + ")" });
            c.rows.push_back ({ "No host position", count (s.noPositionBlocks) });
            c.rows.push_back ({ "Offline (export)", count (s.nonRealtimeBlocks) + (s.nonRealtimeNow ? ", offline now" : ", real time now") });
            c.rows.push_back ({ "Size min / max / last", juce::String (s.minBlock) + " / " + juce::String (s.maxBlock) + " / " + juce::String (s.lastBlock) });
            juce::String sizes;
            for (const auto& [size, n] : s.exactSizes)
                sizes << (sizes.isEmpty() ? "" : "; ") << size << " x " << count (n);
            if (s.otherSizes > 0)
                sizes << "; other x " << count (s.otherSizes);
            c.rows.push_back ({ "Sizes seen", sizes.isEmpty() ? juce::String ("none yet") : sizes });
            juce::String ranges;
            for (int b = 0; b < diag::DiagModel::kBuckets; ++b)
                if (s.histogram[static_cast<std::size_t> (b)] > 0)
                    ranges << (ranges.isEmpty() ? "" : "; ") << bucketText (b) << ": " << count (s.histogram[static_cast<std::size_t> (b)]);
            c.rows.push_back ({ "Size ranges", ranges.isEmpty() ? juce::String ("none yet") : ranges });
            juce::String recent;
            for (std::size_t i = lastN (s.blockStarts, 16); i < s.blockStarts.size(); ++i)
                recent << (recent.isEmpty() ? "" : " ") << s.blockStarts[i].samples;
            c.rows.push_back ({ "Recent sizes", recent.isEmpty() ? juce::String ("none yet") : recent });
            next.push_back (std::move (c));
        }

        {
            Card c { "Position and tempo (checklist 7)", {} };
            juce::String transport;
            if (! s.hasPosition)
                transport = "the host gives no position";
            else
                transport << (s.playing ? "playing" : "stopped") << (s.recording ? ", recording" : "") << ", cycle " << (s.looping ? "on" : "off");
            c.rows.push_back ({ "Transport", transport });
            c.rows.push_back ({ "Position (ppq)", s.hasPpq ? fixed (s.ppq, 4) : juce::String ("none") });
            c.rows.push_back ({ "Tempo", (s.hasBpm ? fixed (s.bpm, 3) + " bpm" : juce::String ("none")) + ", " + juce::String (s.sigNum) + "/" + juce::String (s.sigDen) });
            c.rows.push_back ({ "Tempo seen", fixed (s.minBpm, 3) + " to " + fixed (s.maxBpm, 3) + ", " + count (s.tempoChanges) + " changes" });
            c.rows.push_back ({ "Cycle", s.hasLoop ? fixed (s.loopStart, 4) + " to " + fixed (s.loopEnd, 4) : juce::String ("none") });
            c.rows.push_back ({ "Cycle wraps", count (s.cycleWraps) });
            c.rows.push_back ({ "Wraps inside a block", count (s.wrapsInsideBlock) });
            c.rows.push_back ({ "Other jumps", count (s.jumps) });
            juce::String wraps;
            for (std::size_t i = lastN (s.wraps, 4); i < s.wraps.size(); ++i)
                wraps << (wraps.isEmpty() ? "" : "; ") << fixed (s.wraps[i].before, 4) << " -> " << fixed (s.wraps[i].after, 4);
            c.rows.push_back ({ "Last wraps", wraps.isEmpty() ? juce::String ("none yet") : wraps });
            juce::String starts;
            for (std::size_t i = lastN (s.blockStarts, 6); i < s.blockStarts.size(); ++i)
                starts << (starts.isEmpty() ? "" : " ") << (s.blockStarts[i].hasPosition ? fixed (s.blockStarts[i].ppq, 3) : juce::String ("-"));
            c.rows.push_back ({ "Block starts (ppq)", starts.isEmpty() ? juce::String ("none yet") : starts });
            next.push_back (std::move (c));
        }

        {
            Card c { "MIDI (checklist 3)", {} };
            c.rows.push_back ({ "Notes", count (s.noteOns) + " on, " + count (s.noteOffs) + " off" });
            c.rows.push_back ({ "Last note", noteText (s.lastNote) + (s.lastVelocity >= 0 ? ", velocity " + juce::String (s.lastVelocity) : juce::String()) });
            c.rows.push_back ({ "Controllers", count (s.controllers) + (s.lastController >= 0 ? ", last CC " + juce::String (s.lastController) + " = " + juce::String (s.lastControllerValue) : juce::String()) });
            c.rows.push_back ({ "Mod wheel", s.modWheel >= 0 ? juce::String (s.modWheel) : juce::String ("not moved yet") });
            c.rows.push_back ({ "Pitch bend", count (s.pitchBends) + (s.lastPitchBend >= 0 ? ", last " + juce::String (s.lastPitchBend) + " (8192 = centre)" : juce::String()) });
            c.rows.push_back ({ "Other MIDI", count (s.otherMidi) });
            juce::String recent;
            for (std::size_t i = lastN (s.midi, 8); i < s.midi.size(); ++i)
            {
                const auto& m = s.midi[i];
                recent << (recent.isEmpty() ? "" : "; ");
                switch (m.kind)
                {
                    case diag::EventKind::noteOn:     recent << "on " << core::noteName (m.a) << " " << m.b; break;
                    case diag::EventKind::noteOff:    recent << "off " << core::noteName (m.a); break;
                    case diag::EventKind::controller: recent << "CC" << m.a << " " << m.b; break;
                    case diag::EventKind::pitchBend:  recent << "bend " << m.a; break;
                    case diag::EventKind::otherMidi:
                    case diag::EventKind::blockStart:
                    case diag::EventKind::cycleWrap:
                    case diag::EventKind::wrapInBlock: recent << "0x" << juce::String::toHexString (m.a); break;
                }
            }
            c.rows.push_back ({ "Recent", recent.isEmpty() ? juce::String ("nothing yet") : recent });
            next.push_back (std::move (c));
        }

        {
            Card c { "Cubase's changes and saving (checklist 10 to 12)", {} };
            if (s.lastHostParam >= 0 && s.lastHostParam < params::kNumParams)
            {
                auto& param = processor.getParam (s.lastHostParam);
                c.rows.push_back ({ "Last parameter Cubase changed",
                                    param.getName (64) + " = " + param.getText (param.convertTo0to1 (s.lastHostParamValue), 64)
                                        + " (" + count (s.hostParamChanges) + " changes)" });
            }
            else
            {
                c.rows.push_back ({ "Last parameter Cubase changed", "none yet" });
            }
            c.rows.push_back ({ "Saved", count (s.stateSaves) + " times" + (s.lastSavedBytes >= 0 ? ", last " + juce::String (s.lastSavedBytes) + " bytes" : juce::String()) });
            c.rows.push_back ({ "Loaded", count (s.stateLoads) + " times" + (s.lastLoadedBytes >= 0 ? ", last " + juce::String (s.lastLoadedBytes) + " bytes"
                                                                                  + (s.lastLoadReadable ? juce::String() : juce::String (" (kept unread)")) : juce::String()) });
            const auto banner = processor.getStateBanner();
            if (banner.isNotEmpty())
                c.rows.push_back ({ "Note", banner });
            if (const int unknown = processor.getUnknownChunkCount(); unknown > 0)
                c.rows.push_back ({ "Unknown parts kept", juce::String (unknown) });
            next.push_back (std::move (c));
        }

        {
            Card c { "Keys (checklist 4 and 5)", {} };
            c.rows.push_back ({ "Keys switch", keysOn ? juce::String ("ON: StrandLock takes the keyboard when clicked (never Space)")
                                                      : juce::String ("OFF: every key stays with Cubase") });
            c.rows.push_back ({ "Keys received", juce::String (static_cast<int> (s.keys.size())) });
            juce::String keys;
            for (std::size_t i = lastN (s.keys, 16); i < s.keys.size(); ++i)
                keys << (keys.isEmpty() ? "" : ", ") << s.keys[i].key << (s.keys[i].keysSwitchOn ? "" : " (off)");
            c.rows.push_back ({ "Last keys", keys.isEmpty() ? juce::String ("none yet") : keys });
            next.push_back (std::move (c));
        }

        {
            Card c { "Drops (checklist 6)", {} };
            if (s.drops.empty())
                c.rows.push_back ({ "Nothing yet", "drag a sound file onto the StrandLock window: it loads as the sound" });
            for (std::size_t i = lastN (s.drops, 5); i < s.drops.size(); ++i)
            {
                const auto& d = s.drops[i];
                if (! d.isFile)
                {
                    c.rows.push_back ({ "Text", "\"" + d.what.substring (0, 120) + "\"" });
                    continue;
                }
                // juce::File asserts on a relative path; a host could drop one, so it is shown as it came.
                const bool absolute = juce::File::isAbsolutePath (d.what);
                const juce::String name = absolute ? juce::File (d.what).getFileName() : d.what;
                const juce::String folder = absolute ? juce::File (d.what).getParentDirectory().getFullPathName() : juce::String();
                juce::String what;
                if (d.readable)
                    what << juce::String (d.frames) << " frames, " << fixed (d.seconds, 3) << " s at " << fixed (d.sampleRate, 0)
                         << " Hz, " << d.channels << (d.channels == 1 ? " channel" : " channels");
                else if (d.bytes >= 0)
                    what << juce::String (d.bytes) << " bytes, not a sound file StrandLock can read";
                else
                    what << "not found";
                c.rows.push_back ({ "File", name + ": " + what });
                if (folder.isNotEmpty())
                    c.rows.push_back ({ "Folder", folder });
            }
            next.push_back (std::move (c));
        }

        if (s.lostEvents > 0)
        {
            Card c { "History", {} };
            c.rows.push_back ({ "Not kept", count (s.lostEvents) + " events while the window was closed" });
            next.push_back (std::move (c));
        }

        cards = std::move (next);
        repaint();
    }

    int DiagnosticsPage::rowLines (const Row& row, int valueWidth)
    {
        const float w = Theme::textWidth (valueFont(), row.value);
        return juce::jlimit (1, 3, (int) std::ceil (w / (float) juce::jmax (40, valueWidth - 4)));
    }

    int DiagnosticsPage::labelWidthFor (const Card& card, int cardWidth)
    {
        // The card's longest label, but never more than about half the card.
        float widest = 40.0f;
        for (const auto& row : card.rows)
            widest = juce::jmax (widest, Theme::textWidth (labelFont(), row.label));
        return juce::jmin ((int) std::ceil (widest) + 12, (cardWidth - 2 * kPad) / 2);
    }

    int DiagnosticsPage::cardHeight (const Card& card, int width) const
    {
        const int valueWidth = width - 2 * kPad - labelWidthFor (card, width);
        int h = kPad + 18;
        for (const auto& row : card.rows)
            h += rowLines (row, valueWidth) * lineHeight;
        return h + kPad;
    }

    std::vector<DiagnosticsPage::Placed> DiagnosticsPage::placeCards (int width, int& totalHeight) const
    {
        const int inner = juce::jmax (100, width - 2 * kPad);
        const int columns = juce::jlimit (1, 4, (inner + cardGap) / (330 + cardGap));
        const int colWidth = (inner - cardGap * (columns - 1)) / columns;
        std::vector<int> bottoms ((std::size_t) columns, headerHeight);
        std::vector<Placed> placed;

        for (int i = 0; i < (int) cards.size(); ++i)
        {
            std::size_t col = 0;
            for (std::size_t k = 1; k < bottoms.size(); ++k)
                if (bottoms[k] < bottoms[col])
                    col = k;
            const int h = cardHeight (cards[(std::size_t) i], colWidth);
            placed.push_back ({ i, { kPad + (int) col * (colWidth + cardGap), bottoms[col], colWidth, h } });
            bottoms[col] += h + cardGap;
        }

        totalHeight = headerHeight;
        for (int b : bottoms)
            totalHeight = juce::jmax (totalHeight, b);
        totalHeight += kPad;
        return placed;
    }

    int DiagnosticsPage::heightFor (int width) const
    {
        int h = 0;
        placeCards (width, h);
        return h;
    }

    void DiagnosticsPage::resized()
    {
        const int buttonWidth = copyButton.preferredWidth();
        copyButton.setBounds (getWidth() - kPad - buttonWidth, 7, buttonWidth, 26);
        title.setBounds (kPad, 11, title.preferredWidth(), 18);
        intro.setBounds (title.getRight() + 10, 11, juce::jmax (0, copyButton.getX() - title.getRight() - 20), 18);
    }

    void DiagnosticsPage::paint (juce::Graphics& g)
    {
        int total = 0;
        const auto placed = placeCards (getWidth(), total);

        for (const auto& p : placed)
        {
            const auto& card = cards[(std::size_t) p.card];
            const auto box = p.bounds.toFloat();
            g.setColour (theme (Tok::panel));
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (theme (Tok::line));
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

            auto area = p.bounds.reduced (kPad);
            g.setColour (theme (Tok::acc));
            g.setFont (titleFont());
            g.drawText (card.title.toUpperCase(), area.removeFromTop (18), juce::Justification::centredLeft, true);

            const int labelWidth = labelWidthFor (card, p.bounds.getWidth());
            const int valueWidth = area.getWidth() - labelWidth;
            for (const auto& row : card.rows)
            {
                const int lines = rowLines (row, valueWidth);
                auto line = area.removeFromTop (lines * lineHeight);
                g.setColour (theme (Tok::dim));
                g.setFont (labelFont());
                g.drawText (row.label, line.removeFromLeft (labelWidth).withHeight (lineHeight), juce::Justification::centredLeft, true);
                g.setColour (theme (Tok::txt));
                g.setFont (valueFont());
                g.drawFittedText (row.value, line, juce::Justification::topLeft, lines, 1.0f);
            }
        }
    }

    juce::StringArray DiagnosticsPage::shownText() const
    {
        juce::StringArray all;
        for (const auto& card : cards)
            for (const auto& row : card.rows)
                all.add (card.title + ": " + row.label + ": " + row.value);
        return all;
    }
}
