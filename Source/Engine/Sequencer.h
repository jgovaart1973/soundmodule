#pragma once

#include "Core/SpscQueue.h"
#include "Midi/MidiEvent.h"
#include "Model/Song.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <vector>

namespace km
{
/** One playback event, already flattened from the song. */
struct SeqEvent
{
    double beat = 0.0;
    int8_t part = 0;
    uint8_t size = 3;
    uint8_t data[3] {};
};

/** Immutable, sorted event list the audio thread plays from. Rebuilt on the message thread on every edit. */
struct PlaybackSnapshot
{
    std::vector<SeqEvent> events;
    static std::unique_ptr<PlaybackSnapshot> build (const Song& song);
};

/** Something a part received from a keyboard, time-stamped for recording. */
struct RecordedEvent
{
    int8_t part = 0;
    uint8_t size = 3;
    uint8_t data[3] {};
    int64_t sampleTime = 0;   ///< engine sample counter (for retrospective capture)
    double beat = -1.0;       ///< song position, or -1 when the transport was not rolling
    int loopPass = 0;
    bool recording = false;   ///< record was on and the transport rolling
};

/**
    Transport + playback + recording tap + metronome. Lives inside the ModuleEngine.

    The message thread steers it through atomics (play/stop/record/locate, tempo, loop, metronome)
    and hands over new PlaybackSnapshots; the audio thread never allocates or locks.
*/
class Sequencer
{
public:
    Sequencer();
    ~Sequencer();

    //== message thread: settings =============================================
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> beatsPerBar { 4 }, beatUnit { 4 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 16.0 };
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeLevel { 0.5f };
    std::atomic<int> countInBars { 1 };

    //== message thread: commands =============================================
    void play() noexcept;
    void stop() noexcept;
    void record (bool withCountIn) noexcept;   ///< start recording (punch in when already playing)
    void stopRecording() noexcept;              ///< keep playing, stop recording (punch out)
    void locate (double beat) noexcept;

    /** Give a new snapshot to the audio thread (takes ownership). */
    void setSnapshot (std::unique_ptr<PlaybackSnapshot> snapshot);
    /** Delete snapshots the audio thread no longer uses. */
    void collectGarbage();

    //== any thread: state ====================================================
    double getPosition() const noexcept   { return position.load (std::memory_order_relaxed); }
    bool isPlaying() const noexcept       { return mode.load() == Mode::playing; }
    bool isCountingIn() const noexcept    { return mode.load() == Mode::countIn; }
    bool isRolling() const noexcept       { return mode.load() != Mode::stopped; }
    bool isRecording() const noexcept     { return recording.load(); }
    int getLoopPass() const noexcept      { return loopPass.load(); }
    double getCountInRemaining() const noexcept { return countInLeft.load(); }
    int64_t getSampleCounter() const noexcept   { return sampleCounter.load (std::memory_order_relaxed); }
    double getSampleRate() const noexcept { return sampleRate; }

    SpscQueue<RecordedEvent, 8192> recordQueue;   ///< audio -> message thread

    //== audio thread =========================================================
    void prepare (double sampleRate, int maxBlockSize);

    /** Start of block: handle commands and write this block's playback events. @returns count. */
    int generate (int numSamples, MidiEvent* out, int* positions, int maxEvents) noexcept;

    /** Called by parts for every live event they accept. */
    void capture (int part, const uint8_t* data, int size, int samplePos) noexcept;

    /** Metronome / count-in clicks into the bus. */
    void renderClicks (juce::AudioBuffer<float>& bus, int numSamples) noexcept;

    /** End of block. */
    void advance (int numSamples) noexcept;

    /** Panic: forget which notes are sounding. */
    void resetSounding() noexcept;

private:
    enum class Mode : int { stopped = 0, countIn, playing };
    enum Command : int { cmdPlay = 1, cmdStop = 2, cmdRecord = 4, cmdCountIn = 8, cmdStopRecording = 16 };

    int emitRange (double from, double to, double blockStartBeat, int posOffset, double beatsPerSample,
                   int numSamples, MidiEvent* out, int* positions, int count, int maxEvents) noexcept;
    int emitAllNotesOff (int pos, MidiEvent* out, int* positions, int count, int maxEvents) noexcept;
    void startClick (int pos, bool accent) noexcept;

    std::atomic<int> commands { 0 };
    std::atomic<double> locateRequest { -1.0 };
    std::atomic<PlaybackSnapshot*> pendingSnapshot { nullptr };
    PlaybackSnapshot* current = nullptr;
    SpscQueue<PlaybackSnapshot*, 64> retired;
    std::vector<std::unique_ptr<PlaybackSnapshot>> owned; // message thread: everything ever handed over and not yet freed

    std::atomic<Mode> mode { Mode::stopped };
    std::atomic<bool> recording { false };
    std::atomic<double> position { 0.0 };
    std::atomic<int> loopPass { 0 };
    std::atomic<double> countInLeft { 0.0 };
    std::atomic<int64_t> sampleCounter { 0 };

    double sampleRate = 48000.0;

    // Block mapping (audio thread), used by capture() and renderClicks()
    double blockStartBeat = 0.0, blockBeatsPerSample = 0.0;
    int wrapAt = -1;                 ///< sample index where the loop wrapped in this block, -1 = none
    double wrapBeat = 0.0;           ///< beat at wrapAt
    bool blockRolling = false, blockCountIn = false;
    double countInBeat = 0.0;        ///< beats elapsed in the count-in

    uint8_t sounding[kNumParts][16][128] {};

    // Click synth
    struct ClickStart { int pos; bool accent; };
    static constexpr int maxClickStarts = 16;
    ClickStart clickStarts[maxClickStarts] {};
    int numClickStarts = 0;
    int clickRemaining = 0, clickLength = 0;
    float clickFreq = 1000.0f;
    double clickPhase = 0.0;

    JUCE_DECLARE_NON_COPYABLE (Sequencer)
};

} // namespace km
