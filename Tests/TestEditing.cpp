#include "Model/NoteEditing.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace km;
using Catch::Matchers::WithinAbs;

namespace
{
    std::vector<SongNote> notesAt (std::initializer_list<double> starts, double length = 0.2)
    {
        std::vector<SongNote> v;
        for (auto s : starts)
            v.push_back ({ s, length, 60, 100, 1 });
        return v;
    }
} // namespace

TEST_CASE ("Quantize moves notes to the grid and keeps their length", "[edit]")
{
    auto n = notesAt ({ 0.27, 0.49, 1.13 });
    edit::quantize (n, {}, { 0.25, 1.0, 0.0, false });
    REQUIRE_THAT (n[0].start, WithinAbs (0.25, 1e-9));
    REQUIRE_THAT (n[1].start, WithinAbs (0.5, 1e-9));
    REQUIRE_THAT (n[2].start, WithinAbs (1.25, 1e-9));
    REQUIRE_THAT (n[0].length, WithinAbs (0.2, 1e-9));
}

TEST_CASE ("Quantize strength moves only part of the way", "[edit]")
{
    auto n = notesAt ({ 0.30 });
    edit::quantize (n, {}, { 0.25, 0.5, 0.0, false });
    REQUIRE_THAT (n[0].start, WithinAbs (0.275, 1e-9));
}

TEST_CASE ("Swing delays every second grid position", "[edit]")
{
    auto n = notesAt ({ 0.0, 0.52, 1.0, 1.48 });
    edit::quantize (n, {}, { 0.5, 1.0, 1.0, false });
    REQUIRE_THAT (n[0].start, WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (n[1].start, WithinAbs (2.0 / 3.0, 1e-9)); // full swing = triplet feel
    REQUIRE_THAT (n[2].start, WithinAbs (1.0, 1e-9));
    REQUIRE_THAT (n[3].start, WithinAbs (1.0 + 2.0 / 3.0, 1e-9));
}

TEST_CASE ("Triplet grid and note-end quantize", "[edit]")
{
    auto n = notesAt ({ 0.34 }, 0.3);
    edit::quantize (n, {}, { 1.0 / 3.0, 1.0, 0.0, true });
    REQUIRE_THAT (n[0].start, WithinAbs (1.0 / 3.0, 1e-9));
    REQUIRE_THAT (n[0].start + n[0].length, WithinAbs (2.0 / 3.0, 1e-9));
}

TEST_CASE ("Quantize and transpose only touch the selection", "[edit]")
{
    auto n = notesAt ({ 0.1, 0.6 });
    edit::quantize (n, { 1 }, { 0.5, 1.0, 0.0, false });
    REQUIRE_THAT (n[0].start, WithinAbs (0.1, 1e-9));
    REQUIRE_THAT (n[1].start, WithinAbs (0.5, 1e-9));

    edit::transpose (n, { 0 }, 12);
    REQUIRE (n[0].note == 72);
    REQUIRE (n[1].note == 60);
    edit::transpose (n, {}, 100);
    REQUIRE (n[0].note == 127);
}

TEST_CASE ("Drawing a controller line replaces points in that range only", "[edit]")
{
    const auto& mod = edit::controllerLanes()[0];
    std::vector<SongControl> c;
    c.push_back (mod.make (0.5, 0.2, 1));
    c.push_back (mod.make (3.0, 0.9, 1));
    c.push_back (edit::controllerLanes()[3].make (1.0, 1.0, 1)); // sustain: other lane, untouched

    edit::drawLine (c, mod, 1, 0.0, 2.0, 0.0, 1.0, 0.5);
    int modPoints = 0;
    for (auto& p : c)
        if (mod.matches (p)) ++modPoints;
    REQUIRE (modPoints == 6);  // 5 new points (0..2 step 0.5) + the one at beat 3
    REQUIRE (c.front().beat == 0.0);
    REQUIRE_THAT (mod.valueOf (c.front()), WithinAbs (0.0, 0.01));

    for (size_t i = 1; i < c.size(); ++i)
        REQUIRE (c[i - 1].beat <= c[i].beat);

    edit::eraseRange (c, mod, 0.0, 2.5);
    modPoints = 0;
    for (auto& p : c)
        if (mod.matches (p)) ++modPoints;
    REQUIRE (modPoints == 1);
    REQUIRE (c.size() == 2); // sustain point survived
}

TEST_CASE ("Pitch bend points use the full 14-bit range with the centre at 0.5", "[edit]")
{
    const auto& bend = edit::controllerLanes()[4];
    REQUIRE (bend.kind == edit::ControllerLane::pitchBend);
    const auto centre = bend.make (0.0, 0.5, 1);
    REQUIRE ((centre.status & 0xf0) == 0xe0);
    REQUIRE_THAT (bend.valueOf (centre), WithinAbs (0.5, 0.001));
    REQUIRE_THAT (bend.valueOf (bend.make (0.0, 1.0, 1)), WithinAbs (1.0, 1e-9));
}
