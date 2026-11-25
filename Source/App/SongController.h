#pragma once

#include "Engine/Sequencer.h"
#include "Model/NoteEditing.h"
#include "Model/Song.h"

#include <deque>
#include <functional>
#include <map>

namespace km
{
class AppController;

/**
    Message-thread side of the arranger: owns the Song, turns what the parts received into
    recorded clips (with loop takes), keeps a "retrospective" history of everything played,
    and handles song files, MIDI import/export and audio export.
*/
class SongController
{
public:
    SongController (AppController& app, Sequencer& sequencer);
    ~SongController();

    const Song& getSong() const noexcept { return song; }

    //== transport =============================================================
    void play();
    void stop();
    void togglePlay();
    void record();               ///< arms tracks if none are armed, then records (with count-in if enabled)
    void goToStart();
    /** Rewind (-1) / forward (+1) one bar: to the previous / next bar line. */
    void stepBars (int direction);
    void locate (double beat);
    bool isPlaying() const noexcept   { return sequencer.isRolling(); }
    bool isRecording() const noexcept { return sequencer.isRecording(); }
    double getPosition() const noexcept { return sequencer.getPosition(); }

    void setBpm (double bpm);
    void setTimeSignature (int beatsPerBar, int beatUnit);
    void setLoop (bool enabled, double start, double end);
    void setMetronome (bool on);
    bool isMetronomeOn() const noexcept { return sequencer.metronomeOn.load(); }
    void setCountIn (bool on)           { countIn = on; }
    bool isCountInOn() const noexcept   { return countIn; }

    //== tracks & clips =======================================================
    int addTrack (int part);
    void removeTrack (int track);
    void setTrackPart (int track, int part);
    void setTrackName (int track, const juce::String& name);
    void setTrackMute (int track, bool);
    void setTrackSolo (int track, bool);
    void setTrackArmed (int track, bool);
    void setTrackDrums (int track, bool);
    void deleteClip (int track, int clip);
    void moveClip (int track, int clip, double newStart);
    void useTake (int track, int clip);   ///< unmute this take, mute overlapping ones

    //== clip contents (piano roll) ============================================
    int createClip (int track, double start, double length);     ///< empty clip; @returns its index
    int duplicateClip (int track, int clip);                      ///< copy placed right after it
    void setClipLength (int track, int clip, double length);
    void setClipNotes (int track, int clip, const std::vector<SongNote>& notes, const juce::String& what);
    void setClipControls (int track, int clip, const std::vector<SongControl>& controls, const juce::String& what);
    void quantizeClip (int track, int clip, const edit::QuantizeSettings& settings, const std::set<int>& selection);
    const Clip* getClip (int track, int clip) const;

    //== audio tracks ==========================================================
    int addAudioTrack();
    void setTrackVolume (int track, float db);
    void setTrackPan (int track, float pan);
    void setTrackInput (int track, int input);
    void setTrackMonitor (int track, bool on);
    void moveAudioClip (int track, int clip, double newStart);
    void deleteAudioClip (int track, int clip);
    void useAudioTake (int track, int clip);
    /** Place an audio file on a track (track < 0: a new audio track). @returns false + error if unreadable. */
    bool importAudio (const juce::File& file, int track, double startBeat, juce::String& error);
    AudioCache& getAudioCache() noexcept { return audioCache; }
    bool isRecordingAudio() const noexcept { return ! audioRecordings.empty(); }

    //== chord track ===========================================================
    /** Put a chord at a beat (replaces a chord that starts there). */
    void setChord (double beat, double length, int root, int type);
    void removeChord (double beat);
    /** A chord from 'beat' on, also in the middle of another chord (which is then split). */
    void insertChord (double beat, int root, int type);
    /** Playback transposition of a MIDI track in semitones. */
    void setTrackTranspose (int track, int semitones);
    /** Make the transposition part of the notes (and set it back to 0). */
    void applyTrackTranspose (int track);
    /** Copy bars [fromBeat, toBeat) of tracks [firstTrack, lastTrack] to destBeat, trackOffset tracks lower.
        What was at the destination is replaced. MIDI tracks only. @returns number of clips made. */
    int copyBars (int firstTrack, int lastTrack, double fromBeat, double toBeat, double destBeat, int trackOffset = 0);
    /** Key signature for the notation view (Song::autoKey = guess). */
    void setKeySignature (int fifths);
    /** Recognise the chords played in a clip and put them on the chord track. @returns how many. */
    int detectChords (int track, int clip);
    /** Write the chord track as a new clip on a MIDI track (block chords, and/or a bass line). @returns clip index. */
    int makeAccompaniment (int track, bool chordsToo, bool bass);

    //== tempo map =============================================================
    /** Set the tempo from a beat on (beat <= 0: the start tempo). */
    void setTempoAt (double beat, double bpm);
    void removeTempoChange (double beat);
    /** Called when the audio device restarts (sample rate may have changed). */
    void audioConfigChanged() { changed (false); }

    //== undo ==================================================================
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    juce::String getUndoDescription() const { return undoStack.empty() ? juce::String() : undoStack.back().what; }
    juce::String getRedoDescription() const { return redoStack.empty() ? juce::String() : redoStack.back().what; }
    bool undo();
    bool redo();

    /** Retrospective record: turn the last thing you played (even with the transport stopped)
        into clips at the playhead. @returns false if nothing was played recently. */
    bool captureRecentPlaying();

    //== files =================================================================
    void newSong();
    bool saveSong();                                  ///< false if the song has no file yet
    bool saveSongAs (const juce::File& file);
    bool loadSong (const juce::File& file);
    juce::File getSongFile() const { return songFile; }
    bool isDirty() const noexcept { return dirty; }

    bool exportMidi (const juce::File& file);
    int importMidi (const juce::File& file, juce::String& error);
    bool exportAudio (const juce::File& file, juce::String& error); ///< .wav or .flac, 24 bit

    //== called by AppController ==============================================
    void tick();                 ///< ~30 Hz: recording, garbage collection
    void restoreAutosave();
    void finishRecording();      ///< store an open take (e.g. when the app closes mid-recording)
    void writeAutosave();

    std::function<void()> onSongChanged;
    std::function<void (const juce::String&)> onStatus;

    /** For tests: how many events the retrospective history holds. */
    size_t getHistorySize() const noexcept { return history.size(); }

private:
    struct ActiveTake
    {
        int track = -1;
        Clip clip;
        std::map<int, std::pair<double, int>> pending; // (channel*128+note) -> (beat, velocity)
        double lastBeat = 0.0;
    };

    void changed (bool markDirty = true);
    /** Call before every edit: remembers the song for undo. Quick repeats of the same edit
        (dragging the tempo) are merged into one undo step. */
    void pushUndo (const juce::String& what);
    Clip* clipAt (int track, int clip);
    void syncTransport();
    void openTakes (double startBeat, int takeNumber);
    void commitTakes (double endBeat, bool loopTake);
    static void addEventToClip (Clip& clip, std::map<int, std::pair<double, int>>& pending,
                                const uint8_t* data, int size, double beatInClip);
    static void closePending (Clip& clip, std::map<int, std::pair<double, int>>& pending, double endBeatInClip);
    void setStatus (const juce::String& message);

    AppController& app;
    Sequencer& sequencer;
    Song song;
    juce::File songFile;
    bool dirty = false;
    bool countIn = true;

    // Audio recording
    struct ActiveAudioRecording
    {
        int track = -1, slot = -1;
        juce::File file;
    };
    void startAudioRecording();
    void finishAudioRecording();
    std::vector<ActiveAudioRecording> audioRecordings;
    bool audioRecordingSeen = false;
    juce::TimeSliceThread diskThread { "KM disk writer" };
    AudioCache audioCache;

    // Recording session
    std::vector<ActiveTake> takes;
    bool takesOpen = false;
    int sessionPass = 0, takeNumber = 1;
    double recordStart = 0.0;

    struct UndoEntry
    {
        Song song;
        juce::String what;
    };
    std::vector<UndoEntry> undoStack, redoStack;
    juce::String lastUndoWhat;
    juce::uint32 lastUndoTime = 0;
    static constexpr size_t maxUndo = 100;

    // Retrospective history (everything the parts received from keyboards)
    std::deque<RecordedEvent> history;
};

} // namespace km
