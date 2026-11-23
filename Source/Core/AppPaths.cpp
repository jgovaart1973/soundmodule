#include "Core/AppPaths.h"

namespace km::paths
{
namespace
{
    juce::File ensure (const juce::File& dir)
    {
        if (! dir.isDirectory())
            dir.createDirectory();
        return dir;
    }
} // namespace

/** Set KLANKMODULE_HOME to keep all data in one folder (tests, portable installs). */
juce::File overrideRoot()
{
    const auto root = juce::SystemStats::getEnvironmentVariable ("KLANKMODULE_HOME", {});
    return root.isNotEmpty() ? ensure (juce::File (root)) : juce::File();
}

juce::File appDataDir()
{
    if (auto root = overrideRoot(); root != juce::File())
        return ensure (root.getChildFile ("AppData"));
    return ensure (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KlankModule"));
}

juce::File localDataDir()
{
    if (auto root = overrideRoot(); root != juce::File())
        return ensure (root.getChildFile ("Local"));
   #if JUCE_WINDOWS
    const auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
    if (local.isNotEmpty())
        return ensure (juce::File (local).getChildFile ("KlankModule"));
   #endif
    return ensure (appDataDir().getChildFile ("Local"));
}

juce::File userDir()
{
    if (auto root = overrideRoot(); root != juce::File())
        return ensure (root.getChildFile ("Documents"));
    return ensure (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KlankModule"));
}

juce::File performancesDir() { return ensure (userDir().getChildFile ("Performances")); }
juce::File soundsDir()       { return ensure (userDir().getChildFile ("Sounds")); }
juce::File songsDir()        { return ensure (userDir().getChildFile ("Songs")); }
juce::File recordingsDir()   { return ensure (userDir().getChildFile ("Recordings")); }
juce::File songAutosaveFile() { return appDataDir().getChildFile ("autosave-song.kmsong"); }

juce::File autosaveFile()      { return appDataDir().getChildFile ("autosave.json"); }
juce::File settingsFile()      { return appDataDir().getChildFile ("settings.xml"); }
juce::File midiLearnFile()     { return appDataDir().getChildFile ("midi-learn.json"); }
juce::File programMapFile()    { return appDataDir().getChildFile ("program-map.json"); }
juce::File pluginListFile()    { return appDataDir().getChildFile ("plugins.xml"); }
juce::File deadMansPedalFile() { return appDataDir().getChildFile ("scan-in-progress.txt"); }
juce::File crashLogFile()      { return ensure (localDataDir().getChildFile ("logs")).getChildFile ("crash.log"); }
juce::File logFile()           { return ensure (localDataDir().getChildFile ("logs")).getChildFile ("klankmodule.log"); }
} // namespace km::paths
