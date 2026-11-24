#pragma once

#include "Model/Song.h"

#include <algorithm>
#include <set>

/** Pure editing helpers for clips (unit-tested, no UI). Times in beats (quarter notes). */
namespace km::edit
{
struct GridOption
{
    const char* name;
    double beats;
};

/** Grid choices for the piano roll and quantize. */
inline const std::vector<GridOption>& gridOptions()
{
    static const std::vector<GridOption> options {
        { "1/4", 1.0 },        { "1/8", 0.5 },          { "1/16", 0.25 },     { "1/32", 0.125 },
        { "1/8 triool", 1.0 / 3.0 }, { "1/16 triool", 1.0 / 6.0 }, { "1/2", 2.0 },      { "1 maat (4/4)", 4.0 },
        { "1/4 triool", 2.0 / 3.0 }
    };
    return options;
}

inline double snapToGrid (double beat, double grid) { return grid > 0 ? std::round (beat / grid) * grid : beat; }
inline double floorToGrid (double beat, double grid) { return grid > 0 ? std::floor (beat / grid + 1.0e-9) * grid : beat; }

struct QuantizeSettings
{
    double grid = 0.25;
    double strength = 1.0;   ///< 0..1: how far notes move towards the grid
    double swing = 0.0;      ///< 0..1: delays every second grid position (1 = triplet feel)
    bool lengths = false;    ///< also quantize note ends
};

/** Where a beat should go on a (swung) grid. */
inline double gridTarget (double beat, double grid, double swing)
{
    const double k = std::round (beat / grid);
    double target = k * grid;
    if (std::fmod (std::abs (k), 2.0) > 0.5)        // odd grid position: the "and"
        target += juce::jlimit (0.0, 1.0, swing) * grid / 3.0;
    return target;
}

/** Quantize the given notes (empty selection = all). */
inline void quantize (std::vector<SongNote>& notes, const std::set<int>& selection, const QuantizeSettings& q)
{
    if (q.grid <= 0.0)
        return;

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        if (! selection.empty() && selection.count (i) == 0)
            continue;
        auto& n = notes[(size_t) i];
        const double end = n.start + n.length;
        const double newStart = n.start + (gridTarget (n.start, q.grid, q.swing) - n.start) * q.strength;
        double newEnd = end + (newStart - n.start);   // keep the length by default

        if (q.lengths)
        {
            const double quantEnd = juce::jmax (newStart + q.grid * 0.5, gridTarget (end, q.grid, 0.0));
            newEnd = end + (quantEnd - end) * q.strength;
        }

        n.start = juce::jmax (0.0, newStart);
        n.length = juce::jmax (0.01, newEnd - n.start);
    }
}

inline void transpose (std::vector<SongNote>& notes, const std::set<int>& selection, int semitones)
{
    for (int i = 0; i < (int) notes.size(); ++i)
        if (selection.empty() || selection.count (i) > 0)
            notes[(size_t) i].note = juce::jlimit (0, 127, notes[(size_t) i].note + semitones);
}

/** Identifies one controller lane: a CC number, or pitch bend / channel pressure. */
struct ControllerLane
{
    enum Kind { cc, pitchBend, channelPressure };
    Kind kind = cc;
    int number = 1;    ///< CC number for kind == cc
    const char* name = "Modulatie";

    bool matches (const SongControl& c) const noexcept
    {
        const int status = c.status & 0xf0;
        switch (kind)
        {
            case cc:              return status == 0xb0 && c.data1 == number;
            case pitchBend:       return status == 0xe0;
            case channelPressure: return status == 0xd0;
        }
        return false;
    }

    /** Value 0..1 of a control point (pitch bend: 0.5 = centre). */
    double valueOf (const SongControl& c) const noexcept
    {
        if (kind == pitchBend)
            return (double) (((int) c.data2 << 7) | c.data1) / 16383.0;
        if (kind == channelPressure)
            return c.data1 / 127.0;
        return c.data2 / 127.0;
    }

    SongControl make (double beat, double value, int channel) const noexcept
    {
        SongControl c;
        c.beat = beat;
        const auto ch = (uint8_t) (juce::jlimit (1, 16, channel) - 1);
        if (kind == pitchBend)
        {
            const int v = juce::jlimit (0, 16383, (int) std::lround (value * 16383.0));
            c.status = (uint8_t) (0xe0 | ch);
            c.data1 = (uint8_t) (v & 0x7f);
            c.data2 = (uint8_t) (v >> 7);
        }
        else if (kind == channelPressure)
        {
            c.status = (uint8_t) (0xd0 | ch);
            c.data1 = (uint8_t) juce::jlimit (0, 127, (int) std::lround (value * 127.0));
        }
        else
        {
            c.status = (uint8_t) (0xb0 | ch);
            c.data1 = (uint8_t) number;
            c.data2 = (uint8_t) juce::jlimit (0, 127, (int) std::lround (value * 127.0));
        }
        return c;
    }

    /** Pitch bend rests at the centre, everything else at zero. */
    double restValue() const noexcept { return kind == pitchBend ? 0.5 : 0.0; }
};

inline const std::vector<ControllerLane>& controllerLanes()
{
    static const std::vector<ControllerLane> lanes {
        { ControllerLane::cc, 1, "Modulatie (CC1)" },
        { ControllerLane::cc, 11, "Expressie (CC11)" },
        { ControllerLane::cc, 7, "Volume (CC7)" },
        { ControllerLane::cc, 64, "Sustain (CC64)" },
        { ControllerLane::pitchBend, 0, "Pitch bend" },
        { ControllerLane::channelPressure, 0, "Aftertouch" }
    };
    return lanes;
}

/** Replace a lane's points between from and to by a straight line (resolution in beats). */
inline void drawLine (std::vector<SongControl>& controls, const ControllerLane& lane, int channel,
                      double from, double to, double v0, double v1, double resolution = 1.0 / 32.0)
{
    if (to < from)
    {
        std::swap (from, to);
        std::swap (v0, v1);
    }

    controls.erase (std::remove_if (controls.begin(), controls.end(), [&] (const SongControl& c)
                                    { return lane.matches (c) && c.beat >= from - 1.0e-9 && c.beat <= to + 1.0e-9; }),
                    controls.end());

    const int steps = juce::jmax (1, (int) std::ceil ((to - from) / resolution));
    for (int i = 0; i <= steps; ++i)
    {
        const double t = (double) i / steps;
        controls.push_back (lane.make (from + (to - from) * t, v0 + (v1 - v0) * t, channel));
    }

    // Keep the list in time order (playback sorts again, the editor relies on it).
    std::stable_sort (controls.begin(), controls.end(), [] (const SongControl& a, const SongControl& b) { return a.beat < b.beat; });
}

/** Remove a lane's points in a range. */
inline void eraseRange (std::vector<SongControl>& controls, const ControllerLane& lane, double from, double to)
{
    if (to < from)
        std::swap (from, to);
    controls.erase (std::remove_if (controls.begin(), controls.end(), [&] (const SongControl& c)
                                    { return lane.matches (c) && c.beat >= from && c.beat <= to; }),
                    controls.end());
}

} // namespace km::edit
