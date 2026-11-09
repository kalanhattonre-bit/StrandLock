cmake_minimum_required(VERSION 3.22)

project(StrandLock VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STRANDLOCK_BUILD_PLUGIN "Build the StrandLock VST3 plugin" ON)
option(STRANDLOCK_BUILD_TESTS  "Build the offline StrandLock test runner" ON)
option(STRANDLOCK_SANITIZE     "Build StrandCore and the test runner with AddressSanitizer and UBSan (GCC or Clang)" OFF)

if(STRANDLOCK_SANITIZE AND MSVC)
    message(FATAL_ERROR "STRANDLOCK_SANITIZE is for the GCC/Clang test build (the Linux job)")
endif()

# One version: Source/core/Version.h must say the same as project(VERSION) above.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Source/core/Version.h" _strand_version_line REGEX "kPluginVersion = ")
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _strand_version_match "${_strand_version_line}")
if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "Source/core/Version.h has kPluginVersion \"${CMAKE_MATCH_1}\" but CMakeLists.txt says ${PROJECT_VERSION}")
endif()

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

include(cmake/StrandFlags.cmake)

# Sources are found by folder (PLUGIN-PLAN 1.11), so adding a file never edits this one.
# StrandCore: plain C++20 with no JUCE (the model, the engine, the starter sounds, the analysis).
file(GLOB STRANDCORE_SOURCES CONFIGURE_DEPENDS
    Source/core/*.cpp
    Source/rt/*.cpp
    Source/model/*.cpp
    Source/engine/*.cpp
    Source/sounds/*.cpp
    Source/analysis/*.cpp)

# The plugin layer (JUCE): compiled into the plugin and into the test runner, so the tests run the shipped code.
file(GLOB STRANDLOCK_PLUGIN_SOURCES CONFIGURE_DEPENDS Source/*.cpp)
file(GLOB_RECURSE STRANDLOCK_PLUGIN_FOLDER_SOURCES CONFIGURE_DEPENDS
    Source/params/*.cpp
    Source/state/*.cpp
    Source/host/*.cpp
    Source/doc/*.cpp
    Source/diag/*.cpp
    Source/ui/*.cpp
    Source/probe/*.cpp)
list(APPEND STRANDLOCK_PLUGIN_SOURCES ${STRANDLOCK_PLUGIN_FOLDER_SOURCES})

set(STRANDLOCK_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1)

set(STRANDLOCK_JUCE_MODULES
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_gui_extra)

add_library(StrandCore STATIC ${STRANDCORE_SOURCES})
target_include_directories(StrandCore PUBLIC Source)
set_target_properties(StrandCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
strand_core_flags(StrandCore)
if(STRANDLOCK_SANITIZE)
    strand_sanitize_target(StrandCore)
endif()

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

    target_sources(StrandLock PRIVATE ${STRANDLOCK_PLUGIN_SOURCES})
    target_include_directories(StrandLock PRIVATE Source)
    target_compile_definitions(StrandLock PUBLIC ${STRANDLOCK_DEFINITIONS})
    target_link_libraries(StrandLock
        PRIVATE
            StrandCore
            ${STRANDLOCK_JUCE_MODULES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

if(STRANDLOCK_BUILD_TESTS)
    file(GLOB STRANDLOCK_TEST_SOURCES CONFIGURE_DEPENDS
        Tests/TestMain.cpp
        Tests/support/*.cpp
        Tests/model/*.cpp
        Tests/engine/*.cpp
        Tests/plugin/*.cpp
        Tests/ui/*.cpp
        Tests/analysis/*.cpp)

    juce_add_console_app(StrandLockTests PRODUCT_NAME "StrandLockTests")

    target_sources(StrandLockTests PRIVATE ${STRANDLOCK_TEST_SOURCES} ${STRANDLOCK_PLUGIN_SOURCES})
    target_include_directories(StrandLockTests PRIVATE Source Tests)
    # In Debug, jassert reports through Logger::writeToLog, where the runner counts it (no assertion may fire).
    target_compile_definitions(StrandLockTests PRIVATE ${STRANDLOCK_DEFINITIONS} STRANDLOCK_TESTS=1
                                                       $<$<CONFIG:Debug>:JUCE_LOG_ASSERTIONS=1>)
    target_link_libraries(StrandLockTests
        PRIVATE
            StrandCore
            ${STRANDLOCK_JUCE_MODULES}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    strand_fp_flags(StrandLockTests)

    if(STRANDLOCK_SANITIZE)
        strand_sanitize_sources(${STRANDLOCK_TEST_SOURCES} ${STRANDLOCK_PLUGIN_SOURCES})
        target_compile_definitions(StrandLockTests PRIVATE STRANDLOCK_SANITIZE=1)
        target_link_options(StrandLockTests PRIVATE ${STRANDLOCK_SANITIZER_LINK_FLAGS})
    endif()
endif()
