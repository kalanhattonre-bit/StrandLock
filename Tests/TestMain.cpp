// StrandLock's offline test runner (PLUGIN-PLAN 7.1, 7.2). This file is only the runner: tests,
// snapshots, fingerprints and state fixtures register themselves (Tests/support/TestRegistry.h), so
// nobody edits it after the scaffold.
//
//   StrandLockTests                              run every test
//   StrandLockTests --only alloc                 run one group (a comma list runs several: --only contract,model)
//   StrandLockTests --list                       list the registered tests and writers
//   StrandLockTests --alloc-minutes N            minutes of audio for the allocation runs (default 10)
//   StrandLockTests --fixtures DIR               the golden fixtures (default: Tests/fixtures, found upward from here)
//   StrandLockTests --snapshot DIR               write every SL_SNAPSHOT as DIR/<name>.png
//   StrandLockTests --write-fingerprint DIR      write every SL_FINGERPRINT as DIR/<name>.txt
//   StrandLockTests --write-state-fixtures DIR   write every SL_STATE_FIXTURE as DIR/<name>.bin
//
// It exits 0 only when every check passed, every writer wrote its file and no JUCE assertion fired.
// Assertions are compiled into the Debug build only; there the runner counts them through a Logger
// (the Debug build sets JUCE_LOG_ASSERTIONS=1, so jassert reports to Logger::writeToLog).

#include <juce_audio_processors/juce_audio_processors.h>

#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace
{
    using namespace sl::test;

    /** Counts JUCE assertion reports and prints every message it is sent. */
    struct AssertionCounter final : public juce::Logger
    {
        std::atomic<int> count { 0 };

        void logMessage (const juce::String& message) override
        {
            if (message.startsWith ("JUCE Assertion failure"))
                ++count;
            std::printf ("%s\n", message.toRawUTF8());
            std::fflush (stdout);
        }
    };

    struct Args
    {
        std::vector<std::string> only;
        bool list = false;
        double allocMinutes = 10.0;
        std::string fixtures, snapshotDir, fingerprintDir, stateFixtureDir;
    };

    void printUsage()
    {
        std::printf ("StrandLockTests [--only G[,G...]] [--list] [--alloc-minutes N] [--fixtures DIR]\n"
                     "                [--snapshot DIR | --write-fingerprint DIR | --write-state-fixtures DIR]\n");
    }

    std::vector<std::string> splitList (const std::string& text)
    {
        std::vector<std::string> out;
        std::string item;
        for (const char c : text + ",")
        {
            if (c == ',')
            {
                if (! item.empty())
                    out.push_back (item);
                item.clear();
            }
            else if (c != ' ')
            {
                item += c;
            }
        }
        return out;
    }

    /** False (after printing why) for an unknown flag or a flag without its value. */
    bool parseArgs (int argc, char** argv, Args& a)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg (argv[i]);
            const bool hasValue = i + 1 < argc;
            auto value = [&] { return std::string (argv[++i]); };

            if (arg == "--list")                                       a.list = true;
            else if (arg == "--only" && hasValue)                      a.only = splitList (value());
            else if (arg == "--alloc-minutes" && hasValue)             a.allocMinutes = juce::String (value()).getDoubleValue();
            else if (arg == "--fixtures" && hasValue)                  a.fixtures = value();
            else if (arg == "--snapshot" && hasValue)                  a.snapshotDir = value();
            else if (arg == "--write-fingerprint" && hasValue)         a.fingerprintDir = value();
            else if (arg == "--write-state-fixtures" && hasValue)      a.stateFixtureDir = value();
            else
            {
                std::printf ("Unknown or incomplete argument: %s\n", arg.c_str());
                printUsage();
                return false;
            }
        }

        const int writers = (a.snapshotDir.empty() ? 0 : 1) + (a.fingerprintDir.empty() ? 0 : 1) + (a.stateFixtureDir.empty() ? 0 : 1);
        if (writers > 1)
        {
            std::printf ("Give one of --snapshot, --write-fingerprint, --write-state-fixtures at a time.\n");
            return false;
        }
        return true;
    }

    /** Fills options(): the fixtures folder and the repository root. */
    void resolvePaths (const Args& a)
    {
        auto& o = options();
        o.allocMinutes = a.allocMinutes;

        const auto cwd = juce::File::getCurrentWorkingDirectory();
        if (! a.fixtures.empty())
        {
            const auto dir = cwd.getChildFile (juce::String::fromUTF8 (a.fixtures.c_str()));
            o.fixturesDir = dir.getFullPathName().toStdString();
            const auto root = dir.getParentDirectory().getParentDirectory();
            if (root.getChildFile ("docs").isDirectory())
                o.repoRoot = root.getFullPathName().toStdString();
            return;
        }

        for (auto dir = cwd;; dir = dir.getParentDirectory())
        {
            const auto fixtures = dir.getChildFile ("Tests").getChildFile ("fixtures");
            if (fixtures.isDirectory())
            {
                o.fixturesDir = fixtures.getFullPathName().toStdString();
                o.repoRoot = dir.getFullPathName().toStdString();
                return;
            }
            if (dir.isRoot() || dir.getParentDirectory() == dir)
                break;
        }
    }

    bool lessByGroupThenName (const TestEntry& a, const TestEntry& b)
    {
        const int g = std::strcmp (a.group, b.group);
        return g != 0 ? g < 0 : std::strcmp (a.name, b.name) < 0;
    }

    void listEverything()
    {
        auto tests = registeredTests();
        std::stable_sort (tests.begin(), tests.end(), lessByGroupThenName);
        std::printf ("Tests (%d):\n", (int) tests.size());
        for (const auto& t : tests)
            std::printf ("  %-10s %s  (%s:%d)\n", t.group, t.name, t.file, t.line);

        auto listWriters = [] (const char* title, const auto& list)
        {
            std::printf ("%s (%d):\n", title, (int) list.size());
            for (const auto& w : list)
                std::printf ("  %s  (%s:%d)\n", w.name, w.file, w.line);
        };
        listWriters ("Snapshots, --snapshot", registeredSnapshots());
        listWriters ("Fingerprints, --write-fingerprint", registeredFingerprints());
        listWriters ("State fixtures, --write-state-fixtures", registeredStateFixtures());
    }

    //==============================================================================================
    struct GroupTally
    {
        std::string group;
        int tests = 0;
        long long checks = 0;
        long long failures = 0;
    };

    int runTests (const std::vector<std::string>& only)
    {
        auto tests = registeredTests();
        std::stable_sort (tests.begin(), tests.end(), lessByGroupThenName);

        for (std::size_t i = 1; i < tests.size(); ++i)
            if (std::strcmp (tests[i].group, tests[i - 1].group) == 0 && std::strcmp (tests[i].name, tests[i - 1].name) == 0)
                check (false, fmt ("test names are unique in a group: %s / %s is registered twice (%s:%d, %s:%d)",
                                   tests[i].group, tests[i].name, tests[i - 1].file, tests[i - 1].line, tests[i].file, tests[i].line));

        std::vector<TestEntry> chosen;
        for (const auto& t : tests)
            if (only.empty() || std::find (only.begin(), only.end(), std::string (t.group)) != only.end())
                chosen.push_back (t);

        for (const auto& g : only)
            if (std::none_of (tests.begin(), tests.end(), [&g] (const TestEntry& t) { return g == t.group; }))
                std::printf ("Note: group \"%s\" has no tests yet.\n", g.c_str());

        if (chosen.empty())
        {
            std::printf ("No registered test matches --only; nothing ran.\n");
            return 1;
        }

        std::vector<GroupTally> groups;
        for (const auto& t : chosen)
        {
            std::printf ("\n######## %s / %s\n", t.group, t.name);
            std::fflush (stdout);

            const Tally before = tally();
            try
            {
                t.run();
            }
            catch (const std::exception& e)
            {
                check (false, fmt ("%s / %s ran to the end (it threw: %s)", t.group, t.name, e.what()));
            }
            catch (...)
            {
                check (false, fmt ("%s / %s ran to the end (it threw a non-standard exception)", t.group, t.name));
            }

            if (tally().checks == before.checks)
                check (false, fmt ("%s / %s made at least one check (a test with no check proves nothing)", t.group, t.name));

            const Tally after = tally();
            if (groups.empty() || groups.back().group != t.group)
                groups.push_back ({ t.group, 0, 0, 0 });
            auto& g = groups.back();
            ++g.tests;
            g.checks += after.checks - before.checks;
            g.failures += after.failures - before.failures;
        }

        const Tally total = tally();
        int numTests = 0;
        std::printf ("\n== Summary ==\n%-12s %6s %8s %8s\n", "group", "tests", "checks", "failed");
        for (const auto& g : groups)
        {
            std::printf ("%-12s %6d %8lld %8lld\n", g.group.c_str(), g.tests, g.checks, g.failures);
            numTests += g.tests;
        }

        if (total.failures == 0)
            std::printf ("\nALL PASSED: %lld checks in %d tests, 0 failure(s)\n", total.checks, numTests);
        else
            std::printf ("\nFAILED: %lld of %lld checks failed in %d tests\n", total.failures, total.checks, numTests);
        std::fflush (stdout);
        return total.failures == 0 ? 0 : 1;
    }

    //==============================================================================================
    juce::File writerDirectory (const std::string& name)
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (name.c_str()));
        const auto made = dir.createDirectory();
        if (made.failed())
            std::printf ("FAILED to create %s: %s\n", dir.getFullPathName().toRawUTF8(), made.getErrorMessage().toRawUTF8());
        return dir;
    }

    bool writeBytes (const juce::File& file, const void* data, std::size_t size)
    {
        file.deleteFile();
        bool ok = false;
        {
            juce::FileOutputStream out (file);
            ok = out.openedOk() && out.write (data, size);
            out.flush();
            ok = ok && out.getStatus().wasOk();
        }
        std::printf ("%s %s (%d bytes)\n", ok ? "wrote" : "FAILED to write", file.getFullPathName().toRawUTF8(), (int) size);
        return ok;
    }

    int writeSnapshots (const std::string& dirName)
    {
        const auto dir = writerDirectory (dirName);
        auto list = registeredSnapshots();
        std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return std::strcmp (a.name, b.name) < 0; });
        if (list.empty())
        {
            std::printf ("FAILED: no SL_SNAPSHOT is registered\n");
            return 1;
        }

        bool ok = true;
        for (const auto& s : list)
        {
            juce::Image image;
            try
            {
                image = s.write();
            }
            catch (...)
            {
                std::printf ("FAILED: snapshot %s threw\n", s.name);
            }

            juce::MemoryOutputStream png;
            juce::PNGImageFormat format;
            const bool encoded = image.isValid() && format.writeImageToStream (image, png);
            const auto file = dir.getChildFile (juce::String (s.name) + ".png");
            const bool wrote = encoded && writeBytes (file, png.getData(), png.getDataSize());
            if (encoded)
                std::printf ("    %s is %d x %d\n", s.name, image.getWidth(), image.getHeight());
            else
                std::printf ("FAILED: snapshot %s gave no image\n", s.name);
            ok = ok && wrote;
        }
        return ok ? 0 : 1;
    }

    int writeFingerprints (const std::string& dirName)
    {
        const auto dir = writerDirectory (dirName);
        auto list = registeredFingerprints();
        std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return std::strcmp (a.name, b.name) < 0; });

        if (list.empty())
        {
            // PLUGIN-PLAN 7.1: until M9 the upload still gets a file.
            const std::string text = "no sound fingerprints before M9\n";
            return writeBytes (dir.getChildFile ("placeholder.txt"), text.data(), text.size()) ? 0 : 1;
        }

        bool ok = true;
        for (const auto& f : list)
        {
            try
            {
                const std::string text = f.write();
                ok = writeBytes (dir.getChildFile (juce::String (f.name) + ".txt"), text.data(), text.size()) && ok;
            }
            catch (...)
            {
                std::printf ("FAILED: fingerprint %s threw\n", f.name);
                ok = false;
            }
        }
        return ok ? 0 : 1;
    }

    int writeStateFixtures (const std::string& dirName)
    {
        const auto dir = writerDirectory (dirName);
        auto list = registeredStateFixtures();
        std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return std::strcmp (a.name, b.name) < 0; });

        if (list.empty())
        {
            std::printf ("No SL_STATE_FIXTURE is registered yet: nothing to write.\n");
            return 0;
        }

        bool ok = true;
        for (const auto& s : list)
        {
            try
            {
                const juce::MemoryBlock block = s.write();
                ok = writeBytes (dir.getChildFile (juce::String (s.name) + ".bin"), block.getData(), block.getSize()) && ok;
            }
            catch (...)
            {
                std::printf ("FAILED: state fixture %s threw\n", s.name);
                ok = false;
            }
        }
        return ok ? 0 : 1;
    }

    int run (int argc, char** argv)
    {
        Args a;
        if (! parseArgs (argc, argv, a))
            return 2;

        resolvePaths (a);
        std::printf ("StrandLockTests: fixtures %s\n", options().fixturesDir.empty() ? "(not found)" : options().fixturesDir.c_str());
        std::printf ("Allocation counting: %s\n", sl::test::alloc::coverage());

        if (a.list)
        {
            listEverything();
            return 0;
        }
        if (! a.snapshotDir.empty())
            return writeSnapshots (a.snapshotDir);
        if (! a.fingerprintDir.empty())
            return writeFingerprints (a.fingerprintDir);
        if (! a.stateFixtureDir.empty())
            return writeStateFixtures (a.stateFixtureDir);
        return runTests (a.only);
    }
}

int main (int argc, char** argv)
{
    sl::test::alloc::install();

    // The counter outlives JUCE, so assertions during JUCE's own shutdown are counted too.
    AssertionCounter assertions;
    juce::Logger::setCurrentLogger (&assertions);

    int result = 0;
    {
        const juce::ScopedJuceInitialiser_GUI juceInit;
        result = run (argc, argv);
    }

    juce::Logger::setCurrentLogger (nullptr);

   #if JUCE_ASSERTIONS_ENABLED_OR_LOGGED
    const int fired = assertions.count.load();
    std::printf ("JUCE assertions: %d%s\n", fired, fired == 0 ? "" : " (each one is a failure)");
    if (fired > 0 && result == 0)
        result = 1;
   #else
    std::printf ("JUCE assertions: compiled out in this build (the Debug runner counts them)\n");
   #endif

    return result;
}
