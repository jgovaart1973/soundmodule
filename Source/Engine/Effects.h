#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace km
{
/** Global effect and master parameters (atomics: written by UI / MIDI learn, read by audio). */
struct MasterParams
{
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<float> reverbSize { 0.55f };
    std::atomic<float> reverbDamping { 0.4f };
    std::atomic<float> delayMs { 375.0f };
    std::atomic<float> delayFeedback { 0.35f };
    std::atomic<float> limiterThresholdDb { -1.0f };
};

/** Send effect 1: stereo reverb (100% wet). */
class ReverbFx
{
public:
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& bus, int numSamples, float size, float damping) noexcept;
    void reset() noexcept { reverb.reset(); }

private:
    juce::Reverb reverb;
    float lastSize = -1.0f, lastDamping = -1.0f;
};

/** Send effect 2: stereo feedback delay (100% wet), max 2 seconds. */
class DelayFx
{
public:
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& bus, int numSamples, float timeMs, float feedback) noexcept;
    void reset() noexcept;

private:
    juce::AudioBuffer<float> line;
    int writePos = 0;
    double sr = 44100.0;
    juce::SmoothedValue<float> delaySamples;
    float lowpassState[2] {};
};

/**
    Master volume + transparent peak limiter so the output never clips.
    Below the threshold the signal is untouched (no compression colouring);
    above it the gain drops instantly and recovers over ~80 ms. Zero latency.
*/
class MasterFx
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& bus, int numSamples, float volumeDb, float thresholdDb) noexcept;
    void reset() noexcept { envelope = 0.0f; }

    /** Current gain reduction in dB (for metering). */
    float getGainReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

private:
    juce::SmoothedValue<float> gain;
    float envelope = 0.0f;
    float releaseCoef = 0.999f;
    std::atomic<float> reductionDb { 0.0f };
};

} // namespace km
