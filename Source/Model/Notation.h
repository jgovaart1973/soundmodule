#pragma once

#include "Model/Song.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

/**
    Notation layout (phase 3c): turns the notes of a clip into bars of notes, chords and rests
    on a grand staff, with note values, ties, dots, accidentals and beam groups. Pure functions,
    so they are unit-tested; ScoreView only draws the result.
*/
namespace km::notation
{
constexpr int noAccidental = 99;

/** Diatonic step: C4 (MIDI 60) = 28, D4 = 29 ... (octave * 7 + letter). */
struct Spelling
{
    int step = 28;
    int accidental = 0;   ///< -1 flat, 0 natural, +1 sharp
};

/** -1, 0 or +1: what the key signature does to a letter (0 = C ... 6 = B). keyFifths -7..7. */
inline int keyAccidental (int letter, int keyFifths) noexcept
{
    static constexpr int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 }; // F C G D A E B
    static constexpr int flatOrder[7] = { 6, 2, 5, 1, 4, 0, 3 };  // B E A D G C F
    for (int i = 0; i < std::min (7, std::abs (keyFifths)); ++i)
        if ((keyFifths > 0 ? sharpOrder[i] : flatOrder[i]) == letter)
            return keyFifths > 0 ? 1 : -1;
    return 0;
}

/** Spell a MIDI note in a key: sharps in sharp keys and C major, flats in flat keys. */
inline Spelling spell (int note, int keyFifths) noexcept
{
    static constexpr int sharpLetter[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static constexpr int sharpAcc[12]    = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    static constexpr int flatLetter[12]  = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    static constexpr int flatAcc[12]     = { 0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0 };
    const int pc = ((note % 12) + 12) % 12;
    const int octave = note / 12 - 1;
    const bool flats = keyFifths < 0;
    const int letter = flats ? flatLetter[pc] : sharpLetter[pc];
    int acc = flats ? flatAcc[pc] : sharpAcc[pc];

    // Keys with many accidentals: B# / E# / Cb / Fb where the key asks for it.
    if (keyFifths >= 6 && pc == 5) return { octave * 7 + 2, 1 };          // E#
    if (keyFifths >= 7 && pc == 0) return { (octave - 1) * 7 + 6, 1 };    // B#
    if (keyFifths <= -6 && pc == 11) return { (octave + 1) * 7 + 0, -1 };  // Cb
    if (keyFifths <= -7 && pc == 4) return { octave * 7 + 3, -1 };         // Fb
    return { octave * 7 + letter, acc };
}

/** Name of a key signature, e.g. "G majeur / E mineur". */
inline juce::String keyName (int fifths)
{
    static const char* major[15] = { "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#" };
    static const char* minor[15] = { "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#" };
    const int i = std::clamp (fifths, -7, 7) + 7;
    return juce::String (major[i]) + " majeur / " + minor[i] + " mineur";
}

/** Guess the key from the notes (Krumhansl-Kessler profiles, duration weighted). Returns fifths. */
inline int guessKey (const std::vector<SongNote>& notes)
{
    static constexpr double majorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double minorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    std::array<double, 12> w {};
    for (auto& n : notes)
        w[(size_t) (((n.note % 12) + 12) % 12)] += std::max (0.1, n.length);
    double total = 0.0;
    for (double x : w)
        total += x;
    if (total <= 0.0)
        return 0;

    auto correlate = [&] (const double* profile, int tonic)
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i) { mx += w[(size_t) i]; my += profile[i]; }
        mx /= 12.0; my /= 12.0;
        double num = 0.0, dx = 0.0, dy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = w[(size_t) ((i + tonic) % 12)] - mx, y = profile[i] - my;
            num += x * y; dx += x * x; dy += y * y;
        }
        return dx > 0.0 && dy > 0.0 ? num / std::sqrt (dx * dy) : 0.0;
    };

    // Tonic pitch class -> fifths (major); minor tonic = relative major + 3 semitones down.
    static constexpr int fifthsOfMajor[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    double best = -2.0;
    int bestFifths = 0;
    for (int t = 0; t < 12; ++t)
    {
        const double maj = correlate (majorProfile, t), min = correlate (minorProfile, t);
        if (maj > best) { best = maj; bestFifths = fifthsOfMajor[t]; }
        if (min > best) { best = min; bestFifths = fifthsOfMajor[(t + 3) % 12]; }
    }
    return bestFifths;
}

//==============================================================================
/** A written note value: 1 = whole, 2 = half, 4 = quarter, 8, 16, 32. */
struct Value
{
    int denominator = 4;
    bool dotted = false;
    double beats() const noexcept { return (4.0 / denominator) * (dotted ? 1.5 : 1.0); }
};

/** Written values that add up to 'length' quarter notes starting at 'pos' in a bar (pos 0 = downbeat).
    Long values only start where they are easy to read (half notes on a half bar, etc.). */
inline std::vector<Value> splitValues (double pos, double length, double barLength, double beatLength)
{
    static const Value candidates[] = { { 1, false }, { 2, true }, { 2, false }, { 4, true }, { 4, false },
                                        { 8, true },  { 8, false }, { 16, true }, { 16, false }, { 32, false } };
    std::vector<Value> out;
    const double eps = 1.0e-6;
    int guard = 0;
    while (length > 0.125 - eps && ++guard < 64)
    {
        bool placed = false;
        for (auto v : candidates)
        {
            const double b = v.beats();
            if (b > length + eps)
                continue;
            if (v.denominator == 1 && (std::abs (pos) > eps || std::abs (barLength - 4.0) > eps))
                continue; // whole note only as a full 4/4 bar
            const double base = 4.0 / v.denominator; // undotted value
            double align = std::min (base, beatLength);
            if (base >= 2.0)
                align = std::fmod (barLength, 2.0) < eps ? 2.0 : beatLength;
            if (v.dotted && base >= 1.0)
                align = beatLength;
            const double r = std::fmod (pos + eps, align);
            if (r > 2 * eps)
                continue;
            out.push_back (v);
            pos += b;
            length -= b;
            placed = true;
            break;
        }
        if (! placed)
        {
            out.push_back ({ 32, false }); // finer than the grid: round to a 32nd
            pos += 0.125;
            length -= 0.125;
        }
    }
    return out;
}

struct Head
{
    int note = 60;
    int step = 28;
    int accidental = noAccidental;   ///< what to print: -1 / 0 (natural) / +1, or noAccidental
    bool tieToNext = false;
};

struct Item
{
    double start = 0.0;          ///< quarter notes from the start of the clip (real time, also inside triplets)
    Value value;                 ///< written value (inside a triplet: the value under the "3")
    bool rest = true;
    std::vector<Head> heads;     ///< sorted low to high
    int voice = 0;               ///< 0 = upper voice (stems up when two voices share a staff), 1 = lower voice
    int beamGroup = -1;          ///< items with the same id (>= 0) in a bar + staff share a beam
    int tuplet = -1;             ///< index into Bar::tuplets[staff], -1 = not in a triplet
};

/** Three notes in the time of two. */
struct Tuplet
{
    int voice = 0;
    double start = 0.0, end = 0.0;   ///< real time
    int actual = 3, normal = 2;
};

struct Bar
{
    double start = 0.0;
    std::array<std::vector<Item>, 2> staves;       ///< 0 = treble, 1 = bass; sorted by start, then voice
    std::array<std::vector<Tuplet>, 2> tuplets;
    std::array<bool, 2> twoVoices { false, false }; ///< the lower voice has notes in this bar
};

struct Layout
{
    int keyFifths = 0;
    double barLength = 4.0, beatLength = 1.0;
    std::vector<Bar> bars;
};

/** Staff position in half spaces above the bottom line (treble: E4, bass: G2). */
inline int staffPosition (int step, int staff) noexcept { return step - (staff == 0 ? 30 : 18); }

//==============================================================================
namespace detail
{
    constexpr double eps = 1.0e-6;
    inline long key48 (double t) { return std::lround (t * 48.0); }

    /**
        Split the notes of one staff into an upper and a lower voice. A note goes to the upper voice
        when it starts while that voice is free or when it is higher than what the upper voice holds;
        notes of an onset that end at a different time than the top note go to the lower voice.
        Chords whose notes end (nearly) together stay one chord.
    */
    inline std::array<std::vector<SongNote>, 2> splitVoices (std::vector<SongNote> notes, double grid)
    {
        std::array<std::vector<SongNote>, 2> v;
        std::sort (notes.begin(), notes.end(), [] (const SongNote& a, const SongNote& b)
        {
            return std::abs (a.start - b.start) > 1.0e-9 ? a.start < b.start : a.note > b.note;
        });
        const double tol = std::max (0.13, grid * 1.01);
        double upperEnd = -1.0e9;
        int upperTop = -1;
        size_t i = 0;
        while (i < notes.size())
        {
            size_t j = i;
            while (j < notes.size() && std::abs (notes[j].start - notes[i].start) < grid * 0.5)
                ++j;
            const auto& top = notes[i];
            const double topEnd = top.start + top.length;
            const bool upperFree = top.start >= upperEnd - tol;
            if (upperFree || top.note > upperTop)
            {
                for (size_t k = i; k < j; ++k)
                {
                    const double end = notes[k].start + notes[k].length;
                    (std::abs (end - topEnd) <= tol ? v[0] : v[1]).push_back (notes[k]);
                }
                upperEnd = topEnd;
                upperTop = top.note;
            }
            else
            {
                for (size_t k = i; k < j; ++k)
                    v[1].push_back (notes[k]);
            }
            i = j;
        }
        return v;
    }

    /** A stretch of time with one grid: plain (duple) or a triplet (3 in the time of 2). */
    struct Cell
    {
        double start = 0.0, end = 0.0;
        double grid = 0.25;
        bool triplet = false;
    };

    inline double gridError (const std::vector<double>& times, double a, double b, double q)
    {
        double e = 0.0;
        for (double t : times)
            if (t >= a - eps && t < b - eps)
                e += std::abs ((t - a) - std::round ((t - a) / q) * q);
        return e;
    }

    inline int countIn (const std::vector<double>& times, double a, double b)
    {
        int n = 0;
        for (double t : times)
            if (t > a + eps && t < b - eps)
                ++n;
        return n;
    }

    /** Per beat (and per half bar for quarter-note triplets): is it written straight or as a triplet? */
    inline std::vector<Cell> findCells (const std::vector<SongNote>& notes, int numBars, double barLength,
                                        double beatLength, double grid)
    {
        std::vector<double> times;
        for (auto& n : notes)
        {
            times.push_back (n.start);
            times.push_back (n.start + n.length);
        }
        std::vector<Cell> cells;
        const bool simpleMeter = std::abs (beatLength - 1.0) < eps;   // triplets in x/4 meters
        for (int b = 0; b < numBars; ++b)
        {
            const double bs = b * barLength;
            const int beats = (int) std::floor (barLength / beatLength + eps);
            int k = 0;
            while (k < beats)
            {
                const double a = bs + k * beatLength;
                if (simpleMeter && grid <= 0.5 + eps)
                {
                    // Quarter-note triplet over two beats
                    if (k + 1 < beats)
                    {
                        const double e3 = gridError (times, a, a + 2.0, 2.0 / 3.0);
                        const double ed = gridError (times, a, a + 2.0, grid);
                        if (countIn (times, a, a + 2.0) > 0 && e3 < 0.02 * (1 + countIn (times, a, a + 2.0)) && e3 + 0.05 < ed)
                        {
                            cells.push_back ({ a, a + 2.0, 2.0 / 3.0, true });
                            k += 2;
                            continue;
                        }
                    }
                    const double ed = gridError (times, a, a + 1.0, grid);
                    const double e8 = gridError (times, a, a + 1.0, 1.0 / 3.0);
                    const double e16 = grid <= 0.25 + eps ? gridError (times, a, a + 1.0, 1.0 / 6.0) : 1.0e9;
                    if (countIn (times, a, a + 1.0) > 0 && std::min (e8, e16) + 0.04 < ed)
                    {
                        cells.push_back ({ a, a + 1.0, e8 <= e16 + 0.01 ? 1.0 / 3.0 : 1.0 / 6.0, true });
                        ++k;
                        continue;
                    }
                }
                cells.push_back ({ a, a + beatLength, grid, false });
                ++k;
            }
            const double covered = bs + beats * beatLength;
            if (covered < bs + barLength - eps)
                cells.push_back ({ covered, bs + barLength, grid, false });
        }
        return cells;
    }

    inline const Cell& cellAt (const std::vector<Cell>& cells, double t)
    {
        for (auto& c : cells)
            if (t >= c.start - eps && t < c.end - eps)
                return c;
        return cells.back();
    }

    inline double quantize (const std::vector<Cell>& cells, double t, double total)
    {
        if (t >= total - eps)
            return total;
        const auto& c = cellAt (cells, std::max (0.0, t));
        return std::min (c.end, c.start + std::round ((t - c.start) / c.grid) * c.grid);
    }
} // namespace detail

/**
    @param notes       clip-relative notes
    @param clipLength  quarter notes
    @param barLength   quarter notes per bar (Song::beatsPerBarAsQuarters)
    @param beatLength  quarter notes per counted beat (1 in x/4, 1.5 in 6/8, 0.5 in 5/8 ...)
    @param grid        display grid for straight notes (does not change the song); triplets are found
                       automatically per beat (eighth / sixteenth triplets) or per half bar (quarter triplets)
    @param split       notes >= split go to the treble staff
    @param maxVoices   1 or 2 voices per staff
*/
inline Layout layout (const std::vector<SongNote>& notes, double clipLength, double barLength, double beatLength,
                      int keyFifths, double grid = 0.25, int split = 60, int maxVoices = 2)
{
    using namespace detail;
    Layout L;
    L.keyFifths = keyFifths;
    L.barLength = std::max (0.25, barLength);
    L.beatLength = std::max (0.125, beatLength);
    grid = std::max (0.125, grid);

    double end = clipLength;
    for (auto& n : notes)
        end = std::max (end, n.start + n.length);
    const int numBars = std::max (1, (int) std::ceil (end / L.barLength - 1.0e-6));
    const double total = numBars * L.barLength;
    L.bars.resize ((size_t) numBars);
    for (int b = 0; b < numBars; ++b)
        L.bars[(size_t) b].start = b * L.barLength;

    for (int staff = 0; staff < 2; ++staff)
    {
        std::vector<SongNote> staffNotes;
        for (auto& n : notes)
            if ((n.note >= split) == (staff == 0) && n.start >= 0.0 && n.start < total)
                staffNotes.push_back (n);

        auto voices = maxVoices > 1 ? splitVoices (staffNotes, grid) : std::array<std::vector<SongNote>, 2> { staffNotes, {} };
        if (voices[0].empty() && ! voices[1].empty())
            std::swap (voices[0], voices[1]);

        // Bars in which the lower voice really has notes
        std::vector<bool> lowerVoiceIn ((size_t) numBars, false);
        for (auto& n : voices[1])
        {
            const int first = std::min (numBars - 1, (int) std::floor (n.start / L.barLength + 1.0e-9));
            const int last = std::min (numBars - 1, (int) std::floor ((n.start + n.length) / L.barLength - 1.0e-6));
            for (int b = first; b <= std::max (first, last); ++b)
                lowerVoiceIn[(size_t) b] = true;
        }
        for (int b = 0; b < numBars; ++b)
            L.bars[(size_t) b].twoVoices[(size_t) staff] = lowerVoiceIn[(size_t) b];

        for (int voice = 0; voice < 2; ++voice)
        {
            const auto& vn = voices[(size_t) voice];
            if (voice == 1 && vn.empty())
                continue;
            const auto cells = findCells (vn, numBars, L.barLength, L.beatLength, grid);

            // 1. Quantized chords: start -> (end, notes)
            std::map<long, std::pair<double, std::vector<int>>> chords;
            for (auto& n : vn)
            {
                const double s = quantize (cells, n.start, total);
                double e = quantize (cells, n.start + n.length, total);
                if (e < s + eps)
                    e = std::min (total, s + cellAt (cells, s).grid);
                if (s >= total - eps)
                    continue;
                auto& c = chords[key48 (s)];
                c.first = std::max (c.first, e);
                c.second.push_back (n.note);
            }

            // 2. One line per voice: a chord lasts until the next one starts; gaps become rests.
            struct Seg { double start, end; bool rest; std::vector<int> notes; };
            std::vector<Seg> segs;
            double t = 0.0;
            for (auto it = chords.begin(); it != chords.end(); ++it)
            {
                const double s = (double) it->first / 48.0;
                double e = it->second.first;
                auto next = std::next (it);
                if (next != chords.end())
                    e = std::min (e, (double) next->first / 48.0);
                if (s > t + eps)
                    segs.push_back ({ t, s, true, {} });
                auto ns = it->second.second;
                std::sort (ns.begin(), ns.end());
                ns.erase (std::unique (ns.begin(), ns.end()), ns.end());
                segs.push_back ({ s, e, false, ns });
                t = e;
            }
            if (t < total - eps)
                segs.push_back ({ t, total, true, {} });

            // 3. Cut at bar lines and triplet boundaries; write note values; tie the pieces.
            for (auto& seg : segs)
            {
                double pos = seg.start;
                while (pos < seg.end - eps)
                {
                    const int b = std::min (numBars - 1, (int) std::floor (pos / L.barLength + 1.0e-9));
                    auto& bar = L.bars[(size_t) b];
                    const double barEnd = bar.start + L.barLength;
                    const auto& cell = cellAt (cells, pos);

                    // Region with one kind of writing: a triplet cell, or straight cells up to the next triplet.
                    double regionEnd = cell.end;
                    if (! cell.triplet)
                        for (auto& c : cells)
                            if (c.start >= cell.start - eps && c.start < barEnd - eps)
                            {
                                if (c.triplet) break;
                                regionEnd = c.end;
                            }
                    regionEnd = std::min (regionEnd, barEnd);
                    const double pieceEnd = std::min (seg.end, regionEnd);

                    // The lower voice stays silent (no rests) in bars where it has nothing to play.
                    const bool silentLower = voice == 1 && ! bar.twoVoices[(size_t) staff];
                    if (silentLower)
                    {
                        pos = pieceEnd;
                        continue;
                    }

                    auto emit = [&] (double at, Value v, double realLength, int tuplet)
                    {
                        Item item;
                        item.start = at;
                        item.value = v;
                        item.rest = seg.rest;
                        item.voice = voice;
                        item.tuplet = tuplet;
                        for (int n : seg.notes)
                        {
                            Head h;
                            h.note = n;
                            const auto sp = spell (n, keyFifths);
                            h.step = sp.step;
                            h.accidental = sp.accidental;   // resolved to "print or not" below
                            h.tieToNext = at + realLength < seg.end - eps;
                            item.heads.push_back (h);
                        }
                        bar.staves[(size_t) staff].push_back (item);
                    };

                    if (seg.rest && ! cell.triplet && std::abs (pos - bar.start) < eps && std::abs (pieceEnd - barEnd) < eps)
                    {
                        emit (pos, { 1, false }, L.barLength, -1);   // whole-bar rest, whatever the time signature
                        pos = pieceEnd;
                        continue;
                    }

                    if (cell.triplet)
                    {
                        // Written values are 3/2 of the real length, counted inside the triplet span.
                        auto& tuplets = bar.tuplets[(size_t) staff];
                        int id = -1;
                        for (int k = 0; k < (int) tuplets.size(); ++k)
                            if (tuplets[(size_t) k].voice == voice && std::abs (tuplets[(size_t) k].start - cell.start) < eps)
                                id = k;
                        if (id < 0)
                        {
                            tuplets.push_back ({ voice, cell.start, cell.end, 3, 2 });
                            id = (int) tuplets.size() - 1;
                        }
                        const double span = (cell.end - cell.start) * 1.5;
                        const double unit = span / 3.0;
                        for (auto v : splitValues ((pos - cell.start) * 1.5, (pieceEnd - pos) * 1.5, span, unit))
                        {
                            const double real = v.beats() / 1.5;
                            emit (pos, v, real, id);
                            pos += real;
                        }
                        pos = pieceEnd;   // no drift from rounding
                        continue;
                    }

                    for (auto v : splitValues (pos - bar.start, pieceEnd - pos, L.barLength, L.beatLength))
                    {
                        emit (pos, v, v.beats(), -1);
                        pos += v.beats();
                    }
                    pos = pieceEnd;
                }
            }
        }
    }

    // 4. Order, accidentals (shared by both voices of a staff), beams (per voice, per beat).
    for (auto& bar : L.bars)
    {
        for (size_t staffIndex = 0; staffIndex < 2; ++staffIndex)
        {
            auto& staff = bar.staves[staffIndex];
            std::stable_sort (staff.begin(), staff.end(), [] (const Item& a, const Item& b)
            {
                return std::abs (a.start - b.start) > eps ? a.start < b.start : a.voice < b.voice;
            });

            std::map<int, int> inForce;   // step -> accidental in force in this bar
            std::array<std::array<bool, 128>, 2> prevTied {};
            for (auto& item : staff)
            {
                auto& tied = prevTied[(size_t) item.voice];
                std::array<bool, 128> tiedNow {};
                for (auto& h : item.heads)
                {
                    const int actual = h.accidental;
                    auto it = inForce.find (h.step);
                    const int current = it != inForce.end() ? it->second : keyAccidental (((h.step % 7) + 7) % 7, keyFifths);
                    const bool continuation = tied[(size_t) (h.note & 127)];
                    h.accidental = (actual == current || continuation) ? noAccidental : actual;
                    inForce[h.step] = actual;
                    tiedNow[(size_t) (h.note & 127)] = h.tieToNext;
                }
                tied = tiedNow;
            }

            int group = 0;
            for (int voice = 0; voice < 2; ++voice)
            {
                std::vector<size_t> idx;
                for (size_t i = 0; i < staff.size(); ++i)
                    if (staff[i].voice == voice)
                        idx.push_back (i);
                auto beamable = [&] (const Item& it) { return ! it.rest && it.value.denominator >= 8; };
                auto beatOf = [&] (const Item& it) { return (int) std::floor ((it.start - bar.start) / L.beatLength + 1.0e-6); };
                for (size_t a = 0; a < idx.size(); ++a)
                {
                    if (! beamable (staff[idx[a]]))
                        continue;
                    size_t z = a;
                    while (z + 1 < idx.size() && beamable (staff[idx[z + 1]]) && beatOf (staff[idx[z + 1]]) == beatOf (staff[idx[a]])
                           && staff[idx[z + 1]].tuplet == staff[idx[a]].tuplet)
                        ++z;
                    if (z > a)
                    {
                        for (size_t k = a; k <= z; ++k)
                            staff[idx[k]].beamGroup = group;
                        ++group;
                    }
                    a = z;
                }
            }
        }
    }
    return L;
}

} // namespace km::notation
