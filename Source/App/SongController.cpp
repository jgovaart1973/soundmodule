#include "App/SongController.h"

#include "App/AppController.h"
#include "Core/AppPaths.h"
#include "Engine/BuiltInSynth.h"
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
    syncTransport();
    sequencer.setSnapshot (PlaybackSnapshot::build (song));
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
    if (takesOpen)
    {
        Song playing = song;
        for (auto& t : playing.tracks)
            if (t.armed)
                t.mute = true;
        sequencer.setSnapshot (PlaybackSnapshot::build (playing));
    }
    else
    {
        sequencer.setSnapshot (PlaybackSnapshot::build (song));
    }

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

    // Nothing armed: arm a track for every part that is switched on (creating tracks as needed),
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
                if (song.tracks[(size_t) t].part == p)
                    found = t;
            if (found < 0)
                found = addTrack (p);
            song.tracks[(size_t) found].armed = true;
        }
        changed();
    }

    sequencer.record (countIn && ! sequencer.isRolling());
    setStatus (countIn && ! sequencer.isRolling() ? "Aftellen... daarna opnemen." : "Opnemen.");
}

void SongController::setBpm (double bpm)
{
    song.bpm = juce::jlimit (20.0, 400.0, bpm);
    changed();
}

void SongController::setTimeSignature (int beatsPerBar, int beatUnit)
{
    song.beatsPerBar = juce::jlimit (1, 16, beatsPerBar);
    song.beatUnit = juce::jlimit (1, 16, beatUnit);
    changed();
}

void SongController::setLoop (bool enabled, double start, double end)
{
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
    song.tracks.erase (song.tracks.begin() + track);
    changed();
}

void SongController::setTrackPart (int track, int part)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        song.tracks[(size_t) track].part = juce::jlimit (0, kNumParts - 1, part);
        changed();
    }
}

void SongController::setTrackName (int track, const juce::String& name)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        song.tracks[(size_t) track].name = name;
        changed();
    }
}

void SongController::setTrackMute (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
        song.tracks[(size_t) track].mute = on;
        changed();
    }
}

void SongController::setTrackSolo (int track, bool on)
{
    if (juce::isPositiveAndBelow (track, (int) song.tracks.size()))
    {
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

void SongController::deleteClip (int track, int clip)
{
    if (! juce::isPositiveAndBelow (track, (int) song.tracks.size()))
        return;
    auto& clips = song.tracks[(size_t) track].clips;
    if (juce::isPositiveAndBelow (clip, (int) clips.size()))
    {
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
        if (! song.tracks[(size_t) t].armed)
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

    const int64_t t0 = history[first].sampleTime;
    const double beatsPerSample = song.bpm / 60.0 / sr;
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
            if (song.tracks[(size_t) t].part == part)
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

    const size_t before = song.tracks.size();
    const int added = song.importMidiFile (midi);

    // Give every part used by the file a sound, so it plays straight away.
    for (size_t t = before; t < song.tracks.size(); ++t)
    {
        const int part = song.tracks[t].part;
        if (! app.getPerformance().parts[(size_t) part].instrument.isEmpty())
            continue;

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
    auto snapshot = PlaybackSnapshot::build (song);
    if (snapshot->events.empty())
    {
        error = "Het nummer is leeg.";
        return false;
    }

    std::vector<std::pair<int64_t, MidiEvent>> events;
    events.reserve (snapshot->events.size());
    const double samplesPerBeat = sr * song.secondsPerBeat();
    for (auto& e : snapshot->events)
    {
        MidiEvent m = MidiEvent::make (-1, e.data[0], e.data[1], e.data[2], e.size);
        m.targetPart = e.part;
        events.push_back ({ (int64_t) std::llround (e.beat * samplesPerBeat), m });
    }

    OfflineRenderer renderer (sr, 512);
    juce::String loadError;
    renderer.loadPerformance (app.capturePerformance(), loadError);
    const auto audio = renderer.renderEvents (events, (int64_t) std::ceil ((song.getEndBeat() * song.secondsPerBeat() + 3.0) * sr));

    const bool flac = file.hasFileExtension ("flac");
    if (! OfflineRenderer::writeAudioFile (audio, sr, file, flac))
    {
        error = "Kon niet schrijven naar " + file.getFullPathName();
        return false;
    }
    setStatus (juce::String::fromUTF8 ("Geëxporteerd: ") + file.getFileName() + (loadError.isNotEmpty() ? " (let op: " + loadError.trim() + ")" : juce::String()));
    return true;
}

} // namespace km
