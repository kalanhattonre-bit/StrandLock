#pragma once

// Tests and writers register themselves, so adding one never edits Tests/TestMain.cpp (PLUGIN-PLAN 7.1-7.3).
//
//   SL_TEST ("model", nodesSampleSmooth) { check (...); }
//       A test in a group. Every test runs by default; --only <group> runs one group (a comma list runs
//       several). The runner sorts by group, then name. A test that makes no check fails.
//
//   SL_SNAPSHOT (editorDark) { return image; }            (a juce::Image)
//       --snapshot DIR writes it as DIR/editorDark.png.
//
//   SL_FINGERPRINT (strandDemo) { return text; }          (a std::string)
//       --write-fingerprint DIR writes it as DIR/strandDemo.txt (until M9 registers one, the runner
//       writes DIR/placeholder.txt so the upload always has a file).
//
//   SL_STATE_FIXTURE (v1_0_defaults) { return block; }    (a juce::MemoryBlock)
//       --write-state-fixtures DIR writes it as DIR/v1_0_defaults.bin.
//
// Names are C++ identifiers; within a group (and within each kind of writer) they must be unique.
// A file using SL_SNAPSHOT or SL_STATE_FIXTURE includes the JUCE headers for its return type.

#include <string>
#include <vector>

namespace juce
{
    class Image;
    class MemoryBlock;
}

namespace sl::test
{
    using TestFn = void (*)();
    using SnapshotFn = juce::Image (*)();
    using FingerprintFn = std::string (*)();
    using StateFixtureFn = juce::MemoryBlock (*)();

    struct TestEntry
    {
        const char* group;
        const char* name;
        TestFn run;
        const char* file;
        int line;
    };

    template <class Fn>
    struct WriterEntry
    {
        const char* name;
        Fn write;
        const char* file;
        int line;
    };

    inline std::vector<TestEntry>& registeredTests()                         { static std::vector<TestEntry> list; return list; }
    inline std::vector<WriterEntry<SnapshotFn>>& registeredSnapshots()       { static std::vector<WriterEntry<SnapshotFn>> list; return list; }
    inline std::vector<WriterEntry<FingerprintFn>>& registeredFingerprints() { static std::vector<WriterEntry<FingerprintFn>> list; return list; }
    inline std::vector<WriterEntry<StateFixtureFn>>& registeredStateFixtures() { static std::vector<WriterEntry<StateFixtureFn>> list; return list; }

    struct TestRegistrar
    {
        TestRegistrar (const char* group, const char* name, TestFn run, const char* file, int line)
        {
            registeredTests().push_back ({ group, name, run, file, line });
        }
    };

    template <class Fn>
    struct WriterRegistrar
    {
        WriterRegistrar (std::vector<WriterEntry<Fn>>& list, const char* name, Fn write, const char* file, int line)
        {
            list.push_back ({ name, write, file, line });
        }
    };

    /** What the runner was started with, for the tests that need it. TestMain fills it before any test runs. */
    struct Options
    {
        double allocMinutes = 10.0;   // --alloc-minutes N: minutes of audio for the allocation runs
        std::string fixturesDir;      // --fixtures DIR, else Tests/fixtures found upward from the working folder
        std::string repoRoot;         // the folder holding docs/ and Tests/; empty when it was not found
    };

    inline Options& options() { static Options o; return o; }
}

#define SL_TEST(group, name) \
    static void slTest_##name(); \
    [[maybe_unused]] static const ::sl::test::TestRegistrar slTestRegistrar_##name { group, #name, &slTest_##name, __FILE__, __LINE__ }; \
    static void slTest_##name()

#define SL_SNAPSHOT(name) \
    static ::juce::Image slSnapshot_##name(); \
    [[maybe_unused]] static const ::sl::test::WriterRegistrar<::sl::test::SnapshotFn> slSnapshotRegistrar_##name { ::sl::test::registeredSnapshots(), #name, &slSnapshot_##name, __FILE__, __LINE__ }; \
    static ::juce::Image slSnapshot_##name()

#define SL_FINGERPRINT(name) \
    static std::string slFingerprint_##name(); \
    [[maybe_unused]] static const ::sl::test::WriterRegistrar<::sl::test::FingerprintFn> slFingerprintRegistrar_##name { ::sl::test::registeredFingerprints(), #name, &slFingerprint_##name, __FILE__, __LINE__ }; \
    static std::string slFingerprint_##name()

#define SL_STATE_FIXTURE(name) \
    static ::juce::MemoryBlock slStateFixture_##name(); \
    [[maybe_unused]] static const ::sl::test::WriterRegistrar<::sl::test::StateFixtureFn> slStateFixtureRegistrar_##name { ::sl::test::registeredStateFixtures(), #name, &slStateFixture_##name, __FILE__, __LINE__ }; \
    static ::juce::MemoryBlock slStateFixture_##name()
