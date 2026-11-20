#include "PatternLibrary.h"
#include <algorithm>
#include <initializer_list>
#include <numeric>
#include <random>
#include <stdexcept>

namespace breakvar
{
namespace
{
bool fail(std::string* reason, const char* message)
{
    if (reason != nullptr) *reason = message;
    return false;
}

// Explicit rejection sampling keeps seeded results independent of standard
// library implementations of uniform_int_distribution and shuffle.
class Random
{
public:
    explicit Random(std::uint32_t seed) : engine(seed) {}
    int below(int bound)
    {
        const auto n = static_cast<std::uint32_t>(bound);
        const auto threshold = (std::uint32_t{0} - n) % n;
        std::uint32_t value;
        do { value = engine(); } while (value < threshold);
        return static_cast<int>(value % n);
    }
    template <typename Iterator> void shuffle(Iterator first, Iterator last)
    {
        for (auto count = last - first; count > 1; --count)
            std::iter_swap(first + count - 1, first + below(static_cast<int>(count)));
    }
private:
    std::mt19937 engine;
};

bool publish(Pattern& destination, const Pattern& proposed, std::string* reason)
{
    if (!proposed.validate(reason)) return false;
    destination = proposed;
    return true;
}

Pattern makeFactory(int inputBars, int outputBars, int grid,
                    std::initializer_list<int> lengths, std::initializer_list<int> order)
{
    auto p = Pattern::makeDefault();
    p.inputBars = inputBars;
    p.outputBars = outputBars;
    p.grid = grid;
    p.cells.fill(-1);
    p.order.fill(-1);
    int cell = 0, colour = 0;
    for (int length : lengths)
    {
        std::fill_n(p.cells.begin() + cell, length, colour++);
        cell += length;
    }
    p.orderCount = static_cast<int>(order.size());
    std::copy(order.begin(), order.end(), p.order.begin());
    return p;
}

// A full numbered beat, an eighth-note onset, a sixteenth-note onset, or
// deliberate silence. Source beats 4..7 use the second bar when available;
// on a one-bar source they return to beats 0..3. These are rhythmic positions,
// not detected drum classes. Each occurrence can retain its own play length.
struct Hit
{
    int beat;
    int cells;
    Hit(int sourceBeat) : beat(sourceBeat), cells(4) {}
    Hit(int sourceBeat, int length) : beat(sourceBeat), cells(length) {}
};
Hit half(int beat) { return {beat, 2}; }
Hit tick(int beat) { return {beat, 1}; }
Hit rest(int cells) { return {-1, cells}; }

Pattern makeBeatArrangement(int sourceBars, std::initializer_list<Hit> hits)
{
    auto p = Pattern::makeDefault();
    p.inputBars = sourceBars;
    p.grid = 4;
    p.cells.fill(-1);
    p.order.fill(-1);
    p.orderLengths.fill(0);
    for (int cell = 0; cell < p.activeCells(); ++cell) p.cells[cell] = cell / 4;
    p.orderCount = 0;
    int duration = 0;
    for (const auto hit : hits)
    {
        if (p.orderCount >= maxOrder) throw std::logic_error("Factory order exceeds capacity");
        const auto index = p.orderCount++;
        p.order[index] = hit.beat < 0 ? restToken(hit.cells) : hit.beat % (sourceBars * 4);
        if (hit.beat >= 0 && hit.cells != 4) p.orderLengths[index] = hit.cells;
        duration += hit.cells;
    }
    p.outputBars = duration / cellsPerBar;
    if (duration % cellsPerBar != 0 || !p.validate())
        throw std::logic_error("Factory recipe must fill whole supported output bars");
    return p;
}

void appendCategorisedFactories(std::vector<NamedPattern>& patterns)
{
    for (const int bars : {1, 2})
    {
        const auto add = [&](const char* category, const char* name, std::initializer_list<Hit> hits)
        {
            patterns.push_back({std::string(bars == 1 ? "One bar - " : "Two bars - ") + name,
                                makeBeatArrangement(bars, hits), category});
        };

        // DnB: keep the beat-two/four anchors while redirecting kicks and short
        // replies. Both source bars contribute when a two-bar break is loaded.
        add("DnB", "Rolling kick reply", {0,1,half(2),half(0),3, 4,5,6,tick(7),tick(7),tick(7),tick(7)});
        add("DnB", "Snare redirect", {0,half(1),half(1),2,3, 4,7,half(6),half(4),5});
        add("DnB", "Split step driver", {0,1,half(0),half(2),3, 4,5,half(4),half(6),half(7),half(5)});
        add("DnB", "Kick pickup gaps", {half(0),half(0),1,2,3, 4,5,tick(4),rest(1),tick(6),rest(1),7});
        add("DnB", "Broken snare reply", {0,1,2,half(3),half(1), 4,half(5),half(5),6,half(7),half(3)});
        add("DnB", "Double downbeat", {half(0),half(0),1,half(2),half(2),3, 4,5,6,half(7),half(7)});
        add("DnB", "Late kick answer", {0,1,half(2),rest(2),3, half(4),half(6),5,6,7});
        add("DnB", "Backbeat machine", {0,tick(1),tick(1),half(1),2,3, 4,5,half(6),half(4),7});
        add("DnB", "Crossbar pressure", {0,5,2,half(7),half(3), 4,1,half(0),half(6),3});
        add("DnB", "Push pull return", {0,1,half(2),half(0),half(3),half(3), 4,half(5),rest(2),6,7});

        // Jungle: asymmetrical beat order, short backbeat retriggers and
        // interrupted replies. Eighth and sixteenth caps retain useful tails
        // elsewhere instead of forcing the entire source into tiny slices.
        add("Jungle", "Ghost train relay", {0,half(1),tick(3),tick(1),2,half(3),half(1), 4,tick(5),tick(7),half(5),6,half(7),half(3)});
        add("Jungle", "Ragga switchback", {0,3,half(2),half(0),1, 4,7,tick(5),tick(5),half(6),3});
        add("Jungle", "Snare pinball", {0,half(1),half(3),2,tick(1),tick(3),half(1), 4,half(7),half(5),6,tick(7),tick(5),half(7)});
        add("Jungle", "Broken amen answer", {0,1,half(3),half(2),tick(1),tick(1),half(0), 4,5,half(7),rest(2),tick(5),tick(5),half(6)});
        add("Jungle", "Reverse beat chase", {0,3,2,1, 4,half(7),half(5),half(6),half(4),tick(7),tick(7),half(5)});
        add("Jungle", "Ghost kick ladder", {0,half(1),half(1),tick(0),tick(2),half(0),3, 4,half(5),half(7),tick(4),tick(6),half(4),7});
        add("Jungle", "Rinse and cut", {0,1,2,tick(3),rest(1),tick(1),rest(1), 4,half(5),half(5),6,tick(7),tick(5),tick(7),rest(1)});
        add("Jungle", "Backspin fragments", {0,half(3),half(2),1,half(2),half(1), 4,half(7),half(6),5,tick(7),tick(5),half(3)});
        add("Jungle", "Scatter snare rally", {half(0),half(2),1,tick(3),tick(1),half(3),2, 4,tick(5),tick(7),half(5),half(6),half(4),7});
        add("Jungle", "Final ghost return", {0,half(1),half(3),2,3, 4,5,tick(6),tick(4),half(6),tick(7),tick(7),tick(5),tick(3)});

        // Hip-hop: the source retains its own groove; these patterns change
        // phrase placement and breathing space, without imposing swing DSP.
        add("Hip-hop", "Pocket kick repeat", {half(0),rest(2),1,2,3, 4,5,half(4),rest(2),7});
        add("Hip-hop", "Dusty double snare", {0,half(1),rest(2),2,3, 4,half(5),half(5),6,rest(4)});
        add("Hip-hop", "Half time pockets", {0,rest(4),1,2, 4,rest(4),5,half(6),half(4)});
        add("Hip-hop", "Boom bap cutback", {0,1,half(2),rest(2),3, half(4),half(4),5,rest(4),7});
        add("Hip-hop", "Crate chop reply", {0,3,2,rest(4), 4,1,half(6),half(4),5});
        add("Hip-hop", "Lazy beat pickup", {0,1,rest(2),half(2),3, 4,5,rest(2),half(4),half(7),rest(2)});
        add("Hip-hop", "Kick space snare", {half(0),rest(2),1,rest(4),3, 4,half(5),rest(2),6,7});
        add("Hip-hop", "Tape stop pockets", {0,1,2,rest(4), half(4),rest(2),5,6,rest(4)});
        add("Hip-hop", "Staggered backbeat", {0,half(1),half(1),rest(4),3, 4,5,half(6),rest(2),half(7),half(5)});
        add("Hip-hop", "Snare crumb pickup", {0,1,2,tick(3),rest(1),half(1), 4,5,rest(4),half(7),rest(2)});

        // Buildups run for four output bars. Retrigger density increases toward
        // the last bar; the input remains a one- or two-bar live source window.
        add("Buildups", "Kick pressure ladder", {
            0,1,2,3,  half(4),half(4),5,half(6),half(6),7,
            half(0),half(0),half(0),half(0),half(1),half(1),half(1),half(1),
            tick(4),tick(4),tick(4),tick(4),tick(4),tick(4),tick(4),tick(4),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5)});
        add("Buildups", "Snare pressure ladder", {
            0,1,2,3,  4,half(5),half(5),6,half(7),half(7),
            0,tick(1),tick(1),tick(1),tick(1),2,tick(3),tick(3),tick(3),tick(3),
            tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5),tick(5)});
        add("Buildups", "Alternating ignition", {
            0,1,2,3,  4,5,half(6),half(6),half(7),half(7),
            half(0),half(1),half(2),half(3),half(0),half(1),half(2),half(3),
            tick(4),tick(5),tick(4),tick(5),tick(4),tick(5),tick(4),tick(5),tick(6),tick(7),tick(6),tick(7),tick(6),tick(7),tick(6),tick(7)});
        add("Buildups", "Gated tension climb", {
            0,rest(4),1,rest(4),  half(4),rest(2),half(5),rest(2),half(6),rest(2),half(7),rest(2),
            half(0),half(1),half(0),half(1),half(2),half(3),half(2),half(3),
            tick(4),tick(4),tick(5),tick(5),tick(4),tick(4),tick(5),tick(5),tick(6),tick(6),tick(7),tick(7),tick(6),tick(6),tick(7),tick(7)});
        add("Buildups", "Kick to snare ascent", {
            0,0,2,3,  half(4),half(4),half(4),half(4),6,7,
            half(0),half(0),half(1),half(1),half(2),half(2),half(3),half(3),
            tick(4),tick(4),tick(4),tick(4),tick(5),tick(5),tick(5),tick(5),tick(7),tick(7),tick(7),tick(7),tick(5),tick(5),tick(5),tick(5)});
        add("Buildups", "Backbeat compression", {
            0,1,2,1,  4,half(5),half(5),6,half(5),half(5),
            half(1),half(1),half(3),half(3),half(1),half(1),half(3),half(3),
            tick(5),tick(7),tick(5),tick(7),tick(5),tick(7),tick(5),tick(7),tick(5),tick(7),tick(5),tick(7),tick(5),tick(7),tick(5),tick(7)});
        add("Buildups", "Tail spiral", {
            0,1,2,3,  4,5,half(7),half(6),half(7),half(6),
            half(3),half(2),half(1),half(0),half(3),half(2),half(1),half(0),
            tick(7),tick(6),tick(5),tick(4),tick(7),tick(6),tick(5),tick(4),tick(7),tick(6),tick(5),tick(4),tick(7),tick(6),tick(5),tick(4)});
        add("Buildups", "Eight to sixteen", {
            0,1,2,3,  half(4),half(5),half(6),half(7),half(4),half(5),half(6),half(7),
            half(0),half(1),half(2),half(3),tick(0),tick(1),tick(2),tick(3),tick(0),tick(1),tick(2),tick(3),
            tick(4),tick(4),tick(6),tick(6),tick(5),tick(5),tick(7),tick(7),tick(4),tick(4),tick(6),tick(6),tick(5),tick(5),tick(7),tick(7)});
        add("Buildups", "Snare gate opens", {
            0,rest(4),1,rest(4),  4,half(5),rest(2),6,half(7),rest(2),
            half(1),rest(2),half(1),rest(2),tick(3),tick(3),half(3),tick(1),tick(1),half(1),
            tick(5),tick(5),tick(5),tick(5),tick(7),tick(7),tick(7),tick(7),tick(5),tick(5),tick(5),tick(5),tick(7),tick(7),tick(7),tick(7)});
        add("Buildups", "Last bar overload", {
            0,1,2,3,  4,5,6,half(7),half(7),
            0,half(1),half(1),half(2),half(2),tick(3),tick(3),tick(3),tick(3),
            tick(4),tick(5),tick(6),tick(7),tick(7),tick(6),tick(5),tick(4),tick(4),tick(4),tick(5),tick(5),tick(6),tick(6),tick(7),tick(7)});

        // Breakdowns intentionally leave room: sparse hits, half-bar dropouts
        // and suspended answers, rather than a silent copy of a normal loop.
        add("Breakdowns", "Kick and air", {0,rest(4),rest(4),3, 4,rest(4),5,rest(4)});
        add("Breakdowns", "Suspended backbeat", {rest(4),1,rest(8), 4,rest(4),7,rest(4)});
        add("Breakdowns", "Half bar breathing", {0,1,rest(8), 4,rest(8),5});
        add("Breakdowns", "Snare islands", {half(1),rest(2),rest(4),3,rest(4), half(5),rest(2),rest(8),7});
        add("Breakdowns", "Broken heartbeat", {half(0),rest(2),half(0),rest(2),rest(8), 4,rest(4),half(5),rest(2),rest(4)});
        add("Breakdowns", "Vanishing tail", {0,1,half(2),rest(2),rest(4), half(4),rest(2),half(5),rest(2),rest(8)});
        add("Breakdowns", "Drop to one hit", {0,1,2,rest(4), 4,rest(4),rest(8)});
        add("Breakdowns", "Empty middle", {0,rest(8),3, 4,rest(8),half(7),rest(2)});
        add("Breakdowns", "Kick snare shadows", {tick(0),rest(1),rest(2),half(1),rest(2),rest(8), tick(4),rest(1),rest(2),half(7),rest(2),rest(8)});
        add("Breakdowns", "Late reply gap", {0,rest(4),rest(8), rest(8),5,half(7),rest(2)});

        // Shuffles exchange whole beats and half-beat replies. They do not
        // time-stretch, quantise, or impose a swing curve on the source audio.
        add("Shuffles", "Inside out bars", {0,2,1,3, 4,6,7,5});
        add("Shuffles", "Backbeat mirror", {0,3,2,1, 4,5,7,6});
        add("Shuffles", "Kick snare crossing", {0,5,2,7, 4,3,1,6});
        add("Shuffles", "Pair exchange", {2,3,0,1, 6,5,4,7});
        add("Shuffles", "Forward backward", {0,2,3,1, 7,6,4,5});
        add("Shuffles", "Offbeat handover", {half(0),half(2),1,3,2, half(4),half(6),7,5,6});
        add("Shuffles", "Three against four", {0,1,2,0, 5,6,4,7});
        add("Shuffles", "Snare first response", {1,0,3,2, 5,6,7,4});
        add("Shuffles", "Zigzag phrase", {0,3,1,2, 6,5,4,7});
        add("Shuffles", "Half beat weave", {0,half(3),half(1),2,half(1),half(3), 4,half(7),half(5),half(6),half(4),7});

        // Fills keep the first bar and develop the ending. Literal source
        // positions keep the familiar kick/snare assumptions easy to edit.
        add("Fills", "Last beat snare spray", {0,1,2,3, 4,5,6,tick(5),tick(7),tick(5),tick(7)});
        add("Fills", "Kick snare flourish", {0,1,2,3, 4,5,half(4),half(6),tick(5),tick(5),half(7)});
        add("Fills", "Three beat then rush", {0,1,2,3, 4,5,6,half(7),tick(5),tick(5)});
        add("Fills", "Tail ricochet", {0,1,2,3, 4,5,half(7),half(6),half(5),half(7)});
        add("Fills", "Stop before return", {0,1,2,3, 4,5,tick(6),tick(6),half(4),rest(4)});
        add("Fills", "Kick drum rattle", {0,1,2,3, 4,5,tick(4),tick(4),tick(6),tick(6),half(7),half(5)});
        add("Fills", "Snare call and cut", {0,1,2,3, 4,half(5),half(7),6,tick(5),rest(1),tick(7),rest(1)});
        add("Fills", "Double tail comeback", {0,1,2,3, 4,5,half(7),half(7),half(4),half(5)});
        add("Fills", "Ghost note cascade", {0,1,2,3, 4,5,tick(7),tick(6),tick(5),tick(4),tick(5),tick(6),tick(7),tick(7)});
        add("Fills", "Backbeat breakaway", {0,1,2,3, 4,half(7),half(5),half(6),rest(2),tick(5),tick(7),half(4)});
    }
}

Pattern makeKickBuild(int sourceBars, const std::array<int, 3>& hitsPerRate, bool gated)
{
    // Keep the first source beat as the kick throughout, including on two-bar
    // input. The remaining source stays intact and available for manual edits.
    auto p = makeFactory(sourceBars, 2, 4, {4, sourceBars * cellsPerBar - 4}, {});
    const auto append = [&](int token, int cap = 0)
    {
        if (p.orderCount >= maxOrder) throw std::logic_error("Kick build exceeds order capacity");
        p.order[p.orderCount] = token;
        p.orderLengths[p.orderCount++] = cap;
    };
    int duration = 0;
    for (int rate = 0; rate < 3; ++rate)
    {
        const int spacing = 4 >> rate; // Quarter, eighth, then sixteenth.
        for (int hit = 0; hit < hitsPerRate[rate]; ++hit)
        {
            append(0, gated ? 1 : spacing == 4 ? 0 : spacing);
            if (gated && spacing == 4) append(restToken(2));
            if (gated && spacing > 1) append(restToken(1));
            duration += spacing;
        }
    }
    if (duration != 32 || !p.validate())
        throw std::logic_error("Kick build must fill exactly two output bars");
    return p;
}

Pattern makeEndShuffle(int sourceBars, const std::array<int, 4>& tail)
{
    // Only the final beat is divided into sixteenths. One contiguous opening
    // region preserves the first twelve or twenty-eight source cells intact.
    auto p = makeFactory(sourceBars, sourceBars, 1,
                         {sourceBars * cellsPerBar - 4, 1, 1, 1, 1}, {0});
    for (const int cell : tail) p.order[p.orderCount++] = cell + 1;
    if (!p.validate()) throw std::logic_error("End shuffle must use contiguous source regions");
    return p;
}

void appendKickBuildsAndEndShuffles(std::vector<NamedPattern>& patterns)
{
    struct Build { const char* name; std::array<int, 3> hitsPerRate; bool gated; };
    constexpr std::array<Build, 6> builds {{
        {"Even ramp", {4,4,8}, false}, {"Late rush", {6,2,4}, false},
        {"Early rush", {2,4,16}, false}, {"Stepped ramp", {3,6,8}, false},
        {"Gated even ramp", {4,4,8}, true}, {"Gated late rush", {6,2,4}, true}
    }};
    struct Shuffle { const char* name; std::array<int, 4> tail; };
    constexpr std::array<Shuffle, 8> shuffles {{
        {"Last eighth flip", {0,1,3,2}}, {"Early flick", {1,0,2,3}},
        {"Middle flick", {0,2,1,3}}, {"Eighth swap", {2,3,0,1}},
        {"Turnback", {0,3,2,1}}, {"Tail rotate", {1,2,3,0}},
        {"Tail return", {3,0,1,2}}, {"Crossed pairs", {1,0,3,2}}
    }};
    for (const int bars : {1, 2})
    {
        const std::string prefix = bars == 1 ? "One bar - " : "Two bars - ";
        for (const auto& build : builds)
            patterns.push_back({prefix + "Kick build - " + build.name,
                                makeKickBuild(bars, build.hitsPerRate, build.gated), "Buildups"});
        for (const auto& shuffle : shuffles)
            patterns.push_back({prefix + "End shuffle - " + shuffle.name,
                                makeEndShuffle(bars, shuffle.tail), "Fills"});
    }
}
}
const std::vector<NamedPattern>& factoryPatterns()
{
    static const std::vector<NamedPattern> patterns = []
    {
    std::vector<NamedPattern> result {
        { "Original", Pattern::makeDefault() },
        { "One bar - Straight", makeFactory(1, 1, 4, {4,4,4,4}, {0,1,2,3}) },
        { "One bar - Backbeat stutter", makeFactory(1, 2, 2, {4,2,2,4,2,2}, {0,1,1,3,4,4,0,2,3,5}) },
        { "One bar - Half swap", makeFactory(1, 1, 4, {8,8}, {1,0}) },
        { "One bar - Quarter rotation", makeFactory(1, 1, 4, {4,4,4,4}, {1,2,3,0}) },
        { "One bar - Tail fill", makeFactory(1, 2, 1, {8,4,2,1,1}, {0,1,2,3,4,0,1,3,4,3,4}) },
        { "One bar - Skip and repeat", makeFactory(1, 1, 2, {4,4,4,4}, {0,0,2,3}) },
        { "Two bars - Straight", makeFactory(2, 2, 4, {8,8,8,8}, {0,1,2,3}) },
        { "Two bars - Backbeat echoes", makeFactory(2, 4, 2, {8,4,4,8,4,4}, {0,1,1,2,3,4,4,5}) },
        { "Two bars - Bar swap", makeFactory(2, 2, 4, {16,16}, {1,0}) },
        { "Two bars - Phrase rotation", makeFactory(2, 2, 4, {8,8,8,8}, {2,3,0,1}) },
        { "Two bars - Closing fill", makeFactory(2, 4, 2, {8,8,8,4,2,2}, {0,1,2,3,4,5,0,1,2,4,5,4,5}) },
        { "Two bars - Broken return", makeFactory(2, 2, 4, {12,4,8,8}, {0,2,1,3}) },

        // Keep the original bank above in place: factory selection indices and
        // saved factory selections rely on those positions. Three beats are
        // always twelve source cells, including when the source is two bars.
        { "One bar - First three beats", makeFactory(1, 2, 4, {12,4}, {0}) },
        { "One bar - Three beat turnaround", makeFactory(1, 2, 4, {4,4,4,4}, {0,1,2,0,2,1}) },
        { "One bar - Three beat tail", makeFactory(1, 2, 4, {4,4,4,4}, {0,1,2,0,1,2,3,3}) },
        { "Two bars - First three beats", makeFactory(2, 4, 4, {12,20}, {0}) },
        { "Two bars - Three beat pendulum", makeFactory(2, 4, 4, {12,4,12,4}, {0,2}) },
        { "Two bars - Three beat answer", makeFactory(2, 2, 4, {12,4,12,4}, {0,0,3,1}) },

        // Grid-based starting points for breaks with a downbeat at the source
        // start. Names describe rhythmic intent; no drum-hit detection is used.
        { "One bar - Jungle backbeat", makeFactory(1, 2, 2, {4,2,2,4,2,2}, {0,1,2,3,4,5,0,1,1,3,5,4}) },
        { "One bar - Jungle ghost steps", makeFactory(1, 2, 2, {4,2,2,2,2,2,2}, {0,1,2,3,4,5,6,0,1,2,4,3,6,5}) },
        { "One bar - Jungle snare rush", makeFactory(1, 2, 1, {4,2,2,4,1,1,1,1}, {0,1,2,3,4,5,6,7,0,1,2,3,4,4,6,6}) },
        { "One bar - Jungle tail switch", makeFactory(1, 2, 2, {4,4,4,2,2}, {0,1,2,3,4,0,1,4,3,2}) },
        { "One bar - DnB kick return", makeFactory(1, 2, 2, {2,2,4,4,2,2}, {0,1,2,3,4,5,0,0,2,1,1,4,5}) },
        { "One bar - DnB snare answer", makeFactory(1, 2, 2, {4,2,2,4,2,2}, {0,1,2,3,4,5,0,4,5,3,1,2}) },
        { "One bar - DnB two step", makeFactory(1, 2, 2, {4,4,2,2,4}, {0,1,2,2,4,0,1,3,2,4}) },
        { "Two bars - Jungle backbeat relay", makeFactory(2, 2, 4, {4,4,4,4,4,4,4,4}, {0,1,2,5,4,1,6,7}) },
        { "Two bars - Jungle ghost relay", makeFactory(2, 2, 2, {4,4,4,2,2,4,4,8}, {0,1,2,4,3,5,6,7}) },
        { "Two bars - Jungle break exchange", makeFactory(2, 2, 4, {4,4,8,4,4,8}, {0,4,2,3,1,5}) },
        { "Two bars - Jungle last beat rush", makeFactory(2, 2, 1, {8,8,8,4,1,1,1,1}, {0,1,2,3,4,4,6,6}) },
        { "Two bars - DnB kick conversation", makeFactory(2, 2, 2, {2,2,4,8,2,2,4,8}, {0,1,2,3,4,0,6,7}) },
        { "Two bars - DnB half beat chase", makeFactory(2, 2, 2, {4,4,4,4,4,4,2,6}, {0,1,2,6,6,4,5,6,6}) },
        { "Two bars - DnB turnaround", makeFactory(2, 2, 4, {4,4,4,4,4,4,4,4}, {0,1,2,3,4,7,6,5}) },

        // Six beats are exactly the first 24 cells of a two-bar source. Their
        // shifting relationship to the output bar is deliberate, not triplets.
        { "Two bars - First six beats", makeFactory(2, 4, 4, {24,8}, {0}) },
        { "Two bars - First six beats long", makeFactory(2, 8, 4, {24,8}, {0}) },
        { "Two bars - Six beat turn", makeFactory(2, 4, 4, {8,8,8,8}, {0,1,2,0,2,1}) },
        { "Two bars - Six beat kick return", makeFactory(2, 4, 1, {1,3,4,8,8,8}, {0,1,2,3,4,0,0,0,0,2,3,4}) },
        { "Two bars - Six beat snare return", makeFactory(2, 4, 1, {4,1,3,8,8,8}, {0,1,2,3,4,0,1,1,1,1,3,4}) },
        { "Two bars - Six beat stop", makeFactory(2, 4, 4, {24,8}, {0,restToken(8)}) },

        // Rolls retrigger a short onset from beat one (cell 0) or beat two
        // (cell 4). Longer untouched passages keep the underlying break intact.
        { "One bar - Kick eighth roll", makeFactory(1, 2, 2, {2,2,4,8}, {0,1,2,3,0,0,2,3}) },
        { "One bar - Kick sixteenth roll", makeFactory(1, 2, 1, {1,3,4,8}, {0,1,2,3,0,0,0,0,2,3}) },
        { "One bar - Kick double roll", makeFactory(1, 2, 1, {1,3,4,4,4}, {0,1,2,3,4,0,0,0,0,2,0,0,0,0,4}) },
        { "One bar - Kick accelerating", makeFactory(1, 2, 1, {1,1,2,4,8}, {0,1,2,3,4,0,1,0,0,3,4}) },
        { "Two bars - Kick eighth answer", makeFactory(2, 2, 2, {2,2,4,8,4,4,8}, {0,1,2,3,0,0,5,6}) },
        { "Two bars - Kick sixteenth chase", makeFactory(2, 2, 1, {1,3,4,8,4,4,8}, {0,1,2,3,0,0,0,0,5,0,0,0,0,5}) },
        { "One bar - Snare eighth roll", makeFactory(1, 2, 2, {4,2,2,8}, {0,1,2,3,0,1,1,3}) },
        { "One bar - Snare sixteenth roll", makeFactory(1, 2, 1, {4,1,3,8}, {0,1,2,3,0,1,1,1,1,3}) },
        { "One bar - Snare double roll", makeFactory(1, 2, 1, {4,1,3,4,4}, {0,1,2,3,4,0,1,1,1,1,3,1,1,1,1}) },
        { "One bar - Snare accelerating", makeFactory(1, 2, 1, {4,1,1,2,8}, {0,1,2,3,4,0,1,2,1,1,4}) },
        { "Two bars - Snare eighth answer", makeFactory(2, 2, 2, {4,2,2,8,4,4,8}, {0,1,2,3,4,1,1,6}) },
        { "Two bars - Snare sixteenth chase", makeFactory(2, 2, 1, {4,1,3,8,4,4,4,4}, {0,1,2,3,4,1,1,1,1,6,1,1,1,1}) },
        { "One bar - Jungle kick snare rolls", makeFactory(1, 2, 1, {1,3,1,3,4,4}, {0,1,2,3,4,5,0,0,0,0,2,2,2,2,4,2,2,2,2}) },
        { "One bar - Hip hop double tap", makeFactory(1, 2, 2, {2,2,2,2,4,4}, {0,0,2,3,4,5,0,1,2,2,4,5}) },
        { "Two bars - DnB roll exchange", makeFactory(2, 2, 1, {1,3,1,3,8,4,4,8}, {0,1,2,3,4,0,0,0,0,2,2,2,2,7}) },
        { "Two bars - Jungle roll crescendo", makeFactory(2, 2, 1, {1,1,2,1,1,2,8,16}, {0,1,2,3,4,5,6,0,1,0,0,3,4,3,3,0,0,0,0,3,3,3,3}) },

        // Rests are output phrases, never a ninth source colour. Short rests
        // gate the hits; beat, half-bar and full-bar rests create dropouts.
        { "One bar - Hip hop kick gaps", makeFactory(1, 1, 1, {1,3,4,4,4}, {0,restToken(1),0,restToken(1),2,3,4}) },
        { "One bar - Hip hop snare gaps", makeFactory(1, 1, 1, {4,1,3,4,4}, {0,1,restToken(1),1,restToken(1),3,4}) },
        { "One bar - Jungle stop start", makeFactory(1, 2, 1, {4,1,3,4,4}, {0,1,2,3,4,0,1,restToken(1),1,restToken(1),3,restToken(4)}) },
        { "One bar - DnB gated eighths", makeFactory(1, 2, 2, {2,2,2,2,4,4}, {0,restToken(2),2,restToken(2),4,5,0,1,2,restToken(2),4,2,restToken(2)}) },
        { "Two bars - Jungle drop and roll", makeFactory(2, 2, 1, {4,1,3,8,16}, {0,1,2,3,restToken(4),1,1,1,1,restToken(4),1,1,1,1}) },
        { "Two bars - DnB half bar dropout", makeFactory(2, 2, 4, {8,8,8,8}, {0,1,2,restToken(8)}) },
        { "Two bars - Hip hop cutaway", makeFactory(2, 2, 4, {4,4,8,4,4,8}, {0,1,2,3,restToken(4),5}) },
        { "Two bars - Jungle bar silence", makeFactory(2, 4, 4, {16,16}, {0,restToken(16),1,0}) }
    };
    // Explicit metadata preserves the released bank and CLI indices exactly.
    constexpr std::array<int, 63> categories {
        0,0,2,6,6,7,6,0,2,6,6,7,6,0,0,0,0,0,0,
        2,2,2,2,1,1,1,2,2,2,2,1,1,1,0,0,0,0,0,0,
        7,7,7,4,7,7,7,7,7,4,7,7,2,3,1,4,3,3,2,1,2,1,3,2
    };
    for (std::size_t i = 0; i < result.size(); ++i) result[i].category = presetCategories[categories[i]];
    appendCategorisedFactories(result);
    appendKickBuildsAndEndShuffles(result);
    return result;
    }();
    return patterns;
}
bool randomiseSlices(Pattern& p, std::uint32_t seed, std::string* reason)
{
    if (!p.validate(reason)) return false;
    const int slots = p.activeCells() / p.grid;
    std::array<bool, colourCount> referenced {};
    for (int i = 0; i < p.orderCount; ++i)
        if (!isRest(p.order[i])) referenced[p.order[i]] = true;
    std::array<int, colourCount> colours {}, optional {};
    int requiredCount = 0, optionalCount = 0;
    for (int c = 0; c < colourCount; ++c)
        if (referenced[c]) colours[requiredCount++] = c;
        else optional[optionalCount++] = c;
    if (requiredCount > slots)
        return fail(reason, "The selection grid has too few slices for all ordered colours.");

    Random random(seed);
    const int maximum = std::min(colourCount, slots);
    const int minimum = std::min(maximum, std::max(2, requiredCount));
    const int count = minimum + random.below(maximum - minimum + 1);
    random.shuffle(optional.begin(), optional.begin() + optionalCount);
    for (int i = requiredCount; i < count; ++i) colours[i] = optional[i - requiredCount];
    random.shuffle(colours.begin(), colours.begin() + count);

    std::array<int, maxCells> cuts {};
    std::iota(cuts.begin(), cuts.begin() + slots - 1, 1);
    random.shuffle(cuts.begin(), cuts.begin() + slots - 1);
    std::sort(cuts.begin(), cuts.begin() + count - 1);
    cuts[count - 1] = slots;
    Pattern proposed = p;
    const auto fillPartition = [&]
    {
        proposed.cells.fill(-1);
        int start = 0;
        for (int i = 0; i < count; ++i)
        {
            const int end = cuts[i] * p.grid;
            std::fill(proposed.cells.begin() + start, proposed.cells.begin() + end, colours[i]);
            start = end;
        }
    };
    fillPartition();
    if (proposed.cells == p.cells && count > 1)
    {
        std::swap(colours[0], colours[1]);
        fillPartition();
    }
    return publish(p, proposed, reason);
}

bool shuffleOrder(Pattern& p, std::uint32_t seed, std::string* reason)
{
    if (!p.validate(reason)) return false;
    Pattern proposed = p;
    Random random(seed);
    std::array<int, maxOrder> positions {};
    std::iota(positions.begin(), positions.begin() + p.orderCount, 0);
    random.shuffle(positions.begin(), positions.begin() + p.orderCount);
    for (int i = 0; i < p.orderCount; ++i)
    {
        proposed.order[i] = p.order[positions[i]];
        proposed.orderLengths[i] = p.orderLengths[positions[i]];
    }
    // A shuffle can select the existing permutation. Make a diverse order
    // visibly change while retaining every occurrence, even for tiny orders.
    if (proposed.order == p.order && proposed.orderLengths == p.orderLengths)
        for (int i = 1; i < proposed.orderCount; ++i)
            if (proposed.order[i] != proposed.order[0]
                || proposed.orderLengths[i] != proposed.orderLengths[0])
            {
                std::swap(proposed.order[0], proposed.order[i]);
                std::swap(proposed.orderLengths[0], proposed.orderLengths[i]);
                break;
            }
    return publish(p, proposed, reason);
}

bool generateOrder(Pattern& p, std::uint32_t seed, std::string* reason)
{
    if (!p.validate(reason)) return false;
    std::array<int, orderTokenCount> choices {};
    std::array<int, orderTokenCount> lengths {};
    int count = 0;
    for (int c = 0; c < colourCount; ++c)
    {
        const auto r = p.region(c);
        if (!r.valid()) continue;
        choices[count++] = c;
        lengths[c] = r.end - r.first;
    }
    const int audibleCount = count;
    // Silence is opt-in: only lengths already present in this order are
    // available to New sequence. Existing patterns never gain surprise gaps.
    for (int i = 0; i < p.orderCount; ++i)
    {
        const int token = p.order[i];
        if (!isRest(token) || lengths[token] > 0) continue;
        choices[count++] = token;
        lengths[token] = restCells(token);
    }
    if (count == 0) return fail(reason, "Assign an input region or add a rest before generating an order.");

    Random random(seed);
    Pattern proposed = p;
    proposed.order.fill(-1);
    proposed.orderLengths.fill(0);
    proposed.orderCount = 0;
    const int total = p.outputBars * cellsPerBar;
    int duration = 0;
    while (duration < total && proposed.orderCount < maxOrder)
    {
        // Keep a positive opening when audio regions exist; a deliberate
        // all-rest pattern remains useful and can generate only its rests.
        const int available = proposed.orderCount == 0 && audibleCount > 0 ? audibleCount : count;
        int colour = choices[random.below(available)];
        // Occasional immediate repeats create stutters, alongside rearrangements.
        if (proposed.orderCount > 0 && random.below(4) == 0)
            colour = proposed.order[proposed.orderCount - 1];
        proposed.order[proposed.orderCount++] = colour;
        duration += lengths[colour];
    }
    if (count > 1 && proposed.orderCount == p.orderCount
        && std::equal(proposed.order.begin(), proposed.order.begin() + proposed.orderCount, p.order.begin()))
    {
        // With just one audible choice, retain the audible opening and vary
        // the next token instead. A single full-loop audible phrase cannot
        // change without introducing an unwanted leading rest.
        const int target = audibleCount == 1 ? 1 : 0;
        if (target < proposed.orderCount)
        {
            const int previous = proposed.order[target];
            const int replacement = choices[0] == previous ? choices[1] : choices[0];
            proposed.order[target] = replacement;
            duration += lengths[replacement] - lengths[previous];
            while (duration < total && proposed.orderCount < maxOrder)
            {
                proposed.order[proposed.orderCount++] = replacement;
                duration += lengths[replacement];
            }
            // A longer replacement can push later tokens beyond the output
            // end. Keep only entries that will actually play this loop.
            int used = 0, covered = 0;
            while (used < proposed.orderCount && covered < total)
                covered += lengths[proposed.order[used++]];
            std::fill(proposed.order.begin() + used, proposed.order.end(), -1);
            proposed.orderCount = used;
        }
    }
    return publish(p, proposed, reason);
}
}
