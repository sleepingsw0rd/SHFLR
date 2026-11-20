#include "Pattern.h"
#include <algorithm>

namespace breakvar
{
namespace
{
bool validInputBars(int bars) noexcept { return bars == 1 || bars == 2 || bars == 4; }
bool validOutputBars(int bars) noexcept { return validInputBars(bars) || bars == 8; }
bool fail(std::string* reason, const char* message)
{
    if (reason != nullptr) *reason = message;
    return false;
}
void success(std::string* reason)
{
    if (reason != nullptr) reason->clear();
}
}

Pattern Pattern::makeDefault() noexcept
{
    Pattern p;
    p.cells.fill(-1);
    p.order.fill(-1);
    std::fill(p.cells.begin(), p.cells.begin() + 12, 0);
    std::fill(p.cells.begin() + 12, p.cells.begin() + 20, 1);
    std::fill(p.cells.begin() + 20, p.cells.begin() + 32, 2);
    constexpr int defaultOrder[] { 0, 0, 0, 2, 1, 1 };
    std::copy(std::begin(defaultOrder), std::end(defaultOrder), p.order.begin());
    return p;
}

Region Pattern::region(int colour) const noexcept
{
    if (!validInputBars(inputBars) || colour < 0 || colour >= colourCount) return {};
    Region result;
    bool found = false;
    bool ended = false;
    for (int i = 0; i < activeCells(); ++i)
    {
        if (cells[i] == colour)
        {
            // Malformed disconnected regions must not be interpreted as one phrase.
            if (ended) return {};
            if (!found) { result.first = i; found = true; }
            result.end = i + 1;
        }
        else if (found) ended = true;
    }
    return result;
}

int Pattern::phraseLength(int token) const noexcept
{
    if (isRest(token)) return restCells(token);
    const auto source = region(token);
    return source.end - source.first;
}

int Pattern::orderLength(int index) const noexcept
{
    if (orderCount < 0 || orderCount > maxOrder || index < 0 || index >= orderCount) return 0;
    const int cap = orderLengths[index];
    if (cap < 0 || cap > maxCells || (isRest(order[index]) && cap != 0)) return 0;
    const int fullLength = phraseLength(order[index]);
    return cap == 0 ? fullLength : std::min(cap, fullLength);
}

bool Pattern::setOrderLength(int index, int lengthCells, std::string* reason)
{
    if (!validate(reason)) return false;
    if (index < 0 || index >= orderCount) return fail(reason, "Invalid order length position.");
    if (lengthCells < 0 || lengthCells > maxCells) return fail(reason, "Play length must be Full or 1 to 64 cells.");
    if (isRest(order[index]) && lengthCells != 0) return fail(reason, "Rests retain their selected duration.");
    orderLengths[index] = lengthCells;
    success(reason);
    return true;
}

bool Pattern::validate(std::string* reason) const
{
    if (!validInputBars(inputBars)) return fail(reason, "Input length must be 1, 2 or 4 bars.");
    if (!validOutputBars(outputBars)) return fail(reason, "Output length must be 1, 2, 4 or 8 bars.");
    if (grid != 1 && grid != 2 && grid != 4) return fail(reason, "Selection grid must be 1, 2 or 4 cells.");
    if (orderCount < 0 || orderCount > maxOrder) return fail(reason, "Too many or invalid ordered phrases.");
    if (smoothTenths < 0 || smoothTenths > 100) return fail(reason, "Smooth must be between 0 and 10 ms.");
    if (tightenPercent < 0 || tightenPercent > 100) return fail(reason, "Tighten must be between 0 and 100 percent.");
    for (const int nudge : nudgeTenths)
        if (nudge < -300 || nudge > 300) return fail(reason, "Nudge must be between -30 and 30 ms.");

    std::array<bool, colourCount> seen {};
    int previous = -1;
    for (int i = 0; i < activeCells(); ++i)
    {
        const int colour = cells[i];
        if (colour < -1 || colour >= colourCount) return fail(reason, "Invalid input cell colour.");
        if (colour >= 0 && colour != previous)
        {
            if (seen[colour]) return fail(reason, "Each colour must occupy one contiguous region.");
            seen[colour] = true;
        }
        previous = colour;
    }
    for (int i = activeCells(); i < maxCells; ++i)
        if (cells[i] != -1) return fail(reason, "Inactive input cells must be unassigned.");
    for (int i = 0; i < orderCount; ++i)
    {
        const int colour = order[i];
        if (colour < 0 || colour >= orderTokenCount) return fail(reason, "Invalid ordered phrase.");
        if (!isRest(colour) && !seen[colour]) return fail(reason, "An ordered phrase has no input region.");
        if (orderLengths[i] < 0 || orderLengths[i] > maxCells)
            return fail(reason, "Play length must be Full or 1 to 64 cells.");
        if (isRest(colour) && orderLengths[i] != 0)
            return fail(reason, "Rests retain their selected duration.");
    }
    for (int i = orderCount; i < maxOrder; ++i)
        if (orderLengths[i] != 0) return fail(reason, "Inactive order lengths must be Full.");
    success(reason);
    return true;
}

bool Pattern::assign(int first, int lastInclusive, int colour, std::string* reason)
{
    if (!validate(reason)) return false;
    if (first < 0 || lastInclusive < first || lastInclusive >= activeCells())
        return fail(reason, "Selection lies outside the input window.");
    if (colour < -1 || colour >= colourCount) return fail(reason, "Invalid assignment colour.");
    Pattern proposed = *this;
    std::fill(proposed.cells.begin() + first, proposed.cells.begin() + lastInclusive + 1, colour);
    if (!proposed.validate(reason)) return false;
    *this = proposed;
    return true;
}

bool Pattern::resizeInput(int bars, std::string* reason)
{
    if (!validate(reason)) return false;
    if (!validInputBars(bars)) return fail(reason, "Input length must be 1, 2 or 4 bars.");
    Pattern proposed = *this;
    proposed.inputBars = bars;
    proposed.cells.fill(-1);
    const int oldCells = activeCells();
    const int newCells = proposed.activeCells();
    // Round shared region boundaries identically, preserving normalized positions
    // without introducing overlaps. Tiny regions can collapse on a smaller grid.
    const auto scale = [oldCells, newCells](int boundary) noexcept
    {
        return (boundary * newCells + oldCells / 2) / oldCells;
    };
    for (int colour = 0; colour < colourCount; ++colour)
    {
        const auto source = region(colour);
        if (!source.valid()) continue;
        std::fill(proposed.cells.begin() + scale(source.first),
                  proposed.cells.begin() + scale(source.end), colour);
    }
    // Referenced colours that vanished reject the complete resize transaction.
    if (!proposed.validate(reason)) return false;
    *this = proposed;
    return true;
}

bool Pattern::insert(int index, int colour, std::string* reason)
{
    if (!validate(reason)) return false;
    if (index < 0 || index > orderCount) return fail(reason, "Invalid order insertion position.");
    if (orderCount == maxOrder) return fail(reason, "The phrase order is full.");
    if (phraseLength(colour) == 0)
        return fail(reason, "Choose a colour with an input region or a rest.");
    std::move_backward(order.begin() + index, order.begin() + orderCount, order.begin() + orderCount + 1);
    std::move_backward(orderLengths.begin() + index, orderLengths.begin() + orderCount,
                       orderLengths.begin() + orderCount + 1);
    order[index] = colour;
    orderLengths[index] = 0;
    ++orderCount;
    success(reason);
    return true;
}

bool Pattern::duplicate(int index, std::string* reason)
{
    if (!validate(reason)) return false;
    if (index < 0 || index >= orderCount) return fail(reason, "Invalid order duplication position.");
    const int length = orderLengths[index];
    if (!insert(index + 1, order[index], reason)) return false;
    orderLengths[index + 1] = length;
    return true;
}

bool Pattern::move(int from, int insertionIndex) noexcept
{
    if (!validate() || from < 0 || from >= orderCount || insertionIndex < 0 || insertionIndex > orderCount)
        return false;
    const int target = insertionIndex > from ? insertionIndex - 1 : insertionIndex;
    const int colour = order[from];
    const int length = orderLengths[from];
    if (target > from)
    {
        std::move(order.begin() + from + 1, order.begin() + target + 1, order.begin() + from);
        std::move(orderLengths.begin() + from + 1, orderLengths.begin() + target + 1, orderLengths.begin() + from);
    }
    else if (target < from)
    {
        std::move_backward(order.begin() + target, order.begin() + from, order.begin() + from + 1);
        std::move_backward(orderLengths.begin() + target, orderLengths.begin() + from, orderLengths.begin() + from + 1);
    }
    order[target] = colour;
    orderLengths[target] = length;
    return true;
}

bool Pattern::erase(int index) noexcept
{
    if (!validate() || index < 0 || index >= orderCount) return false;
    std::move(order.begin() + index + 1, order.begin() + orderCount, order.begin() + index);
    std::move(orderLengths.begin() + index + 1, orderLengths.begin() + orderCount, orderLengths.begin() + index);
    order[--orderCount] = -1;
    orderLengths[orderCount] = 0;
    return true;
}

Sequence expandSequence(const Pattern& p) noexcept
{
    Sequence result;
    if (!p.validate() || p.orderCount == 0) return result;
    const int total = p.outputBars * cellsPerBar;
    int position = 0;
    int orderIndex = 0;
    bool repeated = false;
    while (position < total && result.count < static_cast<int>(result.segments.size()))
    {
        const int colour = p.order[orderIndex];
        const int fullLength = p.orderLength(orderIndex);
        const int length = std::min(fullLength, total - position);
        result.segments[result.count++] = { colour, orderIndex, static_cast<double>(position),
            static_cast<double>(length), static_cast<double>(fullLength), repeated };
        position += length;
        if (++orderIndex == p.orderCount) { orderIndex = 0; repeated = true; }
    }
    return result;
}
}
