#include "Engine/BuiltInDrums.h"
#include "Engine/BuiltInSynth.h"
#include "Model/Notation.h"
#include "Model/Performance.h"
#include "UI/ScoreView.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace km;
using namespace km::notation;
using Catch::Matchers::WithinAbs;

namespace
{
    std::vector<const Item*> voiceItems (const Bar& bar, int staff, int voice)
    {
        std::vector<const Item*> v;
        for (auto& it : bar.staves[(size_t) staff])
            if (it.voice == voice)
                v.push_back (&it);
        return v;
    }
} // namespace

//==============================================================================
TEST_CASE ("New built-in instruments exist and play", "[instruments]")
{
    juce::StringArray names;
    for (int p = 0; p < BuiltInSynth::getNumPresets(); ++p)
        names.add (BuiltInSynth::getPresetName (p));
    for (auto* wanted : { "Acoustic Grand Piano", "Fretless Bass", "Soprano Sax", "Alto Sax", "Tenor Sax",
                          "Nylon Guitar", "Steel Guitar", "Clean Electric Guitar", "Choir Aahs", "Choir Oohs", "Flute" })
    {
        INFO (wanted);
        REQUIRE (names.contains (wanted));
    }
    REQUIRE (BuiltInSynth::getPresetName (0) == "Electric Piano");   // old numbers stay the same

    // Every new sound: in tune-ish, makes sound, and a released note dies away.
    for (int p = 8; p < BuiltInSynth::getNumPresets(); ++p)
    {
        INFO (BuiltInSynth::getPresetName (p));
        BuiltInSynth s (p);
        s.prepare (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        float peak = 0.0f;
        for (int k = 0; k < 40; ++k)
        {
            b.clear();
            s.process (b, m);
            m.clear();
            peak = juce::jmax (peak, b.getMagnitude (0, 512));
        }
        REQUIRE (peak > 0.02f);
        REQUIRE (peak < 1.0f);
        m.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
        float last = 1.0f;
        for (int k = 0; k < 400; ++k)
        {
            b.clear();
            s.process (b, m);
            m.clear();
            last = b.getMagnitude (0, 512);
        }
        REQUIRE (last < 1.0e-3f);
    }
}

TEST_CASE ("Drum kits: acoustic and electronic, old files keep their kit", "[instruments]")
{
    REQUIRE (BuiltInDrums (0).getName() == "Drumstel Akoestisch");
    REQUIRE (BuiltInDrums (1).getName() == "Drumstel Elektronisch");
    REQUIRE (BuiltInDrums (1).key.ref == "101");
    REQUIRE (BuiltInDrums::kitForProgram (25) == 1);
    REQUIRE (BuiltInDrums::kitForProgram (0) == 0);

    auto* o = new juce::DynamicObject();
    o->setProperty ("type", "builtin");
    o->setProperty ("ref", "8");
    o->setProperty ("name", "Drumstel (GM)");
    const auto k = InstrumentKey::fromVar (juce::var (o));
    REQUIRE (k.ref == "101");
    REQUIRE (BuiltInDrums::isDrumRef (k.ref.getIntValue()));
}

//==============================================================================
TEST_CASE ("Two voices on one staff: held note above a moving line", "[notation][voices]")
{
    // Whole note G4 held, quarter notes E4 D4 C4 B3 underneath (all treble staff: split at 59)
    std::vector<SongNote> n { { 0, 4, 67, 90, 1 }, { 0, 1, 64, 90, 1 }, { 1, 1, 62, 90, 1 }, { 2, 1, 60, 90, 1 }, { 3, 1, 64, 90, 1 } };
    const auto L = layout (n, 4.0, 4.0, 1.0, 0, 0.25, 59);
    const auto& bar = L.bars[0];
    REQUIRE (bar.twoVoices[0]);
    const auto upper = voiceItems (bar, 0, 0), lower = voiceItems (bar, 0, 1);
    REQUIRE (upper.size() == 1);
    REQUIRE (upper[0]->value.denominator == 1);
    REQUIRE (upper[0]->heads[0].note == 67);
    REQUIRE (lower.size() == 4);
    for (auto* it : lower)
        REQUIRE (it->value.denominator == 4);

    // With one voice the held note is cut off at the next note (old behaviour).
    const auto single = layout (n, 4.0, 4.0, 1.0, 0, 0.25, 59, 1);
    REQUIRE_FALSE (single.bars[0].twoVoices[0]);
    REQUIRE (single.bars[0].staves[0].size() == 4);
}

TEST_CASE ("A chord stays one voice, a bar without the lower voice gets no extra rests", "[notation][voices]")
{
    std::vector<SongNote> n { { 0, 2, 60, 90, 1 }, { 0, 1.95, 64, 90, 1 }, { 0, 2.05, 67, 90, 1 },   // chord, ends nearly together
                              { 4, 4, 72, 90, 1 }, { 4, 2, 64, 90, 1 }, { 6, 2, 65, 90, 1 } };       // bar 2: two voices
    const auto L = layout (n, 8.0, 4.0, 1.0, 0);
    REQUIRE_FALSE (L.bars[0].twoVoices[0]);
    REQUIRE (voiceItems (L.bars[0], 0, 1).empty());
    REQUIRE (voiceItems (L.bars[0], 0, 0)[0]->heads.size() == 3);
    REQUIRE (L.bars[1].twoVoices[0]);
    REQUIRE (voiceItems (L.bars[1], 0, 1).size() == 2);
}

TEST_CASE ("Eighth-note triplets are found and written as three eighths under a 3", "[notation][triplets]")
{
    // Beat 1: three triplet eighths, beat 2: two straight eighths, beats 3-4: half note
    std::vector<SongNote> n { { 0.0, 1.0 / 3, 60, 90, 1 }, { 1.0 / 3, 1.0 / 3, 62, 90, 1 }, { 2.0 / 3, 1.0 / 3, 64, 90, 1 },
                              { 1.0, 0.5, 65, 90, 1 }, { 1.5, 0.5, 67, 90, 1 }, { 2.0, 2.0, 72, 90, 1 } };
    const auto L = layout (n, 4.0, 4.0, 1.0, 0);
    const auto& t = L.bars[0].staves[0];
    REQUIRE (L.bars[0].tuplets[0].size() == 1);
    REQUIRE_THAT (L.bars[0].tuplets[0][0].start, WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (L.bars[0].tuplets[0][0].end, WithinAbs (1.0, 1e-9));
    REQUIRE (t.size() == 6);
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE (t[(size_t) i].tuplet == 0);
        REQUIRE (t[(size_t) i].value.denominator == 8);
        REQUIRE_THAT (t[(size_t) i].start, WithinAbs (i / 3.0, 1e-9));
    }
    REQUIRE (t[0].beamGroup >= 0);
    REQUIRE (t[0].beamGroup == t[2].beamGroup);
    REQUIRE (t[3].tuplet == -1);
    REQUIRE (t[3].value.denominator == 8);
    REQUIRE (t[3].beamGroup != t[0].beamGroup);
    REQUIRE (t[5].value.denominator == 2);
}

TEST_CASE ("Played (slightly off) triplets are still triplets, straight sixteenths are not", "[notation][triplets]")
{
    std::vector<SongNote> played { { 0.01, 0.3, 60, 90, 1 }, { 0.345, 0.3, 62, 90, 1 }, { 0.655, 0.3, 64, 90, 1 } };
    auto L = layout (played, 4.0, 4.0, 1.0, 0);
    REQUIRE (L.bars[0].tuplets[0].size() == 1);

    std::vector<SongNote> straight { { 0, 0.25, 60, 90, 1 }, { 0.25, 0.25, 62, 90, 1 }, { 0.5, 0.25, 64, 90, 1 }, { 0.75, 0.25, 65, 90, 1 } };
    L = layout (straight, 4.0, 4.0, 1.0, 0);
    REQUIRE (L.bars[0].tuplets[0].empty());
}

TEST_CASE ("Quarter-note triplets and sixteenth triplets", "[notation][triplets]")
{
    std::vector<SongNote> q { { 0.0, 2.0 / 3, 60, 90, 1 }, { 2.0 / 3, 2.0 / 3, 62, 90, 1 }, { 4.0 / 3, 2.0 / 3, 64, 90, 1 } };
    auto L = layout (q, 4.0, 4.0, 1.0, 0);
    REQUIRE (L.bars[0].tuplets[0].size() == 1);
    REQUIRE_THAT (L.bars[0].tuplets[0][0].end, WithinAbs (2.0, 1e-9));
    int quarters = 0;
    for (auto& it : L.bars[0].staves[0])
        if (it.tuplet == 0 && ! it.rest && it.value.denominator == 4 && ! it.value.dotted)
            ++quarters;
    REQUIRE (quarters == 3);

    std::vector<SongNote> s;
    for (int i = 0; i < 6; ++i)
        s.push_back ({ i / 6.0, 1.0 / 6, 60 + i, 90, 1 });
    L = layout (s, 4.0, 4.0, 1.0, 0);
    REQUIRE (L.bars[0].tuplets[0].size() == 1);
    int sixteenths = 0;
    for (auto& it : L.bars[0].staves[0])
        if (it.tuplet == 0 && it.value.denominator == 16)
            ++sixteenths;
    REQUIRE (sixteenths == 6);
}

TEST_CASE ("Score with two voices and triplets renders", "[notation][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ScoreView view;
    view.setSize (1000, 600);
    std::vector<SongNote> n { { 0, 4, 76, 90, 1 }, { 0, 1, 67, 90, 1 }, { 1, 1, 69, 90, 1 }, { 2, 2, 71, 90, 1 },
                              { 4.0, 1.0 / 3, 72, 90, 1 }, { 4 + 1.0 / 3, 1.0 / 3, 74, 90, 1 }, { 4 + 2.0 / 3, 1.0 / 3, 76, 90, 1 },
                              { 5, 0.5, 77, 90, 1 }, { 5.5, 0.5, 76, 90, 1 }, { 6, 2.0 / 3, 74, 90, 1 }, { 6 + 2.0 / 3, 2.0 / 3, 72, 90, 1 },
                              { 6 + 4.0 / 3, 2.0 / 3, 71, 90, 1 }, { 0, 8, 48, 90, 1 }, { 4, 4, 43, 90, 1 }, { 0, 2, 55, 90, 1 } };
    view.setContent (n, 8.0, 4, 4, Song::autoKey, "Twee stemmen en triolen");
    const auto img = view.renderImage (1000, 1.0f);
    REQUIRE (img.getHeight() > 100);
    if (auto out = juce::SystemStats::getEnvironmentVariable ("KM_SCORE_PNG2", {}); out.isNotEmpty())
    {
        juce::FileOutputStream st { juce::File (out) };
        st.setPosition (0);
        st.truncate();
        juce::PNGImageFormat().writeImageToStream (view.renderImage (1000, 1.5f), st);
    }
}
