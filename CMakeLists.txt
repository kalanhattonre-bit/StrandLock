cmake_minimum_required(VERSION 3.22)

project(StrandLock VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STRANDLOCK_BUILD_PLUGIN "Build the StrandLock VST3 plugin" ON)
option(STRANDLOCK_BUILD_TESTS  "Build the offline StrandLock test runner" ON)

# Static MSVC runtime: the plugin then needs no Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE is pinned to one JUCE 8 release (the same one GrainLock pins) so every build uses identical framework code.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# Sources shared by the plugin and the test runner, so the tests exercise the shipped code.
set(STRANDLOCK_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/StrandLookAndFeel.cpp)

set(STRANDLOCK_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# The version the interface shows, for StrandLock's own sources only (the plugin wrapper and its
# resource script never see it).
set(STRANDLOCK_PRIVATE_DEFINITIONS
    STRANDLOCK_VERSION="${PROJECT_VERSION}")

if(STRANDLOCK_BUILD_PLUGIN)
    juce_add_plugin(StrandLock
        COMPANY_NAME                "Kalan Hatton"
        COMPANY_WEBSITE             "https://github.com/kalanhattonre-bit/StrandLock"
        BUNDLE_ID                   com.kalanhatton.strandlock
        PLUGIN_MANUFACTURER_CODE    Khtn
        PLUGIN_CODE                 Stlk
        FORMATS                     VST3
        PRODUCT_NAME                "StrandLock"
        VERSION                     ${PROJECT_VERSION}
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Instrument Sampler)

    target_sources(StrandLock PRIVATE ${STRANDLOCK_SOURCES})
    target_include_directories(StrandLock PRIVATE Source)
    target_compile_definitions(StrandLock PUBLIC ${STRANDLOCK_DEFINITIONS} PRIVATE ${STRANDLOCK_PRIVATE_DEFINITIONS})
    target_link_libraries(StrandLock
        PRIVATE
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

if(STRANDLOCK_BUILD_TESTS)
    juce_add_console_app(StrandLockTests PRODUCT_NAME "StrandLockTests")

    target_sources(StrandLockTests PRIVATE Tests/TestMain.cpp ${STRANDLOCK_SOURCES})
    target_include_directories(StrandLockTests PRIVATE Source Tests)
    target_compile_definitions(StrandLockTests PRIVATE ${STRANDLOCK_DEFINITIONS} ${STRANDLOCK_PRIVATE_DEFINITIONS} STRANDLOCK_TESTS=1)
    target_link_libraries(StrandLockTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
