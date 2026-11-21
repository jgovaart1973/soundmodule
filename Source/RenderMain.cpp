// KlankModuleRender: headless renderer for automated listening tests.
//
//   KlankModuleRender [--performance perf.kmperf] [--midi song.mid] --out result.wav
//                     [--samplerate 48000] [--tail 3]
//
// Without --midi a short demo phrase is rendered; without --performance part 1 plays
// the built-in electric piano.

#include "Render/OfflineRenderer.h"

#include <juce_events/juce_events.h>
#include <iostream>

namespace
{
    /** Accepts both "--name value" and "--name=value". */
    struct Args
    {
        juce::StringArray list;

        bool has (const juce::String& name) const
        {
            for (auto& a : list)
                if (a == name || a.startsWith (name + "="))
                    return true;
            return false;
        }

        juce::String value (const juce::String& name) const
        {
            for (int i = 0; i < list.size(); ++i)
            {
                if (list[i].startsWith (name + "="))
                    return list[i].fromFirstOccurrenceOf ("=", false, false).unquoted();
                if (list[i] == name && i + 1 < list.size())
                    return list[i + 1].unquoted();
            }
            return {};
        }

        juce::File file (const juce::String& name) const
        {
            return juce::File::getCurrentWorkingDirectory().getChildFile (value (name));
        }
    };
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // plug-ins need a message thread
    Args args;
    for (int i = 1; i < argc; ++i)
        args.list.add (juce::String::fromUTF8 (argv[i]));

    if (args.has ("--help") || args.has ("-h") || ! args.has ("--out"))
    {
        std::cout << "Usage: KlankModuleRender [--performance file.kmperf] [--midi file.mid] --out file.wav "
                     "[--samplerate 48000] [--tail 3]\n";
        return args.has ("--out") ? 0 : 1;
    }

    const double sampleRate = args.has ("--samplerate") ? args.value ("--samplerate").getDoubleValue() : 48000.0;
    const double tail = args.has ("--tail") ? args.value ("--tail").getDoubleValue() : 3.0;

    km::Performance performance = km::Performance::makeDefault();
    juce::String error;

    if (args.has ("--performance"))
        if (! performance.loadFrom (args.file ("--performance"), error))
        {
            std::cerr << error << "\n";
            return 2;
        }

    juce::MidiMessageSequence sequence;
    if (args.has ("--midi"))
    {
        sequence = km::OfflineRenderer::loadMidiFile (args.file ("--midi"), error);
        if (error.isNotEmpty())
        {
            std::cerr << error << "\n";
            return 3;
        }
    }
    else
    {
        sequence = km::OfflineRenderer::makeDemoSequence();
    }

    km::OfflineRenderer renderer (sampleRate, 256);
    if (! renderer.loadPerformance (performance, error))
        std::cerr << "Warning:\n" << error;

    const auto audio = renderer.render (sequence, tail);
    const auto outFile = args.file ("--out");

    if (! km::OfflineRenderer::writeWav (audio, sampleRate, outFile))
    {
        std::cerr << "Could not write " << outFile.getFullPathName() << "\n";
        return 4;
    }

    std::cout << "Rendered " << audio.getNumSamples() / sampleRate << " s, peak "
              << juce::Decibels::gainToDecibels (audio.getMagnitude (0, audio.getNumSamples())) << " dBFS -> "
              << outFile.getFullPathName() << "\n";
    return 0;
}
