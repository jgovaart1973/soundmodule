#pragma once

#include "Core/SpscQueue.h"

#include <cstdint>
#include <mutex>

namespace km
{
/** Maximum number of MIDI input devices we track per session. */
constexpr int kMaxMidiSources = 32;

/** A short (non-SysEx) MIDI message tagged with the device it came from. */
struct MidiEvent
{
    int8_t  source = -1;   ///< device slot (0..kMaxMidiSources-1), -1 = internal/offline
    uint8_t size = 0;      ///< 1..3 bytes
    uint8_t data[3] {};

    int status() const noexcept   { return data[0] & 0xf0; }
    int channel() const noexcept  { return (data[0] & 0x0f) + 1; } // 1..16
    bool isNoteOn() const noexcept  { return status() == 0x90 && data[2] > 0; }
    bool isNoteOff() const noexcept { return status() == 0x80 || (status() == 0x90 && data[2] == 0); }
    bool isController() const noexcept    { return status() == 0xb0; }
    bool isProgramChange() const noexcept { return status() == 0xc0; }

    static MidiEvent make (int source, uint8_t b0, uint8_t b1 = 0, uint8_t b2 = 0, uint8_t size = 3) noexcept
    {
        MidiEvent e;
        e.source = (int8_t) source;
        e.size = size;
        e.data[0] = b0;
        e.data[1] = b1;
        e.data[2] = b2;
        return e;
    }
};

/**
    MIDI input queue: many MIDI driver threads push, the audio thread pops.
    Producers are serialised with a mutex (never touched by the audio thread),
    the consumer side is wait-free.
*/
class MidiEventQueue
{
public:
    bool push (const MidiEvent& e)
    {
        std::lock_guard<std::mutex> lock (producerMutex);
        return queue.push (e);
    }

    std::optional<MidiEvent> pop() noexcept { return queue.pop(); }

private:
    std::mutex producerMutex;
    SpscQueue<MidiEvent, 4096> queue;
};

} // namespace km
