// model/normalize.json's clip cases (PLUGIN-PLAN 4 M1, 5.3): v3 notes arrays, missing, duplicate and numeric
// ids, dangling and self links, NaN, empty, single, unsorted and out-of-range lanes, garbage note fields and
// clip values that are not clips, through clipFromJson (SLModel.normalizeClip), exactly. The six instrument
// cases are in test_inst.cpp. Also: the typed normalizeClip keeps the clip settings, and changes nothing in a
// clip it already made.

#include "ModelFixture.h"

#include "core/JsonLite.h"
#include "model/Normalize.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <string>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    std::size_t countKind (const JsonValue& cases, const char* kind)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < cases.size(); ++i)
            n += cases[i]["kind"].asString() == kind ? 1 : 0;
        return n;
    }
}

SL_TEST ("model", normalizeClips)
{
    section ("model/normalize.json: clips from v3 arrays, bad ids, dangling links, NaN lanes and garbage, as normalizeClip makes them");

    JsonValue doc;
    if (! fixture::load ("model/normalize.json", doc))
        return;
    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("model/normalize.json (clip cases)", countKind (cases, "clip"));

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        if (c["kind"].asString() != "clip")
            continue;
        const std::string label = c["label"].asString();
        // the input's "NaN" strings stand for NaN numbers: clipFromJson reads a string where a number belongs as
        // missing, which is what the prototype does with a NaN, so the input is used as it is
        const model::Clip clip = model::clipFromJson (c["input"]);
        const JsonValue& want = c["result"];
        bool exact = true;
        std::string why;
        if (clip.bars != static_cast<int> (want["bars"].asNumber()))
            tally.fail (label + ": bars " + std::to_string (clip.bars) + ", want " + core::toJson (want["bars"]));
        else if (! modelfx::clipNotesClose (clip.notes, want["notes"], 0.0, exact, why))
            tally.fail (label + ": " + why);
        else
            tally.pass (exact);
    }
    tally.finish();
}

SL_TEST ("model", normalizeTyped)
{
    section ("normalizeClip on typed clips: the settings are kept, a normalized clip comes back the same, every note is new");

    model::Clip clip = *model::loadClipPreset ("strand-demo");
    clip.beatsPerBar = 3;
    clip.offsetBeats = 1.5;
    clip.loop = false;
    clip.tieAcrossLoop = false;
    const model::Clip again = model::normalizeClip (clip);
    bool same = again.notes.size() == clip.notes.size();
    std::string why;
    for (std::size_t i = 0; same && i < clip.notes.size(); ++i)
        same = again.notes[i] != clip.notes[i] && modelfx::bitIdentical (*again.notes[i], *clip.notes[i], why);
    check (same, "a normalized clip normalizes to the same notes, bit for bit, as new objects " + why);
    check (again.beatsPerBar == 3 && again.offsetBeats == 1.5 && ! again.loop && ! again.tieAcrossLoop && again.bars == clip.bars,
           "the clip settings are kept");

    model::Clip raw = model::newClip (4);
    raw.bars = 99;
    model::Note n = model::mkNote (200, -3, 0, "");
    n.glideTo = "dup";
    raw.notes.push_back (std::make_shared<const model::Note> (n));
    raw.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 1, "dup")));
    raw.notes.push_back (std::make_shared<const model::Note> (model::mkNote (61, 1, 1, "dup")));
    const model::Clip fixed = model::normalizeClip (raw);
    check (fixed.bars == 64 && fixed.notes[0]->p == 127 && fixed.notes[0]->s == 0 && fixed.notes[0]->l == 1e-3 && ! fixed.notes[0]->id.empty()
               && fixed.notes[0]->glideTo == "dup" && fixed.notes[1]->id == "dup" && fixed.notes[2]->id == "dup~1",
           "bars, pitch, start and length are clamped, an empty id is made, a repeated id renamed id~1, a link to a present id kept");
}
