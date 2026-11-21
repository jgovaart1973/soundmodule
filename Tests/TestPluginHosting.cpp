#include "Engine/InstrumentPool.h"
#include "Engine/ModuleEngine.h"
#include "Engine/PluginInstrument.h"

#include <catch2/catch_test_macros.hpp>

using namespace km;

namespace
{
    std::unique_ptr<juce::PluginDescription> scanTestPlugin (juce::AudioPluginFormatManager& formats)
    {
        for (auto* format : formats.getFormats())
        {
            if (format->getName() != "VST3")
                continue;
            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, KM_TEST_PLUGIN_PATH);
            if (! found.isEmpty())
                return std::make_unique<juce::PluginDescription> (*found[0]);
        }
        return nullptr;
    }

    float runBlocks (ModuleEngine& engine, int blocks, std::initializer_list<MidiEvent> events = {})
    {
        std::vector<MidiEvent> ev (events);
        std::vector<int> pos (ev.size(), 0);
        juce::AudioBuffer<float> out (2, 256);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (out, 256, b == 0 ? ev.data() : nullptr, b == 0 ? pos.data() : nullptr, b == 0 ? (int) ev.size() : 0);
            peak = juce::jmax (peak, out.getMagnitude (0, 256));
        }
        return peak;
    }
} // namespace

TEST_CASE ("A VST3 instrument loads, plays, opens its editor and reports live edits", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    auto desc = scanTestPlugin (formats);
    INFO ("Plug-in: " << KM_TEST_PLUGIN_PATH << "  formats: " << formats.getNumFormats());
    REQUIRE (desc != nullptr);
    REQUIRE (desc->isInstrument);

    juce::KnownPluginList known;
    known.addType (*desc);
    InstrumentPool pool (formats, known);
    pool.setAudioConfig (48000.0, 256);

    const InstrumentKey key { InstrumentKey::Type::plugin, desc->createIdentifierString(), desc->name, {} };
    juce::String error;
    auto* inst = pool.acquireSync (key, error);
    INFO (error);
    REQUIRE (inst != nullptr);
    REQUIRE (inst->hasEditor());

    // Plays through a part like any other sound.
    ModuleEngine engine (nullptr);
    engine.prepare (48000.0, 256);
    auto& part = engine.getPart (0);
    part.params.enabled = true;
    part.params.reverbSend = 0.0f;
    part.lend (inst);
    REQUIRE (runBlocks (engine, 20, { MidiEvent::make (-1, 0x90, 69, 100) }) > 0.05f);
    runBlocks (engine, 2, { MidiEvent::make (-1, 0x80, 69, 0) });
    REQUIRE (runBlocks (engine, 10) < 0.001f);

    // Its own window can be created (the "VST" button) ...
    std::unique_ptr<juce::AudioProcessorEditor> editor (inst->createEditor());
    REQUIRE (editor != nullptr);
    REQUIRE (editor->getWidth() > 0);

    // ... and turning a knob there is noticed, so the performance shows "changed".
    inst->consumeEditedFlag();
    auto& plugin = dynamic_cast<PluginInstrument&> (*inst).getInstance();
    REQUIRE_FALSE (plugin.getParameters().isEmpty());
    plugin.getParameters()[0]->setValueNotifyingHost (0.9f);
    REQUIRE (inst->consumeEditedFlag());
    REQUIRE_FALSE (inst->consumeEditedFlag());

    // The edited sound is saved with the performance and restored on load.
    const auto state = inst->getStateBase64();
    REQUIRE (state.isNotEmpty());
    auto restoredKey = key;
    restoredKey.pluginState = state;
    auto* second = pool.acquireSync (restoredKey, error);
    REQUIRE (second != nullptr);
    REQUIRE (second != inst);
    auto& restored = dynamic_cast<PluginInstrument&> (*second).getInstance();
    REQUIRE (std::abs (restored.getParameters()[0]->getValue() - 0.9f) < 0.01f);

    editor.reset();
    juce::Array<Instrument*> back;
    part.reclaimAll (back);
    for (auto* i : back)
        pool.release (i);
    pool.release (second);
}
