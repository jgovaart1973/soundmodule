#include "Engine/BuiltInSynth.h"
#include "Engine/InstrumentPool.h"
#include "Engine/ModuleEngine.h"
#include "Engine/PluginInstrument.h"
#include "Midi/Midi2Translator.h"
#include "Plugins/ClapHosting.h"

#include <catch2/catch_test_macros.hpp>

using namespace km;

namespace
{
    struct Out { uint8_t b0, b1, b2; int size; };

    std::vector<Out> translate (Midi2Translator& t, std::initializer_list<uint32_t> words)
    {
        std::vector<uint32_t> w (words);
        std::vector<Out> out;
        t.translate (w.data(), w.size(), [&] (uint8_t a, uint8_t b, uint8_t c, int s) { out.push_back ({ a, b, c, s }); });
        return out;
    }

    // MIDI 2.0 channel voice message, group 0
    uint32_t m2 (int status, int channel, int idx1, int idx2)
    {
        return (0x4u << 28) | ((uint32_t) status << 20) | ((uint32_t) channel << 16) | ((uint32_t) idx1 << 8) | (uint32_t) idx2;
    }

    float rms (const juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 0, b.getNumSamples()); }
} // namespace

//==============================================================================
TEST_CASE ("MIDI 2.0 translation: scaling, MIDI 1.0 packets, bank + program", "[midi2]")
{
    Midi2Translator t;
    t.perNoteAsMpe = false;

    auto o = translate (t, { 0x20903c64u });   // MIDI 1.0 packet: note on C4 vel 100
    REQUIRE (o.size() == 1);
    REQUIRE (o[0].b0 == 0x90);
    REQUIRE (o[0].b1 == 60);
    REQUIRE (o[0].b2 == 100);

    o = translate (t, { m2 (0x9, 2, 64, 0), 0xffff0000u, m2 (0x9, 2, 65, 0), 0x01000000u });
    REQUIRE (o.size() == 2);
    REQUIRE (o[0].b0 == 0x92);
    REQUIRE (o[0].b2 == 127);
    REQUIRE (o[1].b2 == 1);             // a tiny velocity is still a note-on

    o = translate (t, { m2 (0xb, 0, 74, 0), 0x80000000u, m2 (0xe, 0, 0, 0), 0x80000000u, m2 (0xd, 0, 0, 0), 0xffffffffu });
    REQUIRE (o.size() == 3);
    REQUIRE (o[0].b0 == 0xb0);
    REQUIRE (o[0].b1 == 74);
    REQUIRE (o[0].b2 == 64);
    REQUIRE ((o[1].b1 | (o[1].b2 << 7)) == 8192);  // centre pitch bend
    REQUIRE (o[2].b1 == 127);

    o = translate (t, { m2 (0xc, 0, 0, 1), (5u << 24) | (1u << 8) | 2u });   // program 5, bank 1/2
    REQUIRE (o.size() == 3);
    REQUIRE (o[0].b1 == 0);
    REQUIRE (o[0].b2 == 1);
    REQUIRE (o[1].b1 == 32);
    REQUIRE (o[1].b2 == 2);
    REQUIRE (o[2].b0 == 0xc0);
    REQUIRE (o[2].b1 == 5);

    o = translate (t, { m2 (0x2, 0, 0, 0), 48u << 25 });   // RPN 0 (bend range) = 48
    REQUIRE (o.size() == 4);
    REQUIRE (o[0].b1 == 101);
    REQUIRE (o[2].b1 == 6);
    REQUIRE (o[2].b2 == 48);
}

TEST_CASE ("MIDI 2.0 per-note expression becomes MPE", "[midi2]")
{
    Midi2Translator t;   // perNoteAsMpe = true
    auto o = translate (t, { m2 (0x9, 0, 60, 0), 0x80000000u, m2 (0x9, 0, 64, 0), 0x80000000u });
    REQUIRE (o.size() == 2);
    const int chA = o[0].b0 & 0x0f, chB = o[1].b0 & 0x0f;
    REQUIRE (chA >= 1);
    REQUIRE (chB >= 1);
    REQUIRE (chA != chB);

    // Per-note pitch bend on note 64 only -> pitch bend on its member channel
    o = translate (t, { m2 (0x6, 0, 64, 0), 0xc0000000u });
    REQUIRE (o.size() == 1);
    REQUIRE (o[0].b0 == (0xe0 | chB));

    // Poly pressure -> channel pressure on the member channel; per-note CC74 (timbre)
    o = translate (t, { m2 (0xa, 0, 60, 0), 0xffffffffu, m2 (0x1, 0, 60, 74), 0x40000000u });
    REQUIRE (o.size() == 2);
    REQUIRE (o[0].b0 == (0xd0 | chA));
    REQUIRE (o[1].b0 == (0xb0 | chA));
    REQUIRE (o[1].b1 == 74);

    // Channel-wide messages go to the manager channel
    o = translate (t, { m2 (0xb, 5, 1, 0), 0xffffffffu });
    REQUIRE (o[0].b0 == 0xb0);

    // Note off on the right channel, the channel becomes free
    o = translate (t, { m2 (0x8, 0, 60, 0), 0 });
    REQUIRE (o[0].b0 == (0x80 | chA));
    o = translate (t, { m2 (0x6, 0, 60, 0), 0xc0000000u });
    REQUIRE (o.empty());
}

TEST_CASE ("MPE in the built-in synth: configuration, bend range and per-note pressure", "[mpe]")
{
    BuiltInSynth synth (2);   // organ: steady level, so only the expression changes it
    synth.prepare (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);

    auto run = [&] (std::initializer_list<juce::MidiMessage> msgs, int blocks = 1)
    {
        juce::MidiBuffer m;
        for (auto& msg : msgs)
            m.addEvent (msg, 0);
        float level = 0.0f;   // average over the blocks: smooths out vibrato and phase beating
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            synth.process (buf, m);
            m.clear();
            level += rms (buf);
        }
        return level / (float) blocks;
    };

    REQUIRE_FALSE (synth.isMpeActive());
    run ({ juce::MidiMessage::controllerEvent (1, 101, 0), juce::MidiMessage::controllerEvent (1, 100, 6),
           juce::MidiMessage::controllerEvent (1, 6, 15) });
    REQUIRE (synth.isMpeActive());
    REQUIRE (synth.getBendRange (0) == 2.0f);
    REQUIRE (synth.getBendRange (5) == 48.0f);

    run ({ juce::MidiMessage::controllerEvent (3, 101, 0), juce::MidiMessage::controllerEvent (3, 100, 0),
           juce::MidiMessage::controllerEvent (3, 6, 24) });
    REQUIRE (synth.getBendRange (2) == 24.0f);

    // Pressure on the note's own channel makes it louder; another channel's pressure does not.
    run ({ juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) }, 20);
    const float soft = run ({}, 24);
    run ({ juce::MidiMessage::channelPressureChange (5, 127) }, 2);
    const float other = run ({}, 24);
    run ({ juce::MidiMessage::channelPressureChange (2, 127) }, 2);
    const float loud = run ({}, 24);
    INFO ("soft " << soft << " other " << other << " loud " << loud);
    REQUIRE (loud > soft * 1.3f);
    REQUIRE (other < soft * 1.15f);
}

TEST_CASE ("A part with MPE on configures its sound", "[mpe]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ModuleEngine engine { nullptr };
    engine.prepare (48000.0, 256);
    BuiltInSynth synth (0);
    synth.prepare (48000.0, 256);
    auto& part = engine.getPart (0);
    part.params.enabled = true;
    part.lend (&synth);

    juce::AudioBuffer<float> out (2, 256);
    engine.process (out, 256, nullptr, nullptr, 0);
    REQUIRE_FALSE (synth.isMpeActive());

    part.params.mpe = true;
    engine.process (out, 256, nullptr, nullptr, 0);
    REQUIRE (synth.isMpeActive());
    REQUIRE (synth.getBendRange (3) == 48.0f);

    part.params.mpe = false;
    engine.process (out, 256, nullptr, nullptr, 0);
    REQUIRE_FALSE (synth.isMpeActive());
    REQUIRE (synth.getBendRange (3) == 2.0f);

    juce::Array<Instrument*> back;
    part.reclaimAll (back);
}

//==============================================================================
TEST_CASE ("CLAP: scan, play, parameters and state", "[clap]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ClapPluginFormat format;
    REQUIRE (format.fileMightContainThisPluginType (KM_TEST_CLAP_PATH));

    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, KM_TEST_CLAP_PATH);
    REQUIRE (found.size() == 2);
    REQUIRE (found[0]->name == "KM Test CLAP Synth");
    REQUIRE (found[1]->name == "KM Test CLAP MIDI");
    REQUIRE (found[0]->isInstrument);
    REQUIRE (found[0]->pluginFormatName == "CLAP");
    REQUIRE (found[0]->manufacturerName == "KlankModule");
    REQUIRE (found[0]->uniqueId != found[1]->uniqueId);
    REQUIRE (found[0]->createIdentifierString() != found[1]->createIdentifierString());

    for (auto* desc : found)
    {
        INFO (desc->name);
        juce::String error;
        auto inst = ClapPluginFormat::createInstance (*desc, 48000.0, 512, error);
        INFO (error);
        REQUIRE (inst != nullptr);
        REQUIRE (inst->getName() == desc->name);
        inst->prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 10);
        buf.clear();
        inst->processBlock (buf, midi);
        REQUIRE (rms (buf) > 0.02f);

        auto params = inst->getParameters();
        REQUIRE (params.size() == 1);
        REQUIRE (params[0]->getName (32) == "Gain");
        REQUIRE (params[0]->getText (0.5f, 32) == "50 %");

        params[0]->setValueNotifyingHost (0.0f);
        midi.clear();
        buf.clear();
        inst->processBlock (buf, midi);
        REQUIRE (rms (buf) < 1.0e-6f);

        // State: save at gain 0.25, restore into a fresh instance.
        params[0]->setValueNotifyingHost (0.25f);
        inst->processBlock (buf, midi);
        juce::MemoryBlock state;
        inst->getStateInformation (state);
        REQUIRE (state.getSize() == sizeof (double));

        auto second = ClapPluginFormat::createInstance (*desc, 48000.0, 512, error);
        REQUIRE (second != nullptr);
        second->setStateInformation (state.getData(), (int) state.getSize());
        REQUIRE (std::abs (second->getParameters()[0]->getValue() - 0.25f) < 1.0e-4f);

        auto* editor = inst->createEditorAndMakeActive();
        REQUIRE (editor != nullptr);
        delete editor;
        inst->releaseResources();
    }
}

TEST_CASE ("CLAP instruments work through the plug-in list and the instrument pool", "[clap]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager formats;
    addPluginFormats (formats);

    juce::AudioPluginFormat* clap = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == "CLAP")
            clap = f;
    REQUIRE (clap != nullptr);

    juce::KnownPluginList known;
    juce::OwnedArray<juce::PluginDescription> found;
    known.scanAndAddFile (KM_TEST_CLAP_PATH, true, found, *clap);
    REQUIRE (known.getNumTypes() == 2);

    InstrumentPool pool (formats, known);
    pool.setAudioConfig (48000.0, 256);
    const auto& desc = known.getTypes()[0];
    const InstrumentKey key { InstrumentKey::Type::plugin, desc.createIdentifierString(), desc.name, {} };
    juce::String error;
    auto* inst = pool.acquireSync (key, error);
    INFO (error);
    REQUIRE (inst != nullptr);
    inst->prepare (48000.0, 256);

    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
    buf.clear();
    inst->process (buf, midi);
    REQUIRE (rms (buf) > 0.02f);
    REQUIRE (inst->hasEditor());
    REQUIRE (inst->getStateBase64().isNotEmpty());
    pool.release (inst);
}
