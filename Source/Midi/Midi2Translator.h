#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace km
{
/**
    MIDI 2.0 (Universal MIDI Packets) -> the engine's MIDI 1.0 events (phase 3d).

    The engine and all sounds (built-in, SFZ, VST3, CLAP) speak MIDI 1.0 + MPE, so MIDI 2.0
    input is translated following the MIDI 2.0 translation rules:
      - 16-bit velocity, 32-bit controllers, pressure and pitch bend are scaled down
        (velocity never becomes 0, which would be a note-off);
      - Program Change with bank becomes CC0 / CC32 + Program Change;
      - RPN / NRPN become the CC 101/100 (99/98) + data entry sequence.
    Per-note expression (per-note pitch bend, poly pressure, per-note controllers) has no
    MIDI 1.0 equivalent on one channel. With 'perNoteAsMpe' every note gets its own member
    channel (2..16, MPE lower zone) and the expression is sent on that channel, so MPE-capable
    sounds respond per note. Channel-wide messages go to the MPE manager channel (1).

    Not thread-safe: one translator per input, used on that input's thread.
*/
class Midi2Translator
{
public:
    bool perNoteAsMpe = true;

    void reset() noexcept
    {
        memberOf = {};
        for (auto& c : channelNotes)
            c = 0;
        nextMember = 0;
    }

    static int wordsForType (uint32_t type) noexcept
    {
        static constexpr int sizes[16] = { 1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4 };
        return sizes[type & 0xf];
    }

    /** Translate a run of UMP words. emit (uint8_t b0, uint8_t b1, uint8_t b2, int size). */
    template <typename Emit>
    void translate (const uint32_t* words, size_t numWords, Emit&& emit)
    {
        size_t i = 0;
        while (i < numWords)
        {
            const uint32_t type = words[i] >> 28;
            const auto n = (size_t) wordsForType (type);
            if (i + n > numWords)
                break;
            if (type == 0x2)
                midi1 (words[i], emit);
            else if (type == 0x4)
                midi2 (words[i], words[i + 1], emit);
            i += n;
        }
    }

    // MIDI 2.0 -> 1.0 value scaling (MIDI 2.0 spec, "Translation" chapter)
    static uint8_t scale16to7 (uint32_t v) noexcept  { return (uint8_t) (v >> 9); }
    static uint8_t scale32to7 (uint32_t v) noexcept  { return (uint8_t) (v >> 25); }
    static uint16_t scale32to14 (uint32_t v) noexcept { return (uint16_t) (v >> 18); }

private:
    template <typename Emit>
    void midi1 (uint32_t w, Emit& emit)
    {
        const auto status = (uint8_t) ((w >> 16) & 0xff);
        if (status < 0x80 || status >= 0xf0)
            return;
        const int type = status & 0xf0;
        emit (status, (uint8_t) ((w >> 8) & 0x7f), (uint8_t) (w & 0x7f), (type == 0xc0 || type == 0xd0) ? 2 : 3);
    }

    template <typename Emit>
    void midi2 (uint32_t w0, uint32_t w1, Emit& emit)
    {
        const int status = (int) ((w0 >> 20) & 0xf);
        const int ch = (int) ((w0 >> 16) & 0xf);
        const auto idx1 = (uint8_t) ((w0 >> 8) & 0x7f), idx2 = (uint8_t) (w0 & 0x7f);
        // Channel-wide messages: the MPE manager channel when notes are spread over member channels.
        const int wideCh = perNoteAsMpe ? 0 : ch;

        switch (status)
        {
            case 0x9: // note on
            {
                uint8_t vel = scale16to7 (w1 >> 16);
                if (vel == 0)
                    vel = 1;
                const int out = noteChannel (ch, idx1, true);
                emit ((uint8_t) (0x90 | out), idx1, vel, 3);
                break;
            }
            case 0x8: // note off
            {
                const int out = noteChannel (ch, idx1, false);
                emit ((uint8_t) (0x80 | out), idx1, scale16to7 (w1 >> 16), 3);
                release (ch, idx1);
                break;
            }
            case 0xa: // poly pressure: per note
            {
                if (perNoteAsMpe)
                {
                    if (const int out = memberFor (ch, idx1); out >= 0)
                        emit ((uint8_t) (0xd0 | out), scale32to7 (w1), 0, 2);
                }
                else
                {
                    emit ((uint8_t) (0xa0 | ch), idx1, scale32to7 (w1), 3);
                }
                break;
            }
            case 0x6: // per-note pitch bend (same +-48 semitone default range as MPE member channels)
            {
                if (const int out = perNoteAsMpe ? memberFor (ch, idx1) : -1; out >= 0)
                {
                    const auto v = scale32to14 (w1);
                    emit ((uint8_t) (0xe0 | out), (uint8_t) (v & 0x7f), (uint8_t) (v >> 7), 3);
                }
                break;
            }
            case 0x0: // registered per-note controller
            case 0x1: // assignable per-note controller
            {
                if (! perNoteAsMpe)
                    break;
                const int out = memberFor (ch, idx1);
                if (out < 0)
                    break;
                // Assignable per-note controllers use CC numbers (74 = MPE timbre); registered #1 = modulation.
                int cc = -1;
                if (status == 0x1)        cc = idx2;
                else if (idx2 == 1)       cc = 1;
                else if (idx2 == 2)       cc = 2;
                else if (idx2 == 7)       cc = 7;
                else if (idx2 == 10)      cc = 10;
                if (cc >= 0 && cc < 120)
                    emit ((uint8_t) (0xb0 | out), (uint8_t) cc, scale32to7 (w1), 3);
                break;
            }
            case 0xb: // control change
                emit ((uint8_t) (0xb0 | wideCh), idx1, scale32to7 (w1), 3);
                break;
            case 0xc: // program change (+ bank)
                if ((w0 & 1u) != 0)
                {
                    emit ((uint8_t) (0xb0 | wideCh), 0, (uint8_t) ((w1 >> 8) & 0x7f), 3);
                    emit ((uint8_t) (0xb0 | wideCh), 32, (uint8_t) (w1 & 0x7f), 3);
                }
                emit ((uint8_t) (0xc0 | wideCh), (uint8_t) ((w1 >> 24) & 0x7f), 0, 2);
                break;
            case 0xd: // channel pressure
                emit ((uint8_t) (0xd0 | wideCh), scale32to7 (w1), 0, 2);
                break;
            case 0xe: // pitch bend
            {
                const auto v = scale32to14 (w1);
                emit ((uint8_t) (0xe0 | wideCh), (uint8_t) (v & 0x7f), (uint8_t) (v >> 7), 3);
                break;
            }
            case 0x2: // RPN
            case 0x3: // NRPN
            {
                const bool rpn = status == 0x2;
                emit ((uint8_t) (0xb0 | wideCh), rpn ? 101 : 99, idx1, 3);
                emit ((uint8_t) (0xb0 | wideCh), rpn ? 100 : 98, idx2, 3);
                emit ((uint8_t) (0xb0 | wideCh), 6, (uint8_t) (w1 >> 25), 3);
                emit ((uint8_t) (0xb0 | wideCh), 38, (uint8_t) ((w1 >> 18) & 0x7f), 3);
                break;
            }
            default:
                break; // per-note management, relative controllers: no MIDI 1.0 equivalent
        }
    }

    /** Output channel (0-based) for a note. Allocates a member channel on note-on in MPE mode. */
    int noteChannel (int ch, int note, bool noteOn) noexcept
    {
        if (! perNoteAsMpe)
            return ch;
        auto& slot = memberOf[(size_t) (ch * 128 + note)];
        if (noteOn)
        {
            if (slot != 0)
                --channelNotes[slot - 1];
            // Least busy member channel, round robin among equals: 1..15 (MIDI channels 2..16).
            int best = -1;
            for (int k = 0; k < 15; ++k)
            {
                const int c = 1 + (nextMember + k) % 15;
                if (best < 0 || channelNotes[(size_t) c] < channelNotes[(size_t) best])
                    best = c;
            }
            nextMember = best % 15;
            ++channelNotes[(size_t) best];
            slot = (uint8_t) (best + 1);
            return best;
        }
        return slot != 0 ? slot - 1 : 1;
    }

    int memberFor (int ch, int note) const noexcept
    {
        const auto slot = memberOf[(size_t) (ch * 128 + (note & 0x7f))];
        return slot != 0 ? slot - 1 : -1;
    }

    void release (int ch, int note) noexcept
    {
        if (! perNoteAsMpe)
            return;
        auto& slot = memberOf[(size_t) (ch * 128 + note)];
        if (slot != 0 && channelNotes[(size_t) (slot - 1)] > 0)
            --channelNotes[(size_t) (slot - 1)];
        slot = 0;
    }

    std::array<uint8_t, 16 * 128> memberOf {};   ///< (input channel, note) -> member channel + 1
    std::array<int, 16> channelNotes {};
    int nextMember = 0;
};

} // namespace km
