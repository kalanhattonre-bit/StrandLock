#include "UserSettings.h"

#include "StateChunks.h"

#include "model/Recipes.h"

#include <algorithm>
#include <iterator>
#include <mutex>

namespace sl::state
{
    namespace
    {
        const char* const kThemeKey = "theme";
        const char* const kUiScaleKey = "uiScalePercent";
        const char* const kKeysKey = "keysDefault";
        const char* const kPinsKey = "dicePins";   // the Dice fields pinned, comma separated (M6)
        const char* const kLibraryKey = "libraryOn";   // the sound library (M7): off unless turned on

        /** The names that are Dice fields, each once, in their order. */
        std::vector<std::string> onlyDiceFields (const std::vector<std::string>& names)
        {
            std::vector<std::string> out;
            for (const auto& n : names)
            {
                const bool field = std::find (std::begin (model::kDiceFields), std::end (model::kDiceFields), n) != std::end (model::kDiceFields);
                if (field && std::find (out.begin(), out.end(), n) == out.end())
                    out.push_back (n);
            }
            return out;
        }

        juce::PropertiesFile::Options options()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "StrandLock";
            o.filenameSuffix = ".settings";
            o.folderName = "StrandLock";
            o.osxLibrarySubFolder = "Application Support";
            o.commonToAllUsers = false;
            o.ignoreCaseOfKeyNames = false;
            o.doNotSave = false;
            o.millisecondsBeforeSaving = -1;   // no timer: every set saves at once (Cubase may be closed any time)
            o.storageFormat = juce::PropertiesFile::storeAsXML;
            return o;
        }
    }

    UserSettings::UserSettings (const juce::File& f)
        : file (f),
          properties (std::make_unique<juce::PropertiesFile> (f, options()))
    {
    }

    UserSettings::~UserSettings()
    {
        save();
    }

    juce::File UserSettings::defaultFile()
    {
       #if STRANDLOCK_TESTS
        // One file per test run, chosen once, so no run sees another's (or Kalan's) settings.
        static const juce::File runFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                              .getChildFile ("StrandLockTests")
                                              .getNonexistentChildFile ("settings", ".settings", false);
        return runFile;
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("StrandLock")
                   .getChildFile ("StrandLock.settings");
       #endif
    }

    std::shared_ptr<UserSettings> UserSettings::shared()
    {
        static std::mutex lock;
        static std::weak_ptr<UserSettings> current;
        const std::lock_guard<std::mutex> hold (lock);
        if (auto alive = current.lock())
            return alive;
        auto made = std::make_shared<UserSettings> (defaultFile());
        current = made;
        return made;
    }

    std::optional<Theme> UserSettings::theme() const
    {
        const auto t = properties->getValue (kThemeKey);
        if (t == "dark")
            return Theme::dark;
        if (t == "light")
            return Theme::light;
        return std::nullopt;
    }

    void UserSettings::setTheme (Theme t)
    {
        properties->setValue (kThemeKey, t == Theme::light ? "light" : "dark");
        changed();
    }

    std::optional<int> UserSettings::uiScalePercent() const
    {
        if (! properties->containsKey (kUiScaleKey))
            return std::nullopt;
        return juce::jlimit (kMinUiScalePercent, kMaxUiScalePercent, properties->getIntValue (kUiScaleKey, 100));
    }

    void UserSettings::setUiScalePercent (int percent)
    {
        properties->setValue (kUiScaleKey, juce::jlimit (kMinUiScalePercent, kMaxUiScalePercent, percent));
        changed();
    }

    bool UserSettings::keysDefault() const
    {
        return properties->getBoolValue (kKeysKey, false);
    }

    void UserSettings::setKeysDefault (bool on)
    {
        properties->setValue (kKeysKey, on);
        changed();
    }

    std::vector<std::string> UserSettings::dicePins() const
    {
        juce::StringArray parts;
        parts.addTokens (properties->getValue (kPinsKey), ",", "");
        parts.trim();
        parts.removeEmptyStrings();
        std::vector<std::string> names;
        for (const auto& p : parts)
            names.push_back (p.toStdString());
        return onlyDiceFields (names);
    }

    void UserSettings::setDicePins (const std::vector<std::string>& pins)
    {
        juce::StringArray parts;
        for (const auto& p : onlyDiceFields (pins))
            parts.add (juce::String (p));
        properties->setValue (kPinsKey, parts.joinIntoString (","));
        changed();
    }

    bool UserSettings::libraryOn() const
    {
        return properties->getBoolValue (kLibraryKey, false);
    }

    void UserSettings::setLibraryOn (bool on)
    {
        properties->setValue (kLibraryKey, on);
        changed();
    }

    bool UserSettings::save()
    {
        if (! file.getParentDirectory().createDirectory().wasOk())
            return false;
        return properties->saveIfNeeded();
    }

    void UserSettings::changed()
    {
        save();
        sendChangeMessage();
    }
}
