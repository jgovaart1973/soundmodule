#include "Model/Song.h"

#include <map>

namespace km
{
namespace
{
    juce::var prop (const juce::var& v, const char* key, juce::var fallback)
    {
        if (auto* o = v.getDynamicObject())
            if (o->hasProperty (key))
                return o->getProperty (key);
        return fallback;
    }

    double roundUpToBar (double beats, double barLength)
    {
        return juce::jmax (barLength, std::ceil (beats / barLength - 1.0e-6) * barLength);
    }

    constexpr int ticksPerQuarter = 960;
} // namespace

double Song::getEndBeat() const
{
    double end = 0.0;
    for (auto& t : tracks)
    {
        for (auto& c : t.clips)
            end = juce::jmax (end, c.end());
        for (auto& a : t.audioClips)
            end = juce::jmax (end, audioClipEndBeat (a));
    }
    return end;
}

//==============================================================================
double Song::tempoAt (double beat) const noexcept
{
    double t = bpm;
    for (auto& c : tempoChanges)
    {
        if (c.beat > beat + 1.0e-9)
            break;
        t = c.bpm;
    }
    return t;
}

double Song::beatToSeconds (double beat) const noexcept
{
    double seconds = 0.0, from = 0.0, tempo = bpm;
    for (auto& c : tempoChanges)
    {
        if (c.beat >= beat)
            break;
        seconds += (c.beat - from) * 60.0 / tempo;
        from = c.beat;
        tempo = c.bpm;
    }
    return seconds + (beat - from) * 60.0 / tempo;
}

double Song::secondsToBeat (double seconds) const noexcept
{
    double at = 0.0, from = 0.0, tempo = bpm;
    for (auto& c : tempoChanges)
    {
        const double segment = (c.beat - from) * 60.0 / tempo;
        if (at + segment >= seconds)
            break;
        at += segment;
        from = c.beat;
        tempo = c.bpm;
    }
    return from + (seconds - at) * tempo / 60.0;
}

void Song::setTempoAt (double beat, double newBpm)
{
    newBpm = juce::jlimit (20.0, 400.0, newBpm);
    if (beat <= 1.0e-9)
    {
        bpm = newBpm;
        return;
    }
    for (auto& c : tempoChanges)
        if (std::abs (c.beat - beat) < 1.0e-6)
        {
            c.bpm = newBpm;
            return;
        }
    tempoChanges.push_back ({ beat, newBpm });
    std::sort (tempoChanges.begin(), tempoChanges.end(), [] (const TempoChange& a, const TempoChange& b) { return a.beat < b.beat; });
}

void Song::removeTempoChange (double beat)
{
    tempoChanges.erase (std::remove_if (tempoChanges.begin(), tempoChanges.end(),
                                        [beat] (const TempoChange& c) { return std::abs (c.beat - beat) < 1.0e-6; }),
                        tempoChanges.end());
}

//==============================================================================
juce::String Song::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "KlankModule.Song");
    root->setProperty ("version", currentVersion);
    root->setProperty ("name", name);
    root->setProperty ("bpm", bpm);
    root->setProperty ("beatsPerBar", beatsPerBar);
    root->setProperty ("beatUnit", beatUnit);
    root->setProperty ("loopEnabled", loopEnabled);
    root->setProperty ("loopStart", loopStart);
    root->setProperty ("loopEnd", loopEnd);

    juce::Array<juce::var> tempoList;
    for (auto& tc : tempoChanges)
        tempoList.add (juce::Array<juce::var> { tc.beat, tc.bpm });
    root->setProperty ("tempoChanges", tempoList);

    juce::Array<juce::var> chordList;
    for (auto& c : chords)
        chordList.add (juce::Array<juce::var> { c.beat, c.length, c.root, c.type });
    root->setProperty ("chords", chordList);
    root->setProperty ("keySignature", keySignature);

    juce::var perf;
    juce::JSON::parse (performance.toJson(), perf);
    root->setProperty ("performance", perf);

    juce::Array<juce::var> trackList;
    for (auto& t : tracks)
    {
        auto* to = new juce::DynamicObject();
        to->setProperty ("name", t.name);
        to->setProperty ("kind", t.isAudio() ? "audio" : "midi");
        to->setProperty ("part", t.part + 1);
        to->setProperty ("volumeDb", t.volumeDb);   // every track has a fader and pan
        to->setProperty ("pan", t.pan);
        if (t.isAudio())
        {
            to->setProperty ("input", t.input);
            to->setProperty ("monitor", t.monitor);
            juce::Array<juce::var> audioList;
            for (auto& a : t.audioClips)
            {
                auto* ao = new juce::DynamicObject();
                ao->setProperty ("name", a.name);
                ao->setProperty ("file", a.file);
                ao->setProperty ("start", a.start);
                ao->setProperty ("offset", a.offset);
                ao->setProperty ("duration", a.duration);
                ao->setProperty ("gainDb", a.gainDb);
                ao->setProperty ("muted", a.muted);
                ao->setProperty ("take", a.take);
                audioList.add (juce::var (ao));
            }
            to->setProperty ("audioClips", audioList);
        }
        to->setProperty ("mute", t.mute);
        to->setProperty ("solo", t.solo);
        if (t.drums)
            to->setProperty ("drums", true);
        if (t.transpose != 0)
            to->setProperty ("transpose", t.transpose);

        juce::Array<juce::var> clipList;
        for (auto& c : t.clips)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("name", c.name);
            co->setProperty ("start", c.start);
            co->setProperty ("length", c.length);
            co->setProperty ("take", c.take);
            co->setProperty ("muted", c.muted);

            // Compact arrays: [start, length, note, velocity, channel]
            juce::Array<juce::var> notes;
            for (auto& n : c.notes)
                notes.add (juce::Array<juce::var> { n.start, n.length, n.note, n.velocity, n.channel });
            co->setProperty ("notes", notes);

            // [beat, status, data1, data2]
            juce::Array<juce::var> controls;
            for (auto& m : c.controls)
                controls.add (juce::Array<juce::var> { m.beat, (int) m.status, (int) m.data1, (int) m.data2 });
            co->setProperty ("controls", controls);

            clipList.add (juce::var (co));
        }
        to->setProperty ("clips", clipList);
        trackList.add (juce::var (to));
    }
    root->setProperty ("tracks", trackList);

    return juce::JSON::toString (juce::var (root), false);
}

bool Song::fromJson (const juce::String& json, juce::String& error)
{
    juce::var root;
    if (juce::JSON::parse (json, root).failed())
    {
        error = "Ongeldige JSON in nummer.";
        return false;
    }
    if (prop (root, "format", "").toString() != "KlankModule.Song")
    {
        error = "Dit is geen KlankModule-nummer.";
        return false;
    }

    Song s;
    s.name = prop (root, "name", "Naamloos").toString();
    s.bpm = juce::jlimit (20.0, 400.0, (double) prop (root, "bpm", 120.0));
    s.beatsPerBar = juce::jlimit (1, 16, (int) prop (root, "beatsPerBar", 4));
    s.beatUnit = juce::jlimit (1, 16, (int) prop (root, "beatUnit", 4));
    s.loopEnabled = (bool) prop (root, "loopEnabled", false);
    s.loopStart = juce::jmax (0.0, (double) prop (root, "loopStart", 0.0));
    s.loopEnd = juce::jmax (s.loopStart + 0.25, (double) prop (root, "loopEnd", 16.0));

    if (auto* tempos = prop (root, "tempoChanges", {}).getArray())
        for (auto& tc : *tempos)
            if (tc.size() >= 2 && (double) tc[0] > 0.0)
                s.setTempoAt ((double) tc[0], (double) tc[1]);

    if (const int k = (int) prop (root, "keySignature", autoKey); k >= -7 && k <= 7)
        s.keySignature = k;

    if (auto* chordArray = prop (root, "chords", {}).getArray())
        for (auto& c : *chordArray)
            if (c.size() >= 4)
                s.chords.push_back ({ juce::jmax (0.0, (double) c[0]), juce::jmax (0.25, (double) c[1]),
                                      juce::jlimit (0, 11, (int) c[2]), juce::jlimit (0, 13, (int) c[3]) });

    if (auto perf = prop (root, "performance", {}); perf.isObject())
    {
        juce::String perr;
        if (! s.performance.fromJson (juce::JSON::toString (perf), perr))
            s.performance = Performance::makeDefault();
    }

    if (auto* trackArray = prop (root, "tracks", {}).getArray())
    {
        for (auto& tv : *trackArray)
        {
            Track t;
            t.name = prop (tv, "name", "Spoor").toString();
            t.part = juce::jlimit (0, kNumParts - 1, (int) prop (tv, "part", 1) - 1);
            t.mute = (bool) prop (tv, "mute", false);
            t.solo = (bool) prop (tv, "solo", false);
            t.drums = (bool) prop (tv, "drums", false);
            t.transpose = juce::jlimit (-48, 48, (int) prop (tv, "transpose", 0));
            t.kind = prop (tv, "kind", "midi").toString() == "audio" ? Track::Kind::audio : Track::Kind::midi;
            t.volumeDb = juce::jlimit (-80.0f, 12.0f, (float) (double) prop (tv, "volumeDb", 0.0));
            t.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) prop (tv, "pan", 0.0));
            t.input = juce::jlimit (0, 2, (int) prop (tv, "input", 0));
            t.monitor = (bool) prop (tv, "monitor", false);

            if (auto* audioArray = prop (tv, "audioClips", {}).getArray())
                for (auto& av : *audioArray)
                {
                    AudioClip a;
                    a.name = prop (av, "name", "").toString();
                    a.file = prop (av, "file", "").toString();
                    a.start = juce::jmax (0.0, (double) prop (av, "start", 0.0));
                    a.offset = juce::jmax (0.0, (double) prop (av, "offset", 0.0));
                    a.duration = juce::jmax (0.0, (double) prop (av, "duration", 0.0));
                    a.gainDb = juce::jlimit (-60.0f, 24.0f, (float) (double) prop (av, "gainDb", 0.0));
                    a.muted = (bool) prop (av, "muted", false);
                    a.take = (int) prop (av, "take", 1);
                    if (a.file.isNotEmpty())
                        t.audioClips.push_back (a);
                }

            if (auto* clips = prop (tv, "clips", {}).getArray())
            {
                for (auto& cv : *clips)
                {
                    Clip c;
                    c.name = prop (cv, "name", "").toString();
                    c.start = juce::jmax (0.0, (double) prop (cv, "start", 0.0));
                    c.length = juce::jmax (0.25, (double) prop (cv, "length", 4.0));
                    c.take = (int) prop (cv, "take", 1);
                    c.muted = (bool) prop (cv, "muted", false);

                    if (auto* notes = prop (cv, "notes", {}).getArray())
                        for (auto& n : *notes)
                            if (n.size() >= 5)
                                c.notes.push_back ({ (double) n[0], juce::jmax (0.001, (double) n[1]),
                                                     juce::jlimit (0, 127, (int) n[2]), juce::jlimit (1, 127, (int) n[3]),
                                                     juce::jlimit (1, 16, (int) n[4]) });

                    if (auto* controls = prop (cv, "controls", {}).getArray())
                        for (auto& m : *controls)
                            if (m.size() >= 4)
                                c.controls.push_back ({ (double) m[0], (uint8_t) juce::jlimit (0x80, 0xef, (int) m[1]),
                                                        (uint8_t) ((int) m[2] & 0x7f), (uint8_t) ((int) m[3] & 0x7f) });
                    t.clips.push_back (std::move (c));
                }
            }
            s.tracks.push_back (std::move (t));
        }
    }

    *this = std::move (s);
    return true;
}

bool Song::saveTo (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);
    return temp.getFile().replaceWithText (toJson()) && temp.overwriteTargetFileWithTemporary();
}

bool Song::loadFrom (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Bestand niet gevonden: " + file.getFullPathName();
        return false;
    }
    return fromJson (file.loadFileAsString(), error);
}

//==============================================================================
juce::MidiFile Song::toMidiFile() const
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);
    auto tick = [] (double beat) { return std::round (beat * ticksPerQuarter); };

    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, name));
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)));
    for (auto& tc : tempoChanges)
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / tc.bpm)), tick (tc.beat));
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (beatsPerBar, beatUnit));
    file.addTrack (conductor);

    for (auto& t : tracks)
    {
        if (t.isAudio())
            continue; // audio cannot go into a MIDI file
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, t.name));
        const int channel = t.part + 1; // channel = part number, so importing maps back to the same part

        // Track fader / pan as GM volume (CC7, 40*log10 curve) and pan (CC10) at the start
        if (std::abs (t.volumeDb) > 0.05f)
            seq.addEvent (juce::MidiMessage::controllerEvent (channel, 7, juce::jlimit (0, 127, juce::roundToInt (100.0 * std::pow (10.0, t.volumeDb / 40.0)))), 0.0);
        if (std::abs (t.pan) > 0.005f)
            seq.addEvent (juce::MidiMessage::controllerEvent (channel, 10, juce::jlimit (0, 127, juce::roundToInt (64.0 + t.pan * 63.0))), 0.0);

        for (auto& c : t.clips)
        {
            if (c.muted)
                continue;
            for (auto& n : c.notes)
            {
                if (n.start >= c.length)
                    continue;
                const double on = c.start + n.start;
                const double off = c.start + juce::jmin (n.start + n.length, c.length);
                seq.addEvent (juce::MidiMessage::noteOn (channel, t.playedNote (n.note), (juce::uint8) n.velocity), tick (on));
                seq.addEvent (juce::MidiMessage::noteOff (channel, t.playedNote (n.note)), tick (off));
            }
            for (auto& m : c.controls)
                if (m.beat < c.length)
                    seq.addEvent (juce::MidiMessage ((m.status & 0xf0) | (channel - 1), m.data1, m.data2), tick (c.start + m.beat));
        }
        seq.sort();
        seq.updateMatchedPairs();
        file.addTrack (seq);
    }
    return file;
}

int Song::importMidiFile (const juce::MidiFile& source)
{
    juce::MidiFile file (source);
    const short format = file.getTimeFormat();
    const bool ticks = format > 0;

    // Tempo and time signature from the first meta events.
    juce::MidiMessageSequence tempoEvents, sigEvents;
    file.findAllTempoEvents (tempoEvents);
    file.findAllTimeSigEvents (sigEvents);
    if (tempoEvents.getNumEvents() > 0)
    {
        bpm = juce::jlimit (20.0, 400.0, 60.0 / tempoEvents.getEventPointer (0)->message.getTempoSecondsPerQuarterNote());
        tempoChanges.clear();
        if (format > 0) // tempo changes need musical (tick) time
            for (int i = 0; i < tempoEvents.getNumEvents(); ++i)
            {
                const auto& m = tempoEvents.getEventPointer (i)->message;
                const double beat = m.getTimeStamp() / (double) format;
                const double value = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (beat <= 1.0e-9) bpm = juce::jlimit (20.0, 400.0, value);
                else                setTempoAt (beat, value);
            }
    }
    if (sigEvents.getNumEvents() > 0)
    {
        int num = 4, den = 4;
        sigEvents.getEventPointer (0)->message.getTimeSignatureInfo (num, den);
        beatsPerBar = juce::jlimit (1, 16, num);
        beatUnit = juce::jlimit (1, 16, den);
    }

    if (! ticks)
        file.convertTimestampTicksToSeconds();
    auto toBeat = [&] (double t) { return ticks ? t / (double) format : t / secondsPerBeat(); };

    const double startOffset = 0.0; // imported tracks start at bar 1
    int added = 0;

    for (int ti = 0; ti < file.getNumTracks(); ++ti)
    {
        juce::MidiMessageSequence seq (*file.getTrack (ti));
        seq.updateMatchedPairs();

        juce::String trackName;
        std::map<int, Clip> perChannel; // type-0 files hold all channels in one track

        for (int e = 0; e < seq.getNumEvents(); ++e)
        {
            const auto* holder = seq.getEventPointer (e);
            const auto& msg = holder->message;

            if (msg.isTrackNameEvent())
                trackName = msg.getTextFromTextMetaEvent();

            const int ch = msg.getChannel();
            if (ch < 1)
                continue;

            if (msg.isNoteOn())
            {
                const double on = toBeat (msg.getTimeStamp());
                double off = on + 0.25;
                if (holder->noteOffObject != nullptr)
                    off = toBeat (holder->noteOffObject->message.getTimeStamp());
                perChannel[ch].notes.push_back ({ on + startOffset, juce::jmax (0.01, off - on), msg.getNoteNumber(),
                                                  juce::jlimit (1, 127, (int) msg.getVelocity()), ch });
            }
            else if (msg.isController() || msg.isPitchWheel() || msg.isChannelPressure() || msg.isProgramChange())
            {
                const auto* raw = msg.getRawData();
                perChannel[ch].controls.push_back ({ toBeat (msg.getTimeStamp()) + startOffset, raw[0],
                                                     (uint8_t) (msg.getRawDataSize() > 1 ? raw[1] : 0),
                                                     (uint8_t) (msg.getRawDataSize() > 2 ? raw[2] : 0) });
            }
        }

        for (auto& [ch, clip] : perChannel)
        {
            if (clip.notes.empty())
                continue;

            double end = 0.0;
            for (auto& n : clip.notes)
                end = juce::jmax (end, n.start + n.length);

            clip.start = 0.0;
            clip.length = roundUpToBar (end, beatsPerBarAsQuarters());
            clip.name = trackName.isNotEmpty() ? trackName : "Kanaal " + juce::String (ch);

            Track t;
            t.name = clip.name;
            t.part = juce::jlimit (0, kNumParts - 1, ch - 1);
            t.clips.push_back (std::move (clip));
            tracks.push_back (std::move (t));
            ++added;
        }
    }
    return added;
}

} // namespace km
