#pragma once

// Which host StrandLock runs in (PLUGIN-PLAN 1.1, M0), for the Diagnostics page and its Copy report: the
// name juce::PluginHostType recognises, the host program's file version (on Windows, from the program's own
// version resource: "13.0.41"), its path and the plugin format. Found once, on the message thread.

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>

namespace sl::host
{
    struct HostInfo
    {
        juce::String name;      // PluginHostType's description: "Steinberg Cubase", "Unknown"
        juce::String version;   // the host program's file version, or empty when it has none
        juce::String path;      // the host program
        juce::String format;    // "VST3", "Standalone", "Undefined" (the test runner)
        bool isCubase = false;
        bool isSteinberg = false;

        /** "Steinberg Cubase 13.0.41 (VST3)". */
        juce::String summary() const;
    };

    HostInfo detectHost();

    /** The four-part file version in a Windows program's version resource ("13.0.41.123"), or empty (and
        always empty elsewhere). */
    juce::String executableVersion (const juce::File& program);
}
