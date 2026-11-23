#pragma once

#include <juce_core/juce_core.h>

namespace km::paths
{
/** Non-empty when the environment variable KLANKMODULE_HOME redirects all data (tests, portable use). */
juce::File overrideRoot();

juce::File appDataDir();        ///< %APPDATA%\KlankModule (settings, learn, program map, plug-in list)
juce::File localDataDir();      ///< %LOCALAPPDATA%\KlankModule (logs, crash reports)
juce::File userDir();           ///< Documents\KlankModule
juce::File performancesDir();   ///< Documents\KlankModule\Performances
juce::File soundsDir();         ///< Documents\KlankModule\Sounds
juce::File songsDir();          ///< Documents\KlankModule\Songs
juce::File recordingsDir();     ///< Documents\KlankModule\Recordings (audio takes)
juce::File songAutosaveFile();

juce::File autosaveFile();
juce::File settingsFile();
juce::File midiLearnFile();
juce::File programMapFile();
juce::File pluginListFile();
juce::File deadMansPedalFile();
juce::File crashLogFile();
juce::File logFile();
} // namespace km::paths
