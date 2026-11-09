#pragma once

// Recipes and Dice (S8; sl-model.js "recipes and dice"). A recipe is a complete set of Sample settings
// (Straight plus its own changes) and, for some, a change to the selected notes' Sample lanes, so its result
// never depends on what was set before. Dice rolls the Sample settings within musical limits, each field
// with its own seeded roll, so pinning one field never changes another.

#include "Clip.h"
#include "ClipEdit.h"
#include "Shapes.h"

#include "core/Inst.h"
#include "core/Lanes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sl::model
{
    /** STRAIGHT: the Sample settings every recipe and sound preset starts from. */
    core::InstPatch straightPatch();

    /** What a recipe does to one lane of each selected note. */
    struct RecipeLane
    {
        enum class Action : std::uint8_t
        {
            reset,      // the prototype's null: flat at the lane default, no generator
            shape,      // a shape key on the drawing (Each), and the lane's generator cleared
            generator   // a generator (normalizeGen), the drawing made flat at `base` first when it is set
        };

        core::LaneId lane = core::LaneId::travel;
        Action action = Action::reset;
        ShapeKey shape = ShapeKey::reset;
        Gen gen;
        std::optional<double> base;
    };

    struct Recipe
    {
        int n = 0;
        const char* name = "";
        core::InstPatch inst;              // STRAIGHT merged with the recipe's own changes
        std::vector<RecipeLane> notes;     // in the prototype's key order
    };

    /** SLModel.RECIPES, 1 to 8. */
    const std::vector<Recipe>& recipes();

    struct RecipeResult
    {
        Clip clip;
        core::Inst inst;
    };

    /** SLModel.applyRecipe (clip, inst, n, ids): inst = normalizeInst (mergeInst (normalizeInst (inst), recipe));
        the lanes of the notes in ids changed in the recipe's order. Nothing for a number that is not a recipe
        (the prototype throws). A recipe without note changes shares every note. */
    std::optional<RecipeResult> applyRecipe (const Clip& clip, const core::Inst& inst, int n, const Ids& ids);

    /** SLModel.DICE_FIELDS. */
    inline constexpr const char* kDiceFields[] = {
        "engine", "lock", "motion", "speed", "heads", "spread", "grain", "refresh", "spray", "tape", "rev", "drift", "headTune", "jump"
    };

    /** SLModel.dice (inst, seed, pins): each field not pinned rolls rand01 (hash32 (0x0d1ce, i, 0, seed | 0)), i = 1
        .. 19. pins are DICE_FIELDS names ('grain' pins grainVoice, grainCloud and grainLock) or exact inst keys. */
    core::Inst dice (const core::Inst& inst, double seed, const std::vector<std::string>& pins = {});
}
