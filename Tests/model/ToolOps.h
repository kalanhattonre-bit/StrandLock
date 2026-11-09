#pragma once

// Runs one case of model/tools.json (PLUGIN-PLAN 5.3) through the C++ tools: reads the case's args the way
// the prototype reads them, calls the matching model function and returns what it gave. Shared by the
// fixture test (test_tools.cpp) and the immutability test (test_immutability.cpp).

#include "core/JsonLite.h"
#include "model/Clip.h"
#include "model/NoteSettings.h"

#include <optional>
#include <string>
#include <vector>

namespace sl::test::toolops
{
    struct OpResult
    {
        bool ran = false;                                   // the case could be read and was run
        std::string why;                                    // when it could not
        std::optional<model::Clip> clip;                    // every op but selectSimilar and getMixed
        std::optional<bool> refused;                        // chop and the edits that add notes
        std::optional<double> count;
        std::optional<std::vector<std::string>> ids;        // selectSimilar, addNote, duplicateNotes
        bool hasMixed = false;                              // getMixed
        std::optional<model::MixedSettings> mixed;
    };

    /** The case's op on `in` (the clip the case names). */
    OpResult run (const core::JsonValue& c, const model::Clip& in);

    /** SLModel.getMixed's result as the prototype's object ("mixed" for a field the notes do not share). */
    core::JsonValue mixedToJson (const model::MixedSettings& m);

    /** A setNoteSample / addNote patch as applyNotePatch reads it: 'mixed', undefined and values of the
        wrong type or outside their list are left unset. */
    model::SamplePatch samplePatchFrom (const core::JsonValue& patch);
}
