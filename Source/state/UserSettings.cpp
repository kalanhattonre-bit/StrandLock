#include "UserSettings.h"

#include "StateChunks.h"

#include <mutex>

namespace sl::state
{
    namespace
    {
        const char* const kThemeKey = "theme";
        const char* const kUiScaleKey = "uiScalePercent";
        const char* const kKeysKey = "keysDefault";

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
