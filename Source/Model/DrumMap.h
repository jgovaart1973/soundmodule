#pragma once

#include <vector>

namespace km
{
/** One row of the drum editor: a General MIDI percussion note. */
struct DrumRow
{
    int note;
    const char* name;
};

/** The General MIDI drum map, in the order drummers think (kick at the bottom of the list, cymbals on top). */
inline const std::vector<DrumRow>& gmDrumRows()
{
    static const std::vector<DrumRow> rows {
        { 49, "Crash" },        { 57, "Crash 2" },     { 51, "Ride" },         { 53, "Ride bell" },
        { 46, "Open hihat" },   { 44, "Pedaal hihat" }, { 42, "Gesloten hihat" },
        { 50, "Hoge tom" },     { 48, "Hoge midtom" },  { 47, "Lage midtom" },  { 45, "Lage tom" },
        { 43, "Hoge floortom" }, { 41, "Lage floortom" },
        { 54, "Tamboerijn" },   { 56, "Cowbell" },      { 70, "Shaker" },       { 75, "Claves" },
        { 39, "Handklap" },     { 37, "Rimshot" },      { 40, "Snare 2" },      { 38, "Snare" },
        { 36, "Kick" },         { 35, "Kick 2" }
    };
    return rows;
}

inline const char* gmDrumName (int note)
{
    for (auto& r : gmDrumRows())
        if (r.note == note)
            return r.name;
    return nullptr;
}

} // namespace km
