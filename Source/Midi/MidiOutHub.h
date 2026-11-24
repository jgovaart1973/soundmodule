#pragma once

#include "Core/SpscQueue.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <vector>

namespace km
{
/**
    MIDI output to hardware synths (phase 3c).

    The audio thread schedules short messages with a sample position; they get a timestamp
    (block start + position + output latency + user offset) and go through a lock-free queue to
    a high-priority sender thread that sends each message on time. That way MIDI to a hardware
    synth lines up with what you hear from the sound card, and the audio thread never touches
    the MIDI driver.

    Also generates MIDI clock (24 ppq), Start / Continue / Stop and Song Position Pointer for
    the ports chosen as clock outputs.
*/
class MidiOutHub final : private juce::Thread
{
public:
    static constexpr int maxPorts = 32;

    MidiOutHub();
    ~MidiOutHub() override;

    //== message thread ========================================================
    /** Port slot for a device identifier (opens the device if needed). -1 when there is no room. */
    int acquirePort (const juce::String& identifier, const juce::String& name);
    juce::String getPortName (int slot) const;
    bool isPortOpen (int slot) const;

    /** Clock outputs: device identifiers that receive MIDI clock and transport messages. */
    void setClockOutputs (const juce::StringArray& identifiers);
    juce::StringArray getClockOutputs() const;

    /** Extra delay in ms (positive = later). Can be negative to send earlier than the audio. */
    void setUserOffsetMs (double ms) noexcept       { userOffsetMs.store (ms); }
    double getUserOffsetMs() const noexcept         { return userOffsetMs.load(); }
    void setOutputLatencyMs (double ms) noexcept    { outputLatencyMs.store (ms); }

    /** Send right now from the message thread (Program Change, SysEx ...). */
    void sendNow (int slot, const juce::MidiMessage& m);

    /** Testing: receive what would go to a device (called on the sender thread). No devices are opened. */
    std::function<void (int slot, const juce::MidiMessage&, double timeMs)> testSink;

    //== audio thread ==========================================================
    void beginBlock (double sampleRate, int numSamples) noexcept;
    void send (int slot, const uint8_t* data, int size, int samplePos) noexcept;

    /** Clock for this block. rolling / start beat / beats per sample come from the sequencer. */
    void processClock (bool rolling, double blockStartBeat, double beatsPerSample, int numSamples) noexcept;

    /** Total messages dropped because the queue was full (diagnostics). */
    uint32_t getDropped() const noexcept { return dropped.load(); }

    /** Block until everything queued so far has been handed to the devices (tests, shutdown). */
    void flush (int timeoutMs = 1000);

private:
    struct Event
    {
        double timeMs = 0.0;
        int slot = -1;        ///< -1 = all clock ports
        uint8_t data[3] {};
        uint8_t size = 0;
    };

    struct Port
    {
        juce::String identifier, name;
        std::unique_ptr<juce::MidiOutput> output;
        bool clock = false;
    };

    void run() override;
    void deliver (const Event& e);
    void deliverTo (int slot, const juce::MidiMessage& m, double timeMs);
    void schedule (int slot, const uint8_t* data, int size, double timeMs) noexcept;

    juce::CriticalSection portLock;     ///< message thread <-> sender thread (never the audio thread)
    std::array<Port, maxPorts> ports;
    std::atomic<int> numPorts { 0 };
    std::atomic<bool> anyClock { false };

    SpscQueue<Event, 8192> queue;
    std::vector<Event> pending;          ///< sender thread only
    std::atomic<uint32_t> dropped { 0 }, queued { 0 }, delivered { 0 };

    std::atomic<double> userOffsetMs { 0.0 }, outputLatencyMs { 0.0 };

    // audio thread state
    double blockTimeMs = 0.0, sampleRate = 48000.0;
    double expectedNextMs = -1.0;
    bool clockRolling = false;
    double nextTickBeat = 0.0;

    JUCE_DECLARE_NON_COPYABLE (MidiOutHub)
};

} // namespace km
