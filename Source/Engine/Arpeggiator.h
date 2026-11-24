#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace km
{
/**
    Arpeggiator for one part (audio thread, no allocation).

    Held notes are played one at a time (or as repeated chords) on a beat grid. With the transport
    running the steps lock to the song's beats; otherwise it runs freely at the current tempo,
    starting the moment you press the first key. With latch on, the chord keeps playing after the
    keys are released, until you play a new chord.
*/
class Arpeggiator
{
public:
    enum class Mode : int { up = 0, down, upDown, random, asPlayed, chord };

    struct Settings
    {
        Mode mode = Mode::up;
        double stepBeats = 0.5;   ///< 0.5 = eighth notes
        int octaves = 1;          ///< 1..4
        double gate = 0.6;        ///< note length as part of a step (0.05..1)
        bool latch = false;
    };

    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray names { "Op", "Neer", "Op-neer", "Willekeurig", "Zoals gespeeld", "Akkoord" };
        return names;
    }

    struct Rate { const char* name; double beats; };
    static const std::array<Rate, 6>& rates()
    {
        static const std::array<Rate, 6> r { { { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 },
                                               { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 } } };
        return r;
    }

    //==========================================================================
    void noteOn (int note, int velocity, int channel, bool latch) noexcept
    {
        if (keysDown == 0 && latch)
            numNotes = 0;                    // a new chord replaces the latched one
        ++keysDown;

        for (int i = 0; i < numNotes; ++i)
            if (notes[(size_t) i].note == note)
                return;
        if (numNotes < (int) notes.size())
            notes[(size_t) numNotes++] = { note, velocity, channel, orderCounter++ };
    }

    void noteOff (int note, bool latch) noexcept
    {
        keysDown = juce::jmax (0, keysDown - 1);
        if (latch)
            return;
        for (int i = 0; i < numNotes; ++i)
            if (notes[(size_t) i].note == note)
            {
                notes[(size_t) i] = notes[(size_t) --numNotes];
                break;
            }
    }

    bool isHeld (int note) const noexcept
    {
        for (int i = 0; i < numNotes; ++i)
            if (notes[(size_t) i].note == note)
                return true;
        return false;
    }

    bool hasNotes() const noexcept   { return numNotes > 0; }
    bool isSounding() const noexcept { return numSounding > 0; }

    /** Generate this block's notes. emit (const uint8_t* data, int size, int samplePos). */
    template <typename Emit>
    void process (const Settings& s, bool synced, double blockStartBeat, double beatsPerSample, int n, Emit&& emit) noexcept
    {
        if (numNotes == 0 && numSounding == 0)
        {
            started = false;
            return;
        }
        if (beatsPerSample <= 0.0 || n <= 0)
            return;

        const double step = juce::jmax (1.0 / 64.0, s.stepBeats);
        const double beat0 = synced ? blockStartBeat : (started ? freeBeat : 0.0);
        const double end = beat0 + n * beatsPerSample;

        if (! started)
        {
            // Synced: wait for the next grid step. Free: start right now.
            nextStep = synced ? std::ceil (beat0 / step - 1.0e-9) * step : beat0;
            stepIndex = 0;
            started = true;
        }
        else if (synced && (nextStep < beat0 - step || nextStep > beat0 + step))
        {
            nextStep = std::ceil (beat0 / step - 1.0e-9) * step; // the transport jumped
        }

        auto posOf = [&] (double beat) { return juce::jlimit (0, n - 1, (int) ((beat - beat0) / beatsPerSample)); };

        for (;;)
        {
            const double offTime = numSounding > 0 ? offAtBeat : std::numeric_limits<double>::max();
            if (juce::jmin (nextStep, offTime) >= end)
                break;

            if (offTime <= nextStep)
            {
                releaseSounding (posOf (offTime), emit);
                continue;
            }

            const int pos = posOf (nextStep);
            releaseSounding (pos, emit);

            if (numNotes > 0)
            {
                int seqNotes[256], seqVel[256], seqCh[256];
                const int len = buildSequence (s, seqNotes, seqVel, seqCh);

                auto play = [&] (int note, int vel, int ch)
                {
                    const uint8_t on[3] = { (uint8_t) (0x90 | (ch - 1)), (uint8_t) note, (uint8_t) juce::jlimit (1, 127, vel) };
                    emit (on, 3, pos);
                    if (numSounding < (int) sounding.size())
                        sounding[(size_t) numSounding++] = { note, vel, ch, 0 };
                };

                if (s.mode == Mode::chord)
                {
                    for (int i = 0; i < len; ++i)
                        play (seqNotes[i], seqVel[i], seqCh[i]);
                }
                else if (len > 0)
                {
                    rng = rng * 1664525u + 1013904223u;
                    const int idx = s.mode == Mode::random ? (int) ((rng >> 8) % (uint32_t) len) : stepIndex % len;
                    play (seqNotes[idx], seqVel[idx], seqCh[idx]);
                }
                offAtBeat = nextStep + step * juce::jlimit (0.05, 1.0, s.gate);
            }

            ++stepIndex;
            nextStep += step;
        }

        freeBeat = end;
        if (numNotes == 0 && numSounding == 0)
            started = false;
    }

    /** End the notes that are sounding now (e.g. before the sound is switched), keep the held chord. */
    template <typename Emit>
    void releaseNow (int pos, Emit&& emit) noexcept { releaseSounding (pos, emit); }

    /** Stop immediately (panic, arpeggiator switched off). */
    template <typename Emit>
    void stopAll (int pos, Emit&& emit) noexcept
    {
        releaseSounding (pos, emit);
        numNotes = 0;
        keysDown = 0;
        started = false;
    }

private:
    struct Held { int note, velocity, channel, order; };

    template <typename Emit>
    void releaseSounding (int pos, Emit& emit) noexcept
    {
        for (int i = 0; i < numSounding; ++i)
        {
            const uint8_t off[3] = { (uint8_t) (0x80 | (sounding[(size_t) i].channel - 1)), (uint8_t) sounding[(size_t) i].note, 0 };
            emit (off, 3, pos);
        }
        numSounding = 0;
    }

    /** The order the notes are played in, expanded over the octaves. */
    int buildSequence (const Settings& s, int* outNotes, int* outVel, int* outCh) const noexcept
    {
        std::array<Held, 32> base {};
        std::copy (notes.begin(), notes.begin() + numNotes, base.begin());
        if (s.mode == Mode::asPlayed)
            std::sort (base.begin(), base.begin() + numNotes, [] (const Held& a, const Held& b) { return a.order < b.order; });
        else
            std::sort (base.begin(), base.begin() + numNotes, [] (const Held& a, const Held& b) { return a.note < b.note; });

        int len = 0;
        const int octaves = juce::jlimit (1, 4, s.octaves);
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < numNotes; ++i)
            {
                const int note = base[(size_t) i].note + 12 * o;
                if (note > 127 || len >= 128)
                    continue;
                outNotes[len] = note;
                outVel[len] = base[(size_t) i].velocity;
                outCh[len] = base[(size_t) i].channel;
                ++len;
            }

        if (s.mode == Mode::down)
        {
            std::reverse (outNotes, outNotes + len);
            std::reverse (outVel, outVel + len);
            std::reverse (outCh, outCh + len);
        }
        else if (s.mode == Mode::upDown && len > 2)
        {
            // 1 2 3 4 3 2 | 1 2 3 4 3 2 ...
            for (int i = len - 2; i >= 1; --i)
            {
                outNotes[len + (len - 2 - i)] = outNotes[i];
                outVel[len + (len - 2 - i)] = outVel[i];
                outCh[len + (len - 2 - i)] = outCh[i];
            }
            len = len + (len - 2);
        }
        return len;
    }

    std::array<Held, 32> notes {};
    int numNotes = 0, keysDown = 0, orderCounter = 0;
    std::array<Held, 128> sounding {};
    int numSounding = 0;
    double offAtBeat = 0.0, freeBeat = 0.0, nextStep = 0.0;
    bool started = false;
    int stepIndex = 0;
    uint32_t rng = 0x9e3779b9u;
};

} // namespace km
