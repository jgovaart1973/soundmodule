#pragma once

#include "Engine/Instrument.h"

namespace km
{
/** Hosts a VST3 instrument plug-in. */
class PluginInstrument final : public Instrument,
                               private juce::AudioProcessorListener
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
    bool consumeEditedFlag() noexcept override { return edited.exchange (false); }

private:
    // Called by the plug-in (any thread) when the user turns a knob in its window.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { edited = true; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.programChanged || d.nonParameterStateChanged)
            edited = true;
    }

    std::atomic<bool> edited { false };
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer panicMidi;
    std::atomic<bool> panicPending { false };
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginInstrument)
};

} // namespace km
