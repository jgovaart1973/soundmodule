#pragma once

#include "Model/Song.h"

#include <array>
#include <vector>

/** Chord names, chord recognition and voicing (pure functions, unit-tested). */
namespace km::chords
{
struct ChordType
{
    const char* suffix;
    std::vector<int> intervals;   ///< semitones above the root
};

inline const std::vector<ChordType>& types()
{
    static const std::vector<ChordType> t {
        { "",     { 0, 4, 7 } },      { "m",    { 0, 3, 7 } },      { "7",    { 0, 4, 7, 10 } },
        { "maj7", { 0, 4, 7, 11 } },  { "m7",   { 0, 3, 7, 10 } },  { "dim",  { 0, 3, 6 } },
        { "aug",  { 0, 4, 8 } },      { "sus2", { 0, 2, 7 } },      { "sus4", { 0, 5, 7 } },
        { "6",    { 0, 4, 7, 9 } },   { "m6",   { 0, 3, 7, 9 } },   { "add9", { 0, 2, 4, 7 } },
        { "m7b5", { 0, 3, 6, 10 } },  { "dim7", { 0, 3, 6, 9 } }
    };
    return t;
}

inline const char* rootName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}

inline juce::String name (const ChordEvent& c)
{
    return juce::String (rootName (c.root)) + types()[(size_t) juce::jlimit (0, (int) types().size() - 1, c.type)].suffix;
}

struct Match
{
    int root = 0, type = 0;
    double score = -1.0;
};

/** Best chord for a set of pitch-class weights (how long/loud each of the 12 tones sounded). */
inline Match detect (const std::array<double, 12>& w, int bassPc = -1)
{
    double total = 0.0;
    for (auto x : w)
        total += x;

    Match best;
    if (total <= 0.0)
        return best;

    for (int root = 0; root < 12; ++root)
    {
        for (int t = 0; t < (int) types().size(); ++t)
        {
            const auto& iv = types()[(size_t) t].intervals;
            bool in[12] {};
            double matched = 0.0;
            int missing = 0;
            for (int i : iv)
            {
                const int pc = (root + i) % 12;
                in[pc] = true;
                matched += w[(size_t) pc];
                if (w[(size_t) pc] < total * 0.02)
                    ++missing;
            }
            double outside = 0.0;
            for (int pc = 0; pc < 12; ++pc)
                if (! in[pc])
                    outside += w[(size_t) pc];

            double score = matched - 0.8 * outside - 0.35 * missing * total / (double) iv.size();
            if (w[(size_t) root] < total * 0.02)
                score -= 0.3 * total;                    // the root itself should sound
            if (root == bassPc)
                score += 0.15 * total;                   // the lowest note is usually the root
            score -= 0.01 * total * (double) iv.size();  // prefer the simpler chord on a tie

            if (score > best.score)
                best = { root, t, score };
        }
    }
    return best;
}

/** Recognise chords in a clip: one per bar, or two when the harmony clearly changes halfway the bar.
    Consecutive equal chords are merged. Beats in song time. */
inline std::vector<ChordEvent> detectInClip (const Clip& clip, double barLength)
{
    std::vector<ChordEvent> result;
    if (clip.notes.empty())
        return result;

    struct Found { Match m; double quality = -1.0; };
    auto analyse = [&clip] (double from0, double to0)
    {
        std::array<double, 12> w {};
        int bass = 128;
        for (auto& n : clip.notes)
        {
            const double from = juce::jmax (from0, n.start), to = juce::jmin (to0, n.start + n.length);
            if (to <= from)
                continue;
            w[(size_t) (n.note % 12)] += (to - from) * (0.5 + n.velocity / 254.0);
            // the lowest note that starts (or sounds at the start) of this stretch counts as bass
            bass = juce::jmin (bass, n.note);
        }
        double total = 0.0;
        for (auto x : w)
            total += x;
        Found f;
        f.m = detect (w, bass < 128 ? bass % 12 : -1);
        f.quality = total > 0.0 ? f.m.score / total : -1.0;
        return f;
    };
    auto add = [&result] (double at, double length, const Match& m)
    {
        if (! result.empty() && result.back().root == m.root && result.back().type == m.type
            && std::abs (result.back().beat + result.back().length - at) < 1.0e-6)
            result.back().length += length;
        else
            result.push_back ({ at, length, m.root, m.type });
    };

    for (double bar = 0.0; bar < clip.length - 1.0e-6; bar += barLength)
    {
        const auto whole = analyse (bar, bar + barLength);
        if (whole.m.score < 0.0)
            continue;

        // Two chords in one bar? Only when both halves are clear chords that fit better than one chord.
        if (barLength >= 2.0 - 1.0e-6)
        {
            const double half = barLength * 0.5;
            const auto a = analyse (bar, bar + half), b = analyse (bar + half, bar + barLength);
            const bool different = a.m.root != b.m.root || a.m.type != b.m.type;
            if (different && a.quality > 0.55 && b.quality > 0.55 && (a.quality + b.quality) * 0.5 > whole.quality + 0.12)
            {
                add (clip.start + bar, half, a.m);
                add (clip.start + bar + half, half, b.m);
                continue;
            }
        }
        add (clip.start + bar, barLength, whole.m);
    }
    return result;
}

/** Close-position voicing with the root between C4 and B4 (MIDI 60..71). */
inline std::vector<int> voicing (int root, int type)
{
    std::vector<int> notes;
    const int base = 60 + ((root % 12) + 12) % 12;
    for (int i : types()[(size_t) juce::jlimit (0, (int) types().size() - 1, type)].intervals)
        notes.push_back (base + i);
    // Keep it in a comfortable range: move notes above A4 down an octave.
    for (auto& n : notes)
        if (n > 69 + 4)
            n -= 12;
    std::sort (notes.begin(), notes.end());
    return notes;
}

/** Block chords (and optionally a bass line on the root) for a list of chords. Clip-relative to clipStart. */
inline std::vector<SongNote> accompaniment (const std::vector<ChordEvent>& chords, double clipStart, bool bass, bool chordsToo = true)
{
    std::vector<SongNote> notes;
    for (auto& c : chords)
    {
        const double start = c.beat - clipStart;
        if (chordsToo)
            for (int n : voicing (c.root, c.type))
                notes.push_back ({ start, c.length * 0.98, n, 90, 1 });
        if (bass)
            notes.push_back ({ start, c.length * 0.98, 36 + ((c.root % 12) + 12) % 12, 100, 1 });
    }
    return notes;
}

} // namespace km::chords
