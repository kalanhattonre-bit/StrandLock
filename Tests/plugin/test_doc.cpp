// The document layer (PLUGIN-PLAN 1.3, 2.4, 3, 7.4): SHA-256 for the SMPL file record; ClipDocument's caps (the
// note cap, the snapshot memory budget), its versions and the INST atomics it writes; the SnapshotPublisher
// compiling only the newest of a burst, sharing the tables of notes it saw before, and bounding a snapshot's
// memory; EditHistory's undo that never puts back what automation moved; the SampleManager's cache of 4
// starters, newest-wins requests and the sawopen time; and ParamBridge::applyPatch's host-visible gestures.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "doc/ClipDocument.h"
#include "doc/EditHistory.h"
#include "doc/SampleManager.h"
#include "doc/Sha256.h"
#include "doc/SnapshotPublisher.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Presets.h"
#include "model/Tools.h"
#include "params/ParamBridge.h"
#include "params/ParameterLayout.h"
#include "rt/VersionExchange.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    model::Clip clipOf (int n, double len)
    {
        model::Clip c = model::newClip (64);
        for (int i = 0; i < n; ++i)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (48 + i % 24, (double) (i % 200), len, "c" + std::to_string (i))));
        return c;
    }

    /** Every note with a generator on every lane: no lane is flat, so the tables take their full length. */
    model::Clip everyLaneMoving (int n, double len)
    {
        model::Clip c = clipOf (n, len);
        for (int lane = 0; lane < core::kNumLanes; ++lane)
            c = model::setGen (c, model::Ids::all(), static_cast<core::LaneId> (lane), model::makeGen (model::GenKind::pattern, model::GenPatch {}));
        return c;
    }

    double msSince (std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    }
}

//==================================================================================================
SL_TEST ("doc", sha256Vectors)
{
    section ("SHA-256 (the SMPL file record's): the FIPS 180-4 test vectors, and pieces hash as the whole");

    const std::string abc = "abc", two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check (doc::toHex (doc::sha256 ("", 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "\"\" -> e3b0c442...");
    check (doc::toHex (doc::sha256 (abc.data(), abc.size())) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "\"abc\" -> ba7816bf...");
    check (doc::toHex (doc::sha256 (two.data(), two.size())) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "the 448-bit message -> 248d6a61... (the padding spills into a second block)");
    const std::string million (1000000, 'a');
    doc::Sha256 pieces;
    for (std::size_t at = 0; at < million.size(); at += 997)
        pieces.update (million.data() + at, std::min<std::size_t> (997, million.size() - at));
    const auto piecewise = pieces.finish();
    check (doc::toHex (doc::sha256 (million.data(), million.size())) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
               && piecewise == doc::sha256 (million.data(), million.size()),
           "a million 'a' -> cdc76e5c..., the same in pieces of 997 bytes");
}

SL_TEST ("doc", clipDocumentCaps)
{
    section ("ClipDocument: every edit through limitEdit and the snapshot budget; a refused edit leaves the clip and its version unchanged");

    params::NonParamAtomics atomics;
    doc::ClipDocument d (atomics);
    int changes = 0;
    std::uint64_t lastVersion = 0;
    d.onClipChanged = [&] (const model::ClipPtr&, std::uint64_t v) { ++changes; lastVersion = v; };

    check (d.version() == 1 && d.current()->notes.empty() && d.current()->bars == 4, "a new document: an empty 4-bar clip, version 1");

    auto added = d.apply (model::addNote (*d.current(), 60, 0, 1));
    check (added.accepted && d.version() == 2 && changes == 1 && lastVersion == 2 && d.current()->notes.size() == 1 && added.result.ids.size() == 1
               && d.encodedBytes() == model::encodedClipSize (*d.current()),
           "addNote: accepted, version 2, onClipChanged once, the running size is the clip's encoded size");

    const auto same = d.apply (*d.current());
    check (same.accepted && d.version() == 2 && changes == 1, "an edit that changes nothing is accepted without a new version");

    // The note cap.
    d.replace (std::make_shared<const model::Clip> (clipOf (4096, 1)));
    const auto full = d.current();
    const int before = changes;
    const auto refused = d.apply (model::addNote (*full, 60, 0, 1));
    check (refused.refused && ! refused.accepted && d.current() == full && changes == before && refused.message == "That would make 4097 notes; the limit is 4096.",
           fmt ("a 4097th note is refused, the clip unchanged, no change sent, the toast: \"%s\"", refused.message.c_str()));
    const auto dup = d.apply (model::duplicateNotes (*full, model::Ids { "c0", "c1" }));
    check (dup.refused && d.current() == full && dup.message.find ("4098") != std::string::npos, "Duplicate past 4096 notes is refused too");
    const auto chop = d.apply (model::chop (*full, model::Ids { "c0" }, core::RateId::r1_64, {}, 4096));
    check (chop.refused && d.current() == full, fmt ("Chop past 4096 notes is refused (\"%s\")", chop.message.c_str()));

    // The snapshot budget: 300 notes of 4096 beats with every lane moving need about 393 MB of tables even at
    // res 8; 100 such notes fit (about 131 MB at res 8).
    d.replace (std::make_shared<const model::Clip> (model::newClip (4)));
    const auto small = d.current();
    const auto tooBig = d.apply (everyLaneMoving (300, 4096));
    check (tooBig.refused && d.current() == small && tooBig.message.find ("too detailed to play") != std::string::npos,
           fmt ("a clip whose snapshot would not fit even at res 8 is refused (\"%s\")", tooBig.message.c_str()));
    const auto fits = d.apply (everyLaneMoving (100, 4096));
    check (fits.accepted && d.current()->notes.size() == 100, "100 such notes fit (they compile at a coarser resolution)");

    // The INST atomics follow the document.
    state::InstState s;
    s.seed = 42;
    s.mod1Target = "pitch";
    s.mod2Target = "sparkle";   // a lane this version does not know: kept as text, plays as Mod 2's default (Tone)
    s.mod1Cc = 11;
    s.scale = core::kScales[2].name;
    s.regionStartExact = 0.125;
    d.setInstState (s);
    check (atomics.seed.load() == 42 && atomics.mod1Target.load() == static_cast<std::int8_t> (core::LaneId::pitch)
               && atomics.mod2Target.load() == static_cast<std::int8_t> (core::LaneId::tone) && atomics.mod1Cc.load() == 11 && atomics.scale.load() == 2
               && atomics.regionStartExact.load() == 0.125 && d.instState().mod2Target == "sparkle",
           "setInstState writes the audio thread's atomics: seed 42, Mod 1 -> Pitch on CC11, an unknown Mod 2 target plays Tone, scale 2, the exact Region start");
}

SL_TEST ("doc", snapshotPublisherNewestOnly)
{
    section ("SnapshotPublisher: of a burst of 50 versions only the newest is compiled; seen notes share their tables; the memory budget");

    rt::VersionExchange<core::Snapshot> out;
    doc::SnapshotPublisher pub (out);
    pub.setHeld (true);
    for (int v = 1; v <= 50; ++v)
        pub.submit (std::make_shared<const model::Clip> (clipOf (v, 1)), (std::uint64_t) v);
    pub.setHeld (false);
    const bool published = pub.waitForVersion (50, 10000);
    const auto st = pub.stats();
    const auto* snap = out.current();
    check (published && st.submitted == 50 && st.compiled == 1 && st.skipped == 49 && st.publishedVersion == 50,
           fmt ("50 versions submitted while the thread was held: %d compiled, %d skipped, version %d published",
                (int) st.compiled, (int) st.skipped, (int) st.publishedVersion));
    check (snap != nullptr && snap->clipVersion == 50 && snap->notes.size() == 50 && st.lastRes == 256,
           fmt ("the audio side holds version 50 (50 notes) at res %d", st.lastRes));

    // Strand Demo, then one note's pitch moved: the other 25 notes keep the very same tables.
    auto demo = std::make_shared<const model::Clip> (*model::loadClipPreset ("strand-demo"));
    pub.submit (demo, 51);
    pub.waitForVersion (51, 10000);
    std::vector<const core::NoteTables*> first;
    for (const auto& t : out.current()->tables)
        first.push_back (t.get());
    model::NotePatch patch;
    patch.p = demo->notes[3]->p + 1;
    pub.submit (std::make_shared<const model::Clip> (model::updateNotes (*demo, model::Ids (demo->notes[3]->id), patch)), 52);
    pub.waitForVersion (52, 10000);
    int shared = 0, fresh = 0;
    const auto* second = out.current();
    for (std::size_t i = 0; second != nullptr && i < second->tables.size() && i < first.size(); ++i)
    {
        const auto* t = second->tables[i].get();
        const bool seen = std::find (first.begin(), first.end(), t) != first.end();
        shared += seen ? 1 : 0;
        fresh += seen ? 0 : 1;
    }
    const auto st2 = pub.stats();
    check (shared == 25 && fresh == 1 && st2.cachedNotes == 26 && st2.lastTableBytes == doc::snapshotTableBytes (*second),
           fmt ("after a one-note edit %d notes share their tables, %d is compiled again; the cache holds the last clip's %d notes", shared, fresh, (int) st2.cachedNotes));
    note (fmt ("Strand Demo after a one-note edit compiled in %.2f ms (%d bytes of tables)", st2.lastCompileMs, (int) st2.lastTableBytes));

    // The memory budget: the resolution steps down until the tables fit, and a clip that fits nowhere plays empty.
    doc::SnapshotSizer sizer;
    const auto coarse = everyLaneMoving (100, 1000);   // about 1 GB at res 256, 244 MB at res 64
    std::size_t bytes = 0;
    const int res = sizer.resolutionFor (coarse, &bytes);
    check (res == 64 && bytes <= doc::kSnapshotBudgetBytes && bytes == (std::size_t) 100 * 10 * 64001 * 4 && sizer.bytesAt (coarse, 128) > doc::kSnapshotBudgetBytes,
           fmt ("100 notes of 1000 beats with every lane moving compile at res %d (%.0f MB; at 128 they would need %.0f MB)", res,
                (double) bytes / 1048576.0, (double) sizer.bytesAt (coarse, 128) / 1048576.0));
    const auto flat = clipOf (4096, 64);
    check (sizer.resolutionFor (flat) == 256 && sizer.bytesAt (flat, 256) == (std::size_t) 4096 * 10 * 2 * 4,
           "4096 long notes with flat lanes fit at res 256: every flat table is 2 floats");
    pub.submit (std::make_shared<const model::Clip> (everyLaneMoving (300, 4096)), 53);
    pub.waitForVersion (53, 10000);
    const auto st3 = pub.stats();
    check (st3.tooDetailed && st3.lastRes == 0 && out.current() != nullptr && out.current()->notes.empty() && out.current()->clipVersion == 53,
           "a clip that does not fit even at res 8 is not compiled: an empty snapshot plays, and tooDetailed says so");
}

SL_TEST ("doc", editHistoryAndAutomation)
{
    section ("EditHistory: undo and redo write only the parameters whose values differ between the two states (automation in between is kept)");

    doc::EditHistory h;
    doc::DocState start;
    start.clip = std::make_shared<const model::Clip> (model::newClip (4));
    for (int i = 0; i < params::kNumParams; ++i)
        start.pv[(std::size_t) i] = params::defaultPlain (i);
    h.reset (start);

    const int drift = params::index (params::P::drift), root = params::index (params::P::rootNote);
    doc::NonParamState np;
    check (h.push ("Drift", start.clip, np, { { drift, 0.3f } }), "push \"Drift\" (Drift 0.3)");
    auto clip2 = std::make_shared<const model::Clip> (model::addNote (*start.clip, 60, 0, 1).clip);
    check (h.push ("Add a note", clip2, np, {}), "push \"Add a note\" (no parameter)");
    doc::NonParamState np2;
    np2.sound.starterId = "pad";
    check (h.push ("Sound: Evolving pad", clip2, np2, { { root, 60.0f } }), "push \"Sound: Evolving pad\" (a sound with the root it already had)");
    check (! h.push ("Nothing", clip2, np2, {}), "pushing the same state again adds nothing");

    // No step changed Speed (macro1), so no undo writes it: whatever automation set there stays.
    auto u1 = h.undo();
    check (u1.has_value() && u1->label == "Sound: Evolving pad" && u1->params.empty() && u1->state.np.sound.starterId == "sawopen",
           "undo \"Sound\": the sound before comes back; no parameter is written (the root was the same)");
    auto u2 = h.undo();
    check (u2.has_value() && u2->label == "Add a note" && u2->params.empty() && u2->state.clip == start.clip,
           "undo \"Add a note\": the clip before, no parameter written (so Speed keeps its automation)");
    auto u3 = h.undo();
    bool driftBack = u3.has_value() && u3->params.size() == 1 && u3->params[0].first == drift && u3->params[0].second == params::defaultPlain (drift);
    check (driftBack && ! h.canUndo(), "undo \"Drift\": writes Drift back to 0 and nothing else; nothing left to undo");
    auto r1 = h.redo();
    check (r1.has_value() && r1->params.size() == 1 && r1->params[0].second == 0.3f && h.redoLabel().value_or ("") == "Add a note",
           "redo writes Drift 0.3 again; the next redo is \"Add a note\"");
}

SL_TEST ("doc", sampleManagerCacheAndNewest)
{
    section ("SampleManager: the starter made on the worker (sawopen's time), a cache of 4, newest wins, a missing file");

    rt::VersionExchange<engine::SampleData> out;
    doc::SampleManager m (out);

    // A seed no other test uses, so the worker really makes it (sharedStarterSound shares a live one).
    const auto t0 = std::chrono::steady_clock::now();
    const auto r1 = m.loadStarter (sounds::StarterId::sawopen, 9001, doc::LoadOrigin::startup);
    const bool done = m.waitFor (r1, 10000);
    const double waited = msSince (t0);
    const auto cur = m.current();
    check (done && cur != nullptr && cur->builtIn && cur->starter == sounds::StarterId::sawopen && cur->seed == 9001 && out.current() != nullptr
               && out.current()->L == cur->left && m.startersMade() == 1,
           "sawopen (seed 9001) made on the worker and published: the strip's view and the engine's SampleData share their samples");
    note (fmt ("sawopen made on the worker in %.2f ms (target 60 ms; the JS takes 88 ms); the request took %.2f ms", m.lastStarterMs(), waited));
    check (m.lastStarterMs() <= 300.0, fmt ("sawopen in under 5 x its 60 ms target (%.2f ms)", m.lastStarterMs()));

    // The cache of 4: sawopen, saw, square, vowel, noise -> sawopen falls out.
    for (auto id : { sounds::StarterId::saw, sounds::StarterId::square, sounds::StarterId::vowel, sounds::StarterId::noise })
        m.waitFor (m.loadStarter (id, 9001, doc::LoadOrigin::project), 10000);
    check (m.startersMade() == 5 && m.cachedStarters() == 4, fmt ("5 starters made, 4 kept (%d)", m.cachedStarters()));
    m.waitFor (m.loadStarter (sounds::StarterId::noise, 9001, doc::LoadOrigin::project), 10000);
    m.waitFor (m.loadStarter (sounds::StarterId::saw, 9001, doc::LoadOrigin::project), 10000);
    check (m.startersMade() == 5 && m.current()->starter == sounds::StarterId::saw, "noise and saw again come from the cache (none made)");
    m.waitFor (m.loadStarter (sounds::StarterId::sawopen, 9001, doc::LoadOrigin::project), 10000);
    check (m.startersMade() == 6, "sawopen again is made again: it was the oldest of 5");

    // Newest wins: three user loads in a row; only the last reaches the owner.
    const auto a = m.loadStarter (sounds::StarterId::pad, 9002, doc::LoadOrigin::user);
    const auto b = m.loadStarter (sounds::StarterId::pluck, 9002, doc::LoadOrigin::user);
    const auto c = m.loadStarter (sounds::StarterId::beat, 9002, doc::LoadOrigin::user);
    m.waitIdle (20000);
    auto pending = m.takePending();
    const bool onlyNewest = pending.size() == 1 && pending[0].request == c && pending[0].view->starter == sounds::StarterId::beat
                         && pending[0].patch.root == 60.0 && pending[0].patch.cents == 0.0 && pending[0].patch.region.start == 0.0
                         && pending[0].patch.region.end == 1.0;
    check (onlyNewest && a < b && b < c, fmt ("of requests %d, %d, %d only the newest (Beat loop) waits for the owner, with root 60, cents 0, the whole Region",
                                              (int) a, (int) b, (int) c));
    check (m.current()->starter == sounds::StarterId::sawopen, "nothing is published before the owner commits it");
    const bool committed = ! pending.empty() && m.commit (std::move (pending[0]));
    check (committed && m.current()->starter == sounds::StarterId::beat, "the owner commits it: Beat loop plays");

    // A user load the project overtook is never published.
    const auto userLoad = m.loadStarter (sounds::StarterId::square, 9001, doc::LoadOrigin::user);
    m.waitFor (userLoad, 10000);
    auto late = m.takePending();
    m.waitFor (m.loadStarter (sounds::StarterId::vowel, 9001, doc::LoadOrigin::project), 10000);
    const bool lateRefused = late.size() == 1 && ! m.commit (std::move (late[0]));
    check (lateRefused && m.current()->starter == sounds::StarterId::vowel, "a user load older than the newest request is not published when the owner commits it");

    // A project file that is not there: the default starter plays and the record is kept.
    state::SampleRef missing;
    missing.kind = state::kSampleFile;
    missing.file.path = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLockTests").getChildFile ("no such file.wav")
                            .getFullPathName().toStdString();
    missing.file.displayName = "no such file.wav";
    missing.file.fileSize = 1234;
    m.waitFor (m.loadRef (missing, doc::LoadOrigin::project), 10000);
    const auto mv = m.current();
    const auto reports = m.takeReports();
    const bool toast = ! reports.empty() && ! reports.back().ok && reports.back().message.find ("no such file.wav") != std::string::npos;
    check (mv->missing && mv->builtIn && mv->starter == sounds::kDefaultStarter && state::encodeSmpl (mv->ref) == state::encodeSmpl (missing) && toast,
           fmt ("a missing file: the Saw that opens up plays, the record is kept as it was, the toast: \"%s\"",
                reports.empty() ? "" : reports.back().message.c_str()));
    check (! m.busy() && m.finishedRequest() == m.lastRequest(), "the worker is idle and every request is finished");
}

SL_TEST ("doc", snapshotPublisherOrderAndFailure)
{
    section ("SnapshotPublisher: a version older than one handed over is dropped; a compile that throws ends its wait and the thread lives on (fix pass RT2-4, RT2-3)");

    rt::VersionExchange<core::Snapshot> out;
    doc::SnapshotPublisher pub (out);

    // A state load and an edit on two threads can hand over version 9 and then 8.
    pub.setHeld (true);
    pub.submit (std::make_shared<const model::Clip> (clipOf (9, 1)), 9);
    pub.submit (std::make_shared<const model::Clip> (clipOf (8, 1)), 8);
    pub.setHeld (false);
    const bool nine = pub.waitForVersion (9, 3000);
    const auto st = pub.stats();
    const auto* snap = out.current();
    check (nine && st.stale == 1 && st.compiled == 1 && snap != nullptr && snap->clipVersion == 9 && snap->notes.size() == 9,
           fmt ("versions 9 then 8 handed over: 8 is dropped (%d stale), 9 is compiled and plays (version %d, %d notes)", (int) st.stale,
                snap != nullptr ? (int) snap->clipVersion : -1, snap != nullptr ? (int) snap->notes.size() : -1));

    // A compile that runs out of memory.
    pub.failNextCompile();
    pub.submit (std::make_shared<const model::Clip> (clipOf (5, 1)), 10);
    const auto t0 = std::chrono::steady_clock::now();
    const bool ten = pub.waitForVersion (10, 3000);
    const double waited = msSince (t0);
    const auto st2 = pub.stats();
    const auto* empty = out.current();
    check (ten && st2.failed == 1 && st2.tooDetailed && empty != nullptr && empty->clipVersion == 10 && empty->notes.empty(),
           fmt ("a compile that throws: the wait for its version ends (%.1f ms), its notes are silent (an empty snapshot) and tooDetailed says so", waited));
    pub.submit (std::make_shared<const model::Clip> (clipOf (7, 1)), 11);
    const bool eleven = pub.waitForVersion (11, 3000);
    const auto st3 = pub.stats();
    check (eleven && out.current() != nullptr && out.current()->notes.size() == 7 && ! st3.tooDetailed && st3.failed == 1,
           "the compiler thread lives on: the next version compiles and plays (7 notes)");
}

SL_TEST ("state", userLoadOvertakenByStateLoad)
{
    section ("A sound the user picked that a state load overtook changes nothing when it arrives: the parameters, SMPL and the undo history stay the state's (fix pass RT2-1)");

    const int root = params::index (params::P::rootNote), start = params::index (params::P::regionStart);

    // The state: the Square sweep, with root D3 and the Region from 0.25.
    juce::MemoryBlock saved;
    {
        StrandLockProcessor maker;
        maker.loadStarterSound (sounds::StarterId::square);
        maker.waitForSounds();
        params::setPlainValue (maker.getParam (root), root, 62.0f);
        params::setPlainValue (maker.getParam (start), start, 0.25f);
        maker.getStateInformation (saved);
    }

    StrandLockProcessor p;
    p.waitForSounds();
    // The user picks the Evolving pad; the worker makes it and leaves it for the message thread, which has not
    // run yet (no message loop here) when the host loads the project.
    const auto picked = p.loadStarterSound (sounds::StarterId::pad);
    p.getSampleManager().waitIdle (10000);
    p.setStateInformation (saved.getData(), (int) saved.getSize());
    const int published = p.deliverSoundLoads();

    juce::MemoryBlock after;
    p.getStateInformation (after);
    const auto sound = p.getCurrentSound();
    check (picked > 0 && published == 0 && sound != nullptr && sound->starter == sounds::StarterId::square && p.getSampleRef().starterId == "square",
           fmt ("the pad arrives after the project: it is not published, the Square sweep plays and SMPL names it (\"%s\")", p.getSampleRef().starterId.c_str()));
    check (juce::exactlyEqual (params::plainValueOf (p.getParam (root)), 62.0f) && juce::exactlyEqual (params::plainValueOf (p.getParam (start)), 0.25f),
           fmt ("the state's root (%.0f) and Region start (%.2f) stay: the pad's patch (root 60, the whole sound) is not applied",
                (double) params::plainValueOf (p.getParam (root)), (double) params::plainValueOf (p.getParam (start))));
    check (! p.canUndo() && after == saved, "no undo step, and a save gives back the loaded state byte for byte");
}

SL_TEST ("doc", prepareWaitsOnlyForSelfPublished)
{
    section ("prepareToPlay waits for a project's sound, not for one the user picked, which only the message thread publishes (fix pass RT2-5)");

    StrandLockProcessor p;
    p.waitForSounds();
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    auto& m = p.getSampleManager();

    // The user's pick, not made yet (the worker is held, as if it were decoding a long file).
    m.setHeld (true);
    p.loadStarterSound (sounds::StarterId::pluck);
    auto t0 = std::chrono::steady_clock::now();
    p.prepareToPlay (48000.0, 256);
    const double userMs = msSince (t0);
    p.releaseResources();
    m.setHeld (false);
    p.waitForSounds();
    const auto pick = p.getCurrentSound();
    check (userMs < 2000.0 && pick != nullptr && pick->starter == sounds::StarterId::pluck,
           fmt ("with the user's pick still to make, prepareToPlay returned in %.1f ms (it waited up to 10 s before); the pick plays once delivered", userMs));

    // A no-op is not a pass: a project's sound, held 300 ms on the worker, is still waited for.
    m.setHeld (true);
    state::SampleRef ref;
    ref.kind = state::kSampleStarter;
    ref.starterId = "vowel";
    ref.seed = 1;
    m.loadRef (ref, doc::LoadOrigin::project);
    std::thread release ([&m] {
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        m.setHeld (false);
    });
    t0 = std::chrono::steady_clock::now();
    p.prepareToPlay (48000.0, 256);
    const double projectMs = msSince (t0);
    release.join();
    p.releaseResources();
    const auto loaded = p.getCurrentSound();
    check (projectMs >= 250.0 && projectMs < 5000.0 && loaded != nullptr && loaded->starter == sounds::StarterId::vowel,
           fmt ("a project's sound held 300 ms: prepareToPlay waited %.0f ms for it, and it plays", projectMs));
}

SL_TEST ("params", applyPatchGestures)
{
    section ("ParamBridge::applyPatch (plan 2.4): each changed parameter in a gesture of its own, as host-visible edits; the INST fields go to the state");

    struct Counter final : public juce::AudioProcessorListener
    {
        int begins = 0, ends = 0, changes = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
    };

    StrandLockProcessor p;
    Counter counter;
    p.addListener (&counter);

    core::InstPatch patch;
    patch.root = 62.0;
    patch.cents = -30.5;
    patch.region.start = 0.25;
    patch.region.end = 0.5;
    patch.engine = core::Engine::tape;
    patch.macros = std::array<double, core::kNumMacros> { 0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7 };
    patch.vibSync = static_cast<std::int8_t> (core::RateId::r1_8);
    patch.seed = 77;
    const auto changed = params::applyPatch (p.getApvts(), patch);

    auto plain = [&p] (params::P which) { return params::plainValueOf (p.getParam (which)); };
    check (changed.size() == 14 && counter.begins == 14 && counter.ends == 14 && counter.changes >= 14,
           fmt ("14 parameters changed, each in its own gesture the host sees: %d changed, %d begins, %d ends, %d changes",
                (int) changed.size(), counter.begins, counter.ends, counter.changes));
    check (plain (params::P::rootNote) == 62.0f && plain (params::P::rootCents) == -30.5f && plain (params::P::regionStart) == 0.25f
               && plain (params::P::regionEnd) == 0.5f && plain (params::P::engine) == 0.0f && plain (params::P::macro3) == 0.7f
               && plain (params::P::vibSync) == (float) ((int) core::RateId::r1_8 + 1),
           "root D3, cents -30.5, Region 25-50 %, engine Tape, the macros 0.7, Vibrato Sync 1/8 (its index + 1), all exact");
    check (plain (params::P::drift) == 0.0f && p.getInstState().seed == 1, "Drift was not named, so it is untouched; the seed is not a parameter");
    check (p.getDiag().snapshot().hostParamChanges == 0, "the diagnostics do not count them as the host's (StrandLock's own gestures)");

    const int beforeBegins = counter.begins;
    const auto again = params::applyPatch (p.getApvts(), patch);
    check (again.empty() && counter.begins == beforeBegins, "the same patch again changes nothing and opens no gesture");

    state::InstState s;
    core::InstPatch statePatch;
    statePatch.seed = 77;
    statePatch.mod1.target = static_cast<std::int8_t> (core::LaneId::pitch);
    statePatch.mod2.target = core::kModOff;
    statePatch.mod2.cc = 300;
    statePatch.scale = 2;
    statePatch.scaleLock = true;
    const bool any = params::applyStateFields (s, statePatch);
    check (any && s.seed == 77 && s.mod1Target == "pitch" && s.mod2Target == "off" && s.mod2Cc == 127 && s.scale == core::kScales[2].name && s.scaleLock == 1,
           "applyStateFields: seed 77, Mod 1 -> \"pitch\", Mod 2 off on CC127 (300 clamped), the scale by name, scale lock on");
    p.removeListener (&counter);
}

SL_TEST ("doc", processorFootprint)
{
    section ("The processor object stays small: tests keep several on the test thread's stack (1 MB on Windows)");

    // The diagnostics ring (4096 events, about 200 KB) and the command queue live on the heap. With them inline,
    // probe/levels' five processors overflowed Windows' 1 MB main-thread stack (CI run 42732142894).
    const std::size_t size = sizeof (StrandLockProcessor);
    check (size <= 128u * 1024u, fmt ("sizeof (StrandLockProcessor) = %d bytes (at most 128 KB, so five fit in 1 MB with room)", (int) size));
}
