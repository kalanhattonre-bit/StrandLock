#pragma once

// Driving engine::Engine in tests (M2 E2).
//
//  - renderFixtureCase: the render harness of engine/renders.json, exactly as tools/fixtures/engine.js drives the
//    prototype (tools/README-fixtures.md): process () split at every host block boundary, every transport and
//    event sample and every observation point; at each split point the observations of [0, p) first, then the
//    transport entries, then the events. Parity settings: ramps 0 ms, instrument values in double.
//  - Script: a small transport and event driver for the engine-level M2 rows: actions at exact samples (each
//    splits the block, as MidiRouter and HostSync do), host blocks of any sizes, and a hook at every block start
//    (a fake host's setTransport).

#include "core/Inst.h"
#include "core/JsonLite.h"
#include "core/Snapshot.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "engine/SampleData.h"
#include "model/Clip.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sl::test::render
{
    /** What one fixture render produced, in the shapes the fixture records. */
    struct Rendered
    {
        std::vector<float> L, R;
        std::vector<std::vector<std::int64_t>> stats;    // [sample, voices, grains, thinnedHeads, maxGrains, budget, cuts, steals]
        std::vector<std::int64_t> displayAt;             // the sample of each display row
        std::vector<engine::DisplayFrame> display;       // the frame filled there
        std::vector<float> trace;                        // head 0's display pos of the first voice, every 32 samples (NaN = none)
        engine::Stats last;                              // stats () at the end
    };

    /** The case's sound: {starter, seed} (SLSounds.make) or {signal, args} (the test generators). */
    std::unique_ptr<engine::SampleData> soundOf (const core::JsonValue& spec);

    /** The case's clip: a name from the file's clips, "preset:<id>", or a Clip object; then recipe n. Parity clip
        settings (beatsPerBar 4, offsetBeats 0, loop on, tieAcrossLoop off). False when it cannot be made. */
    bool clipOf (const core::JsonValue& ref, const core::JsonValue* recipe, const core::Inst& inst, const std::map<std::string, model::Clip>& clips,
                 model::Clip& out);

    /** mergeInst (current, patch) on JSON: an object value of an object field merges one level (Object.assign), any
        other value replaces. */
    core::JsonValue mergeInstJson (const core::JsonValue& current, const core::JsonValue& patch);

    /** A raw engine-only snapshot (tools/fixtures/engine.js rawSnapOut: BLUEPRINT 3.2, every setting resolved,
        lanes as Tables, glide and chain by id) as a core::Snapshot: the notes in the file's order, each setting
        explicit (no "follow the instrument" marker), the glide and chain the same in all three Auto Slide modes,
        parity clip settings. nullptr (why says what) when a field cannot be read. */
    std::unique_ptr<core::Snapshot> rawSnapshotFromJson (const core::JsonValue& snap, std::string& why);

    /** Renders one renders.json case. False (why says what) when the case cannot be set up. */
    bool renderFixtureCase (const core::JsonValue& c, const std::map<std::string, model::Clip>& clips, Rendered& out, std::string& why);

    //==============================================================================================

    /** A test's own render: actions at exact samples, host blocks, a hook at each block start. */
    class Script
    {
    public:
        using Action = std::function<void (engine::Engine&)>;

        /** Runs fn at sample k (before rendering k). Actions at the same sample run in the order they were added. */
        void at (std::int64_t k, Action fn);

        /** Renders [0, n) into L and R. block (k) gives the size of the host block starting at k (at least 1);
            onBlock (k) runs at every host block start, before that sample's actions. */
        void run (engine::Engine& e, std::int64_t n, const std::function<int (std::int64_t)>& block, std::vector<float>& L,
                  std::vector<float>& R, const std::function<void (engine::Engine&, std::int64_t)>& onBlock = {});

    private:
        std::multimap<std::int64_t, Action> actions;
    };

    /** A fixed block size. */
    std::function<int (std::int64_t)> fixedBlocks (int size);

    /** A sample made from mono samples at a rate. */
    std::unique_ptr<engine::SampleData> monoSample (std::vector<float> x, double sampleRate);

    /** A compiled snapshot of a clip (parity clip settings). */
    std::unique_ptr<core::Snapshot> compile (model::Clip clip, int res = 256);

    /** First sample at or after `from` where |L| or |R| is not 0, -1 when none before `to`. */
    std::int64_t firstSounding (const std::vector<float>& L, const std::vector<float>& R, std::int64_t from, std::int64_t to);
}
