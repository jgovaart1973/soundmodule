#include "Render/OfflineRenderer.h"
#include "Plugins/ClapHosting.h"

#include "Core/AppPaths.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace km
{
OfflineRenderer::OfflineRenderer (double sr, int bs) : sampleRate (sr), blockSize (bs)
{
    km::addPluginFormats (formats);
    if (auto xml = juce::parseXML (paths::pluginListFile()))
        knownPlugins.recreateFromXml (*xml);

    engine.prepare (sampleRate, blockSize);
    pool.setAudioConfig (sampleRate, blockSize);
}

OfflineRenderer::~OfflineRenderer()
{
    juce::Array<Instrument*> back;
    for (int i = 0; i < kNumParts; ++i)
        engine.getPart (i).reclaimAll (back);
    while (auto r = engine.releaseQueue.pop())
        back.add (*r);
    for (auto* inst : back)
        pool.release (inst);
}

bool OfflineRenderer::loadPerformance (const Performance& p, juce::String& error)
{
    bool ok = true;
    for (int i = 0; i < kNumParts; ++i)
    {
        const auto& c = p.parts[(size_t) i];
        auto& params = engine.getPart (i).params;
        params.enabled = c.enabled;
        params.inputSlot = -1; // offline: everything comes from the MIDI file
        params.channel = c.channel;
        params.volumeDb = c.volumeDb;
        params.pan = c.pan;
        params.transpose = c.transpose;
        params.keyLow = c.keyLow;
        params.keyHigh = c.keyHigh;
        params.velLow = c.velLow;
        params.velHigh = c.velHigh;
        params.velocityCurve = (int) c.velocityCurve;
        params.fixedVelocity = c.fixedVelocity;
        params.reverbSend = c.reverbSend;
        params.delaySend = c.delaySend;
        params.mute = c.mute;
        params.solo = c.solo;
        params.arpOn = c.arpOn;
        params.arpMode = c.arpMode;
        params.arpRate = c.arpRate;
        params.arpOctaves = c.arpOctaves;
        params.arpGate = c.arpGate;
        params.arpLatch = c.arpLatch;
        params.mpe = c.mpe;

        if (! c.enabled || c.instrument.isEmpty())
            continue;

        juce::String err;
        if (auto* inst = pool.acquireSync (c.instrument, err))
        {
            if (auto* old = engine.getPart (i).lend (inst))
                pool.release (old);
        }
        else
        {
            error << "Part " << (i + 1) << ": " << err << "\n";
            ok = false;
        }
    }

    engine.master.volumeDb = p.master.volumeDb;
    engine.master.reverbSize = p.master.reverbSize;
    engine.master.reverbDamping = p.master.reverbDamping;
    engine.master.delayMs = p.master.delayMs;
    engine.master.delayFeedback = p.master.delayFeedback;
    engine.master.limiterThresholdDb = p.master.limiterThresholdDb;
    return ok;
}

juce::AudioBuffer<float> OfflineRenderer::render (const juce::MidiMessageSequence& seq, double tailSeconds)
{
    std::vector<std::pair<int64_t, MidiEvent>> events;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto& msg = seq.getEventPointer (i)->message;
        const auto* raw = msg.getRawData();
        const int size = msg.getRawDataSize();
        if (size >= 1 && size <= 3 && raw[0] >= 0x80 && raw[0] < 0xf0)
            events.push_back ({ (int64_t) std::llround (msg.getTimeStamp() * sampleRate),
                                MidiEvent::make (-1, raw[0], size > 1 ? raw[1] : 0, size > 2 ? raw[2] : 0, (uint8_t) size) });
    }
    return renderEvents (events, (int64_t) std::ceil ((seq.getEndTime() + tailSeconds) * sampleRate));
}

juce::AudioBuffer<float> OfflineRenderer::renderSong (const Song& song, AudioCache& cache, double tailSeconds)
{
    auto& seq = engine.sequencer;
    seq.bpm = song.bpm;
    seq.beatsPerBar = song.beatsPerBar;
    seq.beatUnit = song.beatUnit;
    seq.loopEnabled = false;
    seq.metronomeOn = false;
    seq.setSnapshot (PlaybackSnapshot::build (song, &cache, sampleRate, false));
    std::vector<float> trackGain, trackPan;
    song.trackMixForParts (trackGain, trackPan, kNumParts);
    for (int i = 0; i < kNumParts; ++i)
    {
        engine.getPart (i).params.trackGain = trackGain[(size_t) i];
        engine.getPart (i).params.trackPan = trackPan[(size_t) i];
    }
    seq.locate (0.0);
    seq.play();

    const double seconds = song.beatToSeconds (song.getEndBeat()) + tailSeconds;
    auto audio = renderEvents ({}, (int64_t) std::ceil (seconds * sampleRate));
    seq.stop();
    seq.collectGarbage();
    return audio;
}

juce::AudioBuffer<float> OfflineRenderer::renderEvents (const std::vector<std::pair<int64_t, MidiEvent>>& all, int64_t total)
{
    const int totalSamples = (int) juce::jlimit<int64_t> (1, (int64_t) std::numeric_limits<int>::max(), total);
    juce::AudioBuffer<float> result (2, totalSamples);
    result.clear();

    juce::AudioBuffer<float> block (2, blockSize);
    std::vector<MidiEvent> events (4096);
    std::vector<int> positions (4096);
    size_t next = 0;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int n = juce::jmin (blockSize, totalSamples - start);
        int count = 0;

        while (next < all.size() && all[next].first < start + n && count < (int) events.size())
        {
            events[(size_t) count] = all[next].second;
            positions[(size_t) count] = (int) juce::jmax<int64_t> (0, all[next].first - start);
            ++count;
            ++next;
        }

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
        engine.process (view, n, events.data(), positions.data(), count);
        result.copyFrom (0, start, view, 0, 0, n);
        result.copyFrom (1, start, view, 1, 0, n);

        // Hand released instruments back, like the app's timer does.
        while (auto r = engine.releaseQueue.pop())
            pool.release (*r);
        while (engine.programRequests.pop()) {}
        while (engine.sequencer.recordQueue.pop()) {}
    }
    return result;
}

juce::MidiMessageSequence OfflineRenderer::loadMidiFile (const juce::File& file, juce::String& error)
{
    juce::MidiMessageSequence merged;
    juce::FileInputStream in (file);
    juce::MidiFile midi;

    if (! in.openedOk() || ! midi.readFrom (in))
    {
        error = "Kan MIDI-bestand niet lezen: " + file.getFullPathName();
        return merged;
    }

    midi.convertTimestampTicksToSeconds();
    for (int t = 0; t < midi.getNumTracks(); ++t)
        merged.addSequence (*midi.getTrack (t), 0.0);
    merged.updateMatchedPairs();
    return merged;
}

juce::MidiMessageSequence OfflineRenderer::makeDemoSequence()
{
    // A short I-vi-IV-V progression on channel 1: chords, arpeggio and bass.
    juce::MidiMessageSequence seq;
    const int chords[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };
    const double beat = 0.5; // 120 BPM

    for (int bar = 0; bar < 4; ++bar)
    {
        const double t0 = bar * 4 * beat;
        for (int k = 0; k < 3; ++k)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, chords[bar][k], (juce::uint8) 80), t0);
            seq.addEvent (juce::MidiMessage::noteOff (1, chords[bar][k]), t0 + 4 * beat - 0.05);
        }
        seq.addEvent (juce::MidiMessage::noteOn (1, chords[bar][0] - 24, (juce::uint8) 100), t0);
        seq.addEvent (juce::MidiMessage::noteOff (1, chords[bar][0] - 24), t0 + 4 * beat - 0.05);

        for (int step = 0; step < 8; ++step)
        {
            const int note = chords[bar][step % 3] + 12 + (step >= 4 ? 12 : 0);
            const double t = t0 + step * beat * 0.5;
            seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (60 + 8 * (step % 4))), t);
            seq.addEvent (juce::MidiMessage::noteOff (1, note), t + beat * 0.45);
        }
    }
    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

bool OfflineRenderer::writeWav (const juce::AudioBuffer<float>& audio, double sr, const juce::File& file)
{
    return writeAudioFile (audio, sr, file, false);
}

bool OfflineRenderer::writeAudioFile (const juce::AudioBuffer<float>& audio, double sr, const juce::File& file, bool flac)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    juce::FlacAudioFormat flacFormat;
    juce::AudioFormat& format = flac ? static_cast<juce::AudioFormat&> (flacFormat) : wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options); // takes ownership of the stream on success
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

} // namespace km
