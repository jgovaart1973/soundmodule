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

struct Track
{
    juce::String name { "Spoor" };
    int part = 0;          ///< 0..15: which part (sound) plays this track
    bool mute = false, solo = false, armed = false;
    std::vector<Clip> clips;
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
    Performance performance;

    double beatsPerBarAsQuarters() const noexcept { return beatsPerBar * 4.0 / beatUnit; }
    double secondsPerBeat() const noexcept        { return 60.0 / bpm; }
    double getEndBeat() const;

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
