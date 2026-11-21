#pragma once

#include "Engine/Instrument.h"

#include <memory>

namespace sfz { class Sfizz; }

namespace km
{
/** SFZ sample player based on sfizz (BSD-2-Clause). Available when built with KM_WITH_SFIZZ. */
class SfzInstrument final : public Instrument
{
public:
    static bool isAvailable() noexcept;

    SfzInstrument();
    ~SfzInstrument() override;

    /** Loads an .sfz file. Slow (reads samples): call from a background thread before lending. */
    bool load (const juce::File& sfzFile, juce::String& error);

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;
    juce::String getName() const override { return name; }

private:
    std::unique_ptr<sfz::Sfizz> synth;
    juce::String name;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzInstrument)
};

} // namespace km
