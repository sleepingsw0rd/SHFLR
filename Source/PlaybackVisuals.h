#pragma once
#include "Pattern.h"
#include <algorithm>
#include <cmath>

namespace breakvar
{
struct PlaybackVisual
{
    int sequenceIndex = -1;
    int orderIndex = -1;
    int colour = -1;
    double progress = 0.0;
    float pulse = 0.0f;
};

// The GUI supplies normalized output phase and gates active on playing, ready and
// no pending preset. Every expanded occurrence owns its progress and pulse age.
inline PlaybackVisual playbackVisual(const Pattern& pattern, double outputPhase, double bpm,
                                     int numerator, int denominator, bool active) noexcept
{
    if (!active || !std::isfinite(outputPhase) || outputPhase < 0.0 || outputPhase >= 1.0
        || !std::isfinite(bpm) || bpm <= 0.0 || bpm > 10000.0
        || numerator <= 0 || numerator > 128 || denominator <= 0 || denominator > 128
        || (denominator & (denominator - 1)) != 0)
        return {};

    const double secondsPerCell = 60.0 / bpm * numerator * 4.0 / denominator / cellsPerBar;
    if (!std::isfinite(secondsPerCell) || secondsPerCell <= 0.0)
        return {};

    const auto sequence = expandSequence(pattern);
    if (sequence.count == 0) return {};
    const double position = outputPhase * pattern.outputBars * cellsPerBar;
    for (int i = 0; i < sequence.count; ++i)
    {
        const auto& segment = sequence.segments[i];
        if (position < segment.start || position >= segment.start + segment.length)
            continue;

        const double duration = segment.length * secondsPerCell;
        if (!std::isfinite(duration) || duration <= 0.0) return {};
        const double ageCells = position - segment.start;
        const double pulseSeconds = std::min(0.14, duration);
        return { i, segment.orderIndex, isRest(segment.colour) ? -1 : segment.colour,
                 std::clamp(ageCells / segment.length, 0.0, 1.0),
                 static_cast<float>(std::clamp(1.0 - ageCells * secondsPerCell / pulseSeconds, 0.0, 1.0)) };
    }
    return {};
}
}
