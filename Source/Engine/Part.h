#pragma once

#include "Core/SpscQueue.h"
#include "Engine/Instrument.h"
#include "Engine/PartFilter.h"
#include "Midi/MidiEvent.h"

#include <atomic>

namespace km
{
class Sequencer;

/** Realtime parameters of a part. Written by UI / MIDI-learn threads, read by the audio thread. */
struct PartParams
{
    std::atomic<bool>  enabled { false };
    std::atomic<int>   inputSlot { -1 };   ///< -1 = all inputs, -2 = device not present, >=0 device slot
    std::atomic<int>   channel { 0 };      ///< 0 = omni
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<int>   transpose { 0 };
    std::atomic<int>   keyLow { 0 }, keyHigh { 127 };
    std::atomic<int>   velLow { 1 }, velHigh { 127 };
    std::atomic<int>   velocityCurve { 0 };
    std::atomic<int>   fixedVelocity { 100 };
    std::atomic<float> reverbSend { 0.15f };
    std::atomic<float> delaySend { 0.0f };
    std::atomic<bool>  mute { false };
    std::atomic<bool>  solo { false };
};

/** A Program Change that needs a decision on the message thread. */
struct ProgramRequest
{
    enum Kind : int { partProgram, performanceSelect };
    Kind kind = partProgram;
    int part = 0;
    int bank = 0;
    int program = 0;
};

using ReleaseQueue = SpscQueue<Instrument*, 256>;
using ProgramRequestQueue = SpscQueue<ProgramRequest, 256>;

/**
    One of the 16 multitimbral parts.

    Instrument hand-over without glitches or hanging notes:
      - The message thread lends an instrument via lend(); the audio thread adopts it at the
        start of the next block.
      - The previous instrument becomes the "tail": it keeps receiving note-offs and sustain
        for the notes that were started on it, so held notes ring out naturally.
      - After the tail has been silent for 3 s it is handed back through the ReleaseQueue.
*/
class Part
{
public:
    Part (int index, ReleaseQueue& releaseQueue, ProgramRequestQueue& programQueue);

    PartParams params;

    //== message thread ========================================================
    void prepare (double sampleRate, int maxBlockSize);

    /** Lend an instrument (nullptr = remove the sound).
        @returns an instrument that was lent earlier but never adopted (give it back to the pool), or nullptr. */
    Instrument* lend (Instrument* instrument) noexcept;

    /** Only when audio is stopped: collect every instrument this part still holds. */
    void reclaimAll (juce::Array<Instrument*>& out) noexcept;

    //== audio thread ==========================================================
    void beginBlock() noexcept;
    void handleEvent (const MidiEvent& e, int samplePosition) noexcept;
    void render (juce::AudioBuffer<float>& mainBus,
                 juce::AudioBuffer<float>& reverbBus,
                 juce::AudioBuffer<float>& delayBus,
                 int numSamples, bool anySoloActive) noexcept;
    void panic() noexcept;

    /** Sequencer that records what this part receives from keyboards (set once by the engine). */
    void setRecorder (Sequencer* s) noexcept { recorder = s; }

    //== metering (any thread) =================================================
    float getPeakAndReset() noexcept  { return peak.exchange (0.0f); }
    uint32_t getActivityCount() const noexcept { return activity.load (std::memory_order_relaxed); }

    /** The instrument the audio thread is currently playing (for diagnostics/tests only). */
    Instrument* getAdoptedInstrumentUnsafe() const noexcept { return current; }

private:
    enum Owner : uint8_t { none = 0, ownerCurrent = 1, ownerTail = 2 };

    void releaseTail() noexcept;
    void sendToCurrent (const uint8_t* data, int size, int pos) noexcept;
    void sendToTail (const uint8_t* data, int size, int pos) noexcept;
    void record (const uint8_t* data, int size, int pos) noexcept;

    Sequencer* recorder = nullptr;

    const int index;
    ReleaseQueue& releaseQueue;
    ProgramRequestQueue& programQueue;

    std::atomic<Instrument*> pendingSlot { nullptr };

    Instrument* current = nullptr;
    Instrument* tail = nullptr;
    int tailSilentSamples = 0;
    int tailMaxSilentSamples = 0;

    uint8_t owner[16][128] {};
    uint8_t sentNote[16][128] {};
    bool sustainDown[16] {};
    int bankMsb[16] {}, bankLsb[16] {};

    juce::MidiBuffer currentMidi, tailMidi;
    juce::AudioBuffer<float> scratch, tailScratch;
    juce::SmoothedValue<float> gainLeft, gainRight, revGain, dlyGain;

    std::atomic<float> peak { 0.0f };
    std::atomic<uint32_t> activity { 0 };
};

} // namespace km
