#pragma once

// Per-note settings (S7; sl-model.js "note settings"): the Sample settings a note can override, read across a
// selection as a value or "mixed", and the lane-mode rule that changes one lane of one note and nothing else.

#include "Clip.h"
#include "ClipEdit.h"
#include "Node.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "core/Rates.h"

#include <map>
#include <optional>
#include <string>

namespace sl::model
{
    /** A setting a patch may hold: not given (empty), null = follow the instrument (an empty inner value),
        or a value. */
    template <class T>
    using Choice = std::optional<std::optional<T>>;

    /** The Start part of a patch. A non-finite number is ignored, as the prototype ignores it. */
    struct StartPatch
    {
        std::optional<StartMode> mode;
        std::optional<double> pos, range, seed;
    };

    /** applyNotePatch's patch: motion, travel, start, tape, rev, jump, engine, lock, restart, plus vel, glide
        and gc. The prototype also ignores values that are not valid ('mixed', a string that names no engine,
        'yes' for tape): a C++ patch cannot hold those, so Normalize-side readers leave such fields unset. */
    struct SamplePatch
    {
        Choice<core::Motion> motion;
        std::optional<TravelKind> travel;
        std::optional<StartPatch> start;
        Choice<bool> tape, rev, lock;
        Choice<core::RateId> jump;
        Choice<core::Engine> engine;
        std::optional<bool> restart;
        std::optional<double> vel;      // finite: clamped 0..1
        Choice<double> glide;           // finite: at least 0
        std::optional<double> gc;       // finite: clamped -1..1
    };

    /** SLModel.applyNotePatch: writes the patch into n (a note being built or edited). */
    Note& applyNotePatch (Note& n, const SamplePatch& patch);

    /** SLModel.setNoteSample: the patch on a copy of every note in ids. */
    Clip setNoteSample (const Clip& clip, const Ids& ids, const SamplePatch& patch);

    /** One field of getMixed: the value every selected note shares, or mixed. */
    template <class T>
    struct Mixed
    {
        bool mixed = false;
        T value {};
    };

    /** SLModel.getMixed's result. Values compare as JavaScript's ===: a NaN is never shared. */
    struct MixedSettings
    {
        int count = 0;
        Mixed<StartMode> startMode;
        Mixed<double> startPos, startRange;
        Mixed<std::optional<core::Motion>> motion;
        Mixed<TravelKind> travel;
        Mixed<std::optional<bool>> tape, rev;
        Mixed<std::optional<core::RateId>> jump;
        Mixed<std::optional<core::Engine>> engine;
        Mixed<std::optional<bool>> lock;
        Mixed<bool> restart;
        Mixed<double> vel;
        Mixed<std::optional<double>> glide;
        Mixed<double> gc;
    };

    /** SLModel.getMixed: per field the value the selected notes share, or mixed; nothing when no note is
        selected. */
    std::optional<MixedSettings> getMixed (const Clip& clip, const Ids& ids);

    /** SLModel.setLaneNodes, the lane-mode rule (PROMPT 14.5): only that lane of the note with that id
        changes, to normalizeNodes (nodes, the lane default). The note is a copy whose every other field is
        the original's, untouched; every other note is shared. No note with that id: the same notes. */
    Clip setLaneNodes (const Clip& clip, const std::string& id, core::LaneId lane, const NodeList& nodes);

    /** SLModel.setLaneNodesMany: several notes' nodes on one lane at once (the Nodes box drag). */
    Clip setLaneNodesMany (const Clip& clip, core::LaneId lane, const std::map<std::string, NodeList>& nodesById);
}
