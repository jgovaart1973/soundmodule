#pragma once

#include "Engine/Instrument.h"

#include <atomic>

namespace km
{
/**
    Built-in polyphonic synthesiser (32 voices) with eight presets, so the module produces
    sound immediately without any external libraries or plug-ins.
    Uses juce::Synthesiser for voice allocation, sustain pedal, pitch bend and MPE-style
    per-channel expression.
*/
class BuiltInSynth final : public Instrument
{
public:
    static int getNumPresets() noexcept;
    static juce::String getPresetName (int index);

    explicit BuiltInSynth (int presetIndex = 0);

    /** Thread-safe; takes effect on the next note-on. */
    void setPreset (int index) noexcept;
    int getPreset() const noexcept { return preset.load(); }

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;
    juce::String getName() const override { return getPresetName (getPreset()); }

private:
    juce::Synthesiser synth;
    std::atomic<int> preset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInSynth)
};

} // namespace km
