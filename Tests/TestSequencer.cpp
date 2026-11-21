#include "App/AppController.h"
#include "Core/AppPaths.h"
#include "Engine/BuiltInSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdlib>

using namespace km;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    struct TempHome
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("km_seq_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempHome()
        {
            dir.createDirectory();
           #if JUCE_WINDOWS
            _putenv_s ("KLANKMODULE_HOME", dir.getFullPathName().toRawUTF8());
           #else
            setenv ("KLANKMODULE_HOME", dir.getFullPathName().toRawUTF8(), 1);
           #endif
        }
        ~TempHome()
        {
           #if JUCE_WINDOWS
            _putenv_s ("KLANKMODULE_HOME", "");
           #else
            unsetenv ("KLANKMODULE_HOME");
           #endif
            dir.deleteRecursively();
        }
    };

    MidiEvent on (int note, int vel = 100)  { return MidiEvent::make (-1, 0x90, (uint8_t) note, (uint8_t) vel); }
    MidiEvent off (int note)                { return MidiEvent::make (-1, 0x80, (uint8_t) note, 0); }

    /** Drives the engine like an audio device would, in 256-sample blocks.
        Live events are given at absolute sample times from the start of this call. */
    struct Driver
    {
        AppController& app;
        double peak = 0.0;

        float run (double seconds, std::vector<std::pair<double, MidiEvent>> live = {})
        {
            auto& engine = app.getEngine();
            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (2, block);
            float maxPeak = 0.0f;
            size_t next = 0;

            for (int start = 0; start < total; start += block)
            {
                std::vector<MidiEvent> ev;
                std::vector<int> pos;
                while (next < live.size() && (int) (live[next].first * sr) < start + block)
                {
                    ev.push_back (live[next].second);
                    pos.push_back (juce::jmax (0, (int) (live[next].first * sr) - start));
                    ++next;
                }
                engine.process (out, block, ev.data(), pos.data(), (int) ev.size());
                maxPeak = juce::jmax (maxPeak, out.getMagnitude (0, block));
                app.getSongController().tick(); // normally the 30 Hz UI timer
            }
            return maxPeak;
        }
    };

    struct Fixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        TempHome home;
        AppController app;
        Driver drive { app };

        Fixture()
        {
            app.initialise();
            app.getEngine().prepare (sr, block);
            app.getPool().setAudioConfig (sr, block);
            // Organ on part 1 without reverb: sustains while held, silent right after release.
            app.setPartInstrument (0, InstrumentKey::builtIn (2, "Drawbar Organ"));
            app.partParams (0).reverbSend = 0.0f;
            drive.run (0.05); // let the part adopt the new instrument
        }
        ~Fixture() { app.shutdown(); }

        SongController& song() { return app.getSongController(); }
    };

    Song makeSongWithNote (double start, double length, int note = 60)
    {
        Song s;
        Track t;
        t.part = 0;
        Clip c;
        c.start = 0.0;
        c.length = 8.0;
        c.notes.push_back ({ start, length, note, 100, 1 });
        t.clips.push_back (c);
        s.tracks.push_back (t);
        return s;
    }
} // namespace

//==============================================================================
TEST_CASE ("Song survives a JSON round trip including its sounds", "[song]")
{
    Song s = makeSongWithNote (1.0, 2.5, 64);
    s.name = "Test";
    s.bpm = 96.0;
    s.beatsPerBar = 3;
    s.loopEnabled = true;
    s.loopStart = 4.0;
    s.loopEnd = 12.0;
    s.tracks[0].clips[0].controls.push_back ({ 0.5, 0xb0, 64, 127 });
    s.tracks[0].clips[0].take = 2;
    s.performance.parts[3].enabled = true;

    Song r;
    juce::String error;
    REQUIRE (r.fromJson (s.toJson(), error));
    REQUIRE (r.bpm == 96.0);
    REQUIRE (r.beatsPerBar == 3);
    REQUIRE (r.loopEnabled);
    REQUIRE (r.tracks.size() == 1);
    REQUIRE (r.tracks[0].clips[0].take == 2);
    REQUIRE (r.tracks[0].clips[0].notes[0].note == 64);
    REQUIRE_THAT (r.tracks[0].clips[0].notes[0].length, WithinAbs (2.5, 1e-9));
    REQUIRE (r.tracks[0].clips[0].controls[0].data1 == 64);
    REQUIRE (r.performance.parts[3].enabled);
}

TEST_CASE ("MIDI file export and import keep notes, timing and part mapping", "[song]")
{
    Song s = makeSongWithNote (1.0, 0.5, 67);
    s.bpm = 100.0;
    s.tracks[0].part = 4; // exported on channel 5
    s.tracks[0].clips[0].notes.push_back ({ 2.25, 1.0, 72, 90, 1 });

    Song imported;
    REQUIRE (imported.importMidiFile (s.toMidiFile()) == 1);
    REQUIRE_THAT (imported.bpm, WithinAbs (100.0, 0.01));
    REQUIRE (imported.tracks[0].part == 4);

    auto& notes = imported.tracks[0].clips[0].notes;
    REQUIRE (notes.size() == 2);
    REQUIRE (notes[0].note == 67);
    REQUIRE_THAT (notes[0].start, WithinAbs (1.0, 0.002));
    REQUIRE_THAT (notes[1].start, WithinAbs (2.25, 0.002));
    REQUIRE_THAT (notes[1].length, WithinAbs (1.0, 0.002));
    REQUIRE (notes[1].velocity == 90);
}

TEST_CASE ("Playback snapshot orders note-offs before note-ons at the same time", "[song]")
{
    Song s = makeSongWithNote (0.0, 1.0);
    s.tracks[0].clips[0].notes.push_back ({ 1.0, 1.0, 60, 100, 1 }); // repeated note, back to back
    auto snap = PlaybackSnapshot::build (s);
    REQUIRE (snap->events.size() == 4);
    REQUIRE ((snap->events[1].data[0] & 0xf0) == 0x80);
    REQUIRE ((snap->events[2].data[0] & 0xf0) == 0x90);
    REQUIRE (snap->events[1].beat == snap->events[2].beat);
}

TEST_CASE ("Muted clips, muted tracks and solo are respected in playback", "[song]")
{
    Song s = makeSongWithNote (0.0, 1.0);
    s.tracks.push_back (s.tracks[0]);
    REQUIRE (PlaybackSnapshot::build (s)->events.size() == 4);
    s.tracks[1].mute = true;
    REQUIRE (PlaybackSnapshot::build (s)->events.size() == 2);
    s.tracks[1].mute = false;
    s.tracks[1].solo = true;
    REQUIRE (PlaybackSnapshot::build (s)->events.size() == 2);
    s.tracks[1].clips[0].muted = true;
    REQUIRE (PlaybackSnapshot::build (s)->events.empty());
}

//==============================================================================
TEST_CASE ("Song playback plays a note on its part and releases it", "[sequencer]")
{
    Fixture f;
    auto& seq = f.app.getEngine().sequencer;
    seq.setSnapshot (PlaybackSnapshot::build (makeSongWithNote (0.5, 1.0))); // 0.25 s .. 0.75 s at 120 BPM
    seq.play();

    REQUIRE (f.drive.run (0.2) < 0.001f);   // before the note
    REQUIRE (f.drive.run (0.4) > 0.01f);    // note sounding
    f.drive.run (0.3);                      // note-off + organ release
    REQUIRE (f.drive.run (0.3) < 0.001f);   // nothing hangs
    REQUIRE (seq.isPlaying());
    REQUIRE_THAT (seq.getPosition(), WithinAbs (2.4, 0.05));
}

TEST_CASE ("Stop silences notes that were still playing", "[sequencer]")
{
    Fixture f;
    auto& seq = f.app.getEngine().sequencer;
    seq.setSnapshot (PlaybackSnapshot::build (makeSongWithNote (0.0, 8.0)));
    seq.play();
    REQUIRE (f.drive.run (0.5) > 0.01f);
    seq.stop();
    f.drive.run (0.2);
    REQUIRE (f.drive.run (0.3) < 0.001f);
    REQUIRE_FALSE (seq.isRolling());
}

TEST_CASE ("Loop playback wraps and cuts notes that cross the loop end", "[sequencer]")
{
    Fixture f;
    auto& seq = f.app.getEngine().sequencer;
    seq.loopEnabled = true;
    seq.loopStart = 0.0;
    seq.loopEnd = 2.0;                      // 1 second at 120 BPM
    seq.setSnapshot (PlaybackSnapshot::build (makeSongWithNote (1.5, 6.0)));
    seq.play();

    f.drive.run (2.6);                      // a few passes
    REQUIRE (seq.getLoopPass() >= 2);
    REQUIRE (seq.getPosition() < 2.0);

    // In each pass the note starts at beat 1.5 and is cut at the wrap: silence early in the pass.
    while (seq.getPosition() > 0.1 || seq.getPosition() < 0.05)
        f.drive.run (0.005);
    f.drive.run (0.15);
    REQUIRE (f.drive.run (0.2) < 0.001f);  // beats ~0.4..0.8: nothing should sound
}

TEST_CASE ("Metronome clicks only when switched on", "[sequencer]")
{
    Fixture f;
    auto& seq = f.app.getEngine().sequencer;
    seq.setSnapshot (PlaybackSnapshot::build (Song()));
    seq.play();
    REQUIRE (f.drive.run (1.0) < 0.001f);
    seq.metronomeOn = true;
    REQUIRE (f.drive.run (1.0) > 0.05f);
}

//==============================================================================
TEST_CASE ("Recording turns what was played into a clip at the right beats", "[recording]")
{
    Fixture f;
    f.song().setCountIn (false);
    f.song().record();           // no tracks yet: a track for part 1 is created and armed
    f.drive.run (0.05);

    // At 120 BPM a beat is 0.5 s.
    f.drive.run (2.0, { { 0.45, on (60) }, { 0.95, off (60) }, { 1.20, on (64, 80) }, { 1.45, off (64) } });
    f.song().stop();
    f.drive.run (0.1);

    const auto& song = f.song().getSong();
    REQUIRE (song.tracks.size() == 1);
    REQUIRE (song.tracks[0].part == 0);
    REQUIRE (song.tracks[0].clips.size() == 1);

    const auto& clip = song.tracks[0].clips[0];
    REQUIRE (clip.start == 0.0);
    REQUIRE (clip.length >= 4.0);
    REQUIRE (clip.notes.size() == 2);
    REQUIRE_THAT (clip.notes[0].start, WithinAbs (1.0, 0.02));
    REQUIRE_THAT (clip.notes[0].length, WithinAbs (1.0, 0.02));
    REQUIRE (clip.notes[1].note == 64);
    REQUIRE (clip.notes[1].velocity == 80);
    REQUIRE_THAT (clip.notes[1].start, WithinAbs (2.5, 0.02));
    REQUIRE (f.song().isDirty());
}

TEST_CASE ("Count-in delays recording by one bar", "[recording]")
{
    Fixture f;
    auto& seq = f.app.getEngine().sequencer;
    seq.metronomeOn = false;
    f.song().setCountIn (true);
    f.song().record();
    f.drive.run (0.05);
    REQUIRE (seq.isCountingIn());

    f.drive.run (2.0);                               // one 4/4 bar at 120 BPM
    REQUIRE (seq.isPlaying());
    f.drive.run (0.5, { { 0.0, on (62) }, { 0.4, off (62) } });
    f.song().stop();
    f.drive.run (0.1);

    const auto& clip = f.song().getSong().tracks.at (0).clips.at (0);
    REQUIRE (clip.notes.size() == 1);
    REQUIRE (clip.notes[0].start < 0.2);             // recorded at the start of bar 1, not during the count-in
}

TEST_CASE ("Loop recording keeps every pass as a take, only the last one plays", "[recording]")
{
    Fixture f;
    f.song().setCountIn (false);
    f.song().setLoop (true, 0.0, 4.0);               // one bar = 2 s
    f.song().record();
    f.drive.run (0.05);

    f.drive.run (2.0, { { 0.3, on (60) }, { 0.6, off (60) } });   // pass 1
    f.drive.run (2.0, { { 0.3, on (67) }, { 0.6, off (67) } });   // pass 2
    f.drive.run (0.3);
    f.song().stop();
    f.drive.run (0.1);

    const auto& clips = f.song().getSong().tracks.at (0).clips;
    REQUIRE (clips.size() == 2);
    REQUIRE (clips[0].notes.at (0).note == 60);
    REQUIRE (clips[0].muted);                        // earlier take: kept but silent
    REQUIRE (clips[1].notes.at (0).note == 67);
    REQUIRE_FALSE (clips[1].muted);
    REQUIRE (clips[1].take == 2);

    f.song().useTake (0, 0);                         // pick take 1 after all
    REQUIRE_FALSE (f.song().getSong().tracks[0].clips[0].muted);
    REQUIRE (f.song().getSong().tracks[0].clips[1].muted);
}

TEST_CASE ("Retrospective capture keeps what you played with the transport stopped", "[recording]")
{
    Fixture f;
    f.drive.run (1.5, { { 0.10, on (60) }, { 0.40, off (60) }, { 0.60, on (62) }, { 0.90, off (62) } });
    REQUIRE (f.song().getSong().tracks.empty());

    REQUIRE (f.song().captureRecentPlaying());
    const auto& song = f.song().getSong();
    REQUIRE (song.tracks.size() == 1);
    const auto& clip = song.tracks[0].clips.at (0);
    REQUIRE (clip.notes.size() == 2);
    REQUIRE_THAT (clip.notes[0].start, WithinAbs (0.0, 0.01));   // phrase starts at the clip start
    REQUIRE_THAT (clip.notes[1].start, WithinAbs (1.0, 0.02));   // 0.5 s later = 1 beat
    REQUIRE_FALSE (f.song().captureRecentPlaying());              // already taken
}

TEST_CASE ("A recorded song plays back what was recorded", "[recording]")
{
    Fixture f;
    f.song().setCountIn (false);
    f.song().record();
    f.drive.run (1.0, { { 0.2, on (60) }, { 0.7, off (60) } });
    f.song().stop();
    f.drive.run (0.5);
    REQUIRE (f.drive.run (0.3) < 0.001f);

    f.song().locate (0.0);
    f.song().play();
    REQUIRE (f.drive.run (0.6) > 0.01f);   // the recorded note sounds again
}

TEST_CASE ("Audio export renders the song to a file", "[export]")
{
    Fixture f;
    f.song().setCountIn (false);
    f.song().record();
    f.drive.run (1.0, { { 0.1, on (60) }, { 0.6, off (60) } });
    f.song().stop();
    f.drive.run (0.1);

    const auto wav = paths::songsDir().getChildFile ("export.wav");
    const auto flac = paths::songsDir().getChildFile ("export.flac");
    juce::String error;
    REQUIRE (f.song().exportAudio (wav, error));
    REQUIRE (f.song().exportAudio (flac, error));
    REQUIRE (wav.getSize() > 10000);
    REQUIRE (flac.getSize() > 1000);

    const auto mid = paths::songsDir().getChildFile ("export.mid");
    REQUIRE (f.song().exportMidi (mid));
    juce::String importError;
    REQUIRE (f.song().importMidi (mid, importError) == 1);
    REQUIRE (f.song().getSong().tracks.size() == 2);

    // Saving and loading the song restores tracks and sounds.
    const auto songFile = paths::songsDir().getChildFile ("Test.kmsong");
    REQUIRE (f.song().saveSongAs (songFile));
    f.song().newSong();
    REQUIRE (f.song().getSong().tracks.empty());
    REQUIRE (f.song().loadSong (songFile));
    REQUIRE (f.song().getSong().tracks.size() == 2);
}
