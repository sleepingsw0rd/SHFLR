#include "Pattern.h"
#include "PatternLibrary.h"
#include "PlaybackVisuals.h"
#include <climits>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace breakvar;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void regionIs(const Pattern& p, int colour, int first, int end)
{
    const auto r = p.region(colour);
    require(r.first == first && r.end == end, "unexpected colour region");
}
Pattern emptyPattern(int bars = 2)
{
    auto p = Pattern::makeDefault();
    p.inputBars = bars;
    p.cells.fill(-1);
    p.order.fill(-1);
    p.orderCount = 0;
    return p;
}
void defaultsAndExpansion()
{
    const auto p = Pattern::makeDefault();
    require(p.validate(), "default must validate");
    require(p.inputBars == 2 && p.outputBars == 4 && p.grid == 4, "default timing");
    regionIs(p, 0, 0, 12); regionIs(p, 1, 12, 20); regionIs(p, 2, 20, 32);
    const int order[] { 0, 0, 0, 2, 1, 1 };
    require(p.orderCount == 6, "default order count");
    for (int i = 0; i < 6; ++i) require(p.order[i] == order[i], "default order");
    for (int i = 32; i < maxCells; ++i) require(p.cells[i] == -1, "inactive cells unassigned");
    const auto sequence = expandSequence(p);
    require(sequence.count == 6, "default expansion count");
    const double starts[] { 0, 12, 24, 36, 48, 56 };
    const double lengths[] { 12, 12, 12, 12, 8, 8 };
    for (int i = 0; i < 6; ++i)
    {
        const auto& s = sequence.segments[i];
        require(s.colour == order[i] && s.orderIndex == i, "default segment identity");
        require(s.start == starts[i] && s.length == lengths[i]
                && s.fullLength == lengths[i] && !s.repeated, "default segment timing");
    }
}
template <typename P = Pattern>
void soundControlDefaultsAndIdentity()
{
    if constexpr (requires(P p) { p.smoothTenths; p.tightenPercent; p.nudgeTenths; p.nudgeManual; })
    {
        const P initial = P::makeDefault();
        require(initial.smoothTenths == 20 && initial.tightenPercent == 0,
                "new patterns start with short smoothing and automatic timing disabled");
        for (int colour = 0; colour < colourCount; ++colour)
            require(initial.nudgeTenths[colour] == 0 && !initial.nudgeManual[colour],
                    "new colours have no manual timing override");
        auto changed = initial; changed.smoothTenths = 21;
        require(changed != initial, "smoothing changes pattern identity");
        changed = initial; changed.tightenPercent = 50;
        require(changed != initial, "automatic timing amount changes pattern identity");
        changed = initial; changed.nudgeTenths[7] = -1;
        require(changed != initial, "per-colour timing changes pattern identity even for unused colours");
        changed = initial; changed.nudgeManual[0] = true;
        require(changed != initial && changed.validate(), "manual zero differs from automatic timing");
    }
    else require(false, "Pattern must carry smoothing and per-colour timing controls");
}
template <typename P = Pattern>
void soundControlsValidateBeforeEditing()
{
    if constexpr (requires(P p) { p.smoothTenths; p.tightenPercent; p.nudgeTenths; p.nudgeManual; })
    {
        const P initial = P::makeDefault();
        for (const int smooth : {0, 1, 20, 100})
            for (const int tighten : {0, 50, 100})
            {
                auto p = initial; p.smoothTenths = smooth; p.tightenPercent = tighten;
                for (int colour = 0; colour < colourCount; ++colour)
                {
                    p.nudgeTenths[colour] = colour % 2 == 0 ? -300 : 300;
                    p.nudgeManual[colour] = colour % 2 != 0;
                }
                require(p.validate(), "inclusive sound-control endpoints validate");
                const auto expanded = expandSequence(p);
                require(expanded.count == 6 && expanded.segments[3].start == 36
                        && expanded.segments[5].length == 8,
                        "timing corrections never change fixed musical phrase lengths");
            }
        const auto rejects = [](P p)
        {
            std::string reason;
            require(!p.validate(&reason) && !reason.empty() && expandSequence(p).count == 0,
                    "malformed sound controls reject playback with an explanation");
            const auto before = p;
            require(!p.assign(0, 1, 0) && !p.resizeInput(1) && !p.insert(0, 0)
                    && !p.duplicate(0) && !p.move(0, 2) && !p.erase(0)
                    && !p.setOrderLength(0, 4) && !randomiseSlices(p, 1)
                    && !shuffleOrder(p, 1) && !generateOrder(p, 1) && p == before,
                    "every editor rejects malformed sound state transactionally");
        };
        for (const int invalid : {INT_MIN, -1, 101, INT_MAX})
        {
            auto p = initial; p.smoothTenths = invalid; rejects(p);
            p = initial; p.tightenPercent = invalid; rejects(p);
        }
        for (int colour = 0; colour < colourCount; ++colour)
            for (const int invalid : {INT_MIN, -301, 301, INT_MAX})
            {
                auto p = initial; p.nudgeTenths[colour] = invalid;
                p.nudgeManual[colour] = colour % 2 != 0;
                rejects(p);
            }
    }
    else require(false, "Pattern must validate sound controls before editing");
}
template <typename P = Pattern>
void soundControlsSurviveArrangementEdits()
{
    if constexpr (requires(P p) { p.smoothTenths; p.tightenPercent; p.nudgeTenths; p.nudgeManual; })
    {
        P p = P::makeDefault();
        p.smoothTenths = 73; p.tightenPercent = 61;
        p.nudgeTenths = {-300, -40, 0, 15, 300, 90, -25, 1};
        p.nudgeManual = {true, false, true, false, true, false, true, false};
        const auto original = p;
        const auto retained = [&]
        {
            require(p.validate() && p.smoothTenths == original.smoothTenths
                    && p.tightenPercent == original.tightenPercent
                    && p.nudgeTenths == original.nudgeTenths && p.nudgeManual == original.nudgeManual,
                    "arrangement editing retains all sound settings by source colour");
        };
        require(p.assign(0, 1, -1) && p.assign(0, 1, 0), "edit source boundaries"); retained();
        require(p.resizeInput(4), "resize source window"); retained();
        require(p.setOrderLength(0, 3), "trim one occurrence"); retained();
        require(p.insert(1, restToken(4)), "insert silence"); retained();
        require(p.duplicate(0), "duplicate source occurrence"); retained();
        require(p.move(0, p.orderCount), "move source occurrence"); retained();
        require(p.erase(1), "remove order occurrence"); retained();
        require(randomiseSlices(p, 741), "randomise source slices"); retained();
        require(shuffleOrder(p, 852), "shuffle order occurrences"); retained();
        require(generateOrder(p, 963), "generate replacement order"); retained();
    }
    else require(false, "Pattern must preserve sound controls through arrangement edits");
}
void assignmentIsTransactional()
{
    auto p = Pattern::makeDefault();
    const auto original = p;
    std::string reason;
    require(!p.assign(4, 7, 3, &reason) && p == original && !reason.empty(), "reject splitting yellow atomically");
    require(!p.assign(12, 19, 0) && p == original, "reject deleting referenced red");
    require(!p.assign(0, 11, -1) && p == original, "reject clearing referenced yellow");
    require(p.assign(12, 15, 0), "extend yellow into red edge");
    regionIs(p, 0, 0, 16); regionIs(p, 1, 16, 20);
    require(p.assign(16, 17, 3), "create orange at red edge");
    regionIs(p, 3, 16, 18); regionIs(p, 1, 18, 20);
    const auto beforeSeparate = p;
    require(!p.assign(24, 27, 3) && p == beforeSeparate, "reject second orange region");
    require(p.assign(16, 17, -1), "clear unused orange");
    require(!p.region(3).valid() && p.cells[16] == -1 && p.validate(), "clear leaves unassigned valid cells");
}
void unassignedAndAllColours()
{
    auto p = emptyPattern(1);
    require(p.validate(), "unassigned empty order is valid");
    for (int colour = 0; colour < colourCount; ++colour)
        require(p.assign(colour * 2, colour * 2 + 1, colour), "assign every palette colour");
    for (int colour = 0; colour < colourCount; ++colour)
    {
        regionIs(p, colour, colour * 2, colour * 2 + 2);
        require(p.insert(p.orderCount, colour), "insert every palette colour");
    }
    require(p.validate(), "all palette colours validate");
    require(!p.region(-1).valid() && !p.region(colourCount).valid(), "invalid colour has no region");
}
void assignmentRanges()
{
    auto p = emptyPattern();
    const auto original = p;
    const int ranges[][3] { {-1, 0, 0}, {0, 32, 0}, {8, 7, 0}, {0, 0, -2}, {0, 0, 8},
                            {INT_MIN, INT_MAX, 0} };
    for (const auto& range : ranges)
        require(!p.assign(range[0], range[1], range[2]) && p == original, "invalid range rejected atomically");
    require(p.assign(0, 0, 0) && p.assign(1, 1, 0), "adjacent one-cell assignments merge");
    regionIs(p, 0, 0, 2);
    require(p.assign(0, 0, -1), "clear unreferenced region edge");
    regionIs(p, 0, 1, 2);
}
void resizeScalesRegions()
{
    auto p = Pattern::makeDefault();
    require(p.resizeInput(4), "resize to four bars");
    regionIs(p, 0, 0, 24); regionIs(p, 1, 24, 40); regionIs(p, 2, 40, 64);
    require(p.resizeInput(1), "resize to one bar");
    regionIs(p, 0, 0, 6); regionIs(p, 1, 6, 10); regionIs(p, 2, 10, 16);
    for (int i = 16; i < maxCells; ++i) require(p.cells[i] == -1, "resize clears inactive cells");
    const auto original = p;
    require(!p.resizeInput(3) && p == original, "unsupported bars rejected atomically");
    require(p.resizeInput(1) && p == original, "same size is unchanged");
}
void resizeProtectsReferences()
{
    auto p = emptyPattern(4);
    // Boundaries 2/4 and 3/4 both round to cell 1 on the reduced window.
    require(p.assign(2, 2, 0) && p.insert(0, 0), "create tiny referenced colour");
    const auto original = p;
    require(!p.resizeInput(1) && p == original, "reject losing referenced colour on shrink");
    require(p.erase(0), "remove reference before shrink");
    require(p.resizeInput(1) && !p.region(0).valid() && p.validate(), "unused tiny region may disappear");
}
void orderEditing()
{
    auto p = emptyPattern(1);
    require(p.assign(0, 3, 0) && p.assign(4, 7, 1) && p.assign(8, 15, 2), "order fixture regions");
    require(p.insert(0, 0) && p.insert(1, 2) && p.insert(1, 1), "insert at middle shifts order");
    require(p.orderCount == 3 && p.order[0] == 0 && p.order[1] == 1 && p.order[2] == 2, "insert order");
    require(p.move(0, 3), "move first to end insertion boundary");
    require(p.order[0] == 1 && p.order[1] == 2 && p.order[2] == 0, "forward move adjusts insertion boundary");
    require(p.move(2, 0), "move last to beginning");
    require(p.order[0] == 0 && p.order[1] == 1 && p.order[2] == 2, "backward move");
    const auto original = p;
    require(p.move(1, 1) && p == original && p.move(1, 2) && p == original, "adjacent boundaries are no-op moves");
    require(!p.move(-1, 0) && !p.move(3, 0) && !p.move(0, 4) && !p.erase(3) && p == original, "invalid order edits atomic");
    require(!p.insert(-1, 0) && !p.insert(4, 0) && !p.insert(0, 3) && !p.insert(0, -1) && p == original, "invalid insert atomic");
    require(p.erase(1) && p.orderCount == 2 && p.order[1] == 2, "erase shifts remaining order");
    require(p.erase(0) && p.erase(0) && p.orderCount == 0 && p.validate(), "erase can leave empty valid order");
    require(expandSequence(p).count == 0, "empty order expands to silence");
}
void orderCapacity()
{
    auto p = emptyPattern(1);
    require(p.assign(0, 0, 0), "one-cell capacity fixture");
    for (int i = 0; i < maxOrder; ++i) require(p.insert(i, 0), "fill order capacity");
    const auto original = p;
    require(!p.insert(maxOrder, 0) && p == original && p.validate(), "full order cannot overflow");
    p.outputBars = 8;
    const auto seq = expandSequence(p);
    require(seq.count == 128 && seq.segments[127].start == 127 && seq.segments[127].length == 1, "maximum loop fits sequence capacity");
}
void repetitionAndCut()
{
    auto p = emptyPattern(1);
    p.outputBars = 1;
    require(p.assign(2, 6, 0) && p.assign(8, 10, 1), "five/three-cell phrase fixture");
    require(p.insert(0, 0) && p.insert(1, 1), "repeated arrangement fixture");
    auto seq = expandSequence(p);
    require(seq.count == 4, "whole arrangement repeats to output length");
    const double starts[] { 0, 5, 8, 13 };
    const double lengths[] { 5, 3, 5, 3 };
    for (int i = 0; i < 4; ++i)
    {
        const auto& s = seq.segments[i];
        require(s.start == starts[i] && s.length == lengths[i] && s.fullLength == lengths[i], "repeated timing");
        require(s.orderIndex == i % 2 && s.colour == i % 2 && s.repeated == (i >= 2), "repeated identities");
    }
    require(p.erase(1), "single five-cell repeat fixture");
    seq = expandSequence(p);
    require(seq.count == 4 && seq.segments[3].start == 15
            && seq.segments[3].length == 1 && seq.segments[3].fullLength == 5
            && seq.segments[3].repeated, "cut preserves full source length");
}
void beyondEndpointOrderSurvives()
{
    auto p = Pattern::makeDefault();
    p.outputBars = 1;
    const auto seq = expandSequence(p);
    require(seq.count == 2 && seq.segments[1].length == 4 && seq.segments[1].fullLength == 12,
            "long order cuts at loop endpoint");
    require(p.orderCount == 6 && p.erase(5) && p.orderCount == 5, "order beyond output stays editable");
}
void restOrderEditingAndDurations()
{
    auto p = emptyPattern(1);
    p.outputBars = 2;
    // Rests are order entries, independent of the eight coloured source regions.
    const int tokens[] {8, 9, 10, 11, 12};
    const int starts[] {0, 1, 3, 7, 15, 31};
    const int lengths[] {1, 2, 4, 8, 16, 1};
    for (int i = 0; i < 5; ++i)
    {
        require(isRest(tokens[i]) && restCells(tokens[i]) == lengths[i]
                && restToken(lengths[i]) == tokens[i], "rest selection resolves to explicit duration");
        require(p.insert(p.orderCount, restToken(lengths[i])), "insert a rest without assigning source audio");
        require(p.phraseLength(tokens[i]) == lengths[i], "rest phrase length does not require source cells");
    }
    for (const int invalid : {INT_MIN, -1, 0, 7, 13, INT_MAX})
        require(!isRest(invalid) && restCells(invalid) == 0, "non-rest token has no rest duration");
    for (const int invalid : {INT_MIN, -1, 0, 3, 5, 32, INT_MAX})
        require(restToken(invalid) == -1, "unsupported rest length cannot become an order token");
    require(p.validate(), "all-rest arrangement validates");
    const auto sequence = expandSequence(p);
    require(sequence.count == 6, "rest durations fill and repeat through fixed output length");
    for (int i = 0; i < sequence.count; ++i)
    {
        const auto& segment = sequence.segments[i];
        require(segment.start == starts[i] && segment.length == lengths[i], "rest timing uses explicit duration");
        require(segment.colour == tokens[i % 5] && segment.orderIndex == i % 5, "rest segment identity retained");
        require(segment.repeated == (i == 5), "repeated rest is identified");
        require(!p.region(segment.colour).valid(), "rest has no source region");
    }
    const auto before = p;
    require(!p.assign(0, 3, 8) && p == before, "rest cannot be assigned into source lane");
    require(!p.insert(0, 13) && p == before, "unknown rest token rejected atomically");
    require(p.move(4, 0) && p.order[0] == 12, "rest order can be moved");
    require(p.erase(0) && p.order[0] == 8 && p.order[p.orderCount] == -1, "rest removal clears inactive entry");
    require(p.resizeInput(4), "rests do not require a source region during resize");
    const auto resized = expandSequence(p);
    require(resized.segments[0].length == 1 && resized.segments[3].length == 8,
            "source resize preserves absolute rest durations");
}
void mixedRestsCutAndCapacity()
{
    auto p = emptyPattern(1); p.outputBars = 1;
    require(p.assign(0, 2, 0) && p.insert(0, 0) && p.insert(1, 12), "mixed audio and rest arrangement");
    require(p.phraseLength(0) == 3 && p.phraseLength(1) == 0 && p.phraseLength(-1) == 0
            && p.phraseLength(INT_MAX) == 0, "source phrase length derives from a valid assigned region");
    const auto sequence = expandSequence(p);
    require(sequence.count == 2 && sequence.segments[0].length == 3
            && sequence.segments[1].start == 3 && sequence.segments[1].length == 13
            && sequence.segments[1].fullLength == 16, "rest cuts cleanly at output boundary");
    p = emptyPattern(1); p.outputBars = 8;
    require(p.insert(0, 8), "shortest rest fills largest loop");
    const auto longest = expandSequence(p);
    require(longest.count == 128 && longest.segments[127].start == 127
            && longest.segments[127].length == 1, "rest expansion fits fixed sequence capacity");
}
void malformedPatterns()
{
    const auto valid = Pattern::makeDefault();
    for (int bars : {INT_MIN, -1, 0, 3, 5, INT_MAX})
    {
        auto p = valid; p.inputBars = bars;
        require(!p.validate() && !p.region(0).valid() && expandSequence(p).count == 0, "invalid input bars safely rejected");
    }
    for (int bars : {-1, 0, 3, 9, INT_MAX})
    {
        auto p = valid; p.outputBars = bars;
        require(!p.validate() && expandSequence(p).count == 0, "invalid output bars rejected");
    }
    for (int grid : {0, 3, 8, INT_MAX}) { auto p = valid; p.grid = grid; require(!p.validate(), "invalid grid rejected"); }
    for (int count : {INT_MIN, -1, 129, INT_MAX})
    {
        auto p = valid; p.orderCount = count;
        require(!p.validate() && expandSequence(p).count == 0, "invalid order count safely rejected");
        const auto original = p;
        require(!p.insert(0, 0) && !p.move(0, 1) && !p.erase(0)
                && !p.assign(0, 1, 0) && !p.resizeInput(1) && p == original, "mutators reject malformed source atomically");
    }
    for (int colour : {-2, 8, INT_MAX})
    {
        auto p = valid; p.cells[0] = colour;
        require(!p.validate() && expandSequence(p).count == 0, "bad active cell colour rejected");
        p = valid; p.order[0] = colour == 8 ? 13 : colour;
        require(!p.validate() && expandSequence(p).count == 0, "bad ordered colour rejected");
    }
    auto p = valid; p.cells[63] = 0; require(!p.validate(), "assigned inactive cell rejected");
    p = valid; p.cells[5] = -1; require(!p.validate(), "disconnected colour rejected");
    p = valid; p.order[0] = 7; require(!p.validate(), "missing ordered region rejected");
    p = valid; p.orderCount = 0; require(p.validate() && expandSequence(p).count == 0, "empty arrangement valid");
    std::string reason = "stale";
    require(valid.validate(&reason) && reason.empty(), "successful validation clears stale reason");
    p = valid; p.grid = 3;
    require(!p.validate(&reason) && !reason.empty(), "failure provides explanation");
}
template <typename P = Pattern>
void occurrenceLengthControls()
{
    if constexpr (requires(P p) { p.orderLengths; p.orderLength(0); p.setOrderLength(0, 0); })
    {
        P p = Pattern::makeDefault();
        require(p.orderLength(0) == 12 && p.orderLength(1) == 12, "Full defaults follow source length");
        std::string reason = "stale";
        require(p.setOrderLength(1, 8, &reason) && reason.empty(), "set occurrence cap");
        require(p.orderLength(0) == 12 && p.orderLength(1) == 8 && p.orderLengths[1] == 8,
                "a cap changes only its selected occurrence");
        require(p.phraseLength(0) == 12, "occurrence cap preserves source region duration");
        require(p.setOrderLength(1, 64) && p.orderLength(1) == 12 && p.orderLengths[1] == 64,
                "cap above source duration stays stored while playback clamps");
        require(p.setOrderLength(1, 0) && p.orderLength(1) == 12, "Full restores source duration");
        const auto before = p;
        for (const int cells : {INT_MIN, -1, 65, INT_MAX})
            require(!p.setOrderLength(1, cells, &reason) && p == before && !reason.empty(),
                    "invalid occurrence cap rejects atomically with a reason");
        for (const int index : {INT_MIN, -1, 6, maxOrder, INT_MAX})
            require(p.orderLength(index) == 0 && !p.setOrderLength(index, 4) && p == before,
                    "invalid occurrence index cannot read or modify a cap");
        require(p.insert(2, restToken(8)), "insert rest among capped sources");
        const auto beforeRest = p;
        require(p.orderLength(2) == 8 && !p.setOrderLength(2, 4) && p == beforeRest,
                "rest duration cannot be overridden by source playback length");
        require(p.setOrderLength(2, 0) && p == beforeRest, "rest accepts its unchanged Full sentinel");
        for (const int cells : {-1, 65})
        {
            auto bad = p; bad.orderLengths[0] = cells;
            require(!bad.validate() && expandSequence(bad).count == 0, "malformed cap rejects expansion");
            const auto invalid = bad;
            require(!bad.setOrderLength(0, 0) && bad == invalid, "cap setter rejects malformed pattern transactionally");
        }
        auto bad = p; bad.orderLengths[2] = 1;
        require(!bad.validate(), "rest cap is malformed");
        bad = p; bad.orderLengths[127] = 1;
        require(!bad.validate(), "inactive occurrence cap is malformed");
    }
    else require(false, "Pattern must support per-occurrence length controls");
}
void occurrenceLengthsExpandAndClamp()
{
    auto p = emptyPattern(1); p.outputBars = 2;
    require(p.assign(0, 11, 0) && p.insert(0, 0) && p.insert(1, 0) && p.insert(2, 0)
            && p.setOrderLength(1, 8), "three plus two plus three beat fixture");
    auto sequence = expandSequence(p);
    require(sequence.count == 3, "trimmed occurrences fill exactly eight beats");
    const int starts[] {0, 12, 20};
    const int lengths[] {12, 8, 12};
    for (int i = 0; i < 3; ++i)
        require(sequence.segments[i].start == starts[i] && sequence.segments[i].length == lengths[i]
                && sequence.segments[i].fullLength == lengths[i] && sequence.segments[i].orderIndex == i,
                "effective occurrence durations drive expansion and output cut metadata");
    p.outputBars = 1;
    sequence = expandSequence(p);
    require(sequence.count == 2 && sequence.segments[1].length == 4
            && sequence.segments[1].fullLength == 8, "loop cut retains chosen occurrence duration");
    require(p.assign(6, 11, -1) && p.orderLength(0) == 6 && p.orderLength(1) == 6
            && p.orderLengths[1] == 8, "shrinking a source clamps without replacing its absolute cap");
    require(p.assign(6, 11, 0) && p.orderLength(0) == 12 && p.orderLength(1) == 8,
            "extending source restores stored cap while Full follows new region");
    require(p.resizeInput(2) && p.orderLength(0) == 24 && p.orderLength(1) == 8
            && p.orderLengths[1] == 8, "input resize scales source regions but never absolute caps");
    require(p.resizeInput(1) && p.assign(0, 5, -1) && p.region(0).first == 6
            && p.orderLength(1) == 6, "shifting source start clamps selected occurrence duration");
}
void occurrenceLengthsFollowEdits()
{
    auto p = emptyPattern(1);
    require(p.assign(0, 7, 0) && p.assign(8, 15, 1) && p.insert(0, 0) && p.insert(1, 1)
            && p.setOrderLength(0, 3) && p.setOrderLength(1, 6), "paired occurrence fixture");
    require(p.insert(1, 0) && p.orderLengths[0] == 3 && p.orderLengths[1] == 0
            && p.orderLengths[2] == 6, "insert shifts existing caps and creates a Full occurrence");
    require(p.move(0, 3) && p.order[0] == 0 && p.orderLengths[0] == 0
            && p.order[1] == 1 && p.orderLengths[1] == 6 && p.order[2] == 0 && p.orderLengths[2] == 3,
            "forward move preserves each token and cap pair");
    require(p.move(2, 0) && p.orderLengths[0] == 3 && p.orderLengths[1] == 0
            && p.orderLengths[2] == 6, "backward move preserves each token and cap pair");
    const auto before = p;
    require(p.move(0, 1) && p == before && !p.move(0, 4) && p == before,
            "no-op and invalid moves preserve caps");
    require(p.erase(1) && p.orderCount == 2 && p.order[1] == 1 && p.orderLengths[1] == 6
            && p.order[2] == -1 && p.orderLengths[2] == 0 && p.validate(),
            "erase shifts paired cap and clears vacated inactive fields");
    require(p.erase(0) && p.erase(0) && p.orderLengths[0] == 0 && p.orderLengths[1] == 0 && p.validate(),
            "erasing all occurrences clears every prior cap");
}
template <typename P = Pattern>
void duplicatePreservesLength()
{
    if constexpr (requires(P p) { p.duplicate(0); })
    {
        P p = emptyPattern(1);
        require(p.assign(0, 7, 0) && p.insert(0, 0) && p.setOrderLength(0, 3)
                && p.insert(1, restToken(4)), "duplicate fixture");
        require(p.duplicate(0) && p.orderCount == 3 && p.order[1] == 0 && p.orderLengths[1] == 3
                && p.order[2] == restToken(4) && p.orderLengths[2] == 0, "duplicate retains selected source cap");
        require(p.duplicate(2) && p.order[3] == restToken(4) && p.orderLengths[3] == 0,
                "duplicate rest retains duration without source cap");
        const auto before = p;
        require(!p.duplicate(-1) && !p.duplicate(4) && p == before, "invalid duplicate is transactional");
        while (p.orderCount < maxOrder) require(p.duplicate(0), "fill duplicate capacity");
        const auto full = p;
        std::string reason;
        require(!p.duplicate(0, &reason) && p == full && !reason.empty(), "full duplicate rejects transactionally");
    }
    else require(false, "Pattern must duplicate an occurrence with its length");
}

void inactiveVisualIs(const PlaybackVisual& visual)
{
    require(visual.sequenceIndex == -1 && visual.orderIndex == -1 && visual.colour == -1
            && visual.progress == 0.0 && visual.pulse == 0.0f,
            "inactive visual must clear every highlight and progress value");
}
void playbackOccurrenceRetriggers()
{
    auto p = emptyPattern(1); p.outputBars = 2;
    require(p.assign(0, 7, 2) && p.insert(0, 2) && p.insert(1, 2), "adjacent same-colour fixture");
    // Two eight-cell occurrences repeat to fill 32 cells. Each boundary is a fresh attack.
    for (const auto test : { std::pair {0.0, 0}, {0.25, 1}, {0.5, 2}, {0.75, 3}, {0.0, 0} })
    {
        const auto visual = playbackVisual(p, test.first, 120.0, 4, 4, true);
        require(visual.sequenceIndex == test.second && visual.orderIndex == test.second % 2
                && visual.colour == 2 && visual.progress == 0.0 && visual.pulse == 1.0f,
                "same-colour occurrences, sequence repeats and loop wraps retrigger independently");
    }
    const auto nearEnd = playbackVisual(p, 0.749, 120.0, 4, 4, true);
    require(nearEnd.sequenceIndex == 2 && nearEnd.orderIndex == 0
            && std::abs(nearEnd.progress - 0.996) < 1.0e-9 && nearEnd.pulse == 0.0f,
            "the segment immediately before a boundary retains its own progress");
}
void playbackTrimmedProgress()
{
    auto p = emptyPattern(1); p.outputBars = 1;
    require(p.assign(0, 11, 0) && p.insert(0, 0) && p.insert(1, 0)
            && p.setOrderLength(0, 8), "trimmed and loop-cut fixture");
    const auto trimmed = playbackVisual(p, 0.25, 120.0, 4, 4, true);
    require(trimmed.sequenceIndex == 0 && trimmed.orderIndex == 0 && trimmed.colour == 0
            && trimmed.progress == 0.5, "trimmed occurrence progress uses its effective eight-cell length");
    const auto cut = playbackVisual(p, 0.75, 120.0, 4, 4, true);
    require(cut.sequenceIndex == 1 && cut.orderIndex == 1 && cut.colour == 0 && cut.progress == 0.5,
            "loop-cut progress uses the eight audible cells rather than the twelve-cell source");
    const auto edge = playbackVisual(p, std::nextafter(1.0, 0.0), 120.0, 4, 4, true);
    require(edge.sequenceIndex == 1 && edge.progress > 0.999 && edge.progress <= 1.0
            && edge.pulse == 0.0f, "last representable phase keeps bounded occurrence progress");
}
void playbackPulseTiming()
{
    auto p = emptyPattern(1); p.outputBars = 1;
    require(p.assign(0, 15, 0) && p.insert(0, 0), "pulse timing fixture");
    struct Case { double phase; double bpm; int numerator; int denominator; };
    const Case cases[] { {0.035, 120.0, 4, 4}, {0.0175, 60.0, 4, 4},
                         {7.0 / 75.0, 120.0, 3, 8}, {0.04, 120.0, 7, 8},
                         {0.07, 120.0, 64, 128} };
    for (const auto test : cases)
    {
        const auto visual = playbackVisual(p, test.phase, test.bpm, test.numerator, test.denominator, true);
        require(visual.sequenceIndex == 0 && std::abs(visual.pulse - 0.5f) < 1.0e-6f,
                "pulse reaches half brightness at 70ms across tempos and meters");
    }
    require(playbackVisual(p, 0.07, 120.0, 4, 4, true).pulse == 0.0f
            && playbackVisual(p, 0.9, 120.0, 4, 4, true).pulse == 0.0f,
            "pulse ends at 140ms and stays off for the remainder of the occurrence");
    require(p.setOrderLength(0, 1), "short occurrence fixture");
    const auto shortPulse = playbackVisual(p, 0.03125, 120.0, 4, 4, true);
    require(shortPulse.sequenceIndex == 0 && shortPulse.progress == 0.5 && shortPulse.pulse == 0.5f,
            "a 125ms occurrence caps the pulse to its own duration");
    const auto shortRestart = playbackVisual(p, 0.0625, 120.0, 4, 4, true);
    require(shortRestart.sequenceIndex == 1 && shortRestart.orderIndex == 0 && shortRestart.pulse == 1.0f,
            "a repeated short occurrence restarts its capped pulse");
}
void playbackRestHighlight()
{
    auto p = emptyPattern(1); p.outputBars = 1;
    require(p.assign(0, 3, 6) && p.insert(0, 6) && p.insert(1, restToken(4)), "rest highlight fixture");
    const auto restStart = playbackVisual(p, 0.25, 120.0, 4, 4, true);
    require(restStart.sequenceIndex == 1 && restStart.orderIndex == 1 && restStart.colour == -1
            && restStart.progress == 0.0 && restStart.pulse == 1.0f,
            "rest onset highlights its Order occurrence without flashing an Input colour");
    const auto rest = playbackVisual(p, 0.875, 120.0, 4, 4, true);
    require(rest.sequenceIndex == 3 && rest.orderIndex == 1 && rest.colour == -1
            && rest.progress == 0.5 && rest.pulse == 0.0f,
            "repeated rests retain their Order index and audible progress");
    const auto source = playbackVisual(p, 0.5, 120.0, 4, 4, true);
    require(source.sequenceIndex == 2 && source.orderIndex == 0 && source.colour == 6 && source.pulse == 1.0f,
            "source following a rest flashes at its repeated occurrence onset");
}
void playbackInactiveAndInvalidTiming()
{
    const auto p = Pattern::makeDefault();
    require(playbackVisual(p, 0.0, 120.0, 4, 4, true).sequenceIndex == 0,
            "valid active timing must produce a highlight before its gating is checked");
    inactiveVisualIs(playbackVisual(p, 0.0, 120.0, 4, 4, false));
    inactiveVisualIs(playbackVisual(p, 0.5, 120.0, 4, 4, false));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double phase : {-1.0, 1.0, 2.0, nan, inf, -inf})
        inactiveVisualIs(playbackVisual(p, phase, 120.0, 4, 4, true));
    for (const double bpm : {-1.0, 0.0, 10001.0, nan, inf, -inf, std::numeric_limits<double>::denorm_min()})
        inactiveVisualIs(playbackVisual(p, 0.0, bpm, 4, 4, true));
    for (const int numerator : {INT_MIN, -1, 0, 129, INT_MAX})
        inactiveVisualIs(playbackVisual(p, 0.0, 120.0, numerator, 4, true));
    for (const int denominator : {INT_MIN, -1, 0, 3, 129, INT_MAX})
        inactiveVisualIs(playbackVisual(p, 0.0, 120.0, 4, denominator, true));
    inactiveVisualIs(playbackVisual(emptyPattern(), 0.0, 120.0, 4, 4, true));
    auto malformed = p; malformed.outputBars = INT_MAX;
    inactiveVisualIs(playbackVisual(malformed, 0.0, 120.0, 4, 4, true));
    malformed = p; malformed.orderCount = INT_MAX;
    inactiveVisualIs(playbackVisual(malformed, 0.0, 120.0, 4, 4, true));
}
}

int main()
{
    int failures = 0;
    const struct { const char* name; void (*run)(); } tests[] {
        {"defaults and expansion", defaultsAndExpansion}, {"atomic assignment", assignmentIsTransactional},
        {"sound control defaults and identity", soundControlDefaultsAndIdentity<>},
        {"sound controls validate before editing", soundControlsValidateBeforeEditing<>},
        {"sound controls survive arrangement edits", soundControlsSurviveArrangementEdits<>},
        {"unassigned and palette", unassignedAndAllColours}, {"assignment ranges", assignmentRanges},
        {"resize normalized regions", resizeScalesRegions}, {"resize protects references", resizeProtectsReferences},
        {"order editing", orderEditing}, {"order capacity", orderCapacity}, {"repetition and cut", repetitionAndCut},
        {"order beyond endpoint", beyondEndpointOrderSurvives}, {"malformed patterns", malformedPatterns},
        {"rest order editing and durations", restOrderEditingAndDurations},
        {"mixed rest cuts and capacity", mixedRestsCutAndCapacity},
        {"occurrence length controls", occurrenceLengthControls<>},
        {"occurrence expansion and clamp", occurrenceLengthsExpandAndClamp},
        {"occurrence lengths follow edits", occurrenceLengthsFollowEdits},
        {"duplicate preserves length", duplicatePreservesLength<>},
        {"playback occurrence retriggers", playbackOccurrenceRetriggers},
        {"playback trimmed progress", playbackTrimmedProgress},
        {"playback pulse timing", playbackPulseTiming},
        {"playback rest highlight", playbackRestHighlight},
        {"playback inactive and invalid timing", playbackInactiveAndInvalidTiming}
    };
    for (const auto& test : tests)
    {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << test.name << ": " << e.what() << '\n'; }
    }
    std::cout << "Model tests: " << (sizeof(tests) / sizeof(tests[0]) - failures) << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
