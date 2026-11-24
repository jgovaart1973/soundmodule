#pragma once

#include "Engine/Instrument.h"

#include <array>
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

    /** Per-channel expression state shared with the voices (audio thread only). */
    struct Expression
    {
        std::array<float, 16> bendRange {};   ///< semitones per channel (RPN 0)
        std::array<float, 16> timbre {};      ///< CC74, 0..1 (0.5 = neutral)
        std::array<float, 16> pressure {};    ///< channel pressure 0..1
        float managerBend = 0.0f;             ///< MPE: manager channel bend in semitones, applies to all notes
        bool mpe = false;
        Expression() { reset(); }
        void reset() noexcept
        {
            bendRange.fill (2.0f);
            timbre.fill (0.5f);
            pressure.fill (0.0f);
            managerBend = 0.0f;
            mpe = false;
        }
    };

    bool isMpeActive() const noexcept { return expression.mpe; }
    float getBendRange (int channel) const noexcept { return expression.bendRange[(size_t) (channel & 15)]; }

private:
    void preprocess (juce::MidiBuffer& midi) noexcept;

    juce::Synthesiser synth;
    Expression expression;
    juce::MidiBuffer filtered;
    std::array<int, 16> rpnMsb {}, rpnLsb {};
    std::atomic<int> preset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInSynth)
};

} // namespace km
