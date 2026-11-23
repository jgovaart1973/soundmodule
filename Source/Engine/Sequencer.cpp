#include "Engine/Sequencer.h"

#include <algorithm>
#include <thread>

namespace km
{
//==============================================================================
double PlaybackSnapshot::tempoAt (double beat) const noexcept
{
    double bpm = tempo.empty() ? 120.0 : tempo.front().bpm;
    for (auto& seg : tempo)
    {
        if (seg.beat > beat + 1.0e-9)
            break;
        bpm = seg.bpm;
    }
    return bpm;
}

double PlaybackSnapshot::beatToSeconds (double beat) const noexcept
{
    const TempoSegment* seg = nullptr;
    for (auto& s : tempo)
    {
        if (s.beat > beat + 1.0e-9)
            break;
        seg = &s;
    }
    if (seg == nullptr)
        return beat * 60.0 / (tempo.empty() ? 120.0 : tempo.front().bpm);
    return seg->seconds + (beat - seg->beat) * 60.0 / seg->bpm;
}

std::unique_ptr<PlaybackSnapshot> PlaybackSnapshot::build (const Song& song, AudioCache* cache, double sr, bool silenceArmed)
{
    auto snap = std::make_unique<PlaybackSnapshot>();
    snap->sampleRate = sr;

    snap->tempo.push_back ({ 0.0, song.bpm, 0.0 });
    for (auto& tc : song.tempoChanges)
        snap->tempo.push_back ({ tc.beat, tc.bpm, song.beatToSeconds (tc.beat) });

    bool anySolo = false;
    for (auto& t : song.tracks)
        anySolo = anySolo || t.solo;

    snap->tracks.resize (song.tracks.size());
    for (size_t ti = 0; ti < song.tracks.size(); ++ti)
    {
        const auto& t = song.tracks[ti];
        auto& mix = snap->tracks[ti];
        mix.isAudio = t.isAudio();
        mix.audible = ! t.mute && ! (anySolo && ! t.solo);
        mix.armed = t.armed;
        mix.monitor = t.monitor;
        mix.input = t.input;
        const float vol = juce::Decibels::decibelsToGain (t.volumeDb, -80.0f);
        const float angle = (juce::jlimit (-1.0f, 1.0f, t.pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        mix.gainLeft = vol * std::cos (angle) * 1.41421356f;
        mix.gainRight = vol * std::sin (angle) * 1.41421356f;

        if (! t.isAudio() || cache == nullptr || ! mix.audible || (silenceArmed && t.armed))
            continue;

        for (auto& a : t.audioClips)
        {
            if (a.muted || a.duration <= 0.0)
                continue;
            auto data = cache->get (juce::File (a.file), sr);
            if (data == nullptr)
                continue;
            AudioRegion r;
            r.startSeconds = song.beatToSeconds (a.start);
            r.duration = a.duration;
            r.fileOffset = (int64_t) std::llround (a.offset * sr);
            r.data = std::move (data);
            r.gain = juce::Decibels::decibelsToGain (a.gainDb, -80.0f);
            r.track = (int) ti;
            snap->audio.push_back (std::move (r));
        }
    }

    for (auto& t : song.tracks)
    {
        if (t.isAudio() || t.mute || (anySolo && ! t.solo) || (silenceArmed && t.armed))
            continue;
        const auto part = (int8_t) juce::jlimit (0, kNumParts - 1, t.part);

        for (auto& c : t.clips)
        {
            if (c.muted)
                continue;

            for (auto& n : c.notes)
            {
                if (n.start >= c.length || n.start < 0.0)
                    continue;
                const auto ch = (uint8_t) (juce::jlimit (1, 16, n.channel) - 1);
                SeqEvent on, off;
                on.beat = c.start + n.start;
                on.part = part;
                on.data[0] = (uint8_t) (0x90 | ch);
                on.data[1] = (uint8_t) n.note;
                on.data[2] = (uint8_t) juce::jlimit (1, 127, n.velocity);
                off = on;
                off.beat = c.start + juce::jmin (n.start + n.length, c.length);
                off.data[0] = (uint8_t) (0x80 | ch);
                off.data[2] = 0;
                snap->events.push_back (on);
                snap->events.push_back (off);
            }

            for (auto& m : c.controls)
            {
                if (m.beat >= c.length || m.beat < 0.0)
                    continue;
                SeqEvent e;
                e.beat = c.start + m.beat;
                e.part = part;
                const int kind = m.status & 0xf0;
                e.size = (kind == 0xc0 || kind == 0xd0) ? 2 : 3;
                e.data[0] = m.status;
                e.data[1] = m.data1;
                e.data[2] = m.data2;
                snap->events.push_back (e);
            }
        }
    }

    // At equal times: note-offs first, then controllers, then note-ons.
    auto rank = [] (const SeqEvent& e)
    {
        const int s = e.data[0] & 0xf0;
        return s == 0x80 ? 0 : (s == 0x90 ? 2 : 1);
    };
    std::stable_sort (snap->events.begin(), snap->events.end(), [&] (const SeqEvent& a, const SeqEvent& b)
    {
        if (! juce::exactlyEqual (a.beat, b.beat))
            return a.beat < b.beat;
        return rank (a) < rank (b);
    });
    return snap;
}

//==============================================================================
Sequencer::Sequencer() = default;

// Every snapshot ever handed over lives in 'owned' until retired, so nothing else to free.
Sequencer::~Sequencer() = default;

void Sequencer::play() noexcept           { commands.fetch_or (cmdPlay); }
void Sequencer::stop() noexcept           { commands.fetch_or (cmdStop); }
void Sequencer::stopRecording() noexcept  { commands.fetch_or (cmdStopRecording); }
void Sequencer::locate (double beat) noexcept { locateRequest.store (juce::jmax (0.0, beat)); }

void Sequencer::record (bool withCountIn) noexcept
{
    commands.fetch_or (cmdRecord | (withCountIn ? cmdCountIn : 0));
}

void Sequencer::setSnapshot (std::unique_ptr<PlaybackSnapshot> snapshot)
{
    auto* raw = snapshot.get();
    owned.push_back (std::move (snapshot));
    if (auto* old = pendingSnapshot.exchange (raw))
        retired.push (old); // superseded before the audio thread took it
}

void Sequencer::collectGarbage()
{
    while (auto r = retired.pop())
    {
        auto* dead = *r;
        owned.erase (std::remove_if (owned.begin(), owned.end(),
                                     [dead] (const std::unique_ptr<PlaybackSnapshot>& p) { return p.get() == dead; }),
                     owned.end());
    }
}

void Sequencer::prepare (double sr, int block)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    maxBlock = juce::jmax (16, block);
    silence.setSize (1, maxBlock);
    silence.clear();
    clickLength = (int) (0.03 * sampleRate);
}

void Sequencer::resetSounding() noexcept
{
    std::memset (sounding, 0, sizeof (sounding));
}

//==============================================================================
int Sequencer::emitRange (double from, double to, double mapBeat, int posOffset, double bps,
                          int n, MidiEvent* out, int* positions, int count, int maxEvents) noexcept
{
    if (current == nullptr || to <= from)
        return count;

    auto& ev = current->events;
    auto it = std::lower_bound (ev.begin(), ev.end(), from, [] (const SeqEvent& e, double b) { return e.beat < b; });

    for (; it != ev.end() && it->beat < to && count < maxEvents; ++it)
    {
        MidiEvent m;
        m.source = -1;
        m.targetPart = it->part;
        m.size = it->size;
        std::memcpy (m.data, it->data, 3);

        const int status = m.data[0] & 0xf0;
        const int ch = m.data[0] & 0x0f;
        auto& s = sounding[(size_t) it->part][ch][m.data[1] & 0x7f];
        if (status == 0x90 && m.data[2] > 0)
            s = (uint8_t) juce::jmin (255, s + 1);
        else if (status == 0x80 || status == 0x90)
            s = (uint8_t) (s > 0 ? s - 1 : 0);

        out[count] = m;
        positions[count] = juce::jlimit (0, n - 1, posOffset + (int) ((it->beat - mapBeat) / bps));
        ++count;
    }
    return count;
}

int Sequencer::emitAllNotesOff (int pos, MidiEvent* out, int* positions, int count, int maxEvents) noexcept
{
    for (int p = 0; p < kNumParts; ++p)
    {
        bool partUsed = false;
        for (int ch = 0; ch < 16; ++ch)
            for (int note = 0; note < 128; ++note)
            {
                if (sounding[p][ch][note] == 0)
                    continue;
                partUsed = true;
                sounding[p][ch][note] = 0;
                if (count >= maxEvents)
                    continue;
                auto m = MidiEvent::make (-1, (uint8_t) (0x80 | ch), (uint8_t) note, 0);
                m.targetPart = (int8_t) p;
                out[count] = m;
                positions[count++] = pos;
            }

        // Release a sustain pedal the song may have left down.
        if (partUsed && count < maxEvents)
        {
            auto m = MidiEvent::make (-1, 0xb0, 64, 0);
            m.targetPart = (int8_t) p;
            out[count] = m;
            positions[count++] = pos;
        }
    }
    return count;
}

int Sequencer::generate (int n, MidiEvent* out, int* positions, int maxEvents) noexcept
{
    int count = 0;
    wrapAt = -1;
    blockRolling = blockCountIn = false;
    numClickStarts = 0;

    double bps = bpm.load() / 60.0 / sampleRate;
    const double barLength = juce::jmax (0.25, beatsPerBar.load() * 4.0 / juce::jmax (1, beatUnit.load()));
    auto curMode = mode.load();

    if (auto* s = pendingSnapshot.exchange (nullptr))
    {
        if (current != nullptr)
            retired.push (current);
        current = s;
        if (curMode == Mode::playing)
            count = emitAllNotesOff (0, out, positions, count, maxEvents); // notes may have been edited away
    }

    const int cmds = commands.exchange (0);
    const double loc = locateRequest.exchange (-1.0);

    if ((cmds & cmdStop) != 0)
    {
        if (curMode != Mode::stopped)
            count = emitAllNotesOff (0, out, positions, count, maxEvents);
        curMode = Mode::stopped;
        recording = false;
    }

    if (loc >= 0.0)
    {
        if (curMode == Mode::playing)
            count = emitAllNotesOff (0, out, positions, count, maxEvents);
        position = loc;
    }

    if ((cmds & cmdRecord) != 0)
    {
        recording = true;
        if (curMode == Mode::stopped)
        {
            if ((cmds & cmdCountIn) != 0 && countInBars.load() > 0)
            {
                curMode = Mode::countIn;
                countInLeft = countInBars.load() * barLength;
                countInBeat = 0.0;
            }
            else
            {
                curMode = Mode::playing;
            }
        }
    }

    if ((cmds & cmdStopRecording) != 0)
        recording = false;

    if ((cmds & cmdPlay) != 0 && curMode == Mode::stopped)
        curMode = Mode::playing;

    // Follow the song's tempo map (tempo taken at the start of the block).
    bps = (current != nullptr ? current->tempoAt (position.load()) : bpm.load()) / 60.0 / sampleRate;

    auto addClicks = [&] (double from, double to, double mapBeat, int posOffset)
    {
        for (double k = std::ceil (from - 1.0e-9); k < to && numClickStarts < maxClickStarts; k += 1.0)
        {
            const bool accent = std::fmod (k, barLength) < 1.0e-6;
            clickStarts[numClickStarts++] = { juce::jlimit (0, n - 1, posOffset + (int) ((k - mapBeat) / bps)), accent };
        }
    };

    if (curMode == Mode::countIn)
    {
        blockCountIn = true;
        const double c0 = countInBeat, c1 = c0 + n * bps;
        addClicks (c0, c1, c0, 0);
        countInBeat = c1;
        const double left = countInLeft.load() - n * bps;
        countInLeft = juce::jmax (0.0, left);
        if (left <= 0.0)
            curMode = Mode::playing; // recording starts on the next block, exactly at the start position
    }
    else if (curMode == Mode::playing)
    {
        blockRolling = true;
        blockBeatsPerSample = bps;
        const double b0 = position.load();
        const double b1 = b0 + n * bps;
        blockStartBeat = b0;
        const bool click = metronomeOn.load();

        const double ls = loopStart.load(), le = loopEnd.load();
        const bool wraps = loopEnabled.load() && le - ls >= 0.25 && b0 < le && b1 >= le;

        if (! wraps)
        {
            count = emitRange (b0, b1, b0, 0, bps, n, out, positions, count, maxEvents);
            if (click) addClicks (b0, b1, b0, 0);
            position = b1;
        }
        else
        {
            wrapAt = juce::jlimit (0, n - 1, (int) std::ceil ((le - b0) / bps));
            count = emitRange (b0, le, b0, 0, bps, n, out, positions, count, maxEvents);
            if (click) addClicks (b0, le, b0, 0);
            count = emitAllNotesOff (wrapAt, out, positions, count, maxEvents);
            loopPass.fetch_add (1);

            wrapBeat = ls;
            const double rest = (n - wrapAt) * bps;
            count = emitRange (ls, ls + rest, ls, wrapAt, bps, n, out, positions, count, maxEvents);
            if (click) addClicks (ls, ls + rest, ls, wrapAt);
            position = ls + rest;
        }
    }

    mode = curMode;
    return count;
}

void Sequencer::capture (int part, const uint8_t* data, int size, int pos) noexcept
{
    RecordedEvent e;
    e.part = (int8_t) part;
    e.size = (uint8_t) size;
    std::memcpy (e.data, data, (size_t) juce::jlimit (0, 3, size));
    e.sampleTime = sampleCounter.load (std::memory_order_relaxed) + pos;

    if (blockRolling)
    {
        if (wrapAt >= 0 && pos >= wrapAt)
        {
            e.beat = wrapBeat + (pos - wrapAt) * blockBeatsPerSample;
            e.loopPass = loopPass.load();
        }
        else
        {
            e.beat = blockStartBeat + pos * blockBeatsPerSample;
            e.loopPass = loopPass.load() - (wrapAt >= 0 ? 1 : 0);
        }
        e.recording = recording.load();
    }

    recordQueue.push (e); // full queue: the event is only lost for recording, never for playing
}

void Sequencer::renderClicks (juce::AudioBuffer<float>& bus, int n) noexcept
{
    if (numClickStarts == 0 && clickRemaining <= 0)
        return;

    const float level = metronomeLevel.load() * 0.5f;
    auto* l = bus.getWritePointer (0);
    auto* r = bus.getWritePointer (1);
    int next = 0;

    for (int i = 0; i < n; ++i)
    {
        while (next < numClickStarts && clickStarts[next].pos == i)
        {
            clickRemaining = clickLength;
            clickFreq = clickStarts[next].accent ? 1760.0f : 1320.0f;
            clickPhase = 0.0;
            ++next;
        }
        if (clickRemaining <= 0)
            continue;

        const float env = (float) clickRemaining / (float) clickLength;
        const float s = level * env * env * (float) std::sin (clickPhase);
        clickPhase += juce::MathConstants<double>::twoPi * clickFreq / sampleRate;
        --clickRemaining;
        l[i] += s;
        r[i] += s;
    }
}

//==============================================================================
int Sequencer::attachRecorder (std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer, int input)
{
    for (int i = 0; i < maxAudioRecorders; ++i)
    {
        auto& slot = recorders[i];
        if (slot.writer.load() != nullptr)
            continue;
        slot.input = input;
        slot.startBeat = -1.0;
        slot.written = 0;
        slot.stopped = false;
        slot.writer = writer.release();
        return i;
    }
    return -1;
}

AudioRecordResult Sequencer::detachRecorder (int index)
{
    AudioRecordResult result;
    if (! juce::isPositiveAndBelow (index, maxAudioRecorders))
        return result;

    auto& slot = recorders[index];
    auto* w = slot.writer.exchange (nullptr);
    while (slot.busy.load())        // the audio thread is inside write(): a few microseconds at most
        std::this_thread::yield();

    result.writer.reset (w);
    result.startBeat = slot.startBeat.load();
    result.samplesWritten = slot.written.load();
    return result;
}

void Sequencer::processAudio (juce::AudioBuffer<float>& bus, const float* const* inputs, int numInputs, int n) noexcept
{
    auto inputChannel = [&] (int ch) -> const float*
    {
        return (inputs != nullptr && ch < numInputs && inputs[ch] != nullptr) ? inputs[ch] : silence.getReadPointer (0);
    };

    // --- recording: write the selected input straight into a background file writer
    for (auto& slot : recorders)
    {
        slot.busy = true;
        auto* w = slot.writer.load();
        if (w != nullptr && blockRolling && recording.load() && ! slot.stopped.load())
        {
            const int input = slot.input.load();
            const float* chans[2] = { inputChannel (input == 2 ? 1 : 0), inputChannel (input == 0 ? 1 : 0) };
            int count = n;
            if (wrapAt >= 0)
            {
                count = wrapAt;          // audio is recorded once: stop at the loop end
                slot.stopped = true;
            }
            if (slot.startBeat.load() < 0.0)
                slot.startBeat = blockStartBeat;
            if (count > 0 && w->write (chans, count))
                slot.written.fetch_add (count);
        }
        slot.busy = false;
    }

    if (current == nullptr)
        return;
    auto* left = bus.getWritePointer (0);
    auto* right = bus.getWritePointer (1);

    // --- input monitoring for armed audio tracks
    for (auto& mix : current->tracks)
    {
        if (! mix.isAudio || ! mix.armed || ! mix.monitor)
            continue;
        const float* inL = inputChannel (mix.input == 2 ? 1 : 0);
        const float* inR = inputChannel (mix.input == 0 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            left[i] += inL[i] * mix.gainLeft;
            right[i] += inR[i] * mix.gainRight;
        }
    }

    // --- audio clips (time based: they play at their own speed whatever the tempo)
    if (! blockRolling || current->audio.empty())
        return;

    auto playSegment = [&] (double startBeat, int from, int to)
    {
        const double t0 = current->beatToSeconds (startBeat);
        const double t1 = t0 + (to - from) / sampleRate;

        for (auto& r : current->audio)
        {
            if (r.startSeconds >= t1 || r.startSeconds + r.duration <= t0 || r.data == nullptr)
                continue;
            if (std::abs (r.data->sampleRate - sampleRate) > 0.5)
                continue; // device rate changed; a rebuilt snapshot is on its way

            const auto& mix = current->tracks[(size_t) r.track];
            const auto& audio = r.data->audio;
            const int fileChans = audio.getNumChannels();
            const auto regionSamples = (int64_t) (r.duration * sampleRate);

            for (int i = from; i < to; ++i)
            {
                const auto inRegion = (int64_t) std::floor ((t0 - r.startSeconds) * sampleRate) + (i - from);
                if (inRegion < 0 || inRegion >= regionSamples)
                    continue;
                const auto idx = r.fileOffset + inRegion;
                if (idx < 0 || idx >= audio.getNumSamples())
                    continue;
                const float l = audio.getSample (0, (int) idx) * r.gain;
                const float rr = audio.getSample (fileChans > 1 ? 1 : 0, (int) idx) * r.gain;
                left[i] += l * mix.gainLeft;
                right[i] += rr * mix.gainRight;
            }
        }
    };

    if (wrapAt < 0)
    {
        playSegment (blockStartBeat, 0, n);
    }
    else
    {
        playSegment (blockStartBeat, 0, wrapAt);
        playSegment (wrapBeat, wrapAt, n);
    }
}

void Sequencer::advance (int numSamples) noexcept
{
    sampleCounter.fetch_add (numSamples, std::memory_order_relaxed);
}

} // namespace km
