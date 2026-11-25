#include "App/AppController.h"
#include "App/SongController.h"
#include "Engine/BuiltInSynth.h"
#include "Engine/Sequencer.h"
#include "Model/Chords.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace km;
using Catch::Matchers::WithinAbs;

namespace
{
    struct Home
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("km_v05_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        Home()
        {
            dir.createDirectory();
           #if JUCE_WINDOWS
            _putenv_s ("KLANKMODULE_HOME", dir.getFullPathName().toRawUTF8());
           #else
            setenv ("KLANKMODULE_HOME", dir.getFullPathName().toRawUTF8(), 1);
           #endif
        }
        ~Home()
        {
           #if JUCE_WINDOWS
            _putenv_s ("KLANKMODULE_HOME", "");
           #else
            unsetenv ("KLANKMODULE_HOME");
           #endif
            dir.deleteRecursively();
        }
    };

    struct App
    {
        juce::ScopedJuceInitialiser_GUI gui;
        Home home;
        AppController app;
        App()
        {
            app.initialise();
            app.getEngine().prepare (48000.0, 256);
        }
        ~App() { app.shutdown(); }
        SongController& song() { return app.getSongController(); }

        /** Load a song from a model (through a file, like the user would). */
        void load (const Song& s)
        {
            const auto f = home.dir.getChildFile ("t.kmsong");
            f.replaceWithText (s.toJson());
            REQUIRE (song().loadSong (f));
        }
    };

    Track midiTrack (int part, std::vector<SongNote> notes, double start = 0.0, double length = 16.0)
    {
        Track t;
        t.part = part;
        Clip c;
        c.start = start;
        c.length = length;
        c.notes = std::move (notes);
        t.clips.push_back (c);
        return t;
    }

    std::vector<int> notesInSnapshot (const Song& s)
    {
        std::vector<int> n;
        const auto snap = PlaybackSnapshot::build (s, nullptr, 48000.0);
        for (auto& e : snap->events)
            if ((e.data[0] & 0xf0) == 0x90)
                n.push_back (e.data[1]);
        return n;
    }
} // namespace

//==============================================================================
TEST_CASE ("Track transposition: playback, drums untouched, saved, applied to the notes", "[transpose]")
{
    Song s;
    s.tracks.push_back (midiTrack (0, { { 0, 1, 60, 100, 1 }, { 1, 1, 64, 100, 1 } }));
    s.tracks.push_back (midiTrack (9, { { 0, 1, 36, 100, 10 } }));
    s.tracks[1].drums = true;
    s.tracks[0].transpose = 3;
    s.tracks[1].transpose = 5;

    auto played = notesInSnapshot (s);
    std::sort (played.begin(), played.end());
    REQUIRE (played == std::vector<int> { 36, 63, 67 });

    Song back;
    juce::String err;
    REQUIRE (back.fromJson (s.toJson(), err));
    REQUIRE (back.tracks[0].transpose == 3);

    App a;
    a.load (s);
    a.song().setTrackTranspose (0, -12);
    REQUIRE (a.song().getSong().tracks[0].transpose == -12);
    a.song().applyTrackTranspose (0);
    REQUIRE (a.song().getSong().tracks[0].transpose == 0);
    REQUIRE (a.song().getSong().tracks[0].clips[0].notes[0].note == 48);
    a.song().undo();
    REQUIRE (a.song().getSong().tracks[0].transpose == -12);
    REQUIRE (a.song().getSong().tracks[0].clips[0].notes[0].note == 60);
}

TEST_CASE ("Copying bars: one bar, a group of bars, several tracks, replacing what was there", "[copybars]")
{
    Song s;   // 4/4: bar = 4 beats
    s.tracks.push_back (midiTrack (0, { { 0, 1, 60, 100, 1 }, { 2, 3, 62, 100, 1 },     // bar 1 (62 runs into bar 2)
                                        { 4, 1, 64, 100, 1 },                            // bar 2
                                        { 8, 1, 70, 100, 1 } }));                        // bar 3 (will be replaced)
    s.tracks.push_back (midiTrack (1, { { 0, 4, 48, 100, 1 } }));
    App a;
    a.load (s);
    auto& song = a.song();

    // Bar 1 of track 1 -> bar 3
    REQUIRE (song.copyBars (0, 0, 0.0, 4.0, 8.0) == 1);
    {
        const auto& t = song.getSong().tracks[0];
        REQUIRE (t.clips.size() == 2);
        const auto& copy = t.clips[1];
        REQUIRE_THAT (copy.start, WithinAbs (8.0, 1e-9));
        REQUIRE_THAT (copy.length, WithinAbs (4.0, 1e-9));
        REQUIRE (copy.notes.size() == 2);
        REQUIRE_THAT (copy.notes[1].length, WithinAbs (2.0, 1e-9));   // cut at the end of the copied bar
        for (auto& n : t.clips[0].notes)
            REQUIRE (n.note != 70);                                       // replaced
        REQUIRE (copy.name == "Kopie maat 1");
    }

    // Bars 1-2 of both tracks -> bars 5-6
    REQUIRE (song.copyBars (0, 1, 0.0, 8.0, 16.0) == 2);
    REQUIRE (song.getSong().tracks[1].clips.back().notes.size() == 1);
    REQUIRE_THAT (song.getSong().tracks[1].clips.back().start, WithinAbs (16.0, 1e-9));
    REQUIRE (song.getSong().tracks[0].clips.back().name == "Kopie maat 1-2");

    // To another track (one lower) and undo
    REQUIRE (song.copyBars (0, 0, 4.0, 8.0, 24.0, 1) == 1);
    REQUIRE (song.getSong().tracks[1].clips.back().notes[0].note == 64);
    song.undo();
    REQUIRE (song.getSong().tracks[1].clips.size() == 2);

    // Nothing to do
    REQUIRE (song.copyBars (0, 0, 0.0, 4.0, 0.0) == 0);
}

TEST_CASE ("Several chords in one bar", "[chords2]")
{
    App a;
    auto& song = a.song();
    song.insertChord (0.0, 0, 0);        // C for the whole bar
    REQUIRE (song.getSong().chords.size() == 1);
    REQUIRE_THAT (song.getSong().chords[0].length, WithinAbs (4.0, 1e-9));

    song.insertChord (2.0, 7, 0);        // G from beat 3: splits the C
    song.insertChord (3.0, 7, 2);        // G7 on beat 4
    const auto& c = song.getSong().chords;
    REQUIRE (c.size() == 3);
    REQUIRE_THAT (c[0].length, WithinAbs (2.0, 1e-9));
    REQUIRE (chords::name (c[1]) == "G");
    REQUIRE_THAT (c[1].length, WithinAbs (1.0, 1e-9));
    REQUIRE (chords::name (c[2]) == "G7");
    REQUIRE_THAT (c[2].beat, WithinAbs (3.0, 1e-9));
    REQUIRE_THAT (c[2].length, WithinAbs (1.0, 1e-9));

    song.insertChord (2.0, 9, 1);        // change the chord on beat 3 to Am: length stays
    REQUIRE (chords::name (song.getSong().chords[1]) == "Am");
    REQUIRE_THAT (song.getSong().chords[1].length, WithinAbs (1.0, 1e-9));

    // Recognition finds two chords in a bar when the harmony changes halfway
    Clip clip;
    clip.length = 8.0;
    for (int n : { 48, 60, 64, 67 }) clip.notes.push_back ({ 0.0, 1.9, n, 90, 1 });   // C
    for (int n : { 43, 59, 62, 67 }) clip.notes.push_back ({ 2.0, 1.9, n, 90, 1 });   // G
    for (int n : { 45, 60, 64, 69 }) clip.notes.push_back ({ 4.0, 3.9, n, 90, 1 });   // Am whole bar
    const auto found = chords::detectInClip (clip, 4.0);
    REQUIRE (found.size() == 3);
    REQUIRE (chords::name (found[0]) == "C");
    REQUIRE_THAT (found[0].length, WithinAbs (2.0, 1e-9));
    REQUIRE (chords::name (found[1]) == "G");
    REQUIRE_THAT (found[1].beat, WithinAbs (2.0, 1e-9));
    REQUIRE (chords::name (found[2]) == "Am");
    REQUIRE_THAT (found[2].length, WithinAbs (4.0, 1e-9));
}

TEST_CASE ("Rewind and forward step one bar", "[transport]")
{
    App a;
    auto& song = a.song();
    auto& seq = a.app.getEngine().sequencer;
    juce::AudioBuffer<float> out (2, 256);
    auto settle = [&] { a.app.getEngine().process (out, 256, nullptr, nullptr, 0, nullptr, 0); };

    song.stepBars (1);  settle();
    REQUIRE_THAT (seq.getPosition(), WithinAbs (4.0, 1e-6));
    song.stepBars (1);  settle();
    REQUIRE_THAT (seq.getPosition(), WithinAbs (8.0, 1e-6));
    song.locate (9.5);  settle();
    song.stepBars (-1); settle();
    REQUIRE_THAT (seq.getPosition(), WithinAbs (8.0, 1e-6));
    song.stepBars (-1); settle();
    REQUIRE_THAT (seq.getPosition(), WithinAbs (4.0, 1e-6));
    song.stepBars (-1); song.stepBars (-1); settle();
    REQUIRE_THAT (seq.getPosition(), WithinAbs (0.0, 1e-6));
}

TEST_CASE ("New notes start without a click", "[click]")
{
    // A note must not jump from silence to a large value in one sample (heard as a tick).
    for (int preset : { 8, 9, 10, 11, 12, 13, 14 })
    {
        INFO (BuiltInSynth::getPresetName (preset));
        BuiltInSynth s (preset);
        s.prepare (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        for (int n : { 48, 60, 64, 67 })
            m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 127), 100);
        b.clear();
        s.process (b, m);
        // Compare the jumps right at the start of the notes with the jumps once they sound.
        float onsetStep = 0.0f, steadyStep = 0.0f;
        for (int i = 100; i < 104; ++i)
            onsetStep = juce::jmax (onsetStep, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
        for (int i = 300; i < 512; ++i)
            steadyStep = juce::jmax (steadyStep, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
        INFO ("onset " << onsetStep << " steady " << steadyStep);
        REQUIRE (steadyStep > 0.0f);
        REQUIRE (onsetStep < 0.6f * steadyStep + 0.01f);
    }
}
