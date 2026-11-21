// Minimal VST3 instrument used only by the automated tests: a sine synth with one
// "Volume" parameter and the generic JUCE editor (so the host has a window to open).
#include <juce_audio_processors/juce_audio_processors.h>

class KmTestSynth final : public juce::AudioProcessor
{
public:
    KmTestSynth()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (volume = new juce::AudioParameterFloat (juce::ParameterID { "volume", 1 }, "Volume", 0.0f, 1.0f, 0.5f));
    }

    void prepareToPlay (double sr, int) override { sampleRate = sr; }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        int pos = 0;
        auto renderTo = [&] (int end)
        {
            for (; pos < end; ++pos)
            {
                const float s = activeNote >= 0 ? 0.3f * volume->get() * (float) std::sin (phase) : 0.0f;
                phase += juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (juce::jmax (0, activeNote)) / sampleRate;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, pos, s);
            }
        };
        for (const auto m : midi)
        {
            renderTo (m.samplePosition);
            const auto msg = m.getMessage();
            if (msg.isNoteOn()) activeNote = msg.getNoteNumber();
            else if ((msg.isNoteOff() && msg.getNoteNumber() == activeNote) || msg.isAllNotesOff() || msg.isAllSoundOff()) activeNote = -1;
        }
        renderTo (buffer.getNumSamples());
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "KmTestSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& data) override { juce::MemoryOutputStream (data, false).writeFloat (volume->get()); }
    void setStateInformation (const void* data, int size) override
    {
        if (size >= 4)
            *volume = juce::MemoryInputStream (data, (size_t) size, false).readFloat();
    }

    juce::AudioParameterFloat* volume = nullptr;

private:
    double sampleRate = 48000.0, phase = 0.0;
    int activeNote = -1;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KmTestSynth(); }
