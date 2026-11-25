#include "Engine/Part.h"

#include "Engine/Sequencer.h"

namespace km
{
namespace
{
    /** Marker meaning "remove the instrument" in the pending slot. Never processed. */
    struct NullInstrument final : public Instrument
    {
        void prepare (double, int) override {}
        void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) noexcept override {}
        void allNotesOff() noexcept override {}
        juce::String getName() const override { return {}; }
    };

    NullInstrument nullInstrument;
    Instrument* const kClear = &nullInstrument;
} // namespace

Part::Part (int idx, ReleaseQueue& rq, ProgramRequestQueue& pq)
    : index (idx), releaseQueue (rq), programQueue (pq)
{
}

void Part::prepare (double sampleRate, int maxBlockSize)
{
    scratch.setSize (2, maxBlockSize, false, true, false);
    tailScratch.setSize (2, maxBlockSize, false, true, false);
    currentMidi.ensureSize (8192);
    tailMidi.ensureSize (8192);

    for (auto* s : { &gainLeft, &gainRight, &revGain, &dlyGain })
        s->reset (sampleRate, 0.02);

    tailMaxSilentSamples = (int) (3.0 * sampleRate);
}

Instrument* Part::lend (Instrument* instrument) noexcept
{
    auto* previous = pendingSlot.exchange (instrument != nullptr ? instrument : kClear);
    return previous == kClear ? nullptr : previous;
}

void Part::reclaimAll (juce::Array<Instrument*>& out) noexcept
{
    if (auto* p = pendingSlot.exchange (nullptr); p != nullptr && p != kClear)
        out.add (p);
    if (current != nullptr) out.add (current);
    if (tail != nullptr)    out.add (tail);
    current = tail = nullptr;
    std::memset (owner, 0, sizeof (owner));
}

//==============================================================================
void Part::beginBlock() noexcept
{
    adoptPending();

    // MPE configuration for the current sound: when switched on/off, or when the sound changes.
    const bool mpeOn = params.mpe.load (std::memory_order_relaxed);
    if (current != nullptr && (current != mpeConfiguredFor || mpeOn != mpeConfiguredOn))
    {
        if (mpeOn || mpeConfiguredOn)
            sendMpeConfiguration (mpeOn);
        mpeConfiguredFor = current;
        mpeConfiguredOn = mpeOn;
    }
}

void Part::sendMpeConfiguration (bool on) noexcept
{
    auto cc = [this] (int ch, int number, int value)
    {
        const uint8_t m[3] = { (uint8_t) (0xb0 | ch), (uint8_t) number, (uint8_t) value };
        sendToCurrent (m, 3, 0);
    };
    // MPE Configuration Message (RPN 6) on the manager channel: lower zone with 15 member channels.
    cc (0, 101, 0); cc (0, 100, 6); cc (0, 6, on ? 15 : 0); cc (0, 38, 0);
    // Pitch bend sensitivity (RPN 0): members +-48, manager +-2 (the MPE defaults)
    for (int ch = 0; ch < 16; ++ch)
    {
        cc (ch, 101, 0); cc (ch, 100, 0); cc (ch, 6, (on && ch > 0) ? 48 : 2); cc (ch, 38, 0);
        cc (ch, 101, 127); cc (ch, 100, 127);   // RPN null
    }
}

void Part::adoptPending() noexcept
{
    auto* p = pendingSlot.exchange (nullptr);
    if (p == nullptr)
        return;

    auto* incoming = (p == kClear) ? nullptr : p;
    if (incoming == current)
        return;

    // A tail that is still ringing from an earlier switch is cut now.
    if (tail != nullptr)
    {
        tail->allNotesOff();
        releaseTail();
    }

    for (auto& ch : owner)
        for (auto& o : ch)
            if (o == ownerCurrent)
                o = (current != nullptr) ? ownerTail : none;

    // Arpeggiated notes that are sounding belong to the outgoing sound: end them there.
    if (current != nullptr && arp.isSounding())
        arp.releaseNow (0, [this] (const uint8_t* d, int size, int pos) { tailMidi.addEvent (d, size, pos); });

    tail = current;
    tailSilentSamples = 0;
    current = incoming;

    // Tell the new instrument about a sustain pedal that is already down.
    for (int ch = 0; ch < 16; ++ch)
    {
        if (sustainDown[ch] && current != nullptr)
        {
            const uint8_t msg[3] = { (uint8_t) (0xb0 | ch), 64, 127 };
            sendToCurrent (msg, 3, 0);
        }
    }
}

void Part::releaseTail() noexcept
{
    if (tail == nullptr)
        return;

    if (releaseQueue.push (tail))
    {
        tail = nullptr;
        for (auto& ch : owner)
            for (auto& o : ch)
                if (o == ownerTail)
                    o = none;
    }
    // If the queue is full we simply try again next block.
}

void Part::sendToCurrent (const uint8_t* data, int size, int pos) noexcept
{
    if (current != nullptr)
        currentMidi.addEvent (data, size, pos);
}

void Part::sendToTail (const uint8_t* data, int size, int pos) noexcept
{
    if (tail != nullptr)
        tailMidi.addEvent (data, size, pos);
}

void Part::record (const uint8_t* data, int size, int pos) noexcept
{
    if (recorder != nullptr)
        recorder->capture (index, data, size, pos);
}

void Part::handleEvent (const MidiEvent& e, int pos) noexcept
{
    // Song playback goes straight to its part: no input/channel routing, splits or curves
    // (those were already applied when it was recorded).
    const bool direct = e.targetPart >= 0;

    if (direct)
    {
        if (e.targetPart != index)
            return;
    }
    else
    {
        // Input + channel routing
        const int slot = params.inputSlot.load (std::memory_order_relaxed);
        if (slot != -1 && slot != e.source)
            return;

        const int wantedChannel = params.channel.load (std::memory_order_relaxed);
        if (wantedChannel != 0 && wantedChannel != e.channel())
            return;
    }

    const int ch = e.channel() - 1;
    const bool enabled = direct || params.enabled.load (std::memory_order_relaxed);

    // Note-offs and pedal-up must always get through, even for a part that was just disabled.
    if (e.isNoteOff())
    {
        const int note = e.data[1] & 0x7f;
        if (! direct && arpOut[ch][note] != 0)
        {
            arp.noteOff (arpOut[ch][note] - 1, params.arpLatch.load());
            arpOut[ch][note] = 0;
            return;
        }
        const auto o = owner[ch][note];
        if (o == none)
            return;

        const uint8_t msg[3] = { (uint8_t) (0x80 | ch), sentNote[ch][note], (uint8_t) (e.data[2] & 0x7f) };
        if (o == ownerCurrent) sendToCurrent (msg, 3, pos);
        else                   sendToTail (msg, 3, pos);
        owner[ch][note] = none;
        if (! direct)
            record (msg, 3, pos);
        return;
    }

    if (e.isController() && e.data[1] == 64)
    {
        sustainDown[ch] = e.data[2] >= 64;
        sendToCurrent (e.data, 3, pos);
        sendToTail (e.data, 3, pos);
        if (! direct && enabled)
            record (e.data, 3, pos);
        return;
    }

    if (! enabled)
        return;

    if (e.isNoteOn())
    {
        const int note = e.data[1] & 0x7f;
        const int velocity = e.data[2] & 0x7f;
        int outNote = note, outVel = velocity;

        if (! direct)
        {
            if (! filter::inRange (note, params.keyLow.load(), params.keyHigh.load())
                || ! filter::inRange (velocity, params.velLow.load(), params.velHigh.load()))
                return;

            outNote = filter::transposeNote (note, params.transpose.load());
            if (outNote < 0)
                return;

            outVel = filter::applyVelocityCurve (velocity,
                                                 (filter::VelocityCurve) params.velocityCurve.load(),
                                                 params.fixedVelocity.load());
        }

        // Same key struck again while the old instance still rings: release it there first.
        if (owner[ch][note] == ownerTail)
        {
            const uint8_t off[3] = { (uint8_t) (0x80 | ch), sentNote[ch][note], 0 };
            sendToTail (off, 3, pos);
            owner[ch][note] = none;
        }
        else if (owner[ch][note] == ownerCurrent && sentNote[ch][note] != outNote)
        {
            const uint8_t off[3] = { (uint8_t) (0x80 | ch), sentNote[ch][note], 0 };
            sendToCurrent (off, 3, pos);
        }

        if (current == nullptr)
            return;

        // Arpeggiator on: the key joins the pattern instead of sounding directly.
        if (! direct && params.arpOn.load())
        {
            arp.noteOn (outNote, outVel, ch + 1, params.arpLatch.load());
            arpOut[ch][note] = (uint8_t) (outNote + 1);
            activity.fetch_add (1, std::memory_order_relaxed);
            return;
        }

        const uint8_t on[3] = { (uint8_t) (0x90 | ch), (uint8_t) outNote, (uint8_t) outVel };
        sendToCurrent (on, 3, pos);
        owner[ch][note] = ownerCurrent;
        sentNote[ch][note] = (uint8_t) outNote;
        activity.fetch_add (1, std::memory_order_relaxed);
        if (! direct)
            record (on, 3, pos);
        return;
    }

    switch (e.status())
    {
        case 0xb0:
        {
            const int cc = e.data[1];
            if (cc == 0)  bankMsb[ch] = e.data[2];
            if (cc == 32) bankLsb[ch] = e.data[2];

            if (cc == 120 || cc == 123) // all sound / notes off
            {
                sendToTail (e.data, 3, pos);
                for (auto& o : owner[ch])
                    o = none;
            }
            sendToCurrent (e.data, 3, pos);
            if (! direct && cc != 0 && cc != 32)
                record (e.data, 3, pos);
            break;
        }

        case 0xc0:
        {
            ProgramRequest req;
            req.kind = ProgramRequest::partProgram;
            req.part = index;
            req.bank = ((bankMsb[ch] & 0x7f) << 7) | (bankLsb[ch] & 0x7f);
            req.program = e.data[1] & 0x7f;
            programQueue.push (req);
            sendToCurrent (e.data, 2, pos);
            break;
        }

        case 0xe0: // pitch bend: also bend notes that are still ringing on the tail
            sendToCurrent (e.data, 3, pos);
            sendToTail (e.data, 3, pos);
            if (! direct) record (e.data, 3, pos);
            break;

        case 0xa0: sendToCurrent (e.data, 3, pos); if (! direct) record (e.data, 3, pos); break; // poly aftertouch
        case 0xd0: sendToCurrent (e.data, 2, pos); if (! direct) record (e.data, 2, pos); break; // channel pressure
        default: break;
    }
}

//==============================================================================
void Part::render (juce::AudioBuffer<float>& mainBus,
                   juce::AudioBuffer<float>& reverbBus,
                   juce::AudioBuffer<float>& delayBus,
                   int n, bool anySoloActive) noexcept
{
    if (current == nullptr && tail == nullptr)
    {
        currentMidi.clear();
        tailMidi.clear();
        return;
    }

    // Arpeggiator: turn held keys into a pattern for this block.
    const bool arpOn = params.arpOn.load();
    auto toCurrent = [this] (const uint8_t* d, int size, int pos)
    {
        sendToCurrent (d, size, pos);
        record (d, size, pos);   // a recording keeps what the arpeggiator played
    };
    if (arpOn)
    {
        Arpeggiator::Settings s;
        s.mode = (Arpeggiator::Mode) juce::jlimit (0, 5, params.arpMode.load());
        s.stepBeats = Arpeggiator::rates()[(size_t) juce::jlimit (0, 5, params.arpRate.load())].beats;
        s.octaves = params.arpOctaves.load();
        s.gate = params.arpGate.load();
        s.latch = params.arpLatch.load();
        const bool synced = recorder != nullptr && recorder->isBlockRolling();
        const double bps = recorder != nullptr ? recorder->getTempoBeatsPerSample() : 120.0 / 60.0 / 48000.0;
        arp.process (s, synced, synced ? recorder->getBlockStartBeat() : 0.0, bps, n, toCurrent);
    }
    else if (arpWasOn)
    {
        arp.stopAll (0, toCurrent);
        std::memset (arpOut, 0, sizeof (arpOut));
    }
    arpWasOn = arpOn;

    juce::AudioBuffer<float> out (scratch.getArrayOfWritePointers(), 2, n);
    out.clear();

    if (current != nullptr)
        current->process (out, currentMidi);

    if (tail != nullptr)
    {
        juce::AudioBuffer<float> t (tailScratch.getArrayOfWritePointers(), 2, n);
        t.clear();
        tail->process (t, tailMidi);
        out.addFrom (0, 0, t, 0, 0, n);
        out.addFrom (1, 0, t, 1, 0, n);

        bool tailHeld = false;
        for (int ch = 0; ch < 16 && ! tailHeld; ++ch)
        {
            if (sustainDown[ch])
                tailHeld = true;
            else
                for (auto o : owner[ch])
                    if (o == ownerTail) { tailHeld = true; break; }
        }

        if (tailHeld)
            tailSilentSamples = 0;
        else if ((tailSilentSamples += n) > tailMaxSilentSamples)
            releaseTail();
    }

    currentMidi.clear();
    tailMidi.clear();

    // Fader, pan, mute/solo and post-fader sends (all smoothed to avoid zipper noise).
    const bool muted = params.mute.load() || (anySoloActive && ! params.solo.load());
    const float vol = muted ? 0.0f : juce::Decibels::decibelsToGain (params.volumeDb.load(), -80.0f) * params.trackGain.load();
    float pl = 1.0f, pr = 1.0f;
    filter::panGains (juce::jlimit (-1.0f, 1.0f, params.pan.load() + params.trackPan.load()), pl, pr);

    gainLeft.setTargetValue (vol * pl);
    gainRight.setTargetValue (vol * pr);
    revGain.setTargetValue (params.reverbSend.load());
    dlyGain.setTargetValue (params.delaySend.load());

    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);
    auto* ml = mainBus.getWritePointer (0);
    auto* mr = mainBus.getWritePointer (1);
    auto* rl = reverbBus.getWritePointer (0);
    auto* rr = reverbBus.getWritePointer (1);
    auto* dl = delayBus.getWritePointer (0);
    auto* dr = delayBus.getWritePointer (1);

    float blockPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float sl = l[i] * gainLeft.getNextValue();
        float sr = r[i] * gainRight.getNextValue();
        if (! std::isfinite (sl)) sl = 0.0f; // keep NaN/inf from a plug-in out of the shared effects
        if (! std::isfinite (sr)) sr = 0.0f;
        const float rv = revGain.getNextValue();
        const float dv = dlyGain.getNextValue();

        ml[i] += sl;  mr[i] += sr;
        rl[i] += sl * rv;  rr[i] += sr * rv;
        dl[i] += sl * dv;  dr[i] += sr * dv;
        blockPeak = juce::jmax (blockPeak, std::abs (sl), std::abs (sr));
    }

    if (blockPeak > peak.load (std::memory_order_relaxed))
        peak.store (blockPeak, std::memory_order_relaxed);
}

void Part::panic() noexcept
{
    if (current != nullptr) current->allNotesOff();
    if (tail != nullptr)
    {
        tail->allNotesOff();
        releaseTail();
    }

    std::memset (owner, 0, sizeof (owner));
    std::memset (arpOut, 0, sizeof (arpOut));
    arp.stopAll (0, [] (const uint8_t*, int, int) {});
    for (auto& s : sustainDown)
        s = false;
    currentMidi.clear();
    tailMidi.clear();
}

} // namespace km
