#include "Plugins/PluginScanner.h"

namespace km
{
namespace
{
    constexpr const char* scanFlag = "--scan-plugin";
    constexpr int scanTimeoutMs = 45000;
} // namespace

bool isPluginScanCommandLine (const juce::String& commandLine)
{
    return commandLine.contains (scanFlag);
}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::TemporaryFile outFile (".xml");

    juce::StringArray args;
    args.add (exe.getFullPathName());
    args.add (scanFlag);
    args.add (format.getName());
    args.add (fileOrIdentifier);
    args.add (outFile.getFile().getFullPathName());

    juce::ChildProcess child;
    if (! child.start (args, 0))
        return false;

    // Wait, but stay responsive to "cancel scan".
    const auto start = juce::Time::getMillisecondCounter();
    while (child.isRunning())
    {
        if (shouldExit() || juce::Time::getMillisecondCounter() - start > (juce::uint32) scanTimeoutMs)
        {
            child.kill();
            return false;
        }
        juce::Thread::sleep (20);
    }

    if (child.getExitCode() != 0)
        return false; // crashed or failed to load -> counts as a failure (blacklisted)

    if (auto xml = juce::parseXML (outFile.getFile()))
    {
        for (auto* e : xml->getChildIterator())
        {
            auto desc = std::make_unique<juce::PluginDescription>();
            if (desc->loadFromXml (*e))
                result.add (desc.release());
        }
    }
    return true;
}

int runPluginScanChild (const juce::StringArray& args)
{
    // args: ... --scan-plugin <formatName> <fileOrIdentifier> <outFile>
    const int i = args.indexOf (scanFlag);
    if (i < 0 || i + 3 >= args.size())
        return 2;

    const auto formatName = args[i + 1];
    const auto fileOrId = args[i + 2];
    const juce::File outFile (args[i + 3]);

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    for (auto* format : formats.getFormats())
    {
        if (format->getName() != formatName)
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, fileOrId);

        juce::XmlElement root ("PLUGINS");
        for (auto* d : found)
            root.addChildElement (d->createXml().release());

        return root.writeTo (outFile) ? 0 : 3;
    }
    return 4;
}

} // namespace km
