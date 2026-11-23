#pragma once

#include "Engine/Effects.h"
#include "Engine/Part.h"
#include "Engine/Sequencer.h"
#include "Midi/MidiEvent.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <functional>
#include <memory>

namespace km
{
/**
    The realtime heart of the module: 16 parts -> main / reverb / delay buses -> master limiter.
    Acts as the audio device callback for live use, and can be driven block by block for
    offline rendering and tests.
*/
class ModuleEngine final : public juce::AudioIODeviceCallback
{
public:
    /** @param liveInput queue filled by the MIDI router; nullptr for offline use. */
    explicit ModuleEngine (MidiEventQueue* liveInput = nullptr);
    ~ModuleEngine() override;

    Part& getPart (int index) noexcept { return *parts[(size_t) index]; }
    MasterParams master;

    /** MIDI channel whose Program Change selects a performance (0 = off). */
    std::atomic<int> controlChannel { 16 };

    Sequencer sequencer;                   ///< transport, song playback, recording tap, metronome
    ReleaseQueue releaseQueue;             ///< audio -> message: instruments handed back
    ProgramRequestQueue programRequests;   ///< audio -> message: Program Change decisions

    //== message thread ========================================================
    void prepare (double sampleRate, int maxBlockSize);
    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlockSize() const noexcept  { return maxBlock; }

    void requestPanic() noexcept       { panicRequested.store (true); }
    void startTestTone() noexcept      { testToneRemaining.store ((int) (sampleRate * 1.5)); }

    float getMasterPeakAndReset() noexcept { return masterPeak.exchange (0.0f); }
    float getLimiterReductionDb() const noexcept { return masterFx.getGainReductionDb(); }

    /** Called from audioDeviceAboutToStart (before streaming) so the owner can prepare instruments. */
    std::function<void (double sampleRate, int blockSize)> onAboutToStart;
    std::function<void (const juce::String&)> onDeviceError;

    //== audio thread / offline ===============================================
    /** Render numSamples (<= getMaxBlockSize()) into out (stereo). Events have sample offsets. */
    void process (juce::AudioBuffer<float>& out, int numSamples,
                  const MidiEvent* events, const int* eventPositions, int numEvents,
                  const float* const* inputs = nullptr, int numInputs = 0) noexcept;

    //== juce::AudioIODeviceCallback ==========================================
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

private:
    std::array<std::unique_ptr<Part>, kNumParts> parts;
    MidiEventQueue* liveQueue;

    juce::AudioBuffer<float> mainBus, reverbBus, delayBus, deviceScratch;
    ReverbFx reverb;
    DelayFx delay;
    MasterFx masterFx;

    std::vector<MidiEvent> liveEvents, seqEvents;
    std::vector<int> livePositions, seqPositions;

    double sampleRate = 48000.0;
    int maxBlock = 512;

    std::atomic<bool> panicRequested { false };
    std::atomic<int> testToneRemaining { 0 };
    double testTonePhase = 0.0;
    std::atomic<float> masterPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleEngine)
};

} // namespace km
