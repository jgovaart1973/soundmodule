#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace km
{
/**
    Out-of-process plug-in scanning: every plug-in file is inspected by a separate copy of
    this program ("KlankModule --scan-plugin ..."). A plug-in that crashes or hangs during the
    scan only kills that child process; it is then reported as failed and blacklisted.
*/
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;
};

/** Child-process entry point. @returns process exit code. */
int runPluginScanChild (const juce::StringArray& args);

/** True if the command line asks for a child scan. */
bool isPluginScanCommandLine (const juce::String& commandLine);

} // namespace km
