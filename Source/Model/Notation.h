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
    double start = 0.0;          ///< quarter notes from the start of the clip
    Value value;
    bool rest = true;
    std::vector<Head> heads;     ///< sorted low to high
    int beamGroup = -1;          ///< items with the same id (>= 0) in a bar share a beam
};

struct Bar
{
    double start = 0.0;
    std::array<std::vector<Item>, 2> staves;   ///< 0 = treble, 1 = bass
};

struct Layout
{
    int keyFifths = 0;
    double barLength = 4.0, beatLength = 1.0;
    std::vector<Bar> bars;
};

/** Staff position in half spaces above the bottom line (treble: E4, bass: G2). */
inline int staffPosition (int step, int staff) noexcept { return step - (staff == 0 ? 30 : 18); }

/**
    @param notes       clip-relative notes
    @param clipLength  quarter notes
    @param barLength   quarter notes per bar (Song::beatsPerBarAsQuarters)
    @param beatLength  quarter notes per counted beat (1 in x/4, 1.5 in 6/8, 0.5 in 5/8 ...)
    @param grid        quantize grid for display (does not change the song)
    @param split       notes >= split go to the treble staff
*/
inline Layout layout (const std::vector<SongNote>& notes, double clipLength, double barLength, double beatLength,
                      int keyFifths, double grid = 0.25, int split = 60)
{
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
        // 1. Quantized chords on this staff: start -> (end, notes)
        std::map<long, std::pair<double, std::vector<int>>> chords;
        for (auto& n : notes)
        {
            if ((n.note >= split) != (staff == 0))
                continue;
            const double s = std::round (n.start / grid) * grid;
            double e = std::round ((n.start + n.length) / grid) * grid;
            if (e < s + grid)
                e = s + grid;
            if (s < 0.0 || s >= total)
                continue;
            auto& c = chords[std::lround (s / 0.0625)];
            c.first = std::max (c.first, std::min (e, total));
            c.second.push_back (n.note);
        }

        // 2. One voice: a chord lasts until the next one starts; gaps become rests.
        struct Seg { double start, end; bool rest; std::vector<int> notes; };
        std::vector<Seg> segs;
        double t = 0.0;
        for (auto it = chords.begin(); it != chords.end(); ++it)
        {
            const double s = it->first * 0.0625;
            double e = it->second.first;
            auto next = std::next (it);
            if (next != chords.end())
                e = std::min (e, next->first * 0.0625);
            if (s > t + 1.0e-6)
                segs.push_back ({ t, s, true, {} });
            auto ns = it->second.second;
            std::sort (ns.begin(), ns.end());
            ns.erase (std::unique (ns.begin(), ns.end()), ns.end());
            segs.push_back ({ s, e, false, ns });
            t = e;
        }
        if (t < total - 1.0e-6)
            segs.push_back ({ t, total, true, {} });

        // 3. Cut at bar lines, write as note values, tie the pieces of one note.
        for (auto& seg : segs)
        {
            double pos = seg.start;
            while (pos < seg.end - 1.0e-6)
            {
                const int b = std::min (numBars - 1, (int) std::floor (pos / L.barLength + 1.0e-9));
                auto& bar = L.bars[(size_t) b];
                const double barEnd = bar.start + L.barLength;
                const double pieceEnd = std::min (seg.end, barEnd);

                if (seg.rest && std::abs (pos - bar.start) < 1.0e-6 && std::abs (pieceEnd - barEnd) < 1.0e-6)
                {
                    Item whole;          // whole-bar rest, whatever the time signature
                    whole.start = pos;
                    whole.value = { 1, false };
                    bar.staves[(size_t) staff].push_back (whole);
                    pos = pieceEnd;
                    continue;
                }

                for (auto v : splitValues (pos - bar.start, pieceEnd - pos, L.barLength, L.beatLength))
                {
                    Item item;
                    item.start = pos;
                    item.value = v;
                    item.rest = seg.rest;
                    for (int n : seg.notes)
                    {
                        Head h;
                        h.note = n;
                        const auto sp = spell (n, keyFifths);
                        h.step = sp.step;
                        h.accidental = sp.accidental;   // resolved to "print or not" below
                        h.tieToNext = pos + v.beats() < seg.end - 1.0e-6;
                        item.heads.push_back (h);
                    }
                    bar.staves[(size_t) staff].push_back (item);
                    pos += v.beats();
                }
            }
        }
    }

    // 4. Accidentals: print only where the key signature / earlier notes in the bar don't already say it.
    //    Beams: eighths and shorter within one beat.
    for (auto& bar : L.bars)
    {
        for (auto& staff : bar.staves)
        {
            std::map<int, int> inForce;   // step -> accidental in force in this bar
            bool prevTied[128] {};
            for (auto& item : staff)
            {
                bool tiedNow[128] {};
                for (auto& h : item.heads)
                {
                    const int actual = h.accidental;
                    auto it = inForce.find (h.step);
                    const int current = it != inForce.end() ? it->second : keyAccidental (((h.step % 7) + 7) % 7, keyFifths);
                    const bool continuation = prevTied[h.note & 127];
                    h.accidental = (actual == current || continuation) ? noAccidental : actual;
                    inForce[h.step] = actual;
                    tiedNow[h.note & 127] = h.tieToNext;
                }
                std::copy (std::begin (tiedNow), std::end (tiedNow), std::begin (prevTied));
            }

            int group = 0;
            for (size_t i = 0; i < staff.size(); ++i)
            {
                auto beamable = [&] (const Item& it) { return ! it.rest && it.value.denominator >= 8; };
                if (! beamable (staff[i]))
                    continue;
                const int beat = (int) std::floor ((staff[i].start - bar.start) / L.beatLength + 1.0e-9);
                size_t j = i;
                while (j + 1 < staff.size() && beamable (staff[j + 1])
                       && (int) std::floor ((staff[j + 1].start - bar.start) / L.beatLength + 1.0e-9) == beat)
                    ++j;
                if (j > i)
                {
                    for (size_t k = i; k <= j; ++k)
                        staff[k].beamGroup = group;
                    ++group;
                }
                i = j;
            }
        }
    }
    return L;
}

} // namespace km::notation
