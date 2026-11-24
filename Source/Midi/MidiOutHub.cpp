#include "Midi/MidiOutHub.h"

#include <algorithm>
#include <cmath>

namespace km
{
MidiOutHub::MidiOutHub() : juce::Thread ("KlankModule MIDI out")
{
    pending.reserve (16384);
    startThread (juce::Thread::Priority::highest);
}

MidiOutHub::~MidiOutHub()
{
    stopThread (2000);
    // Be nice to hardware: stop the clock, release everything that might hang.
    const juce::ScopedLock sl (portLock);
    for (int i = 0; i < numPorts.load(); ++i)
        if (auto& p = ports[(size_t) i]; p.output != nullptr)
        {
            if (p.clock)
                p.output->sendMessageNow (juce::MidiMessage::midiStop());
            for (int ch = 1; ch <= 16; ++ch)
                p.output->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
        }
}

//==============================================================================
int MidiOutHub::acquirePort (const juce::String& identifier, const juce::String& name)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const juce::ScopedLock sl (portLock);
    const int n = numPorts.load();
    for (int i = 0; i < n; ++i)
    {
        auto& p = ports[(size_t) i];
        if (p.identifier == identifier)
        {
            if (p.output == nullptr && ! testSink)
                p.output = juce::MidiOutput::openDevice (identifier); // plugged in again
            if (name.isNotEmpty())
                p.name = name;
            return i;
        }
    }
    if (n >= maxPorts)
        return -1;

    auto& p = ports[(size_t) n];
    p.identifier = identifier;
    p.name = name.isNotEmpty() ? name : identifier;
    if (! testSink)
        p.output = juce::MidiOutput::openDevice (identifier);
    numPorts.store (n + 1);
    return n;
}

juce::String MidiOutHub::getPortName (int slot) const
{
    const juce::ScopedLock sl (portLock);
    return juce::isPositiveAndBelow (slot, numPorts.load()) ? ports[(size_t) slot].name : juce::String();
}

bool MidiOutHub::isPortOpen (int slot) const
{
    const juce::ScopedLock sl (portLock);
    return juce::isPositiveAndBelow (slot, numPorts.load()) && (ports[(size_t) slot].output != nullptr || testSink);
}

void MidiOutHub::setClockOutputs (const juce::StringArray& identifiers)
{
    JUCE_ASSERT_MESSAGE_THREAD
    for (auto& id : identifiers)
    {
        juce::String name;
        for (auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.identifier == id)
                name = d.name;
        acquirePort (id, name);
    }
    const juce::ScopedLock sl (portLock);
    bool any = false;
    for (int i = 0; i < numPorts.load(); ++i)
    {
        auto& p = ports[(size_t) i];
        const bool was = p.clock;
        p.clock = identifiers.contains (p.identifier);
        if (was && ! p.clock && p.output != nullptr)
            p.output->sendMessageNow (juce::MidiMessage::midiStop());
        any = any || p.clock;
    }
    anyClock.store (any);
}

juce::StringArray MidiOutHub::getClockOutputs() const
{
    const juce::ScopedLock sl (portLock);
    juce::StringArray ids;
    for (int i = 0; i < numPorts.load(); ++i)
        if (ports[(size_t) i].clock)
            ids.add (ports[(size_t) i].identifier);
    return ids;
}

void MidiOutHub::sendNow (int slot, const juce::MidiMessage& m)
{
    const juce::ScopedLock sl (portLock);
    deliverTo (slot, m, juce::Time::getMillisecondCounterHiRes());
}

//==============================================================================
void MidiOutHub::beginBlock (double sr, int numSamples) noexcept
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    const double now = juce::Time::getMillisecondCounterHiRes();
    // Follow the sample clock while it stays close to the wall clock: steadier than "now".
    // Never jump back a little (that could reorder messages); only resync when far off.
    if (expectedNextMs < 0.0 || now - expectedNextMs > 20.0 || expectedNextMs - now > 1000.0)
        blockTimeMs = now;
    else if (now < expectedNextMs)
        blockTimeMs = expectedNextMs - juce::jmin (0.05 * (expectedNextMs - now), 0.5);
    else
        blockTimeMs = expectedNextMs + 0.05 * (now - expectedNextMs);
    expectedNextMs = blockTimeMs + numSamples * 1000.0 / sampleRate;
}

void MidiOutHub::schedule (int slot, const uint8_t* data, int size, double timeMs) noexcept
{
    Event e;
    e.timeMs = timeMs;
    e.slot = slot;
    e.size = (uint8_t) juce::jlimit (1, 3, size);
    std::copy (data, data + e.size, e.data);
    if (queue.push (e))
        queued.fetch_add (1);
    else
        dropped.fetch_add (1);
}

void MidiOutHub::send (int slot, const uint8_t* data, int size, int samplePos) noexcept
{
    if (slot < 0 || size <= 0 || size > 3)
        return;
    schedule (slot, data, size, blockTimeMs + samplePos * 1000.0 / sampleRate + outputLatencyMs.load() + userOffsetMs.load());
}

void MidiOutHub::processClock (bool rolling, double beat0, double beatsPerSample, int n) noexcept
{
    if (! anyClock.load (std::memory_order_relaxed))
    {
        clockRolling = false;
        return;
    }

    const double delay = outputLatencyMs.load() + userOffsetMs.load();
    auto at = [&] (int pos) { return blockTimeMs + pos * 1000.0 / sampleRate + delay; };
    constexpr double tick = 1.0 / 24.0;

    if (rolling && ! clockRolling)
    {
        // Song Position Pointer counts sixteenth notes; then Start (from the top) or Continue.
        const int sixteenths = juce::jmax (0, (int) std::floor (beat0 * 4.0 + 1.0e-9));
        if (sixteenths == 0)
        {
            const uint8_t start = 0xfa;
            schedule (-1, &start, 1, at (0));
        }
        else
        {
            const uint8_t spp[3] = { 0xf2, (uint8_t) (sixteenths & 0x7f), (uint8_t) ((sixteenths >> 7) & 0x7f) };
            schedule (-1, spp, 3, at (0));
            const uint8_t cont = 0xfb;
            schedule (-1, &cont, 1, at (0));
        }
        nextTickBeat = std::ceil (beat0 / tick - 1.0e-9) * tick;
        clockRolling = true;
    }
    else if (! rolling && clockRolling)
    {
        const uint8_t stop = 0xfc;
        schedule (-1, &stop, 1, at (0));
        clockRolling = false;
    }

    if (! clockRolling || beatsPerSample <= 0.0)
        return;

    const double end = beat0 + n * beatsPerSample;
    if (nextTickBeat < beat0 - 2 * tick || nextTickBeat > end + 2 * tick)
        nextTickBeat = std::ceil (beat0 / tick - 1.0e-9) * tick; // loop jump or locate
    while (nextTickBeat < end)
    {
        const uint8_t clk = 0xf8;
        schedule (-1, &clk, 1, at (juce::jlimit (0, n - 1, (int) ((nextTickBeat - beat0) / beatsPerSample))));
        nextTickBeat += tick;
    }
}

//==============================================================================
void MidiOutHub::run()
{
    while (! threadShouldExit())
    {
        while (auto e = queue.pop())
            pending.push_back (*e);

        if (! pending.empty())
        {
            std::stable_sort (pending.begin(), pending.end(), [] (const Event& a, const Event& b) { return a.timeMs < b.timeMs; });
            const double now = juce::Time::getMillisecondCounterHiRes();
            size_t done = 0;
            {
                const juce::ScopedLock sl (portLock);
                while (done < pending.size() && pending[done].timeMs <= now + 0.25)
                    deliver (pending[done++]);
            }
            pending.erase (pending.begin(), pending.begin() + (long) done);
            delivered.fetch_add ((uint32_t) done);
        }

        wait (1);
    }
}

void MidiOutHub::deliver (const Event& e)
{
    const juce::MidiMessage m (e.data, (int) e.size);
    if (e.slot >= 0)
    {
        deliverTo (e.slot, m, e.timeMs);
        return;
    }
    for (int i = 0; i < numPorts.load(); ++i)
        if (ports[(size_t) i].clock)
            deliverTo (i, m, e.timeMs);
}

void MidiOutHub::deliverTo (int slot, const juce::MidiMessage& m, double timeMs)
{
    if (! juce::isPositiveAndBelow (slot, numPorts.load()))
        return;
    if (testSink)
    {
        testSink (slot, m, timeMs);
        return;
    }
    if (auto& out = ports[(size_t) slot].output)
        out->sendMessageNow (m);
}

void MidiOutHub::flush (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (delivered.load() < queued.load() && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (2);
}

} // namespace km
