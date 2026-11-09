#pragma once

// Settings that belong to the person, not the project (PLUGIN-PLAN 3, "Per-user data"): the theme, the UI
// scale and whether the Keys switch starts on. They live in %APPDATA%\StrandLock\StrandLock.settings (a
// juce::PropertiesFile), so they carry across projects; the theme here wins over the UIST copy in a project.
// Every set is written to disk at once. Message thread only.
//
// The test runner (STRANDLOCK_TESTS) never touches the real file: its default file is a fresh one in the
// temporary folder, one per run, so the tests and the snapshots start from no per-user values.

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <memory>
#include <optional>

namespace sl::state
{
    enum class Theme { dark, light };

    class UserSettings final : public juce::ChangeBroadcaster
    {
    public:
        /** Reads (and later writes) this file; it is created on the first set. */
        explicit UserSettings (const juce::File& file);
        ~UserSettings() override;

        /** %APPDATA%\StrandLock\StrandLock.settings (the user's application data folder elsewhere); in the test
            runner, a file of its own in the temporary folder. */
        static juce::File defaultFile();

        /** The one UserSettings of this process for defaultFile(), shared by every StrandLock instance. */
        static std::shared_ptr<UserSettings> shared();

        const juce::File& getFile() const noexcept { return file; }

        /** The person's theme, or nothing when they have never picked one (the project's UIST copy then applies). */
        std::optional<Theme> theme() const;
        void setTheme (Theme t);

        /** The UI scale in percent (75..200), or nothing when never set (100 % then). */
        std::optional<int> uiScalePercent() const;
        void setUiScalePercent (int percent);

        /** Whether the Keys switch starts on in a new window (default off). */
        bool keysDefault() const;
        void setKeysDefault (bool on);

        /** Saves now; false when the file could not be written (shown, never thrown). */
        bool save();

    private:
        juce::File file;
        std::unique_ptr<juce::PropertiesFile> properties;

        void changed();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UserSettings)
    };
}
