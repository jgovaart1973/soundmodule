#pragma once

#include "Engine/InstrumentPool.h"
#include "Engine/ModuleEngine.h"
#include "Model/Performance.h"

namespace km
{
/**
    Renders a MIDI sequence through the complete engine (parts, effects, limiter) without
    an audio device. Used by KlankModuleRender and the automated tests.
*/
class OfflineRenderer
{
public:
    OfflineRenderer (double sampleRate = 48000.0, int blockSize = 256);
    ~OfflineRenderer();

    /** Load all sounds synchronously. @returns false if a sound failed (error describes it). */
    bool loadPerformance (const Performance& performance, juce::String& error);

    /** Render the sequence (timestamps in seconds) plus a tail. */
    juce::AudioBuffer<float> render (const juce::MidiMessageSequence& sequence, double tailSeconds);

    /** Render events at absolute sample positions (sorted), e.g. a song with targetPart set. */
    juce::AudioBuffer<float> renderEvents (const std::vector<std::pair<int64_t, MidiEvent>>& events, int64_t totalSamples);

    ModuleEngine& getEngine() noexcept { return engine; }

    static juce::MidiMessageSequence loadMidiFile (const juce::File& file, juce::String& error);
    static juce::MidiMessageSequence makeDemoSequence();
    static bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& file);
    static bool writeAudioFile (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& file, bool flac);

private:
    double sampleRate;
    int blockSize;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    InstrumentPool pool { formats, knownPlugins };
    ModuleEngine engine { nullptr };
};

} // namespace km
