#include "Engine/PartFilter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace km::filter;

TEST_CASE ("Velocity curves stay in range and are monotonic", "[filter]")
{
    for (auto curve : { VelocityCurve::linear, VelocityCurve::soft, VelocityCurve::hard })
    {
        int previous = 0;
        for (int v = 1; v <= 127; ++v)
        {
            const int out = applyVelocityCurve (v, curve);
            REQUIRE (out >= 1);
            REQUIRE (out <= 127);
            REQUIRE (out >= previous);
            previous = out;
        }
        REQUIRE (applyVelocityCurve (127, curve) == 127);
    }
}

TEST_CASE ("Soft curve boosts, hard curve reduces soft playing", "[filter]")
{
    REQUIRE (applyVelocityCurve (40, VelocityCurve::soft) > 40);
    REQUIRE (applyVelocityCurve (40, VelocityCurve::hard) < 40);
    REQUIRE (applyVelocityCurve (40, VelocityCurve::linear) == 40);
}

TEST_CASE ("Fixed velocity ignores playing strength", "[filter]")
{
    REQUIRE (applyVelocityCurve (1, VelocityCurve::fixed, 90) == 90);
    REQUIRE (applyVelocityCurve (127, VelocityCurve::fixed, 90) == 90);
    REQUIRE (applyVelocityCurve (64, VelocityCurve::fixed, 500) == 127);
}

TEST_CASE ("Key and velocity ranges are inclusive and order-independent", "[filter]")
{
    REQUIRE (inRange (60, 60, 72));
    REQUIRE (inRange (72, 60, 72));
    REQUIRE_FALSE (inRange (59, 60, 72));
    REQUIRE (inRange (65, 72, 60)); // swapped bounds still work
}

TEST_CASE ("Transpose drops notes outside 0..127", "[filter]")
{
    REQUIRE (transposeNote (60, 12) == 72);
    REQUIRE (transposeNote (60, -60) == 0);
    REQUIRE (transposeNote (120, 12) == -1);
    REQUIRE (transposeNote (5, -12) == -1);
}

TEST_CASE ("Equal-power pan is unity in the centre and silent on the far side", "[filter]")
{
    float l = 0, r = 0;
    panGains (0.0f, l, r);
    REQUIRE_THAT (l, Catch::Matchers::WithinAbs (1.0, 1e-4));
    REQUIRE_THAT (r, Catch::Matchers::WithinAbs (1.0, 1e-4));

    panGains (-1.0f, l, r);
    REQUIRE_THAT (r, Catch::Matchers::WithinAbs (0.0, 1e-4));
    panGains (1.0f, l, r);
    REQUIRE_THAT (l, Catch::Matchers::WithinAbs (0.0, 1e-4));
}
