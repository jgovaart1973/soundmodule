#pragma once

#include <algorithm>
#include <cmath>

/** Pure helper functions for key splits, velocity layers and transposition. Unit-tested. */
namespace km::filter
{
enum class VelocityCurve
{
    linear = 0,
    soft,   ///< louder for soft playing (light keyboards)
    hard,   ///< needs more force (heavy/hammer action)
    fixed   ///< always the same velocity (organ style)
};

inline int applyVelocityCurve (int velocity, VelocityCurve curve, int fixedVelocity = 100) noexcept
{
    velocity = std::clamp (velocity, 1, 127);

    switch (curve)
    {
        case VelocityCurve::linear: return velocity;
        case VelocityCurve::fixed:  return std::clamp (fixedVelocity, 1, 127);
        case VelocityCurve::soft:
        case VelocityCurve::hard:
        {
            const double exponent = curve == VelocityCurve::soft ? 0.6 : 1.6;
            const auto v = 127.0 * std::pow (velocity / 127.0, exponent);
            return std::clamp ((int) std::lround (v), 1, 127);
        }
    }
    return velocity;
}

inline bool inRange (int value, int low, int high) noexcept
{
    if (low > high)
        std::swap (low, high);
    return value >= low && value <= high;
}

/** @returns the transposed note, or -1 if it falls outside 0..127. */
inline int transposeNote (int note, int semitones) noexcept
{
    const int n = note + semitones;
    return (n >= 0 && n <= 127) ? n : -1;
}

/** Equal-power pan law. pan in [-1, 1]. */
inline void panGains (float pan, float& left, float& right) noexcept
{
    pan = std::clamp (pan, -1.0f, 1.0f);
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979f;
    left  = std::cos (angle) * 1.41421356f;
    right = std::sin (angle) * 1.41421356f;
}

} // namespace km::filter
