// The view's banner for a clip too detailed to play (P1's hand-off: SnapshotPublisher::stats ().tooDetailed).
// The document refuses an edit that would make such a clip, so only a hand-made project can hold one; the
// banner says why its notes are silent.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <memory>
#include <string>

namespace
{
    using namespace sl;
    using namespace sl::test;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    /** n notes of len beats with a generator on every lane: no lane is flat, so the tables take their full length. */
    model::Clip everyLaneMoving (int n, double len)
    {
        model::Clip c = model::newClip (64);
        for (int i = 0; i < n; ++i)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (48 + i % 24, (double) (i % 200), len, "c" + std::to_string (i))));
        for (int lane = 0; lane < core::kNumLanes; ++lane)
            c = model::setGen (c, model::Ids::all(), static_cast<core::LaneId> (lane), model::makeGen (model::GenKind::pattern, model::GenPatch {}));
        return c;
    }

    /** The view checks the banner on every 8th tick: 8 ticks always include one. */
    juce::String bannerAfterTicks (StrandLockEditor& editor)
    {
        for (int i = 0; i < 8; ++i)
            editor.refreshNow();
        return editor.getView().getBannerText();
    }
}

SL_TEST ("ui", viewTooDetailedBanner)
{
    section ("View: a clip too detailed to play shows a banner; a playable one takes it away");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    check (bannerAfterTicks (editor).isEmpty() && ! proc.getSnapshotPublisher().stats().tooDetailed, "Strand Demo plays: no banner");

    proc.getDocument().replace (std::make_shared<const model::Clip> (everyLaneMoving (300, 4096)));
    proc.waitForSnapshot();
    const auto banner = bannerAfterTicks (editor);
    check (proc.getSnapshotPublisher().stats().tooDetailed && banner.contains ("too detailed to play") && banner.contains ("silent"),
           fmt ("300 notes of 4096 beats with every lane moving (as a hand-made project could hold): \"%s\"", banner.toRawUTF8()));

    proc.getDocument().replace (std::make_shared<const model::Clip> (model::newClip (4.0)));
    proc.waitForSnapshot();
    check (bannerAfterTicks (editor).isEmpty() && ! proc.getSnapshotPublisher().stats().tooDetailed, "an empty clip: the banner goes");
    resetUserSettings (proc);
}
