#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>

namespace km
{
/** An audio file loaded into memory at the engine's sample rate, plus a waveform overview. */
struct AudioFileData
{
    juce::AudioBuffer<float> audio;   ///< 1 or 2 channels
    double sampleRate = 48000.0;
    double originalSampleRate = 48000.0;
    static constexpr int peakBlock = 256;
    std::vector<float> peaks;         ///< max |sample| per peakBlock samples (all channels)

    double durationSeconds() const noexcept { return audio.getNumSamples() / sampleRate; }
};

/**
    Loads audio files (WAV, FLAC, AIFF, OGG, MP3 where the OS supports it) into memory, resampled
    to the playback rate. Message thread only. Buffers are shared and immutable, so the audio
    thread can play them through a PlaybackSnapshot without locks.
*/
class AudioCache
{
public:
    AudioCache();

    /** @returns nullptr (and an error) if the file cannot be read. */
    std::shared_ptr<const AudioFileData> get (const juce::File& file, double sampleRate, juce::String* error = nullptr);

    /** Forget a file (e.g. it was re-recorded). */
    void invalidate (const juce::File& file);

    /** Drop everything not used anywhere else. */
    void trim();

    juce::AudioFormatManager& getFormats() noexcept { return formats; }

private:
    juce::AudioFormatManager formats;
    std::map<juce::String, std::shared_ptr<const AudioFileData>> cache;
};

} // namespace km
