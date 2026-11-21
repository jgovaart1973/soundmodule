#pragma once

#include "Engine/Instrument.h"

namespace km
{
/** Hosts a VST3 instrument plug-in. */
class PluginInstrument final : public Instrument
{
public:
    explicit PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance);
    ~PluginInstrument() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;
    juce::String getName() const override;

    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String getStateBase64() const override;
    void setStateBase64 (const juce::String& state);

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer panicMidi;
    std::atomic<bool> panicPending { false };
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginInstrument)
};

} // namespace km
