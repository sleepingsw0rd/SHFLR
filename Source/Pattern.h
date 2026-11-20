#pragma once
#include <array>
#include <string>

namespace breakvar
{
inline constexpr int colourCount = 8;
inline constexpr int restTokenCount = 5;
inline constexpr int orderTokenCount = colourCount + restTokenCount;
inline constexpr int cellsPerBar = 16;
inline constexpr int maxInputBars = 4;
inline constexpr int maxCells = cellsPerBar * maxInputBars;
inline constexpr int maxOrder = 128;
inline constexpr int waveformSize = 512;

// Rests belong only to the order lane: 1/16, 1/8, 1/4, 1/2 or one bar.
constexpr bool isRest(int token) noexcept { return token >= colourCount && token < orderTokenCount; }
constexpr int restCells(int token) noexcept { return isRest(token) ? 1 << (token - colourCount) : 0; }
constexpr int restToken(int cells) noexcept
{
    for (int i = 0; i < restTokenCount; ++i)
        if (cells == (1 << i)) return colourCount + i;
    return -1;
}

struct Region
{
    int first = 0;
    int end = 0;
    bool valid() const noexcept { return end > first; }
};

struct Pattern
{
    int inputBars = 2;
    int outputBars = 4;
    int grid = 4; // Selection step: 1, 2 or 4 sixteenth-of-a-bar cells.
    std::array<int, maxCells> cells{}; // -1 unassigned; 0..7 colour.
    std::array<int, maxOrder> order{}; // 0..7 source colour, 8..12 rest; -1 inactive.
    std::array<int, maxOrder> orderLengths{}; // 0 Full; 1..64 source-cell cap. Rests/inactive entries use 0.
    int orderCount = 6;
    int smoothTenths = 20; // 0..100 tenths of a millisecond at discontinuous joins.
    int tightenPercent = 0; // 0..100 percent of automatic phrase-start correction.
    std::array<int, colourCount> nudgeTenths{}; // -300..300 tenths of a millisecond per source colour.
    std::array<bool, colourCount> nudgeManual{}; // Manual zero also overrides automatic correction.

    static Pattern makeDefault() noexcept;
    int activeCells() const noexcept { return inputBars * cellsPerBar; }
    Region region(int colour) const noexcept;
    int phraseLength(int token) const noexcept;
    int orderLength(int index) const noexcept;
    bool setOrderLength(int index, int lengthCells, std::string* reason = nullptr);
    bool validate(std::string* reason = nullptr) const;
    bool assign(int first, int lastInclusive, int colour, std::string* reason = nullptr);
    bool resizeInput(int bars, std::string* reason = nullptr);
    bool insert(int index, int colour, std::string* reason = nullptr);
    bool duplicate(int index, std::string* reason = nullptr);
    bool move(int from, int insertionIndex) noexcept;
    bool erase(int index) noexcept;
    bool operator==(const Pattern&) const = default;
};

struct Segment
{
    int colour = -1; // Source colour or an order-only rest token.
    int orderIndex = -1;
    double start = 0.0; // Cells from output loop start.
    double length = 0.0;
    double fullLength = 0.0;
    bool repeated = false;
};

// Fixed-capacity, allocation-free expansion, repeated and cut at output end.
struct Sequence
{
    std::array<Segment, cellsPerBar * 8> segments{};
    int count = 0;
};
Sequence expandSequence(const Pattern&) noexcept;
}
