// The M2 contract around the engine (PLUGIN-PLAN 7.4): EngineConfig against the prototype's constants
// (engine/defs.json), the Engine API's behaviour that both chains rely on (what the JS engine does with no
// sample and no notes, the epochs it reports, 0 allocations on the audio thread), SampleData's setSample rules,
// the display frame and the command queue. Every check here holds for the stub engine and for the real one.
// Group "engine".

#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include "core/Lanes.h"
#include "core/Travel.h"
#include "doc/CommandQueue.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "engine/EngineConfig.h"
#include "engine/SampleData.h"
#include "rt/VersionExchange.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    bool allZero (const std::vector<float>& x)
    {
        for (const float v : x)
            if (v != 0.0f)
                return false;
        return true;
    }

    std::unique_ptr<engine::SampleData> sineSample (std::size_t frames, double sampleRate)
    {
        std::vector<float> l (frames);
        for (std::size_t i = 0; i < frames; ++i)
            l[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.141592653589793 * 220.0 * static_cast<double> (i) / sampleRate));
        return engine::makeSampleData (std::move (l), {}, sampleRate, "sine");
    }
}

SL_TEST ("engine", engineConfigMatchesDefs)
{
    section ("EngineConfig holds the prototype engine's constants (engine/defs.json)");

    JsonValue doc;
    if (! fixture::load ("engine/defs.json", doc))
        return;
    const auto defs = fixture::casesOfKind (doc, "defs");
    check (defs.size() == 1, fmt ("the file has one defs case (%zu)", defs.size()));
    if (defs.empty())
        return;
    const JsonValue& d = *defs[0];

    auto num = [&d] (const char* key) { return d[key].asNumber (std::numeric_limits<double>::quiet_NaN()); };
    check (num ("CTRL") == engine::kCtrl && num ("MAX_HEADS") == engine::kMaxHeads && num ("VOICE_GRAINS") == engine::kVoiceGrains
               && num ("CLOUD_GRAINS") == engine::kCloudGrains && num ("LOCK_MAX_N") == engine::kLockMaxN
               && num ("BUDGET_48K") == engine::kBudget48k && num ("HANN_N") == engine::kHannN && num ("FADE_N") == engine::kFadeN,
           "CTRL, MAX_HEADS, VOICE_GRAINS, CLOUD_GRAINS, LOCK_MAX_N, BUDGET_48K, HANN_N, FADE_N");

    const JsonValue& budgets = d["budgets"];
    int budgetsOk = 0;
    for (std::size_t i = 0; i < budgets.size(); ++i)
    {
        const double rate = std::stod (budgets.keyAt (i));
        if (engine::budgetFor (rate) == static_cast<int> (budgets.valueAt (i).asNumber()))
            ++budgetsOk;
        else
            note (fmt ("budget at %g Hz: %d, the prototype %g", rate, engine::budgetFor (rate), budgets.valueAt (i).asNumber()));
    }
    check (budgets.size() == 5 && budgetsOk == 5, fmt ("budgetFor at 44.1, 48, 88.2, 96 and 192 kHz (%d of %zu)", budgetsOk, budgets.size()));

    const char* tunes[4] = { "same", "detune", "octaves", "fifths" };
    bool tuneOk = true;
    for (int t = 0; t < 4; ++t)
    {
        const JsonValue& row = d["HEAD_TUNE"][tunes[t]];
        tuneOk = tuneOk && row.size() == static_cast<std::size_t> (engine::kMaxHeads);
        for (std::size_t h = 0; tuneOk && h < row.size(); ++h)
            tuneOk = row[h].asNumber() == engine::kHeadTune[t][h];
    }
    check (tuneOk, "HEAD_TUNE, in core::HeadTune order");

    const JsonValue& salts = d["salts"];
    check (salts["SALT_JUMP"].asNumber() == engine::kSaltJump && salts["SALT_SKIP"].asNumber() == engine::kSaltSkip
               && salts["SALT_SPRAY"].asNumber() == engine::kSaltSpray && salts["SALT_DRIFT"].asNumber() == engine::kSaltDrift
               && salts["SALT_TINY"].asNumber() == engine::kSaltTiny && salts["SALT_START"].asNumber() == engine::kSaltStart,
           "the six seed salts");

    bool lanesOk = d["LANE_IDS"].size() == static_cast<std::size_t> (core::kNumLanes) && d["LANE_DEF"].size() == d["LANE_IDS"].size();
    for (int i = 0; lanesOk && i < core::kNumLanes; ++i)
        lanesOk = d["LANE_IDS"][i].asString() == core::kLanes[i].code && d["LANE_DEF"][i].asNumber() == core::kLanes[i].def;
    check (lanesOk, "the engine's LANE_IDS and LANE_DEF are core's lane registry");
    check (num ("F32_EPS") == core::kF32Eps, "F32_EPS is core::kF32Eps");

    // A change here would be a plain typo, so these come from sl-engine.js's text, not from the fixture.
    check (engine::kMotionDir[static_cast<int> (core::Motion::backward)] == -1 && engine::kMotionDir[static_cast<int> (core::Motion::freeze)] == 0
               && engine::kMotionDir[static_cast<int> (core::Motion::mirror)] == 1 && engine::kMotionDir[static_cast<int> (core::Motion::scatter)] == 1,
           "DIR in core::Motion order: backward -1, freeze 0, the others 1");
    check (engine::kVoicePool == 64 && engine::kMaxPolyphony == 32 && engine::kChaseTicksNow == 4096 && engine::kChaseTicksPer32 == 256
               && engine::kWaitList == 512 && engine::kMaxStarts == 256,
           "the plugin's pool, polyphony, chase and list sizes (plan 1.2, 1.8)");
}

SL_TEST ("engine", engineApiContract)
{
    section ("Engine: the contract's behaviour (no sample, no notes, epochs, the clock, 0 allocations)");

    auto e = std::make_unique<engine::Engine>();
    check (e->sampleRate() == 48000.0 && e->stats().budget == 192, "a new engine is at 48 kHz with the 48 kHz budget");

    const double rates[] = { 44100.0, 96000.0, std::numeric_limits<double>::quiet_NaN(), 1.0e6, 1000.0 };
    const double played[] = { 44100.0, 96000.0, 48000.0, 384000.0, 8000.0 };
    bool ratesOk = true;
    for (int i = 0; i < 5; ++i)
    {
        e->prepare (rates[i]);
        ratesOk = ratesOk && e->sampleRate() == played[i] && e->stats().budget == engine::budgetFor (played[i]);
    }
    check (ratesOk, "prepare clamps the rate to 8000..384000 (48000 when not finite) and sets the budget for it");

    e->prepare (48000.0);
    const engine::Stats s0 = e->stats();
    check (s0.voices == 0 && s0.grains == 0 && s0.thinnedHeads == 0 && s0.maxGrains == 0 && s0.cuts == 0 && s0.steals == 0
               && s0.deferredStarts == 0 && s0.chaseTicks == 0 && s0.nonFinite == 0 && s0.filterResets == 0 && s0.preLimiterPeak == 0.0
               && s0.budget == 192 && e->sampleTime() == 0,
           "after prepare every counter is 0 and the clock is at sample 0");
    check (e->minEpochInUse (engine::VersionKind::snapshot) == rt::kNoEpoch && e->minEpochInUse (engine::VersionKind::sample) == rt::kNoEpoch,
           "with no snapshot and no sample, minEpochInUse is kNoEpoch for both");

    // No sample: silence, the clock moves (JS: if (!this.src) { this.k += n; return; }), note-ons are ignored.
    std::vector<float> L (1000, 0.5f), R (1000, -0.5f);
    e->noteOn (60, 0.8);
    e->process (L.data(), R.data(), 1000);
    check (allZero (L) && allZero (R) && e->sampleTime() == 1000 && e->stats().voices == 0,
           "no sample: process overwrites with silence, the clock moves 1000 samples, noteOn starts nothing");
    e->process (L.data(), R.data(), 0);
    check (e->sampleTime() == 1000, "process (n = 0) does nothing");

    // Epochs: the engine reports the versions it holds.
    core::Snapshot snap;
    auto sample = sineSample (48000, 48000.0);
    e->setSnapshot (&snap, 5);
    e->setSample (sample.get(), 7);
    check (e->minEpochInUse (engine::VersionKind::snapshot) == 5 && e->minEpochInUse (engine::VersionKind::sample) == 7,
           "with only a current snapshot (5) and sample (7), those are the lowest epochs in use");
    e->forgetVersions();
    check (e->minEpochInUse (engine::VersionKind::snapshot) == rt::kNoEpoch && e->minEpochInUse (engine::VersionKind::sample) == rt::kNoEpoch,
           "forgetVersions drops both");
    e->setSnapshot (&snap, 8);
    e->setSample (sample.get(), 9);
    e->prepare (48000.0);
    check (e->minEpochInUse (engine::VersionKind::snapshot) == rt::kNoEpoch && e->minEpochInUse (engine::VersionKind::sample) == rt::kNoEpoch,
           "prepare drops them too (the exchanges hand them back after setPrepared (true))");

    // A sample and an empty clip, stopped, nothing played: silence; then every audio-thread call allocates nothing.
    e->setSnapshot (&snap, 10);
    e->setSample (sample.get(), 11);
    e->setInst (core::defaultInst());
    engine::Transport stopped;
    check (std::isnan (stopped.beat) && ! stopped.playing && stopped.bpm == 120.0 && stopped.loop, "Transport's defaults: stopped, no beat, 120, loop");
    e->setTransport (stopped);
    std::fill (L.begin(), L.end(), 0.25f);
    e->process (L.data(), R.data(), 1000);
    check (allZero (L) && allZero (R) && e->stats().voices == 0, "a sample and an empty clip with nothing played render silence");

    auto frame = std::make_unique<engine::DisplayFrame>();
    alloc::reset();
    {
        const alloc::Scope counting;
        e->setInst (core::defaultInst());
        e->setRampMs (0.0);
        e->setRampMs (20.0);
        e->setBypass (true);
        e->setBypass (false);
        e->snapRamps();
        engine::Transport playing;
        playing.playing = true;
        playing.beat = 0.0;
        e->setTransport (playing);
        e->setSnapshot (&snap, 10);
        e->noteOn (60, 100);
        e->noteOn (64, 0.7, engine::NoteOnOpts { 0.25 });
        e->setController (engine::Ctrl::mod1, 0.25);
        e->setController (engine::Ctrl::bend, 1.0);
        e->setController (engine::Ctrl::sustain, 1.0);
        e->process (L.data(), R.data(), 1000);
        e->noteOff (60);
        e->releaseController (engine::Ctrl::sustain);
        e->releaseController (engine::Ctrl::mod1);
        e->process (L.data(), R.data(), 37);
        e->allNotesOff();
        e->process (L.data(), R.data(), 512);
        e->fillDisplay (*frame);
        e->setSample (nullptr, 0);
        e->process (L.data(), R.data(), 64);
        e->setSample (sample.get(), 11);
        (void) e->stats();
        (void) e->minEpochInUse (engine::VersionKind::snapshot);
        e->reset();
        e->resetStats();
    }
    const auto c = alloc::counts();
    check (c.news == 0 && c.deletes == 0, fmt ("every audio-thread call made 0 allocations and 0 frees (%lld new, %lld delete; counting %s)",
                                               c.news, c.deletes, alloc::coverage()));
    check (e->sampleTime() == 1000 + 1000 + 37 + 512 + 64, "the clock counts every sample processed");
    check (frame->sampleTime == 1000 + 1000 + 37 + 512 && frame->stats.budget == 192 && frame->numVoices <= engine::kDisplayVoices
               && frame->numGrains <= engine::kDisplayGrains,
           "fillDisplay writes the sample time, the stats and counts within the frame");
    note (fmt ("sizeof (DisplayFrame) = %zu bytes, sizeof (Engine) = %zu bytes", sizeof (engine::DisplayFrame), sizeof (engine::Engine)));
}

SL_TEST ("engine", sampleDataRules)
{
    section ("SampleData follows the prototype's setSample rules; VersionExchange stamps its epoch");

    check (engine::makeSampleData (std::vector<float> (7, 0.1f), {}, 44100.0, "short") == nullptr,
           "fewer than 8 frames is no sound at all (nullptr)");

    auto mono = engine::makeSampleData (std::vector<float> (8, 0.1f), {}, 44100.0, "mono");
    check (mono != nullptr && ! mono->stereo && mono->R == mono->L && mono->n == 8 && mono->sr == 44100.0 && mono->name == "mono"
               && mono->L[7] == 0.1f,
           "a left channel alone: mono, R is L, rate and name as given, the samples kept alive");

    auto same = engine::makeSampleData (std::vector<float> (64, 0.25f), std::vector<float> (64, 0.25f), 48000.0, "same");
    check (same != nullptr && ! same->stereo && same->R == same->L, "two identical channels: mono (R = L)");

    std::vector<float> r (64, 0.25f);
    r[63] = 0.5f;
    auto stereo = engine::makeSampleData (std::vector<float> (64, 0.25f), r, 48000.0, "stereo");
    check (stereo != nullptr && stereo->stereo && stereo->R != stereo->L && stereo->R[63] == 0.5f, "channels that differ anywhere: stereo");

    auto odd = engine::makeSampleData (std::vector<float> (64, 0.25f), std::vector<float> (63, 0.5f), 48000.0, "odd");
    check (odd != nullptr && ! odd->stereo && odd->R == odd->L, "a right channel of another length is dropped (mono)");

    auto nanRate = engine::makeSampleData (std::vector<float> (64, 0.25f), {}, std::numeric_limits<double>::quiet_NaN(), "nan");
    check (nanRate != nullptr && std::isnan (nanRate->sr), "the rate is kept as given (the engine resolves a non-finite one)");

    rt::VersionExchange<engine::SampleData> ex (true);
    ex.setPrepared (true);
    ex.publish (std::move (mono));
    ex.acquire();
    check (ex.current() != nullptr && ex.current()->epoch == 1 && ex.currentEpoch() == 1 && ! rt::looksPoisoned (ex.current()),
           "published through VersionExchange: epoch 1 stamped in its first field, not mistaken for poison");
}

SL_TEST ("engine", displayFrameAndCommands)
{
    section ("The display frame, DisplaySlot and the command queue");

    check (engine::displayEvery (48000.0) == 1600 && engine::displayEvery (44100.0) == 1470 && engine::displayEvery (96000.0) == 3200
               && engine::displayEvery (2000.0) == 128,
           "displayEvery = max (128, round (sr / 30))");

    auto slot = std::make_unique<engine::DisplaySlot>();
    engine::DisplayFrame& f = slot->beginWrite();
    f.sampleTime = 4800;
    f.numVoices = 1;
    f.voices[0].noteIdHash = 0x12345678u;
    f.voices[0].numHeads = 2;
    f.voices[0].heads[1].pos = 0.75f;
    slot->endWrite();
    check (slot->takeLatest() && slot->latest().sampleTime == 4800 && slot->latest().voices[0].noteIdHash == 0x12345678u
               && slot->latest().voices[0].heads[1].pos == 0.75f,
           "a frame written into DisplaySlot's back buffer reaches the reader whole");

    auto q = std::make_unique<doc::CommandQueue>();
    int pushed = 0;
    for (int i = 0; i < 300; ++i)
        if (q->push (doc::UiCommand::auditionOn (i % 128, 0.7, i % 2 == 0 ? 0.5 : std::numeric_limits<double>::quiet_NaN())))
            ++pushed;
    check (pushed == 256 && q->size() == 256, fmt ("the queue holds 256 commands and refuses the rest (%d pushed)", pushed));
    doc::UiCommand got;
    bool inOrder = true;
    for (int i = 0; i < 256; ++i)
        inOrder = inOrder && q->pop (got) && got.kind == doc::UiCommandKind::auditionOn && got.note == i % 128 && got.velocity == 0.7
               && (i % 2 == 0 ? got.startPos == 0.5 : std::isnan (got.startPos));
    check (inOrder && ! q->pop (got), "they come out in order, whole, and then the queue is empty");

    const auto on = doc::UiCommand::auditionOn (200, 0.8);
    const auto off = doc::UiCommand::auditionOff (-3);
    const auto letGo = doc::UiCommand::letGoWheel (1);
    const auto learn = doc::UiCommand::midiLearnArm (5);
    check (on.note == 127 && std::isnan (on.startPos) && off.kind == doc::UiCommandKind::auditionOff && off.note == 0
               && letGo.kind == doc::UiCommandKind::letGoWheel && letGo.wheel == 1 && learn.kind == doc::UiCommandKind::midiLearnArm
               && learn.wheel == -1 && doc::UiCommand::allNotesOff().kind == doc::UiCommandKind::allNotesOff
               && doc::UiCommand::previewStart().kind == doc::UiCommandKind::previewStart
               && doc::UiCommand::previewStop().kind == doc::UiCommandKind::previewStop,
           "the factories: notes clamped to 0..127, startPos NaN by default, wheel 1, an unknown learn wheel disarms");
}
