#include "Engine/PluginInstrument.h"

namespace km
{
PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance)
    : plugin (std::move (instance))
{
    jassert (plugin != nullptr);

    // Prefer: no audio inputs, stereo main output.
    auto layout = plugin->getBusesLayout();
    for (auto& in : layout.inputBuses)
        in = juce::AudioChannelSet::disabled();
    if (! layout.outputBuses.isEmpty())
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! plugin->setBusesLayout (layout))
        plugin->disableNonMainBuses();

    panicMidi.ensureSize (16 * 3 * 3);
    plugin->addListener (this);
}

PluginInstrument::~PluginInstrument()
{
    plugin->removeListener (this);
    if (prepared)
        plugin->releaseResources();
}

void PluginInstrument::prepare (double sampleRate, int maxBlockSize)
{
    if (prepared)
        plugin->releaseResources();

    plugin->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    plugin->setNonRealtime (false);
    plugin->prepareToPlay (sampleRate, maxBlockSize);

    const int channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    work.setSize (channels, maxBlockSize, false, true, true);
    prepared = true;
}

void PluginInstrument::process (juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept
{
    const int n = stereoOut.getNumSamples();
    if (! prepared || n > work.getNumSamples())
        return;

    if (panicPending.exchange (false))
    {
        // Send "All Sound Off" + "All Notes Off" + sustain off on all 16 channels.
        for (int ch = 1; ch <= 16; ++ch)
        {
            midi.addEvent (juce::MidiMessage::controllerEvent (ch, 64, 0), 0);
            midi.addEvent (juce::MidiMessage::allSoundOff (ch), 0);
            midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        }
    }

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), n);
    view.clear();
    plugin->processBlock (view, midi);

    const int outs = plugin->getTotalNumOutputChannels();
    if (outs <= 0)
        return;

    stereoOut.copyFrom (0, 0, view, 0, 0, n);
    stereoOut.copyFrom (1, 0, view, outs > 1 ? 1 : 0, 0, n);
}

void PluginInstrument::allNotesOff() noexcept
{
    panicPending.store (true);
}

juce::String PluginInstrument::getName() const
{
    return plugin->getName();
}

bool PluginInstrument::hasEditor() const
{
    return plugin->hasEditor();
}

juce::AudioProcessorEditor* PluginInstrument::createEditor()
{
    return plugin->createEditorAndMakeActive();
}

juce::String PluginInstrument::getStateBase64() const
{
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    return state.toBase64Encoding();
}

void PluginInstrument::setStateBase64 (const juce::String& state)
{
    juce::MemoryBlock block;
    if (state.isNotEmpty() && block.fromBase64Encoding (state))
        plugin->setStateInformation (block.getData(), (int) block.getSize());
}

} // namespace km
