#pragma once

// Version numbers shared by the plugin, the model and the engine (PLUGIN-PLAN 1.1).
// CMakeLists.txt checks at configure time that kPluginVersion equals project(VERSION).

namespace sl::core
{
    inline constexpr const char* kPluginVersion = "0.1.0";

    /** Layout version of core::Snapshot (the compiled clip the audio thread reads). */
    inline constexpr int kSnapshotVersion = 1;

    /** formatVersion in the plugin state's 16-byte header (PLUGIN-PLAN 3). */
    inline constexpr int kStateFormatVersion = 1;
}
