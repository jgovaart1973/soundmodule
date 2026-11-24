#include "App/SongController.h"

#include "App/AppController.h"
#include "Core/AppPaths.h"
#include "Engine/BuiltInDrums.h"
#include "Engine/BuiltInSynth.h"
#include "Model/Chords.h"
#include "Model/ProgramMap.h"
#include "Render/OfflineRenderer.h"

namespace km
{
namespace
{
    double barLength (const Song& s) { return juce::jmax (0.25, s.beatsPerBarAsQuarters()); }
    double floorToBar (const Song& s, double beat)  { return std::floor (beat / barLength (s) + 1.0e-6) * barLength (s); }
    double ceilToBar (const Song& s, double beat)   { return std::ceil (beat / barLength (s) - 1.0e-6) * barLength (s); }

    constexpr double retrospectiveGapSeconds = 4.0;
    constexpr size_t maxHistory = 100000;
} // namespace

SongController::SongController (AppController& a, Sequencer& s) : app (a), sequencer (s)
{
    diskThread.startThread (juce::Thread::Priority::high);
    syncTransport();
    sequencer.setSnapshot (PlaybackSnapshot::build (song, &audioCache, sequencer.getSampleRate()));
}

SongController::~SongController()
{
    if (! audioRecordings.empty())
        finishAudioRecording();
    diskThread.stopThread (3000);
}

void SongController::setStatus (const juce::String& message)
{
    if (onStatus)
        onStatus (message);
}

void SongController::syncTransport()
{
    sequencer.bpm = song.bpm;
    sequencer.beatsPerBar = song.beatsPerBar;
    sequencer.beatUnit = song.beatUnit;
    sequencer.loopEnabled = song.loopEnabled;
    sequencer.loopStart = song.loopStart;
    sequencer.loopEnd = song.loopEnd;
}

void SongController::changed (bool markDirty)
{
    if (markDirty)
        dirty = true;
    syncTransport();

    // While recording, armed tracks stay silent so you do not hear the old take doubled.
    sequencer.setSnapshot (PlaybackSnapshot::build (song, &audioCache, sequencer.getSampleRate(),
                                                    takesOpen || ! audioRecordings.empty()));

    if (onSongChanged)
        onSongChanged();
}

//==============================================================================
void SongController::play()       { sequencer.play(); }
void SongController::togglePlay() { if (sequencer.isRolling()) stop(); else play(); }
void SongController::locate (double beat) { sequencer.locate (beat); }

void SongController::stop()
{
    sequencer.stop();
}

void SongController::goToStart()
{
    sequencer.locate (song.loopEnabled && sequencer.isRolling() ? song.loopStart : 0.0);
}

void SongController::record()
{
    if (sequencer.isRecording())
    {
        sequencer.stopRecording(); // punch out, keep playing
        return;
    }

    // Nothing armed: arm a MIDI track for every part that is switched on (creating tracks as needed),
    // so a split or layer records each sound on its own track.
    bool anyArmed = false;
    for (auto& t : song.tracks)
        anyArmed = anyArmed || t.armed;

    if (! anyArmed)
    {
        const auto& perf = app.getPerformance();
        for (int p = 0; p < kNumParts; ++p)
        {
            if (! perf.parts[(size_t) p].enabled || perf.parts[(size_t) p].instrument.isEmpty())
                continue;
            int found = -1;
            for (int t = 0; t < (int) song.tracks.size() && found < 0; ++t)
                if (! song.tracks[(size_t) t].isAudio() && song.tracks[(size_t) t].part == p)
                    found = t;
            if (found < 0)
                found = addTrack (p);
            song.tracks[(size_t) found].armed = true;
        }
        changed();
    }

    startAudioRecording();
    sequencer.record (countIn && ! sequencer.isRolling());
    setStatus (countIn && ! sequencer.isRolling() ? "Aftellen... daarna opnemen." : "Opnemen.");
}

void SongController::setBpm (double bpm)
{
    pushUndo ("Tempo");
    song.bpm = juce::jlimit (20.0, 400.0, bpm);
    changed();
}

void SongController::setTimeSignature (int beatsPerBar, int beatUnit)
{
    pushUndo ("Maatsoort");
    song.beatsPerBar = juce::jlimit (1, 16, beatsPerBar);
    song.beatUnit = juce::jlimit (1, 16, beatUnit);
    changed();
}

void SongController::setLoop (bool enabled, double start, double end)
{
    pushUndo ("Loop");
    song.loopEnabled = enabled;
    song.loopStart = juce::jmax (0.0, juce::jmin (start, end));
    song.loopEnd = juce::jmax (song.loopStart + 1.0, juce::jmax (start, end));
    changed();
}

void SongController::setMetronome (bool on)
{
    sequencer.metronomeOn = on;
}

//==============================================================================
int SongController::addTrack (int part)
{
    pushUndo ("Spoor toevoegen");
    Track t;
    t.part = juce::jlimit (0, kNumParts - 1, part);
    const auto& key = app.getPerformance().parts[(size_t) t.part].instrument;
    t.name = key.displayName.isNotEmpty() ? key.displayName : "Part " + juce::String (t.part + 1);
    song.tracks.push_back (t);
    changed();
    return (int) song.tracks.size() - 1;
}

void SongController::removeTrack (int track)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()) || takesOpen)
        return;
    pushUndo ("Spoor verwijderen");
    song.tracks.erase (song.tracks.begin() + track);
    changed();
}

void SongController::setTrackPart (int track, int part)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        pushUndo ("Part van spoor");
        song.tracks[(size_t) track].part = juce::jlimit (0, kNumParts - 1, part);
        changed();
    }
}

void SongController::setTrackName (int track, const juce::String& name)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        pushUndo ("Spoornaam");
        song.tracks[(size_t) track].name = name;
        changed();
    }
}

void SongController::setTrackMute (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        pushUndo ("Mute");
        song.tracks[(size_t) track].mute = on;
        changed();
    }
}

void SongController::setTrackSolo (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        pushUndo ("Solo");
        song.tracks[(size_t) track].solo = on;
        changed();
    }
}

void SongController::setTrackArmed (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()) && ! takesOpen)
    {
        song.tracks[(size_t) track].armed = on;
        changed (false);
    }
}

void SongController::setTrackDrums (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        song.tracks[(size_t) track].drums = on;
        changed (false);
    }
}

void SongController::deleteClip (int track, int clip)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].clips;
    if (juce::isPositiveAndBelow (clip, (int) clips.size()))
    {
        pushUndo ("Clip verwijderen");
        clips.erase (clips.begin() + clip);
        changed();
    }
}

void SongController::moveClip (int track, int clip, double newStart)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].clips;
    if (juce::isPositiveAndBelow (clip, (int) clips.size()))
    {
        pushUndo ("Clip verplaatsen");
        clips[(size_t) clip].start = juce::jmax (0.0, newStart);
        changed();
    }
}

void SongController::useTake (int track, int clip)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].clips;
    if (! juce::isPositiveAndBelow (clip, (int) clips.size()))
        return;

    pushUndo ("Take kiezen");
    const auto chosen = clips[(size_t) clip];
    for (size_t i = 0; i < clips.size(); ++i)
    {
        auto& c = clips[i];
        if ((int) i == clip)
            c.muted = false;
        else if (c.start < chosen.end() && chosen.start < c.end())
            c.muted = true;
    }
    changed();
}

//==============================================================================
void SongController::pushUndo (const juce::String& what)
{
    const auto now = juce::Time::getMillisecondCounter();
    const bool merge = what == lastUndoWhat && now - lastUndoTime < 1500 && ! undoStack.empty()
                       && (what == "Tempo" || what == "Loop");
    lastUndoWhat = what;
    lastUndoTime = now;
    redoStack.clear();
    if (merge)
        return;

    undoStack.push_back ({ song, what });
    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
}

bool SongController::undo()
{
    if (undoStack.empty() || takesOpen)
        return false;
    redoStack.push_back ({ song, undoStack.back().what });
    const auto what = undoStack.back().what;
    song = std::move (undoStack.back().song);
    undoStack.pop_back();
    lastUndoWhat.clear();
    changed();
    setStatus ("Ongedaan gemaakt: " + what);
    return true;
}

bool SongController::redo()
{
    if (redoStack.empty() || takesOpen)
        return false;
    undoStack.push_back ({ song, redoStack.back().what });
    const auto what = redoStack.back().what;
    song = std::move (redoStack.back().song);
    redoStack.pop_back();
    lastUndoWhat.clear();
    changed();
    setStatus ("Opnieuw: " + what);
    return true;
}

Clip* SongController::clipAt (int track, int clip)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return nullptr;
    auto& clips = song.tracks[(size_t) track].clips;
    return juce::isPositiveAndBelow (clip, (int) clips.size()) ? &clips[(size_t) clip] : nullptr;
}

const Clip* SongController::getClip (int track, int clip) const
{
    return const_cast<SongController*> (this)->clipAt (track, clip);
}

int SongController::createClip (int track, double start, double length)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return -1;
    pushUndo ("Nieuwe clip");
    Clip c;
    c.name = "Clip";
    c.start = juce::jmax (0.0, start);
    c.length = juce::jmax (0.25, length);
    auto& clips = song.tracks[(size_t) track].clips;
    clips.push_back (c);
    changed();
    return (int) clips.size() - 1;
}

int SongController::duplicateClip (int track, int clip)
{
    auto* c = clipAt (track, clip);
    if (c == nullptr)
        return -1;
    pushUndo ("Clip dupliceren");
    Clip copy = *c;
    copy.start = c->end();
    copy.muted = false;
    auto& clips = song.tracks[(size_t) track].clips;
    clips.push_back (copy);
    changed();
    return (int) clips.size() - 1;
}

void SongController::setClipLength (int track, int clip, double length)
{
    if (auto* c = clipAt (track, clip))
    {
        pushUndo ("Cliplengte");
        c->length = juce::jmax (0.25, length);
        changed();
    }
}

void SongController::setClipNotes (int track, int clip, const std::vector<SongNote>& notes, const juce::String& what)
{
    if (auto* c = clipAt (track, clip))
    {
        pushUndo (what);
        c->notes = notes;
        // Notes past the end make the clip longer (to the next bar), never shorter.
        double end = 0.0;
        for (auto& n : notes)
            end = juce::jmax (end, n.start + n.length);
        if (end > c->length)
            c->length = std::ceil (end / barLength (song) - 1.0e-6) * barLength (song);
        changed();
    }
}

void SongController::setClipControls (int track, int clip, const std::vector<SongControl>& controls, const juce::String& what)
{
    if (auto* c = clipAt (track, clip))
    {
        pushUndo (what);
        c->controls = controls;
        changed();
    }
}

void SongController::quantizeClip (int track, int clip, const edit::QuantizeSettings& settings, const std::set<int>& selection)
{
    if (auto* c = clipAt (track, clip))
    {
        auto notes = c->notes;
        edit::quantize (notes, selection, settings);
        setClipNotes (track, clip, notes, "Quantize");
    }
}

//==============================================================================
void SongController::addEventToClip (Clip& clip, std::map<int, std::pair<double, int>>& pending,
                                     const uint8_t* d, int size, double beat)
{
    const int status = d[0] & 0xf0;
    const int ch = (d[0] & 0x0f) + 1;

    if (status == 0x90 && size >= 3 && d[2] > 0)
    {
        const int key = ch * 128 + d[1];
        if (auto it = pending.find (key); it != pending.end()) // retrigger: close the old one
        {
            clip.notes.push_back ({ it->second.first, juce::jmax (0.01, beat - it->second.first), d[1], it->second.second, ch });
            pending.erase (it);
        }
        pending[key] = { beat, d[2] };
    }
    else if (status == 0x80 || (status == 0x90 && size >= 3))
    {
        const int key = ch * 128 + d[1];
        if (auto it = pending.find (key); it != pending.end())
        {
            clip.notes.push_back ({ it->second.first, juce::jmax (0.01, beat - it->second.first), d[1], it->second.second, ch });
            pending.erase (it);
        }
    }
    else
    {
        clip.controls.push_back ({ beat, d[0], (uint8_t) (size > 1 ? d[1] : 0), (uint8_t) (size > 2 ? d[2] : 0) });
    }
}

void SongController::closePending (Clip& clip, std::map<int, std::pair<double, int>>& pending, double endBeat)
{
    for (auto& [key, value] : pending)
        clip.notes.push_back ({ value.first, juce::jmax (0.01, endBeat - value.first), key % 128, value.second, key / 128 });
    pending.clear();
}

void SongController::openTakes (double startBeat, int number)
{
    takes.clear();
    for (int t = 0; t < (int) song.tracks.size(); ++t)
    {
        if (! song.tracks[(size_t) t].armed || song.tracks[(size_t) t].isAudio())
            continue;
        ActiveTake take;
        take.track = t;
        take.clip.start = startBeat;
        take.clip.take = number;
        take.clip.name = "Take " + juce::String (number);
        takes.push_back (std::move (take));
    }
    takesOpen = true;
    changed (false); // silences armed tracks while recording
}

void SongController::commitTakes (double endBeat, bool loopTake)
{
    bool added = false;

    for (auto& take : takes)
    {
        auto& clip = take.clip;
        closePending (clip, take.pending, juce::jmax (0.01, endBeat - clip.start));
        if (clip.notes.empty() && clip.controls.empty())
            continue;

        clip.length = loopTake ? juce::jmax (1.0, endBeat - clip.start)
                               : juce::jmax (barLength (song), ceilToBar (song, endBeat) - clip.start);

        if (! added)
            pushUndo ("Opname");
        auto& clips = song.tracks[(size_t) take.track].clips;
        // Older material in the same range becomes an (inaudible) earlier take.
        for (auto& other : clips)
            if (other.start < clip.end() && clip.start < other.end())
                other.muted = true;
        clips.push_back (clip);
        added = true;
    }

    takes.clear();
    takesOpen = false;
    if (added)
        setStatus (loopTake ? "Take " + juce::String (takeNumber) + " bewaard, volgende take..." : "Opname bewaard.");
    changed (added);
}

void SongController::tick()
{
    sequencer.collectGarbage();

    // Audio recording ends when record is switched off (stop or punch-out).
    if (! audioRecordings.empty())
    {
        if (sequencer.isRecording())
            audioRecordingSeen = true;
        else if (audioRecordingSeen)
            finishAudioRecording();
    }

    const bool recordingNow = sequencer.isRecording() && sequencer.isPlaying();
    if (recordingNow && ! takesOpen)
    {
        recordStart = floorToBar (song, sequencer.getPosition());
        if (song.loopEnabled && recordStart >= song.loopStart && recordStart < song.loopEnd)
            recordStart = song.loopStart;
        takeNumber = 1;
        sessionPass = sequencer.getLoopPass();
        openTakes (recordStart, takeNumber);
    }

    const double sr = sequencer.getSampleRate();
    while (auto ev = sequencer.recordQueue.pop())
    {
        const auto& e = *ev;

        history.push_back (e);
        while (history.size() > maxHistory)
            history.pop_front();

        if (! takesOpen || ! e.recording || e.beat < 0.0)
            continue;

        if (e.loopPass < sessionPass)
            continue; // straggler from a pass that is already stored

        if (e.loopPass > sessionPass)
        {
            // The loop wrapped: this pass becomes a take, the next pass is recorded as a new one.
            commitTakes (song.loopEnd, true);
            sessionPass = e.loopPass;
            openTakes (song.loopStart, ++takeNumber);
        }

        for (auto& take : takes)
        {
            if (song.tracks[(size_t) take.track].part != e.part)
                continue;
            const double inClip = e.beat - take.clip.start;
            if (inClip < -0.05)
                continue; // played before the take started (e.g. during the count-in)
            addEventToClip (take.clip, take.pending, e.data, e.size, juce::jmax (0.0, inClip));
            take.lastBeat = juce::jmax (take.lastBeat, e.beat);
        }
    }

    // The loop wrapped while nothing was played: still close this pass as its own take.
    if (takesOpen && recordingNow && sequencer.getLoopPass() > sessionPass)
    {
        commitTakes (song.loopEnd, true);
        sessionPass = sequencer.getLoopPass();
        openTakes (song.loopStart, ++takeNumber);
    }

    // Recording stopped (stop or punch-out): store the last take.
    if (takesOpen && ! recordingNow && ! sequencer.isCountingIn())
    {
        double end = sequencer.getPosition();
        for (auto& t : takes)
            end = juce::jmax (end, t.lastBeat);
        if (song.loopEnabled && end > song.loopEnd)
            end = song.loopEnd;
        commitTakes (end, false);
    }

    // Drop history older than 10 minutes.
    const auto now = sequencer.getSampleCounter();
    while (! history.empty() && now - history.front().sampleTime > (int64_t) (600.0 * sr))
        history.pop_front();
}

bool SongController::captureRecentPlaying()
{
    if (history.empty())
        return false;

    const double sr = sequencer.getSampleRate();
    const auto gap = (int64_t) (retrospectiveGapSeconds * sr);

    // Walk back from the newest event until a pause of 4 s: that is "what you just played".
    size_t first = history.size() - 1;
    while (first > 0 && history[first].sampleTime - history[first - 1].sampleTime < gap)
        --first;

    // Start the phrase at its first note-on.
    while (first < history.size() && ! ((history[first].data[0] & 0xf0) == 0x90 && history[first].data[2] > 0))
        ++first;
    if (first >= history.size())
        return false;

    pushUndo ("Vastleggen");
    const int64_t t0 = history[first].sampleTime;
    const double start0 = floorToBar (song, sequencer.isRolling() ? song.getEndBeat() : sequencer.getPosition());
    const double beatsPerSample = song.tempoAt (start0) / 60.0 / sr;
    const double start = floorToBar (song, sequencer.isRolling() ? song.getEndBeat() : sequencer.getPosition());

    std::map<int, ActiveTake> perPart;
    double lastBeat = 0.0;
    for (size_t i = first; i < history.size(); ++i)
    {
        const auto& e = history[i];
        auto& take = perPart[e.part];
        const double beat = (double) (e.sampleTime - t0) * beatsPerSample;
        addEventToClip (take.clip, take.pending, e.data, e.size, beat);
        lastBeat = juce::jmax (lastBeat, beat);
    }

    for (auto& [part, take] : perPart)
    {
        closePending (take.clip, take.pending, lastBeat + 0.25);
        if (take.clip.notes.empty())
            continue;

        int track = -1;
        for (int t = 0; t < (int) song.tracks.size() && track < 0; ++t)
            if (! song.tracks[(size_t) t].isAudio() && song.tracks[(size_t) t].part == part)
                track = t;
        if (track < 0)
            track = addTrack (part);

        take.clip.start = start;
        take.clip.length = juce::jmax (barLength (song), ceilToBar (song, lastBeat + 0.01));
        take.clip.name = "Vastgelegd";
        song.tracks[(size_t) track].clips.push_back (take.clip);
    }

    history.clear();
    changed();
    setStatus ("Laatst gespeelde frase vastgelegd op maat " + juce::String ((int) (start / barLength (song)) + 1) + ".");
    return true;
}

//==============================================================================
void SongController::newSong()
{
    if (takesOpen)
        return;
    sequencer.stop();
    sequencer.locate (0.0);
    song = Song();
    songFile = juce::File();
    undoStack.clear();
    redoStack.clear();
    changed (false);
    dirty = false;
}

bool SongController::saveSong()
{
    return songFile != juce::File() && saveSongAs (songFile);
}

bool SongController::saveSongAs (const juce::File& file)
{
    song.performance = app.capturePerformance();
    song.name = file.getFileNameWithoutExtension();
    if (! song.saveTo (file))
    {
        setStatus ("Opslaan mislukt: " + file.getFullPathName());
        return false;
    }
    songFile = file;
    dirty = false;
    app.getSettings().setValue ("songFile", songFile.getFullPathName());
    setStatus ("Nummer opgeslagen: " + song.name);
    if (onSongChanged)
        onSongChanged();
    return true;
}

bool SongController::loadSong (const juce::File& file)
{
    Song loaded;
    juce::String error;
    if (! loaded.loadFrom (file, error))
    {
        setStatus (error);
        return false;
    }

    sequencer.stop();
    sequencer.locate (0.0);
    song = std::move (loaded);
    songFile = file;
    undoStack.clear();
    redoStack.clear();
    app.applyPerformanceFromSong (song.performance); // the song brings its own sounds
    changed (false);
    dirty = false;
    app.getSettings().setValue ("songFile", songFile.getFullPathName());
    setStatus ("Nummer geladen: " + song.name);
    return true;
}

void SongController::writeAutosave()
{
    Song copy = song;
    copy.performance = app.capturePerformance();
    copy.saveTo (paths::songAutosaveFile());
    app.getSettings().setValue ("songDirty", dirty);
}

void SongController::finishRecording()
{
    if (! audioRecordings.empty())
        finishAudioRecording();
    if (! takesOpen)
        return;
    double end = sequencer.getPosition();
    for (auto& t : takes)
        end = juce::jmax (end, t.lastBeat);
    commitTakes (end, false);
}

void SongController::restoreAutosave()
{
    Song restored;
    juce::String error;
    if (restored.loadFrom (paths::songAutosaveFile(), error))
    {
        song = std::move (restored); // sounds come from the module's own autosave
        songFile = juce::File (app.getSettings().getValue ("songFile"));
        changed (false);
        dirty = app.getSettings().getBoolValue ("songDirty", false);
    }
}

//==============================================================================
bool SongController::exportMidi (const juce::File& file)
{
    auto midi = song.toMidiFile();
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! midi.writeTo (out))
    {
        setStatus ("MIDI-export mislukt: " + file.getFullPathName());
        return false;
    }
    setStatus ("MIDI-bestand geschreven: " + file.getFileName());
    return true;
}

int SongController::importMidi (const juce::File& file, juce::String& error)
{
    juce::FileInputStream in (file);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in))
    {
        error = "Kan MIDI-bestand niet lezen: " + file.getFileName();
        return 0;
    }

    pushUndo ("MIDI importeren");
    const size_t before = song.tracks.size();
    const int added = song.importMidiFile (midi);

    // Give every part used by the file a sound, so it plays straight away.
    for (size_t t = before; t < song.tracks.size(); ++t)
    {
        const int part = song.tracks[t].part;
        const bool drumChannel = part == 9; // MIDI channel 10 is the drum channel in General MIDI
        song.tracks[t].drums = drumChannel;
        if (! app.getPerformance().parts[(size_t) part].instrument.isEmpty())
            continue;

        if (drumChannel)
        {
            app.setPartInstrument (part, InstrumentKey::builtIn (BuiltInDrums::presetIndex, "Drumstel (GM)"));
            app.setPartEnabled (part, true);
            song.tracks[t].name = song.tracks[t].name + " (Drumstel)";
            continue;
        }

        int program = 0;
        for (auto& c : song.tracks[t].clips)
            for (auto& m : c.controls)
                if ((m.status & 0xf0) == 0xc0) { program = m.data1; break; }

        const int preset = ProgramMap::gmFamilyToBuiltInPreset (program);
        app.setPartInstrument (part, InstrumentKey::builtIn (preset, BuiltInSynth::getPresetName (preset)));
        app.setPartEnabled (part, true);
        song.tracks[t].name = song.tracks[t].name + " (" + BuiltInSynth::getPresetName (preset) + ")";
    }

    changed();
    setStatus (juce::String (added) + juce::String::fromUTF8 (" sporen geïmporteerd uit ") + file.getFileName());
    return added;
}

bool SongController::exportAudio (const juce::File& file, juce::String& error)
{
    const double sr = sequencer.getSampleRate() > 0 ? sequencer.getSampleRate() : 48000.0;
    if (song.getEndBeat() <= 0.0)
    {
        error = "Het nummer is leeg.";
        return false;
    }

    // Render the whole song (MIDI, audio tracks, tempo changes, effects) through a private engine.
    OfflineRenderer renderer (sr, 512);
    juce::String loadError;
    renderer.loadPerformance (app.capturePerformance(), loadError);
    const auto audio = renderer.renderSong (song, audioCache, 3.0);

    const bool flac = file.hasFileExtension ("flac");
    if (! OfflineRenderer::writeAudioFile (audio, sr, file, flac))
    {
        error = "Kon niet schrijven naar " + file.getFullPathName();
        return false;
    }
    setStatus (juce::String::fromUTF8 ("Geëxporteerd: ") + file.getFileName() + (loadError.isNotEmpty() ? " (let op: " + loadError.trim() + ")" : juce::String()));
    return true;
}

//==============================================================================
void SongController::setChord (double beat, double length, int root, int type)
{
    pushUndo ("Akkoord");
    auto& c = song.chords;
    c.erase (std::remove_if (c.begin(), c.end(), [beat] (const ChordEvent& e) { return std::abs (e.beat - beat) < 1.0e-6; }), c.end());
    c.push_back ({ juce::jmax (0.0, beat), juce::jmax (0.25, length), (root % 12 + 12) % 12, juce::jlimit (0, 13, type) });
    std::sort (c.begin(), c.end(), [] (const ChordEvent& a, const ChordEvent& b) { return a.beat < b.beat; });
    changed();
}

void SongController::removeChord (double beat)
{
    pushUndo ("Akkoord verwijderen");
    auto& c = song.chords;
    c.erase (std::remove_if (c.begin(), c.end(), [beat] (const ChordEvent& e) { return std::abs (e.beat - beat) < 1.0e-6; }), c.end());
    changed();
}

int SongController::detectChords (int track, int clip)
{
    const auto* c = getClip (track, clip);
    if (c == nullptr)
        return 0;

    auto found = chords::detectInClip (*c, barLength (song));
    if (found.empty())
    {
        setStatus ("Geen akkoorden gevonden in deze clip.");
        return 0;
    }

    pushUndo ("Akkoorden herkennen");
    auto& list = song.chords;
    // Replace what the chord track had in this range.
    list.erase (std::remove_if (list.begin(), list.end(), [c] (const ChordEvent& e)
                                { return e.beat >= c->start - 1.0e-6 && e.beat < c->end() - 1.0e-6; }),
                list.end());
    list.insert (list.end(), found.begin(), found.end());
    std::sort (list.begin(), list.end(), [] (const ChordEvent& a, const ChordEvent& b) { return a.beat < b.beat; });
    changed();

    juce::StringArray names;
    for (auto& f : found)
        names.add (chords::name (f));
    setStatus ("Akkoorden: " + names.joinIntoString ("  "));
    return (int) found.size();
}

int SongController::makeAccompaniment (int track, bool chordsToo, bool bass)
{
    if (song.chords.empty())
    {
        setStatus ("Het akkoordspoor is leeg: herken eerst akkoorden of voeg ze toe.");
        return -1;
    }
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()) || song.tracks[(size_t) track].isAudio())
    {
        setStatus ("Kies eerst een MIDI-spoor voor de begeleiding.");
        return -1;
    }

    pushUndo (bass && ! chordsToo ? "Baslijn maken" : "Begeleiding maken");
    const double start = floorToBar (song, song.chords.front().beat);
    const double end = song.chords.back().beat + song.chords.back().length;

    Clip clip;
    clip.name = bass && ! chordsToo ? "Baslijn" : "Begeleiding";
    clip.start = start;
    clip.length = juce::jmax (barLength (song), ceilToBar (song, end) - start);
    clip.notes = chords::accompaniment (song.chords, start, bass, chordsToo);

    auto& clips = song.tracks[(size_t) track].clips;
    clips.push_back (clip);
    changed();
    return (int) clips.size() - 1;
}

//==============================================================================
void SongController::setTempoAt (double beat, double bpm)
{
    pushUndo ("Tempo");
    song.setTempoAt (beat, bpm);
    changed();
}

void SongController::removeTempoChange (double beat)
{
    pushUndo ("Tempowissel verwijderen");
    song.removeTempoChange (beat);
    changed();
}

//==============================================================================
int SongController::addAudioTrack()
{
    pushUndo ("Audiospoor toevoegen");
    Track t;
    t.kind = Track::Kind::audio;
    int count = 1;
    for (auto& other : song.tracks)
        if (other.isAudio())
            ++count;
    t.name = "Audio " + juce::String (count);
    song.tracks.push_back (t);
    changed();
    return (int) song.tracks.size() - 1;
}

void SongController::setTrackVolume (int track, float db)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        pushUndo ("Spoorvolume");
        song.tracks[(size_t) track].volumeDb = juce::jlimit (-80.0f, 12.0f, db);
        changed();
    }
}

void SongController::setTrackPan (int track, float pan)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        pushUndo ("Spoorpan");
        song.tracks[(size_t) track].pan = juce::jlimit (-1.0f, 1.0f, pan);
        changed();
    }
}

void SongController::setTrackInput (int track, int input)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        song.tracks[(size_t) track].input = juce::jlimit (0, 2, input);
        changed();
    }
}

void SongController::setTrackMonitor (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        song.tracks[(size_t) track].monitor = on;
        changed (false);
    }
}

void SongController::moveAudioClip (int track, int clip, double newStart)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].audioClips;
    if (juce::isPositiveAndBelow (clip, (int) clips.size()))
    {
        pushUndo ("Audioclip verplaatsen");
        clips[(size_t) clip].start = juce::jmax (0.0, newStart);
        changed();
    }
}

void SongController::deleteAudioClip (int track, int clip)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].audioClips;
    if (juce::isPositiveAndBelow (clip, (int) clips.size()))
    {
        pushUndo ("Audioclip verwijderen");
        clips.erase (clips.begin() + clip);
        changed();
    }
}

void SongController::useAudioTake (int track, int clip)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].audioClips;
    if (! juce::isPositiveAndBelow (clip, (int) clips.size()))
        return;

    pushUndo ("Take kiezen");
    const double s0 = song.beatToSeconds (clips[(size_t) clip].start);
    const double e0 = s0 + clips[(size_t) clip].duration;
    for (size_t i = 0; i < clips.size(); ++i)
    {
        const double s1 = song.beatToSeconds (clips[i].start), e1 = s1 + clips[i].duration;
        if ((int) i == clip)            clips[i].muted = false;
        else if (s1 < e0 && s0 < e1)    clips[i].muted = true;
    }
    changed();
}

bool SongController::importAudio (const juce::File& file, int track, double startBeat, juce::String& error)
{
    auto data = audioCache.get (file, sequencer.getSampleRate(), &error);
    if (data == nullptr)
        return false;

    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()) || ! song.tracks[(size_t) track].isAudio())
        track = addAudioTrack();

    pushUndo ("Audio importeren");
    AudioClip clip;
    clip.name = file.getFileNameWithoutExtension();
    clip.file = file.getFullPathName();
    clip.start = juce::jmax (0.0, startBeat);
    clip.duration = data->durationSeconds();
    song.tracks[(size_t) track].audioClips.push_back (clip);
    changed();
    setStatus ("Audio geplaatst: " + file.getFileName());
    return true;
}

void SongController::startAudioRecording()
{
    if (! audioRecordings.empty())
        return;

    const double sr = sequencer.getSampleRate();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");

    for (int t = 0; t < (int) song.tracks.size(); ++t)
    {
        const auto& track = song.tracks[(size_t) t];
        if (! track.isAudio() || ! track.armed)
            continue;

        const auto file = paths::recordingsDir().getNonexistentChildFile (
            juce::File::createLegalFileName (song.name + " - " + track.name + " - " + stamp), ".wav", false);

        auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
        if (stream == nullptr)
        {
            setStatus ("Kan niet opnemen naar " + file.getFullPathName());
            continue;
        }

        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                             .withNumChannels (track.input == 0 ? 2 : 1)
                                                             .withBitsPerSample (24);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            continue;

        // A background thread empties a lock-free buffer of ~8 s to disk.
        auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), diskThread, (int) (sr * 8));
        const int slot = sequencer.attachRecorder (std::move (threaded), track.input);
        if (slot >= 0)
            audioRecordings.push_back ({ t, slot, file });
    }

    audioRecordingSeen = false;
    if (! audioRecordings.empty())
        changed (false); // armed audio tracks go silent while they are re-recorded
}

void SongController::finishAudioRecording()
{
    const double sr = sequencer.getSampleRate();
    const double latency = app.getRoundTripLatencySamples() / sr;
    bool undoPushed = false;

    for (auto& rec : audioRecordings)
    {
        auto result = sequencer.detachRecorder (rec.slot);
        result.writer.reset();   // flushes the rest of the buffer and closes the file

        const double recorded = result.samplesWritten / sr;
        if (result.startBeat < 0.0 || recorded - latency < 0.05 || ! juce::isPositiveAndBelow (rec.track, (int) song.tracks.size()))
        {
            rec.file.deleteFile();
            continue;
        }

        if (! undoPushed)
        {
            pushUndo ("Audio-opname");
            undoPushed = true;
        }

        AudioClip clip;
        clip.file = rec.file.getFullPathName();
        clip.start = result.startBeat;
        clip.offset = latency;          // the input arrives this much later than what you heard
        clip.duration = recorded - latency;

        auto& clips = song.tracks[(size_t) rec.track].audioClips;
        const double newStart = song.beatToSeconds (clip.start);
        int takeNo = 1;
        for (auto& other : clips)
        {
            const double os = song.beatToSeconds (other.start);
            if (os < newStart + clip.duration && newStart < os + other.duration)
            {
                other.muted = true;     // older take underneath stays available
                takeNo = juce::jmax (takeNo, other.take + 1);
            }
        }
        clip.take = takeNo;
        clip.name = "Opname " + juce::String (takeNo);
        audioCache.invalidate (rec.file);
        clips.push_back (clip);
    }

    audioRecordings.clear();
    audioRecordingSeen = false;
    if (undoPushed)
        setStatus ("Audio-opname bewaard.");
    changed (undoPushed);
}

} // namespace km
