#pragma once

#include "Model/Performance.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace km
{
/** A note in a clip. Times in beats (quarter notes) relative to the clip start. */
struct SongNote
{
    double start = 0.0;
    double length = 0.25;
    int note = 60;
    int velocity = 100;
    int channel = 1;   ///< 1..16, kept for MPE / multi-channel plug-ins
};

/** Controller, pitch bend or aftertouch. Beat relative to the clip start. */
struct SongControl
{
    double beat = 0.0;
    uint8_t status = 0xb0; ///< full status byte incl. channel
    uint8_t data1 = 0, data2 = 0;
};

struct Clip
{
    juce::String name;
    double start = 0.0;    ///< beat position in the song
    double length = 4.0;   ///< beats
    int take = 1;
    bool muted = false;    ///< older loop takes are muted, not deleted
    std::vector<SongNote> notes;
    std::vector<SongControl> controls;

    double end() const noexcept { return start + length; }
};

/** A recorded or imported audio file placed on an audio track. */
struct AudioClip
{
    juce::String name;
    juce::String file;          ///< absolute path (WAV, FLAC, AIFF, MP3...)
    double start = 0.0;         ///< beat position in the song
    double offset = 0.0;        ///< seconds skipped at the start of the file (trim / latency correction)
    double duration = 0.0;      ///< seconds played from the file
    float gainDb = 0.0f;
    bool muted = false;
    int take = 1;
};

struct Track
{
    enum class Kind { midi, audio };

    juce::String name { "Spoor" };
    Kind kind = Kind::midi;
    int part = 0;          ///< MIDI tracks: 0..15, which part (sound) plays this track
    bool mute = false, solo = false, armed = false;
    bool drums = false;     ///< MIDI tracks: edit as a drum grid (General MIDI drum map)
    std::vector<Clip> clips;

    // Audio tracks
    std::vector<AudioClip> audioClips;
    float volumeDb = 0.0f;
    float pan = 0.0f;
    int input = 0;          ///< 0 = inputs 1+2 (stereo), 1 = input 1 (mono), 2 = input 2 (mono)
    bool monitor = false;   ///< hear the input while armed

    bool isAudio() const noexcept { return kind == Kind::audio; }
};

/** A chord on the chord track. */
struct ChordEvent
{
    double beat = 0.0;
    double length = 4.0;
    int root = 0;   ///< pitch class 0 = C .. 11 = B
    int type = 0;   ///< index into chords::types()
};

/** Tempo from this beat on (step change). */
struct TempoChange
{
    double beat = 0.0;
    double bpm = 120.0;
};

/** A song: tempo, metre, loop range, tracks and the sounds (performance) it was made with. */
struct Song
{
    static constexpr int currentVersion = 1;

    juce::String name { "Nieuw nummer" };
    double bpm = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    bool loopEnabled = false;
    double loopStart = 0.0, loopEnd = 16.0;
    std::vector<Track> tracks;
    std::vector<TempoChange> tempoChanges;   ///< sorted by beat, all > 0; 'bpm' is the tempo at the start
    std::vector<ChordEvent> chords;          ///< chord track, sorted by beat
    static constexpr int autoKey = -99;
    int keySignature = autoKey;              ///< notation: fifths -7..7, or autoKey (guess from the notes)
    Performance performance;

    double beatsPerBarAsQuarters() const noexcept { return beatsPerBar * 4.0 / beatUnit; }
    double secondsPerBeat() const noexcept        { return 60.0 / bpm; }
    double getEndBeat() const;

    //== tempo map ==============================================================
    double tempoAt (double beat) const noexcept;
    double beatToSeconds (double beat) const noexcept;
    double secondsToBeat (double seconds) const noexcept;
    /** Add or replace a tempo change (beat <= 0 sets the start tempo). */
    void setTempoAt (double beat, double bpm);
    void removeTempoChange (double beat);
    double audioClipEndBeat (const AudioClip& c) const noexcept { return secondsToBeat (beatToSeconds (c.start) + c.duration); }

    juce::String toJson() const;
    bool fromJson (const juce::String& json, juce::String& error);
    bool saveTo (const juce::File& file) const;
    bool loadFrom (const juce::File& file, juce::String& error);

    /** Standard MIDI File, type 1: one MIDI track per song track. 960 ticks per quarter. */
    juce::MidiFile toMidiFile() const;

    /** Adds the MIDI file's tracks as new song tracks (tempo/metre taken from the file).
        Tracks are mapped to parts by MIDI channel (channel 1 -> part 1).
        @returns the number of tracks added. */
    int importMidiFile (const juce::MidiFile& file);
};

} // namespace km
