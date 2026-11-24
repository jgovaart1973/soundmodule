#include "Engine/Arpeggiator.h"
#include "Engine/BuiltInDrums.h"
#include "Model/Chords.h"
#include "Model/DrumMap.h"
#include "Model/Song.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

using namespace km;

namespace
{
    struct Ev { int status, note, vel, pos; };

    /** Run the arpeggiator synced for a number of 512-sample blocks at 120 BPM / 48 kHz. */
    std::vector<Ev> runArp (Arpeggiator& a, const Arpeggiator::Settings& s, int blocks, int startBlock = 0)
    {
        std::vector<Ev> out;
        const double sr = 48000.0, bps = 2.0 / sr; // 120 BPM: 2 beats per second
        for (int b = startBlock; b < startBlock + blocks; ++b)
            a.process (s, true, b * 512 * bps, bps, 512, [&] (const uint8_t* d, int, int pos)
            {
                out.push_back ({ d[0] & 0xf0, d[1], d[2], b * 512 + pos });
            });
        return out;
    }

    std::vector<int> onNotes (const std::vector<Ev>& v)
    {
        std::vector<int> n;
        for (auto& e : v)
            if (e.status == 0x90)
                n.push_back (e.note);
        return n;
    }
} // namespace

TEST_CASE ("Drum kits: every GM drum note sounds and dies out", "[drums]")
{
  for (int kitIndex = 0; kitIndex < BuiltInDrums::numKits; ++kitIndex)
  {
    BuiltInDrums kit (kitIndex);
    kit.prepare (48000.0, 512);
    INFO ("kit " << kitIndex);
    for (auto& row : gmDrumRows())
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, row.note, (juce::uint8) 110), 0);
        float peak = 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            buf.clear();
            kit.process (buf, midi);
            midi.clear();
            peak = juce::jmax (peak, buf.getMagnitude (0, 512));
        }
        INFO (row.name << " peak " << peak);
        REQUIRE (peak > 0.01f);
        REQUIRE (peak < 1.5f);
    }
    // After a few seconds everything is silent again.
    float tail = 1.0f;
    for (int b = 0; b < 600; ++b)
    {
        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        juce::MidiBuffer none;
        kit.process (buf, none);
        tail = buf.getMagnitude (0, 512);
    }
    REQUIRE (tail < 1.0e-4f);
  }
    REQUIRE (juce::String (gmDrumName (38)).isNotEmpty());
}

TEST_CASE ("Arpeggiator plays held notes upward on the grid with the gate", "[arp]")
{
    Arpeggiator a;
    Arpeggiator::Settings s;
    s.mode = Arpeggiator::Mode::up;
    s.stepBeats = 0.5;
    s.gate = 0.5;
    a.noteOn (67, 100, 1, false);
    a.noteOn (60, 100, 1, false);
    a.noteOn (64, 100, 1, false);
    const auto ev = runArp (a, s, 200); // ~2.1 s = 4+ beats
    const auto notes = onNotes (ev);
    REQUIRE (notes.size() >= 8);
    REQUIRE (std::vector<int> (notes.begin(), notes.begin() + 6) == std::vector<int> { 60, 64, 67, 60, 64, 67 });

    // Eighth notes at 120 BPM = 12000 samples apart; note off after half a step.
    std::vector<int> onPos, offPos;
    for (auto& e : ev)
        (e.status == 0x90 ? onPos : offPos).push_back (e.pos);
    REQUIRE (std::abs ((onPos[1] - onPos[0]) - 12000) <= 1);
    REQUIRE (std::abs ((offPos[0] - onPos[0]) - 6000) <= 1);
}

TEST_CASE ("Arpeggiator modes: down, up-down, octaves and chord", "[arp]")
{
    auto play = [] (Arpeggiator::Mode m, int octaves)
    {
        Arpeggiator a;
        Arpeggiator::Settings s;
        s.mode = m;
        s.octaves = octaves;
        s.stepBeats = 0.25;
        for (int n : { 60, 64, 67 })
            a.noteOn (n, 100, 1, false);
        return onNotes (runArp (a, s, 60));
    };
    auto first = [] (const std::vector<int>& v, size_t n) { return std::vector<int> (v.begin(), v.begin() + (long) n); };

    REQUIRE (first (play (Arpeggiator::Mode::down, 1), 3) == std::vector<int> { 67, 64, 60 });
    REQUIRE (first (play (Arpeggiator::Mode::up, 2), 6) == std::vector<int> { 60, 64, 67, 72, 76, 79 });
    const auto ud = play (Arpeggiator::Mode::upDown, 1);
    REQUIRE (first (ud, 4) == std::vector<int> { 60, 64, 67, 64 });
    const auto ch = play (Arpeggiator::Mode::chord, 1);
    REQUIRE (first (ch, 6) == std::vector<int> { 60, 64, 67, 60, 64, 67 });
}

TEST_CASE ("Arpeggiator stops after release, latch keeps it going", "[arp]")
{
    Arpeggiator::Settings s;
    s.stepBeats = 0.25;

    Arpeggiator a;
    a.noteOn (60, 100, 1, false);
    runArp (a, s, 20);
    a.noteOff (60, false);
    const auto after = runArp (a, s, 40, 20);
    REQUIRE (onNotes (after).empty());
    REQUIRE_FALSE (a.isSounding());

    Arpeggiator l;
    s.latch = true;
    l.noteOn (60, 100, 1, true);
    l.noteOn (64, 100, 1, true);
    runArp (l, s, 10);
    l.noteOff (60, true);
    l.noteOff (64, true);
    const auto latched = onNotes (runArp (l, s, 40, 10));
    REQUIRE (latched.size() >= 4);
    // A new chord after letting go replaces the latched one.
    l.noteOn (70, 100, 1, true);
    const auto replaced = onNotes (runArp (l, s, 40, 50));
    REQUIRE_FALSE (replaced.empty());
    for (int n : replaced)
        REQUIRE (n == 70);
}

TEST_CASE ("Chord recognition", "[chords]")
{
    auto weights = [] (std::initializer_list<int> notes)
    {
        std::array<double, 12> w {};
        for (int n : notes)
            w[(size_t) (n % 12)] += 1.0;
        return w;
    };
    auto nameOf = [] (const chords::Match& m) { return chords::name ({ 0, 4, m.root, m.type }); };

    REQUIRE (nameOf (chords::detect (weights ({ 60, 64, 67 }))) == "C");
    REQUIRE (nameOf (chords::detect (weights ({ 57, 60, 64 }))) == "Am");
    REQUIRE (nameOf (chords::detect (weights ({ 55, 59, 62, 65 }))) == "G7");
    REQUIRE (nameOf (chords::detect (weights ({ 60, 63, 66 }))) == "Cdim");
    REQUIRE (nameOf (chords::detect (weights ({ 62, 65, 69, 72 }))) == "Dm7");
    // Am7 and C6 share notes: the bass decides.
    REQUIRE (nameOf (chords::detect (weights ({ 57, 60, 64, 67 }), 9)) == "Am7");
}

TEST_CASE ("Chords per bar from a clip, voicing and accompaniment", "[chords]")
{
    Clip clip;
    clip.start = 8.0;
    clip.length = 16.0;
    auto chord = [&] (double at, std::initializer_list<int> notes)
    {
        for (int n : notes)
            clip.notes.push_back ({ at, 3.5, n, 90, 1 });
    };
    chord (0, { 48, 60, 64, 67 });   // C
    chord (4, { 48, 60, 64, 67 });   // C again -> merged
    chord (8, { 45, 60, 64, 69 });   // Am
    chord (12, { 43, 59, 62, 65 });  // G7

    const auto found = chords::detectInClip (clip, 4.0);
    REQUIRE (found.size() == 3);
    REQUIRE (chords::name (found[0]) == "C");
    REQUIRE_THAT (found[0].beat, WithinAbs (8.0, 1e-9));
    REQUIRE_THAT (found[0].length, WithinAbs (8.0, 1e-9));
    REQUIRE (chords::name (found[1]) == "Am");
    REQUIRE (chords::name (found[2]) == "G7");

    REQUIRE (chords::voicing (0, 0) == std::vector<int> { 60, 64, 67 });
    for (int r = 0; r < 12; ++r)
        for (int n : chords::voicing (r, 0))
            REQUIRE ((n >= 55 && n <= 79));

    const auto acc = chords::accompaniment (found, 8.0, true, true);
    REQUIRE (acc.size() == 3 + 1 + 3 + 1 + 4 + 1);
    REQUIRE_THAT (acc.front().start, WithinAbs (0.0, 1e-9));
    int bassNotes = 0;
    for (auto& n : acc)
        if (n.note < 48)
            ++bassNotes;
    REQUIRE (bassNotes == 3);
}

TEST_CASE ("Song stores chords and drum tracks in JSON", "[chords][song]")
{
    Song s;
    s.tracks.push_back ({});
    s.tracks[0].drums = true;
    s.chords.push_back ({ 4.0, 8.0, 9, 1 });
    s.chords.push_back ({ 12.0, 4.0, 7, 2 });

    Song back;
    juce::String err;
    REQUIRE (back.fromJson (s.toJson(), err));
    REQUIRE (back.tracks.size() == 1);
    REQUIRE (back.tracks[0].drums);
    REQUIRE (back.chords.size() == 2);
    REQUIRE_THAT (back.chords[0].beat, WithinAbs (4.0, 1e-9));
    REQUIRE_THAT (back.chords[0].length, WithinAbs (8.0, 1e-9));
    REQUIRE (chords::name (back.chords[0]) == "Am");
    REQUIRE (chords::name (back.chords[1]) == "G7");
}
