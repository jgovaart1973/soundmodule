#include "Engine/ExternalMidiInstrument.h"
#include "Midi/MidiOutHub.h"
#include "Model/Notation.h"
#include "UI/ScoreView.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace km;
using namespace km::notation;
using Catch::Matchers::WithinAbs;

namespace
{
    std::vector<SongNote> line (std::initializer_list<int> pitches, double length, double start = 0.0)
    {
        std::vector<SongNote> v;
        double t = start;
        for (int p : pitches)
        {
            v.push_back ({ t, length, p, 90, 1 });
            t += length;
        }
        return v;
    }

    struct Sink
    {
        struct Msg { int slot; juce::MidiMessage m; double time; };
        juce::CriticalSection lock;
        std::vector<Msg> got;

        void attach (MidiOutHub& hub)
        {
            hub.testSink = [this] (int slot, const juce::MidiMessage& m, double t)
            {
                const juce::ScopedLock sl (lock);
                got.push_back ({ slot, m, t });
            };
        }
        std::vector<Msg> take()
        {
            const juce::ScopedLock sl (lock);
            auto v = got;
            got.clear();
            return v;
        }
    };
} // namespace

//==============================================================================
TEST_CASE ("Spelling follows the key", "[notation]")
{
    REQUIRE (spell (60, 0).step == 28);
    REQUIRE (spell (61, 0).step == 28);
    REQUIRE (spell (61, 0).accidental == 1);    // C#
    REQUIRE (spell (61, -1).step == 29);
    REQUIRE (spell (61, -1).accidental == -1);  // Db
    REQUIRE (spell (65, 6).accidental == 1);    // E# in F# major
    REQUIRE (keyAccidental (3, 1) == 1);        // F# in G major
    REQUIRE (keyAccidental (6, -1) == -1);      // Bb in F major
    REQUIRE (keyAccidental (0, 1) == 0);
    REQUIRE (staffPosition (spell (64, 0).step, 0) == 0);   // E4 on the bottom treble line
    REQUIRE (staffPosition (spell (43, 0).step, 1) == 0);   // G2 on the bottom bass line
}

TEST_CASE ("Key guessing", "[notation]")
{
    REQUIRE (guessKey (line ({ 67, 69, 71, 72, 74, 76, 78, 79, 74, 71, 67 }, 1.0)) == 1);   // G major
    REQUIRE (guessKey (line ({ 62, 65, 69, 62, 64, 65, 67, 69, 70, 73, 74 }, 1.0)) == -1);  // D minor
    REQUIRE (guessKey (line ({ 60, 64, 67, 72, 65, 69, 67, 71, 74 }, 1.0)) == 0);           // C major
}

TEST_CASE ("Quarter notes and a whole-bar rest on the other staff", "[notation]")
{
    const auto L = layout (line ({ 60, 62, 64, 65 }, 1.0), 4.0, 4.0, 1.0, 0);
    REQUIRE (L.bars.size() == 1);
    const auto& treble = L.bars[0].staves[0];
    REQUIRE (treble.size() == 4);
    for (auto& it : treble)
    {
        REQUIRE_FALSE (it.rest);
        REQUIRE (it.value.denominator == 4);
        REQUIRE_FALSE (it.value.dotted);
    }
    const auto& bass = L.bars[0].staves[1];
    REQUIRE (bass.size() == 1);
    REQUIRE (bass[0].rest);
    REQUIRE (bass[0].value.denominator == 1);
}

TEST_CASE ("Notes over the bar line are tied, dotted values are used", "[notation]")
{
    std::vector<SongNote> n { { 0.0, 1.5, 67, 90, 1 }, { 1.5, 0.5, 69, 90, 1 }, { 3.0, 2.0, 72, 90, 1 } };
    const auto L = layout (n, 8.0, 4.0, 1.0, 0);
    REQUIRE (L.bars.size() == 2);
    const auto& b0 = L.bars[0].staves[0];
    REQUIRE (b0[0].value.denominator == 4);
    REQUIRE (b0[0].value.dotted);
    REQUIRE (b0[1].value.denominator == 8);
    REQUIRE (b0.back().value.denominator == 4);
    REQUIRE (b0.back().heads[0].tieToNext);
    const auto& b1 = L.bars[1].staves[0];
    REQUIRE_FALSE (b1[0].rest);
    REQUIRE (b1[0].heads[0].note == 72);
    REQUIRE_FALSE (b1[0].heads[0].tieToNext);
    REQUIRE (b1[0].heads[0].accidental == noAccidental);
    // Rest of bar 2: a quarter + half rest (or equivalent) filling 3 beats
    double restBeats = 0.0;
    for (auto& it : b1)
        if (it.rest)
            restBeats += it.value.beats();
    REQUIRE_THAT (restBeats, WithinAbs (3.0, 1e-9));
}

TEST_CASE ("Accidentals hold for the bar and respect the key", "[notation]")
{
    auto L = layout (line ({ 66, 66, 65, 66 }, 1.0), 8.0, 4.0, 1.0, 0);   // C major: F# F# F F#
    auto& t = L.bars[0].staves[0];
    REQUIRE (t[0].heads[0].accidental == 1);
    REQUIRE (t[1].heads[0].accidental == noAccidental);
    REQUIRE (t[2].heads[0].accidental == 0);   // natural
    REQUIRE (t[3].heads[0].accidental == 1);

    L = layout (line ({ 66, 65 }, 1.0), 4.0, 4.0, 1.0, 1);   // G major
    REQUIRE (L.bars[0].staves[0][0].heads[0].accidental == noAccidental);
    REQUIRE (L.bars[0].staves[0][1].heads[0].accidental == 0);
}

TEST_CASE ("Eighths are beamed per beat, 6/8 in groups of three", "[notation]")
{
    auto L = layout (line ({ 60, 62, 64, 65, 67, 69, 71, 72 }, 0.5), 4.0, 4.0, 1.0, 0);
    auto& t = L.bars[0].staves[0];
    REQUIRE (t.size() == 8);
    for (size_t i = 0; i < 8; ++i)
        REQUIRE (t[i].beamGroup == (int) i / 2);

    L = layout (line ({ 60, 62, 64, 65, 67, 69 }, 0.5), 3.0, 3.0, 1.5, 0);
    auto& c = L.bars[0].staves[0];
    REQUIRE (c.size() == 6);
    REQUIRE (c[0].beamGroup == c[2].beamGroup);
    REQUIRE (c[3].beamGroup == c[5].beamGroup);
    REQUIRE (c[0].beamGroup != c[3].beamGroup);
}

TEST_CASE ("Chords share one item, bass notes go to the bass staff", "[notation]")
{
    std::vector<SongNote> n { { 0, 4, 60, 90, 1 }, { 0, 4, 64, 90, 1 }, { 0, 4, 67, 90, 1 }, { 0, 4, 48, 90, 1 } };
    const auto L = layout (n, 4.0, 4.0, 1.0, 0);
    REQUIRE (L.bars[0].staves[0].size() == 1);
    REQUIRE (L.bars[0].staves[0][0].heads.size() == 3);
    REQUIRE (L.bars[0].staves[0][0].value.denominator == 1);
    REQUIRE (L.bars[0].staves[1][0].heads.size() == 1);
    REQUIRE (L.bars[0].staves[1][0].heads[0].note == 48);
}

TEST_CASE ("Score view renders an image", "[notation][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ScoreView view;
    view.setSize (1000, 600);
    auto notes = line ({ 60, 62, 64, 65, 67, 69, 71, 72, 74, 72, 71, 69 }, 0.5);
    for (auto& n : line ({ 48, 43, 45, 41 }, 2.0))
        notes.push_back (n);
    notes.push_back ({ 8.0, 3.0, 66, 90, 1 });
    view.setContent (notes, 12.0, 4, 4, Song::autoKey, "Test");
    const auto img = view.renderImage (1000, 1.0f);
    REQUIRE (img.getWidth() == 1000);
    REQUIRE (img.getHeight() > 100);

    // Some ink on the paper.
    int dark = 0;
    for (int y = 0; y < img.getHeight(); y += 2)
        for (int x = 0; x < img.getWidth(); x += 2)
            if (img.getPixelAt (x, y).getBrightness() < 0.3f)
                ++dark;
    REQUIRE (dark > 500);

    if (auto out = juce::SystemStats::getEnvironmentVariable ("KM_SCORE_PNG", {}); out.isNotEmpty())
    {
        juce::FileOutputStream s { juce::File (out) };
        s.setPosition (0);
        s.truncate();
        juce::PNGImageFormat().writeImageToStream (view.renderImage (1000, 1.5f), s);
    }
}

//==============================================================================
TEST_CASE ("Hardware MIDI out: channel, program, timing and note-offs", "[midiout]")
{
    MidiOutHub hub;
    Sink sink;
    sink.attach (hub);

    ExternalMidiInstrument::Settings s;
    s.channel = 5;
    s.program = 11;
    s.bank = 130;
    const auto key = ExternalMidiInstrument::makeKey ("test-synth", "Testsynth", s);
    REQUIRE (key.type == InstrumentKey::Type::midiOut);
    REQUIRE (ExternalMidiInstrument::Settings::fromString (key.pluginState).program == 11);

    ExternalMidiInstrument inst (&hub, key);
    REQUIRE (inst.getPort() >= 0);
    REQUIRE (inst.getName().contains ("Testsynth"));

    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 480);   // 10 ms later at 48 kHz
    hub.beginBlock (48000.0, 512);
    inst.process (audio, midi);
    hub.flush();
    juce::Thread::sleep (30);
    auto got = sink.take();

    REQUIRE (got.size() == 5);
    REQUIRE (got[0].m.isController());
    REQUIRE (got[0].m.getControllerNumber() == 0);
    REQUIRE (got[0].m.getControllerValue() == 1);    // bank 130 = MSB 1, LSB 2
    REQUIRE (got[1].m.getControllerValue() == 2);
    REQUIRE (got[2].m.isProgramChange());
    REQUIRE (got[2].m.getProgramChangeNumber() == 11);
    REQUIRE (got[3].m.isNoteOn());
    REQUIRE (got[3].m.getChannel() == 5);
    REQUIRE (got[4].m.getNoteNumber() == 64);
    REQUIRE_THAT (got[4].time - got[3].time, WithinAbs (10.0, 0.5));

    // Panic / sound switch: the hardware gets note-offs for what is still sounding.
    hub.beginBlock (48000.0, 512);
    inst.allNotesOff();
    hub.flush();
    juce::Thread::sleep (30);
    got = sink.take();
    int offs = 0;
    for (auto& m : got)
        if (m.m.isNoteOff() && m.m.getChannel() == 5)
            ++offs;
    REQUIRE (offs == 2);
}

TEST_CASE ("MIDI clock: start, 24 ticks per beat, continue with song position, stop", "[midiout]")
{
    MidiOutHub hub;
    Sink sink;
    sink.attach (hub);
    hub.acquirePort ("clock-port", "Clock");
    hub.setClockOutputs ({ "clock-port" });

    const double sr = 48000.0, bps = 2.0 / sr;   // 120 BPM
    double beat = 0.0;
    for (int b = 0; b < 47; ++b)                  // 47 * 512 samples = just over 1 beat
    {
        hub.beginBlock (sr, 512);
        hub.processClock (true, beat, bps, 512);
        beat += 512 * bps;
    }
    hub.beginBlock (sr, 512);
    hub.processClock (false, beat, bps, 512);
    hub.flush (3000);
    juce::Thread::sleep (50);
    auto got = sink.take();

    REQUIRE (! got.empty());
    REQUIRE (got.front().m.getRawData()[0] == 0xfa);
    REQUIRE (got.back().m.getRawData()[0] == 0xfc);
    int ticks = 0;
    for (auto& m : got)
        if (m.m.getRawData()[0] == 0xf8)
            ++ticks;
    REQUIRE (ticks == 25);   // ticks at 0, 1/24, ... 24/24 (the run lasts 1.0026 beats)

    // Starting in bar 2: Song Position Pointer (16 sixteenths) + Continue
    hub.beginBlock (sr, 512);
    hub.processClock (true, 4.0, bps, 512);
    hub.flush (3000);
    juce::Thread::sleep (30);
    got = sink.take();
    REQUIRE (got.size() >= 2);
    REQUIRE (got[0].m.getRawData()[0] == 0xf2);
    REQUIRE (got[0].m.getRawData()[1] == 16);
    REQUIRE (got[1].m.getRawData()[0] == 0xfb);
}
