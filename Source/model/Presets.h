#pragma once

// Sound and clip presets (U4; sl-model.js "presets"). A sound preset is the sample, engine, Sample settings
// and macros, and never touches a clip; a clip preset is bars and notes, and never touches the instrument.
// The 12 clip preset builders are ported line by line (PLUGIN-PLAN 1.2); model/presets.json checks every node.

#include "Clip.h"

#include "core/Inst.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sl::model
{
    struct SoundPreset
    {
        const char* id;
        const char* name;
        const char* sound;          // the starter sound to load
        core::InstPatch inst;       // the preset's own patch
    };

    /** SLModel.SOUND_PRESETS, in menu order. */
    const std::vector<SoundPreset>& soundPresets();

    /** SOUND_BASE: STRAIGHT's Sample settings, the full Region and centred macros. */
    core::InstPatch soundBase();

    struct SoundPresetResult
    {
        std::string sound;
        core::Inst inst;
    };

    /** SLModel.applySoundPreset (inst, id): the sound to load and normalizeInst (mergeInst (mergeInst
        (normalizeInst (inst), SOUND_BASE), preset)), found by id or by name. The sound knobs (pitch, root, glide,
        scale, wheels, ...) are left alone. Nothing for an unknown preset (the prototype throws). */
    std::optional<SoundPresetResult> applySoundPreset (const core::Inst& inst, std::string_view idOrName);

    struct ClipPreset
    {
        std::string id;
        std::string name;
        int bars = 4;
        std::vector<NotePtr> notes;   // as the builder made them (loadClipPreset normalizes a copy)
    };

    /** SLModel.strandDemo (): v3's demo with v3's nodes, ids sd-1 .. sd-26. */
    ClipPreset strandDemo();

    /** SLModel.CLIP_PRESETS (clipPresetList ()), built once, in menu order. */
    const std::vector<ClipPreset>& clipPresets();

    /** SLModel.loadClipPreset (id): a fresh clip (normalizeClip of the preset's bars and notes), found by id or by
        name. Nothing for an unknown preset (the prototype throws). */
    std::optional<Clip> loadClipPreset (std::string_view idOrName);
}
