#include "Engine/AudioCache.h"

namespace km
{
AudioCache::AudioCache()
{
    formats.registerBasicFormats();
}

std::shared_ptr<const AudioFileData> AudioCache::get (const juce::File& file, double sampleRate, juce::String* error)
{
    const auto key = file.getFullPathName() + "@" + juce::String (sampleRate) + "@" + juce::String (file.getLastModificationTime().toMilliseconds());
    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        if (error != nullptr)
            *error = "Kan audiobestand niet lezen: " + file.getFileName();
        return nullptr;
    }

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) std::numeric_limits<int>::max() / 2);

    juce::AudioBuffer<float> original (channels, juce::jmax (1, length));
    reader->read (&original, 0, length, 0, true, channels > 1);

    auto data = std::make_shared<AudioFileData>();
    data->sampleRate = sampleRate;
    data->originalSampleRate = reader->sampleRate;

    if (std::abs (reader->sampleRate - sampleRate) < 0.5)
    {
        data->audio = std::move (original);
    }
    else
    {
        // Resample once at load time, so playback is a plain copy.
        const double ratio = reader->sampleRate / sampleRate;
        const int outLength = (int) std::ceil (length / ratio);
        data->audio.setSize (channels, juce::jmax (1, outLength));
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, original.getReadPointer (ch), data->audio.getWritePointer (ch), outLength, length, 0);
        }
    }

    // Waveform overview
    const int n = data->audio.getNumSamples();
    data->peaks.resize ((size_t) (n / AudioFileData::peakBlock + 1), 0.0f);
    for (int ch = 0; ch < data->audio.getNumChannels(); ++ch)
    {
        const float* s = data->audio.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            auto& p = data->peaks[(size_t) (i / AudioFileData::peakBlock)];
            p = juce::jmax (p, std::abs (s[i]));
        }
    }

    cache[key] = data;
    return data;
}

void AudioCache::invalidate (const juce::File& file)
{
    const auto prefix = file.getFullPathName() + "@";
    for (auto it = cache.begin(); it != cache.end();)
        it = it->first.startsWith (prefix) ? cache.erase (it) : std::next (it);
}

void AudioCache::trim()
{
    for (auto it = cache.begin(); it != cache.end();)
        it = it->second.use_count() <= 1 ? cache.erase (it) : std::next (it);
}

} // namespace km
