#pragma once

#include "Model/Performance.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace km
{
/**
    A sound source played by one part: built-in synth, SFZ sampler or VST3 plug-in.

    Threading contract:
      - prepare(), getStateBase64(), createEditor(): message thread, never while process() runs
        on the same instance (the pool only prepares instances when audio is stopped or before
        they are lent to a part).
      - process(), allNotesOff(): audio thread only. Must not allocate, lock or do I/O.
*/
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Render into a stereo buffer that has been cleared by the caller. */
    virtual void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept = 0;

    /** Silence immediately (panic). */
    virtual void allNotesOff() noexcept = 0;

    virtual juce::String getName() const = 0;

    virtual bool hasEditor() const { return false; }
    virtual juce::AudioProcessorEditor* createEditor() { return nullptr; }
    virtual juce::String getStateBase64() const { return {}; }

    /** True once after the sound was edited live (e.g. in the plug-in's own window). Any thread. */
    virtual bool consumeEditedFlag() noexcept { return false; }

    /** Key this instance was created from (kept up to date by the pool, message thread only). */
    InstrumentKey key;
};

} // namespace km
