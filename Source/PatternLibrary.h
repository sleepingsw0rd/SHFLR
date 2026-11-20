#pragma once
#include "Pattern.h"
#include <cstdint>
#include <vector>

namespace breakvar
{
inline constexpr std::array<const char*, 8> presetCategories
    { "Essentials", "DnB", "Jungle", "Hip-hop", "Buildups", "Breakdowns", "Shuffles", "Fills" };

struct NamedPattern
{
    std::string name;
    Pattern pattern;
    std::string category = "Essentials";
};

const std::vector<NamedPattern>& factoryPatterns();
// All operations validate before publishing and leave p unchanged on failure.
// Slices partition the source on its grid, keeping all referenced colours and
// leaving order-only rests unchanged.
bool randomiseSlices(Pattern&, std::uint32_t seed, std::string* reason = nullptr);
// Shuffle keeps every occurrence; an empty or single-colour order is a valid no-op.
bool shuffleOrder(Pattern&, std::uint32_t seed, std::string* reason = nullptr);
// Generate references existing regions and rest durations already in the order
// through the output end (last one may cut). Starts with audio when available;
// all-rest patterns are supported. No regions or rests rejects generation.
bool generateOrder(Pattern&, std::uint32_t seed, std::string* reason = nullptr);
}
