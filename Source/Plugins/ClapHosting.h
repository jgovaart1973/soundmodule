#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace km
{
/**
    CLAP plug-in hosting (phase 3d).

    CLAP plug-ins appear as an extra juce::AudioPluginFormat ("CLAP"), so scanning (out of
    process), the plug-in list, the instrument pool, saving the sound in a performance and the
    "original plug-in window" work exactly as for VST3.

    Supported: instruments with audio + note ports (CLAP and MIDI note dialects, MPE), parameters
    (generic editor + automation of the knobs in the plug-in window), state save/load, the
    plug-in's own window (Windows: embedded win32 window), log and thread-check host extensions.
*/
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override   { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool allowAsync = false) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

    /** Synchronous creation (message thread). */
    static std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, double sampleRate,
                                                                      int blockSize, juce::String& error);

protected:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};

/** VST3 (JUCE) + CLAP. Use this instead of juce::addDefaultFormatsToManager. */
void addPluginFormats (juce::AudioPluginFormatManager&);

} // namespace km
