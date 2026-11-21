#include "Engine/BuiltInSynth.h"
#include "Engine/ModuleEngine.h"
#include "Midi/MidiLearn.h"
#include "Render/OfflineRenderer.h"

#include <catch2/catch_test_macros.hpp>

using namespace km;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    struct EngineFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        ModuleEngine engine { nullptr };
        std::vector<std::unique_ptr<BuiltInSynth>> owned;

        EngineFixture() { engine.prepare (sr, block); }

        ~EngineFixture()
        {
            juce::Array<Instrument*> back;
            for (int i = 0; i < kNumParts; ++i)
                engine.getPart (i).reclaimAll (back);
        }

        BuiltInSynth* makeSynth (int preset)
        {
            owned.push_back (std::make_unique<BuiltInSynth> (preset));
            owned.back()->prepare (sr, block);
            return owned.back().get();
        }

        void setupPart (int part, BuiltInSynth* synth, int keyLow = 0, int keyHigh = 127)
        {
            auto& p = engine.getPart (part).params;
            p.enabled = true;
            p.keyLow = keyLow;
            p.keyHigh = keyHigh;
            p.reverbSend = 0.0f;
            p.delaySend = 0.0f;
            engine.getPart (part).lend (synth);
        }

        /** Render `blocks` blocks, events go into the first block. @returns peak level. */
        float run (int blocks, std::initializer_list<MidiEvent> events = {})
        {
            std::vector<MidiEvent> ev (events);
            std::vector<int> pos (ev.size(), 0);
            juce::AudioBuffer<float> out (2, block);
            float peak = 0.0f;

            for (int b = 0; b < blocks; ++b)
            {
                engine.process (out, block, b == 0 ? ev.data() : nullptr, b == 0 ? pos.data() : nullptr,
                                b == 0 ? (int) ev.size() : 0);
                peak = juce::jmax (peak, out.getMagnitude (0, block));

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        REQUIRE (std::isfinite (out.getSample (ch, i)));
            }
            return peak;
        }

        int blocksFor (double seconds) const { return (int) (seconds * sr / block) + 1; }
    };

    MidiEvent noteOn (int note, int vel = 100, int ch = 1) { return MidiEvent::make (-1, (uint8_t) (0x90 | (ch - 1)), (uint8_t) note, (uint8_t) vel); }
    MidiEvent noteOff (int note, int ch = 1)               { return MidiEvent::make (-1, (uint8_t) (0x80 | (ch - 1)), (uint8_t) note, 0); }
    MidiEvent cc (int num, int val, int ch = 1)            { return MidiEvent::make (-1, (uint8_t) (0xb0 | (ch - 1)), (uint8_t) num, (uint8_t) val); }
} // namespace

TEST_CASE ("Every built-in preset makes sound and stays below the limiter threshold", "[engine]")
{
    for (int preset = 0; preset < BuiltInSynth::getNumPresets(); ++preset)
    {
        EngineFixture f;
        f.setupPart (0, f.makeSynth (preset));

        const float peak = f.run (f.blocksFor (0.5), { noteOn (60, 127), noteOn (64, 127), noteOn (67, 127), noteOn (36, 127) });
        INFO ("Preset " << BuiltInSynth::getPresetName (preset));
        REQUIRE (peak > 0.01f);
        REQUIRE (peak <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-4f);
    }
}

TEST_CASE ("Silence when nothing is played", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (0));
    REQUIRE (f.run (20) == 0.0f);
}

TEST_CASE ("Key split sends each half of the keyboard to its own part", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (3), 0, 59);   // bass left
    f.setupPart (1, f.makeSynth (0), 60, 127); // piano right

    f.run (4, { noteOn (40) });
    REQUIRE (f.engine.getPart (0).getActivityCount() == 1);
    REQUIRE (f.engine.getPart (1).getActivityCount() == 0);

    f.run (4, { noteOn (72) });
    REQUIRE (f.engine.getPart (0).getActivityCount() == 1);
    REQUIRE (f.engine.getPart (1).getActivityCount() == 1);
}

TEST_CASE ("MIDI channel filter", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (0));
    f.engine.getPart (0).params.channel = 2;

    f.run (2, { noteOn (60, 100, 1) });
    REQUIRE (f.engine.getPart (0).getActivityCount() == 0);
    f.run (2, { noteOn (60, 100, 2) });
    REQUIRE (f.engine.getPart (0).getActivityCount() == 1);
}

TEST_CASE ("Switching sound while a key is held: old note rings out, no hanging note", "[engine]")
{
    EngineFixture f;
    auto* organ = f.makeSynth (2); // sustaining sound: would hang forever without the note-off
    auto* pad = f.makeSynth (1);
    f.setupPart (0, organ);

    REQUIRE (f.run (8, { noteOn (60) }) > 0.01f);

    // Switch instrument while the key is still down.
    f.engine.getPart (0).lend (pad);
    REQUIRE (f.run (8) > 0.01f); // organ keeps sounding: the note is still held

    // Release the key: the note-off must reach the organ (the tail), not the pad.
    f.run (1, { noteOff (60) });
    f.run (f.blocksFor (0.2)); // natural release time of the organ
    REQUIRE (f.run (f.blocksFor (0.3)) < 0.001f);

    // After 3 s of silence the old instrument is handed back to the pool.
    f.run (f.blocksFor (3.2));
    auto released = f.engine.releaseQueue.pop();
    REQUIRE (released.has_value());
    REQUIRE (*released == organ);
}

TEST_CASE ("Sustain pedal holds notes across a sound switch", "[engine]")
{
    EngineFixture f;
    auto* organ = f.makeSynth (2);
    f.setupPart (0, organ);

    f.run (2, { cc (64, 127), noteOn (60) });
    f.run (2, { noteOff (60) });
    f.engine.getPart (0).lend (f.makeSynth (0));
    REQUIRE (f.run (8) > 0.01f);           // still sustained
    f.run (1, { cc (64, 0) });              // pedal up reaches the old instrument too
    f.run (f.blocksFor (0.2));
    REQUIRE (f.run (f.blocksFor (0.3)) < 0.001f);
}

TEST_CASE ("Disabling a part never leaves a note hanging", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (2));
    f.run (4, { noteOn (60) });
    f.engine.getPart (0).params.enabled = false;
    f.run (1, { noteOff (60) });
    f.run (f.blocksFor (0.2));
    REQUIRE (f.run (f.blocksFor (0.3)) < 0.001f);
}

TEST_CASE ("Panic silences everything", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (2));
    f.run (4, { noteOn (60), noteOn (64) });
    f.engine.requestPanic();
    f.run (1);
    REQUIRE (f.run (f.blocksFor (0.3)) < 0.001f);
}

TEST_CASE ("Mute and solo", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (2));
    f.setupPart (1, f.makeSynth (2));
    f.engine.getPart (0).params.mute = true;
    f.engine.getPart (1).params.keyLow = 100; // part 2 ignores note 60
    REQUIRE (f.run (8, { noteOn (60) }) < 0.001f);

    f.engine.getPart (0).params.mute = false;
    f.engine.getPart (1).params.solo = true; // only part 2 audible
    REQUIRE (f.run (8, { noteOn (62) }) < 0.001f);
}

TEST_CASE ("Program Change on the control channel selects a performance", "[engine]")
{
    EngineFixture f;
    f.engine.controlChannel = 16;
    f.run (1, { MidiEvent::make (-1, 0xcf, 4, 0, 2) });

    auto req = f.engine.programRequests.pop();
    REQUIRE (req.has_value());
    REQUIRE (req->kind == ProgramRequest::performanceSelect);
    REQUIRE (req->program == 4);
}

TEST_CASE ("Program Change on a part carries the bank number", "[engine]")
{
    EngineFixture f;
    f.setupPart (0, f.makeSynth (0));
    f.run (1, { cc (0, 1), cc (32, 2), MidiEvent::make (-1, 0xc0, 7, 0, 2) });

    auto req = f.engine.programRequests.pop();
    REQUIRE (req.has_value());
    REQUIRE (req->kind == ProgramRequest::partProgram);
    REQUIRE (req->bank == 130);
    REQUIRE (req->program == 7);
}

TEST_CASE ("MIDI Learn binds a controller and consumes it", "[learn]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MidiLearn learn;
    std::atomic<float> volume { 0.0f };
    learn.registerParameter ("part1.volume", "Part 1 volume", [&] (float v) { volume = v; });

    learn.arm ("part1.volume");
    REQUIRE (learn.handle (MidiEvent::make (0, 0xb0, 7, 10)));   // learned
    REQUIRE (learn.hasMapping ("part1.volume"));

    REQUIRE (learn.handle (MidiEvent::make (3, 0xb0, 7, 127)));  // any device, mapped
    REQUIRE (volume.load() == 1.0f);
    REQUIRE_FALSE (learn.handle (MidiEvent::make (0, 0xb0, 1, 64))); // other CC passes through
    REQUIRE_FALSE (learn.handle (MidiEvent::make (0, 0x90, 60, 64)));

    MidiLearn restored;
    restored.registerParameter ("part1.volume", "Part 1 volume", [&] (float v) { volume = v; });
    restored.fromJson (learn.toJson());
    REQUIRE (restored.handle (MidiEvent::make (0, 0xb0, 7, 0)));
    REQUIRE (volume.load() == 0.0f);
}

TEST_CASE ("Action mappings fire once per press", "[learn]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MidiLearn learn;
    int presses = 0;
    learn.registerParameter ("action.next", "Next", [&] (float) { ++presses; }, true);
    learn.arm ("action.next");
    learn.handle (MidiEvent::make (0, 0xb0, 80, 0));

    learn.handle (MidiEvent::make (0, 0xb0, 80, 127));
    learn.handle (MidiEvent::make (0, 0xb0, 80, 127)); // still held
    learn.handle (MidiEvent::make (0, 0xb0, 80, 0));
    learn.handle (MidiEvent::make (0, 0xb0, 80, 127));
    REQUIRE (presses == 2);
}

TEST_CASE ("Offline renderer plays the demo through the default performance", "[render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    OfflineRenderer renderer (sr, block);
    juce::String error;
    // Organ without reverb: any hanging note would keep sounding at full level.
    auto performance = Performance::makeDefault();
    performance.parts[0].instrument = InstrumentKey::builtIn (2, "Drawbar Organ");
    performance.parts[0].reverbSend = 0.0f;
    REQUIRE (renderer.loadPerformance (performance, error));

    const auto seq = OfflineRenderer::makeDemoSequence();
    const auto audio = renderer.render (seq, 2.0);
    REQUIRE (audio.getNumSamples() > (int) (seq.getEndTime() * sr));
    REQUIRE (audio.getMagnitude (0, audio.getNumSamples()) > 0.05f);
    REQUIRE (audio.getRMSLevel (0, 0, (int) sr) > 0.005f);

    // The end of the tail is silent: nothing hangs.
    const int lastQuarter = audio.getNumSamples() - (int) (0.25 * sr);
    REQUIRE (audio.getMagnitude (lastQuarter, (int) (0.25 * sr)) < 0.002f);
}
