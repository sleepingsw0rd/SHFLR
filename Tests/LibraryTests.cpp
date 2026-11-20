#include "PatternLibrary.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace breakvar;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::uint64_t factoryFingerprint(const NamedPattern& entry)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto byte = [&](unsigned char value) { hash = (hash ^ value) * 1099511628211ull; };
    const auto integer = [&](int value)
    {
        auto bits = static_cast<std::uint32_t>(value);
        for (int j = 0; j < 4; ++j) { byte(static_cast<unsigned char>(bits)); bits >>= 8; }
    };
    for (const auto c : entry.name) byte(static_cast<unsigned char>(c));
    byte(0);
    for (const auto c : entry.category) byte(static_cast<unsigned char>(c));
    byte(0);
    const auto& p = entry.pattern;
    for (const auto value : {p.inputBars,p.outputBars,p.grid,p.orderCount,p.smoothTenths,p.tightenPercent}) integer(value);
    for (const auto value : p.cells) integer(value);
    for (const auto value : p.order) integer(value);
    for (const auto value : p.orderLengths) integer(value);
    for (const auto value : p.nudgeTenths) integer(value);
    for (const auto value : p.nudgeManual) integer(value ? 1 : 0);
    return hash;
}


void releasedCatalogIncludingCategoriesIsFrozen()
{
    // Captured from all 203 released 0.2.7 entries before adding 0.2.8 presets.
    // Includes name, category, every stored Pattern field and inactive slots.
    constexpr std::array<std::uint64_t, 203> expected {
        0xf8e2ad9d0756a95bull, 0xed019374f77ddc4eull, 0x4a78886b7128cda2ull,
        0xcb4f8afbdf4dde30ull, 0xa41f05bad6d6d7full, 0xf1c64dc776db128bull,
        0xf525eb2037d521b5ull, 0x5cc3acabbc3488bfull, 0x6bd36f999878a83dull,
        0x657eccf18b6e59efull, 0x9d85e1097156b149ull, 0x27033786a95b7063ull,
        0x126505300d0f66ddull, 0xa3c61fa96aecd3bull, 0xd92febbf47bdb94dull,
        0xd7c83cce375b2027ull, 0xd6ab0c66a72622c5ull, 0xb17231838f41f521ull,
        0x3d912e3769cc234dull, 0xae135fbb650db908ull, 0x2cbb25ae373a4a1eull,
        0xdee79673249abfeull, 0x6956c213eaee61d4ull, 0xfe020d167da1de2dull,
        0xbfc6dd58d49d512full, 0x3dcb541124b555b7ull, 0xa48096d7a04b20f6ull,
        0xee2ae37ed4650d4ull, 0x5da05e29d3792271ull, 0x5a20a6e7e21d7c55ull,
        0x69723f98d0349c46ull, 0x467e3482153eb94cull, 0x566ec46f4ce8e298ull,
        0x5b68ddca8b32b215ull, 0xda0cc94f3e1bc74dull, 0x31024d28c1c1e3baull,
        0x63335a84168dea0eull, 0x75dcc87a51ed5c5full, 0x78cb369441490986ull,
        0x9ec371c23739a02dull, 0xdacd43fc576ba74aull, 0xc73a4df851cf4213ull,
        0x4767e63504df8a79ull, 0x9fb50409b6d89cc4ull, 0x7cfa0765bb61977aull,
        0xe8cbb788781fefccull, 0x58bec63e4f0fedfull, 0xcb0738293cd1632full,
        0xc10ef7bc5f953109ull, 0x3134e6d42c29227cull, 0x9a64d994ae277f77ull,
        0xf5a900373a8473aull, 0xa340f0bb8b8c8ccbull, 0xfcf1065915e7236aull,
        0x67670338305d570dull, 0x88088680ef09454full, 0xb2b37c7811f76feeull,
        0x41547eeeadc9d2c9ull, 0xcb544d8121bcbe54ull, 0xc8e8d063c96969ebull,
        0xbf5ceb7cbec7f6efull, 0x1d8506faf019c762ull, 0xe6f57803a015d615ull,
        0xf4fb85bb891ef1feull, 0x9844a7f77d12f750ull, 0x85e20643588a9666ull,
        0x44ed363e9e0e3993ull, 0xc625a15fcb315deeull, 0x8b76423793d21d7ull,
        0x899241d39f2e0e2dull, 0x4d1dd4d6b5378c99ull, 0x1e1107237b07ac95ull,
        0x55dc215a21fdbdfcull, 0xdcdeb71dbf2f01c5ull, 0xfe83db077dfc3b61ull,
        0xc257c0d0e4de57caull, 0x360ad6558ea5ff6eull, 0x351a648d207c5b2cull,
        0x40b2953ed04c49cull, 0x3510361d442b2b7dull, 0x14d2f785e1140c54ull,
        0xfdf62d7cb4b0f2baull, 0xf87ef96943209478ull, 0x7425fb058f8d3eb6ull,
        0x7c2559b9131da3d9ull, 0xcd31437e44df1c0full, 0x496db12661e0bc3bull,
        0xb23748344117e2acull, 0x8db8ed84306fa30full, 0xcfaa447927fced96ull,
        0x3eecf553e546a67aull, 0xfaad0525dc717ff4ull, 0x136878f2e15e7847ull,
        0x387db6b8e7868717ull, 0x3cb6ab8a9167ea46ull, 0x83f909e65c0eb3a3ull,
        0x34cd49d39fb2565ull, 0x7e9b858272107bb2ull, 0x569b8168a2c71e47ull,
        0x711933fb3548ccd2ull, 0x7359200ee767bb4ull, 0x8b265be7d89eb73aull,
        0xa0a08c7f4c97cd06ull, 0x2197c473a2542a99ull, 0x404f0b8a683fe014ull,
        0xd1ab53ef4eb5f04ull, 0x463092d9ea7fbcafull, 0x21af58cd3b233206ull,
        0x42d32f79c67fe6fbull, 0xed031d84bb7f70aull, 0x58e081c8b0ee0cbaull,
        0xf12ebd7a0fef9608ull, 0x18b9a9dd6f5eda04ull, 0x2e9f51155db7905eull,
        0xd4a826c2460b4282ull, 0xa8c608edffa12edull, 0x85c7156ce1b41dfdull,
        0xf20e9b77f6aa97e4ull, 0x8c9b8471f585db4ull, 0x65f74168e5b54cbcull,
        0xfd3115734183976cull, 0x16e424f40969f305ull, 0xb585ad30c9f0b434ull,
        0x1838c8df7d3ba91eull, 0xd9413aaf9fca0c8cull, 0x216041c3b3d695f5ull,
        0x4a44a2f115911a4bull, 0x47470bf353730482ull, 0xa9b6ee7062ad120full,
        0xacdc369947da0c80ull, 0x6f1736d978b2e02aull, 0x25c11ffd8ab91519ull,
        0x5926e85cfcd0472dull, 0xee0ac2df84d993eaull, 0x79b1d766a365eb04ull,
        0x5c995f3c6d13979cull, 0xbbb75e09e0bb8367ull, 0xd683cecb99e1200aull,
        0xfb53b23e8443a7bdull, 0xc73163416f7f2ff1ull, 0x35d8ed655cc4dee5ull,
        0x7c2bc3b4817b7e8full, 0x88057dc186410028ull, 0xed07c27dce6e73b1ull,
        0x401f29bbf60e467full, 0x32918a0f326d32c2ull, 0xc0c70255447cac7cull,
        0x8d3ad4515c76ae6aull, 0xe226e4186a77e330ull, 0x4898a55109f93539ull,
        0xa14178378be79376ull, 0xa50f780afba5ac5aull, 0x64f424f7f6a98916ull,
        0x152b7aa5b704f30aull, 0x1369692213d347d5ull, 0x807620f64042c401ull,
        0xdc5b965ada64e8d3ull, 0x599b301cca2393e8ull, 0x2b25c5d88c39c707ull,
        0x67ce439ba42037f2ull, 0x3750ee8c6f8276d4ull, 0xa4f428605152ca20ull,
        0x465db99c0e263393ull, 0x22619e5a6a3d9881ull, 0xb75c0794495c0846ull,
        0xbcef9d2e4358bb69ull, 0x44ff73487efea699ull, 0xafb0fe9b4353e9e0ull,
        0x5cc96d10ab231945ull, 0xea9c7c27378fe2a6ull, 0x9b357a8792b792c2ull,
        0x2a907a6707ae66fcull, 0x3a5dff183666155aull, 0x7ee113a1a77b7e2full,
        0x4b7283d2ec1183e6ull, 0x1f2011df5fe5aff6ull, 0x1685b064c4c25977ull,
        0xd551c4cea27bf190ull, 0x5c2f1b93176bbf0dull, 0x734a08c9eb0c7916ull,
        0xcbb183823ea2cd58ull, 0xdd9bdb1bda7f798eull, 0xd9890e22ebb3f862ull,
        0xca8b754b4863e452ull, 0x1a755d3f5d726042ull, 0x81a4b855a53f210dull,
        0xe1de8dcd72d4c74dull, 0x51a89db73eed68e6ull, 0x52dabea3cb8f0d6eull,
        0x1641961152b487f6ull, 0x6dcb133e6f84c6caull, 0xe3dc9ed4607fa579ull,
        0xb6fff66a9eef8514ull, 0xcb0b8dad8f2eedacull, 0x4d9bdb13065d8d62ull,
        0x67381032a518242dull, 0x9df39a539b4235a1ull, 0x2935bd726264c69eull,
        0x4e727bc8497ca9cbull, 0xe41365b5ad425398ull, 0xf8785d4bcbe0611aull,
        0x119452a6df8d8fb5ull, 0xd1516b754a06d8d5ull,
    };
    const auto& factories = factoryPatterns();
    require(factories.size() >= expected.size(), "the released catalog cannot lose entries");
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (factoryFingerprint(factories[i]) != expected[i])
            throw std::runtime_error("released 0.2.7 factory or category changed at index " + std::to_string(i));
}

void originalFactoriesAreFrozen()
{
    // Captured from the released 0.2.6 bank before the browser/category work.
    // Every name, source cell, order slot, cap, sound setting and timing field
    // contributes; category metadata is deliberately excluded.
    constexpr std::array<std::uint64_t, 63> expected {
        0xc37eb89f77670b2cull, 0x9a2fcbfed6228117ull, 0x773321eff581f1c3ull,
        0x3b7d07f5615285b4ull, 0x36eef9b33482be7bull, 0xf5181b10ddd430edull,
        0x3dc2f3ab386a654full, 0xe90f8101077f7b60ull, 0x1020733dc7135952ull,
        0xc611dc80f92fbf75ull, 0x46536e1387170a39ull, 0x47c58123caaac305ull,
        0xb1893819af33242dull, 0x10bdcf39c94df88cull, 0x5cee7f53af8cab00ull,
        0xeba750e490f9ef62ull, 0xbde37ae3b6b41eacull, 0x1866872a6efbb7f6ull,
        0x2ca8e13c27616c12ull, 0xcc4c3a3ef22fce69ull, 0xaec5bb7be8dbd969ull,
        0xa71f5d36b5777b37ull, 0x0ba8349e18489effull, 0xd3474ac1331948a5ull,
        0xc5dab37530a0c96bull, 0xe9ad77e72aa58b6full, 0x75b3884d1f5062bfull,
        0x8674ab0afc57af15ull, 0x6769b4f133e949b0ull, 0x6cc154fcd051689cull,
        0x339170277e9c05ceull, 0x353a241d812105e4ull, 0x9766edbef39fef14ull,
        0x5288ed686b2db49cull, 0xb37a7d1ee6cb82a4ull, 0xc075133d9bd17523ull,
        0xc6d8616c2f628679ull, 0xe519c8e9600501e2ull, 0xd53a8622b8141331ull,
        0x2b8ebb8b31930c07ull, 0xf89b26884a51fe60ull, 0x174ee72d1e419b31ull,
        0xc08087a9baa89845ull, 0x8a3fb79c8e36db0eull, 0x92464b0132117954ull,
        0x7c1e13f18564ecceull, 0xf199f095bab3e8d9ull, 0x2b957129db91f625ull,
        0x7f7e7bb7dd2f6927ull, 0x2048063d903b3302ull, 0x2e4fb886c8020ca5ull,
        0x128d05cb497b829bull, 0x9eb0247a290bd54eull, 0x08725f6ffe2d6316ull,
        0x0d38b82d61fbb093ull, 0x8a39c6ba2a6280eeull, 0x1430516dba4b26efull,
        0x851a274e136b96deull, 0x4f72ba9bfec31be8ull, 0xcec2bea80e9efcf0ull,
        0x3873bd3ab88a0167ull, 0x054b0e06700197efull, 0xad6b0cced8d56fd4ull
    };
    const auto& factories = factoryPatterns();
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        std::uint64_t hash = 14695981039346656037ull;
        const auto byte = [&](unsigned char value) { hash = (hash ^ value) * 1099511628211ull; };
        const auto integer = [&](int value)
        {
            auto bits = static_cast<std::uint32_t>(value);
            for (int j = 0; j < 4; ++j) { byte(static_cast<unsigned char>(bits)); bits >>= 8; }
        };
        for (const auto c : factories[i].name) byte(static_cast<unsigned char>(c));
        byte(0);
        const auto& p = factories[i].pattern;
        for (const auto value : {p.inputBars,p.outputBars,p.grid,p.orderCount,p.smoothTenths,p.tightenPercent}) integer(value);
        for (const auto value : p.cells) integer(value);
        for (const auto value : p.order) integer(value);
        for (const auto value : p.orderLengths) integer(value);
        for (const auto value : p.nudgeTenths) integer(value);
        for (const auto value : p.nudgeManual) integer(value ? 1 : 0);
        if (hash != expected[i]) throw std::runtime_error("released factory changed at index " + std::to_string(i));
    }
}

void browserCategoriesContainTenNewPresetsForEachSourceLength()
{
    const auto& factories = factoryPatterns();
    constexpr std::array<const char*,8> expectedCategories
        {"Essentials","DnB","Jungle","Hip-hop","Buildups","Breakdowns","Shuffles","Fills"};
    require(std::equal(presetCategories.begin(), presetCategories.end(), expectedCategories.begin(),
                       [](const char* a, const char* b) { return std::string(a) == b; }),
            "browser categories have the planned stable names and order");
    std::array<std::array<int,8>,2> newCounts {};
    for (std::size_t i = 0; i < factories.size(); ++i)
    {
        const auto& entry = factories[i];
        const auto category = std::find_if(presetCategories.begin(), presetCategories.end(),
            [&](const char* name) { return entry.category == name; });
        require(category != presetCategories.end(), "every factory belongs to a visible browser category");
        require(entry.pattern.inputBars == 1 || entry.pattern.inputBars == 2, "each factory belongs to one source-length tab");
        if (i >= 63 && i < 203) ++newCounts[entry.pattern.inputBars - 1][category - presetCategories.begin()];
    }
    for (const auto& counts : newCounts)
    {
        require(counts[0] == 0, "Essentials retains the existing core workflows");
        for (int category = 1; category < 8; ++category)
            require(counts[category] == 10, "each genre and functional category gets ten NEW presets per source length");
    }
}

void factoriesAreDistinctAndValid()
{
    const auto& factories = factoryPatterns();
    require(factories.size() == 231, "two-bar kick builds and end shuffles append 28 presets to the released 203");
    require(factories.size() >= 13, "provide Original and at least twelve factories");
    std::set<std::string> names;
    bool oneBar = false, twoBars = false, original = false;
    for (std::size_t i = 0; i < factories.size(); ++i)
    {
        const auto& entry = factories[i];
        require(!entry.name.empty() && names.insert(entry.name).second, "factory names must be distinct");
        require(entry.pattern.validate(), "factory must validate");
        if (i < 63) require(std::all_of(entry.pattern.orderLengths.begin(), entry.pattern.orderLengths.end(),
                            [](int cells) { return cells == 0; }),
                "legacy factories keep every source occurrence at Full");
        require(entry.pattern.orderCount > 0, "factory must produce sound");
        require(std::any_of(entry.pattern.order.begin(), entry.pattern.order.begin() + entry.pattern.orderCount,
                            [](int token) { return token >= 0 && token < colourCount; }),
                "factory must contain an audible phrase even when it also uses rests");
        oneBar |= entry.pattern.inputBars == 1;
        twoBars |= entry.pattern.inputBars == 2;
        if (entry.name == "Original")
        {
            require(entry.pattern == Pattern::makeDefault(), "Original must preserve YYYBRR");
            original = true;
        }
        for (std::size_t j = 0; j < i; ++j)
            require(!(entry.pattern == factories[j].pattern), "factory arrangements must be distinct");
        const auto expanded = expandSequence(entry.pattern);
        require(expanded.count > 0, "factory expands to output");
        const auto& last = expanded.segments[expanded.count - 1];
        require(last.start + last.length == entry.pattern.outputBars * cellsPerBar, "factory fills loop");
    }
    require(oneBar && twoBars && original, "factories span one and two bars and Original");
}

const Pattern& factoryNamed(const char* name)
{
    const auto& factories = factoryPatterns();
    const auto found = std::find_if(factories.begin(), factories.end(), [name](const auto& entry)
    {
        return entry.name == name;
    });
    if (found == factories.end()) throw std::runtime_error(std::string("missing factory: ") + name);
    return found->pattern;
}

std::vector<int> audibleSourceCells(const Pattern& pattern)
{
    std::vector<int> result;
    const auto expanded = expandSequence(pattern);
    for (int i = 0; i < expanded.count; ++i)
    {
        const auto& segment = expanded.segments[i];
        const auto source = pattern.region(segment.colour);
        for (int cell = 0; cell < static_cast<int>(segment.length); ++cell)
            result.push_back(segment.colour >= colourCount ? -1 : source.first + cell);
    }
    return result;
}

void kickBuildsAccelerateTheSameKickOverTwoOutputBars()
{
    struct ExpectedBuild
    {
        const char* name;
        std::vector<int> onsets;
        std::vector<int> sourceCells;
        bool gated;
    };
    // Literal output-cell schedules and source mappings catch wrong transition
    // points, accidentally using the next source beat, and clipped endings.
    const std::array<ExpectedBuild, 6> expected {{
        {"Even ramp", {0,4,8,12,16,18,20,22,24,25,26,27,28,29,30,31},
            {0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0}, false},
        {"Late rush", {0,4,8,12,16,20,24,26,28,29,30,31},
            {0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,0,1,0,1,0,0,0,0}, false},
        {"Early rush", {0,4,8,10,12,14,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31},
            {0,1,2,3,0,1,2,3,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, false},
        {"Stepped ramp", {0,4,8,12,14,16,18,20,22,24,25,26,27,28,29,30,31},
            {0,1,2,3,0,1,2,3,0,1,2,3,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0}, false},
        {"Gated even ramp", {0,4,8,12,16,18,20,22,24,25,26,27,28,29,30,31},
            {0,-1,-1,-1,0,-1,-1,-1,0,-1,-1,-1,0,-1,-1,-1,0,-1,0,-1,0,-1,0,-1,0,0,0,0,0,0,0,0}, true},
        {"Gated late rush", {0,4,8,12,16,20,24,26,28,29,30,31},
            {0,-1,-1,-1,0,-1,-1,-1,0,-1,-1,-1,0,-1,-1,-1,0,-1,-1,-1,0,-1,-1,-1,0,-1,0,-1,0,0,0,0}, true}
    }};
    for (const int bars : {1, 2})
        for (const auto& recipe : expected)
        {
            const auto name = std::string(bars == 1 ? "One bar - " : "Two bars - ") + "Kick build - " + recipe.name;
            const auto& p = factoryNamed(name.c_str());
            require(p.inputBars == bars && p.outputBars == 2, "kick builds use the selected source length and exactly two output bars");
            require(audibleSourceCells(p) == recipe.sourceCells, "kick build plays only the first kick tail with the exact rhythm and gaps");
            std::vector<int> onsets;
            const auto sequence = expandSequence(p);
            int duration = 0;
            for (int i = 0; i < p.orderCount; ++i) duration += p.orderLength(i);
            require(duration == 32 && sequence.count == p.orderCount, "kick build order fills 32 cells once without truncation or repetition");
            for (int i = 0; i < sequence.count; ++i)
            {
                const auto& segment = sequence.segments[i];
                if (isRest(segment.colour))
                {
                    require(recipe.gated && (segment.length == 1 || segment.length == 2),
                            "gated builds use only supported one/two-cell rests between hits");
                    continue;
                }
                const auto region = p.region(segment.colour);
                require(region.first == 0 && region.end == 4, "every kick build onset returns to the first source beat");
                require(!recipe.gated || segment.length == 1, "gated builds shorten every hit to the first sixteenth");
                onsets.push_back(static_cast<int>(segment.start));
            }
            require(onsets == recipe.onsets, "kick build starts on the beat, accelerates through eighths and ends in continuous sixteenths");
        }
}

void endShufflesOnlyPermuteTheFinalBeat()
{
    struct ExpectedShuffle { const char* name; std::array<int, 4> tail; };
    constexpr std::array<ExpectedShuffle, 8> expected {{
        {"Last eighth flip", {0,1,3,2}}, {"Early flick", {1,0,2,3}},
        {"Middle flick", {0,2,1,3}}, {"Eighth swap", {2,3,0,1}},
        {"Turnback", {0,3,2,1}}, {"Tail rotate", {1,2,3,0}},
        {"Tail return", {3,0,1,2}}, {"Crossed pairs", {1,0,3,2}}
    }};
    for (const int bars : {1, 2})
    {
        std::set<std::vector<int>> uniqueTails;
        for (const auto& recipe : expected)
        {
            const auto name = std::string(bars == 1 ? "One bar - " : "Two bars - ") + "End shuffle - " + recipe.name;
            const auto& p = factoryNamed(name.c_str());
            require(p.inputBars == bars && p.outputBars == bars && p.grid == 1,
                    "end shuffles preserve source/output length and show sixteenth-cell slices");
            const int leadIn = bars == 1 ? 12 : 28;
            const auto opening = p.region(0);
            require(opening.first == 0 && opening.end == leadIn, "end shuffle lead-in is one intact source region");
            for (int colour = 1; colour <= 4; ++colour)
            {
                const auto tail = p.region(colour);
                require(tail.first == leadIn + colour - 1 && tail.end == leadIn + colour,
                        "end shuffle has four individual tail-sixteenth regions");
            }
            for (int colour = 5; colour < colourCount; ++colour)
                require(!p.region(colour).valid(), "end shuffle uses exactly five source colours");
            require(p.orderCount == 5 && p.order[0] == 0, "end shuffle plays the intact lead-in followed by four tail cells");
            require(std::all_of(p.orderLengths.begin(), p.orderLengths.end(), [](int length) { return length == 0; }),
                    "end shuffle needs no trimmed or hidden occurrence caps");
            const auto sequence = expandSequence(p);
            require(sequence.count == 5, "end shuffle has no repeated or unreachable order entries");
            const auto cells = audibleSourceCells(p);
            require(cells.size() == static_cast<std::size_t>(bars * 16), "end shuffle fills the complete source-length output");
            for (int cell = 0; cell < leadIn; ++cell)
                require(cells[cell] == cell, "end shuffle keeps every leading source cell in place");
            std::vector<int> tail(cells.begin() + leadIn, cells.end());
            for (int cell = 0; cell < 4; ++cell)
                require(tail[cell] == leadIn + recipe.tail[cell], "end shuffle uses the exact local tail permutation");
            require(uniqueTails.insert(tail).second, "all eight local end shuffles are audibly distinct");
            std::sort(tail.begin(), tail.end());
            require(tail == std::vector<int>{leadIn,leadIn+1,leadIn+2,leadIn+3},
                    "end shuffle neither repeats nor replaces tail cells and inserts no silence");
        }
        const auto name = std::string(bars == 1 ? "One bar - " : "Two bars - ") + "End shuffle - Last eighth flip";
        const auto cells = audibleSourceCells(factoryNamed(name.c_str()));
        for (int cell = 0; cell < bars * 16 - 2; ++cell)
            require(cells[cell] == cell, "last eighth flip leaves every preceding sixteenth unchanged");
        require(cells[bars * 16 - 2] == bars * 16 - 1 && cells.back() == bars * 16 - 2,
                "last eighth flip swaps just the final two sixteenths");
    }
}

void newKickBuildsAndEndShufflesAreAppendedInTheirCategories()
{
    const auto& factories = factoryPatterns();
    require(factories.size() == 231, "append exactly six builds and eight end shuffles for each source length");
    const std::array<const char*, 14> suffixes {
        "Kick build - Even ramp", "Kick build - Late rush", "Kick build - Early rush", "Kick build - Stepped ramp",
        "Kick build - Gated even ramp", "Kick build - Gated late rush",
        "End shuffle - Last eighth flip", "End shuffle - Early flick", "End shuffle - Middle flick", "End shuffle - Eighth swap",
        "End shuffle - Turnback", "End shuffle - Tail rotate", "End shuffle - Tail return", "End shuffle - Crossed pairs"
    };
    for (const int bars : {1, 2})
        for (std::size_t i = 0; i < suffixes.size(); ++i)
        {
            const auto& entry = factories[203 + (bars - 1) * 14 + i];
            require(entry.name == std::string(bars == 1 ? "One bar - " : "Two bars - ") + suffixes[i],
                    "new names and indices append after the complete released catalog");
            require(entry.pattern.inputBars == bars && entry.category == (i < 6 ? "Buildups" : "Fills"),
                    "new entries belong to the correct source tab and functional category");
        }
}

void newCategoriesHaveTheirMusicalBehaviour()
{
    const auto& factories = factoryPatterns();
    for (std::size_t i = 63; i < 203; ++i)
    {
        const auto& entry = factories[i];
        const auto& pattern = entry.pattern;
        const auto cells = audibleSourceCells(pattern);
        if (pattern.inputBars == 2)
            require(std::any_of(cells.begin(), cells.end(), [](int cell) { return cell >= 16; }),
                    "new two-bar factories make audible use of the second source bar");
        if (entry.category == "Buildups")
        {
            require(pattern.outputBars == 4, "buildups have four bars to develop");
            std::array<int,4> attacks {};
            const auto sequence = expandSequence(pattern);
            for (int j = 0; j < sequence.count; ++j)
            {
                const auto& part = sequence.segments[j];
                if (!isRest(part.colour)) ++attacks[static_cast<int>(part.start) / 16];
            }
            require(std::is_sorted(attacks.begin(), attacks.end()) && attacks[3] >= attacks[0] * 2,
                    "buildups increase audible onset density toward the final bar");
            require(attacks[3] == 16, "buildups reach a final sixteenth-note roll");
        }
        if (entry.category == "Breakdowns")
            require(std::count(cells.begin(), cells.end(), -1) >= 12,
                    "breakdowns leave at least three beats of deliberate space");
        if (entry.category == "Hip-hop")
            require(std::find(cells.begin(), cells.end(), -1) != cells.end(),
                    "new hip-hop arrangements contain intentional breathing space");
        if (entry.category == "Shuffles")
            require(std::find(cells.begin(), cells.end(), -1) == cells.end(),
                    "shuffle category rearranges phrases without inserting silence");
        if (entry.category == "Fills")
        {
            require(pattern.outputBars == 2, "fills develop over two output bars");
            for (int cell = 0; cell < 16; ++cell)
                require(cells[cell] == cell, "fills retain the complete source opening bar");
            bool changedEnding = false;
            for (int cell = 16; cell < 32; ++cell)
                changedEnding |= cells[cell] != cell % (pattern.inputBars * 16);
            require(changedEnding, "every fill audibly develops the ending");
        }
    }

    require(audibleSourceCells(factoryNamed("One bar - Rolling kick reply")) == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,0,1,12,13,14,15,
        0,1,2,3,4,5,6,7,8,9,10,11,12,12,12,12},
        "DnB reply redirects the third beat to the kick and rolls the last backbeat");
    require(audibleSourceCells(factoryNamed("Two bars - Ghost train relay")) == std::vector<int>{
        0,1,2,3,4,5,12,4,8,9,10,11,12,13,4,5,
        16,17,18,19,20,28,20,21,24,25,26,27,28,29,12,13},
        "Jungle relay uses both source bars and alternates eighth/sixteenth snare positions");
    require(audibleSourceCells(factoryNamed("One bar - Half time pockets")) == std::vector<int>{
        0,1,2,3,-1,-1,-1,-1,4,5,6,7,8,9,10,11,
        0,1,2,3,-1,-1,-1,-1,4,5,6,7,8,9,0,1},
        "hip-hop half-time pocket delays the source backbeat and leaves exact quarter-note rests");
    const auto climb = audibleSourceCells(factoryNamed("Two bars - Snare pressure ladder"));
    require(std::all_of(climb.begin() + 48, climb.end(), [](int cell) { return cell == 20; }),
            "snare buildup finishes by retriggering beat two of source bar two");
    require(audibleSourceCells(factoryNamed("Two bars - Inside out bars")) == std::vector<int>{
        0,1,2,3,8,9,10,11,4,5,6,7,12,13,14,15,
        16,17,18,19,24,25,26,27,28,29,30,31,20,21,22,23},
        "inside-out shuffle has a different beat permutation in each source bar");
}

void threeBeatFactoriesRepeatTheActualFirstThreeBeats()
{
    // A mistaken half-bar or three-quarter-source region sounds different on
    // two-bar breaks: these expectations are the literal first twelve cells.
    const auto& one = factoryNamed("One bar - First three beats");
    require(one.inputBars == 1 && one.outputBars == 2, "one-bar three-beat phrase spans a two-bar output");
    require(audibleSourceCells(one) == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7,8,9,10,11, 0,1,2,3,4,5,6,7},
        "first three beats repeat and cut after eight output beats");

    const auto& two = factoryNamed("Two bars - First three beats");
    require(two.inputBars == 2 && two.outputBars == 4, "two-bar three-beat phrase spans a four-bar output");
    const auto cells = audibleSourceCells(two);
    require(cells.size() == 64, "long three-beat repeat fills sixteen beats");
    for (std::size_t i = 0; i < cells.size(); ++i)
        require(cells[i] == static_cast<int>(i % 12), "two-bar source still repeats only its first three beats");

    const auto& pendulum = factoryNamed("Two bars - Three beat pendulum");
    const auto alternating = audibleSourceCells(pendulum);
    const std::array<int, 24> phrase {0,1,2,3,4,5,6,7,8,9,10,11,16,17,18,19,20,21,22,23,24,25,26,27};
    require(alternating.size() == 64, "alternating three-beat phrases fill four bars");
    for (std::size_t i = 0; i < alternating.size(); ++i)
        require(alternating[i] == phrase[i % phrase.size()], "pendulum alternates three beats from each source bar");
}

void jungleFactoriesKeepAnchorsAndChopTheIntendedTail()
{
    const auto& rush = factoryNamed("One bar - Jungle snare rush");
    require(audibleSourceCells(rush) == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        0,1,2,3,4,5,6,7,8,9,10,11,12,12,14,14},
        "snare rush keeps the first bar and first three beats before sixteenth tail repeats");
    const auto& relay = factoryNamed("Two bars - Jungle backbeat relay");
    require(audibleSourceCells(relay) == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,20,21,22,23,
        16,17,18,19,4,5,6,7,24,25,26,27,28,29,30,31},
        "backbeat relay retains downbeats and swaps chosen beat-two/four source positions");
}

void sixBeatFactoriesRepeatExactlySixSourceBeats()
{
    for (const auto* name : {"Two bars - First six beats", "Two bars - First six beats long"})
    {
        const auto& p = factoryNamed(name);
        const auto cells = audibleSourceCells(p);
        require(p.inputBars == 2, "six-beat phrase requires a two-bar input");
        require(cells.size() == (std::string(name).ends_with("long") ? 128u : 64u),
                "six-beat variants fill four or eight output bars");
        for (std::size_t i = 0; i < cells.size(); ++i)
            require(cells[i] == static_cast<int>(i % 24), "six beats repeats cells zero through twenty-three");
    }
    const auto cells = audibleSourceCells(factoryNamed("Two bars - Six beat turn"));
    const std::array<int, 48> phrase {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
        0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15 };
    for (std::size_t i = 0; i < cells.size(); ++i)
        require(cells[i] == phrase[i % phrase.size()], "six-beat turn exchanges the final two beat-pairs");
}

void rollFactoriesRetriggerTheActualKickAndSnareOnsets()
{
    const auto kickEighth = audibleSourceCells(factoryNamed("One bar - Kick eighth roll"));
    require(kickEighth == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        0,1,0,1,4,5,6,7,8,9,10,11,12,13,14,15},
        "eighth kick roll repeats the first half-beat onset, preserving the following snare");
    const auto kickSixteenth = audibleSourceCells(factoryNamed("One bar - Kick sixteenth roll"));
    require(kickSixteenth == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        0,0,0,0,4,5,6,7,8,9,10,11,12,13,14,15},
        "sixteenth kick roll repeats source cell zero rather than repeating a full beat");
    const auto snareEighth = audibleSourceCells(factoryNamed("One bar - Snare eighth roll"));
    require(snareEighth == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        0,1,2,3,4,5,4,5,8,9,10,11,12,13,14,15},
        "eighth snare roll retriggers beat two, not beat four or the tail");
    const auto snareSixteenth = audibleSourceCells(factoryNamed("One bar - Snare sixteenth roll"));
    require(snareSixteenth == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        0,1,2,3,4,4,4,4,8,9,10,11,12,13,14,15},
        "sixteenth snare roll repeats source cell four");
    const auto acceleration = audibleSourceCells(factoryNamed("One bar - Snare accelerating"));
    require(acceleration == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        0,1,2,3,4,5,4,4,8,9,10,11,12,13,14,15},
        "snare acceleration moves from an eighth into sixteenth retriggers");
}

void gatedFactoriesCreateLiteralGapsWithoutUsingSourceColours()
{
    require(audibleSourceCells(factoryNamed("One bar - Hip hop kick gaps")) == std::vector<int>{
        0,-1,0,-1,4,5,6,7,8,9,10,11,12,13,14,15},
        "kick gaps insert real silent sixteenths between onset hits");
    require(audibleSourceCells(factoryNamed("One bar - Hip hop snare gaps")) == std::vector<int>{
        0,1,2,3,4,-1,4,-1,8,9,10,11,12,13,14,15},
        "snare gaps retain beat one and gate beat two");
    require(audibleSourceCells(factoryNamed("Two bars - Jungle drop and roll")) == std::vector<int>{
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        -1,-1,-1,-1,4,4,4,4,-1,-1,-1,-1,4,4,4,4},
        "drop-and-roll alternates silent beats with the source beat-two snare onset");
    const auto sixStop = audibleSourceCells(factoryNamed("Two bars - Six beat stop"));
    for (std::size_t i = 0; i < sixStop.size(); ++i)
        require(sixStop[i] == (i % 32 < 24 ? static_cast<int>(i % 32) : -1),
                "six-beat stop leaves two beats of genuine silence");
    const auto barStop = audibleSourceCells(factoryNamed("Two bars - Jungle bar silence"));
    require(barStop.size() == 64, "bar-silence arrangement spans four bars");
    for (int i = 0; i < 64; ++i)
        require(barStop[i] == (i < 16 ? i : i < 32 ? -1 : i < 48 ? i - 16 : i - 48),
                "bar-silence arrangement rests then resumes the correct source bar");
}

void expandedFactoriesAreAudibleDistinctAndKeepLegacyIndices()
{
    const auto& factories = factoryPatterns();
    const std::array<const char*, 33> legacyNames {
        "Original", "One bar - Straight", "One bar - Backbeat stutter", "One bar - Half swap",
        "One bar - Quarter rotation", "One bar - Tail fill", "One bar - Skip and repeat",
        "Two bars - Straight", "Two bars - Backbeat echoes", "Two bars - Bar swap",
        "Two bars - Phrase rotation", "Two bars - Closing fill", "Two bars - Broken return",
        "One bar - First three beats", "One bar - Three beat turnaround", "One bar - Three beat tail",
        "Two bars - First three beats", "Two bars - Three beat pendulum", "Two bars - Three beat answer",
        "One bar - Jungle backbeat", "One bar - Jungle ghost steps", "One bar - Jungle snare rush",
        "One bar - Jungle tail switch", "One bar - DnB kick return", "One bar - DnB snare answer",
        "One bar - DnB two step", "Two bars - Jungle backbeat relay", "Two bars - Jungle ghost relay",
        "Two bars - Jungle break exchange", "Two bars - Jungle last beat rush", "Two bars - DnB kick conversation",
        "Two bars - DnB half beat chase", "Two bars - DnB turnaround"
    };
    require(factories.size() >= legacyNames.size() + 30, "add thirty six-beat, roll and gated factories");
    for (std::size_t i = 0; i < legacyNames.size(); ++i)
        require(factories[i].name == legacyNames[i], "existing factory indices and names remain compatible");

    for (std::size_t i = 13; i < factories.size(); ++i)
    {
        const auto& p = factories[i].pattern;
        require(p.inputBars == 1 || p.inputBars == 2, "new factories target one- and two-bar breaks");
        const auto expanded = expandSequence(p);
        std::array<bool, maxOrder> heard {};
        for (int j = 0; j < expanded.count; ++j) heard[expanded.segments[j].orderIndex] = true;
        for (int j = 0; j < p.orderCount; ++j)
            require(heard[j], "every displayed order box is heard before the output loop restarts");
        for (int cell = 0; cell < p.activeCells(); ++cell)
        {
            require(p.cells[cell] >= 0, "new factory source is fully partitioned");
            if (cell && p.cells[cell] != p.cells[cell - 1])
                require(cell % p.grid == 0, "new factory slice boundaries follow the displayed grid");
        }
        const auto mapping = audibleSourceCells(p);
        for (std::size_t j = 0; j < i; ++j)
            if (p.inputBars == factories[j].pattern.inputBars && mapping == audibleSourceCells(factories[j].pattern))
                throw std::runtime_error("audible duplicate: " + factories[i].name + " and " + factories[j].name);
        if (i >= 63)
        {
            const auto period = [](const std::vector<int>& sequence)
            {
                for (std::size_t length = 1; length <= sequence.size(); ++length)
                {
                    if (sequence.size() % length != 0) continue;
                    bool repeats = true;
                    for (std::size_t cell = length; cell < sequence.size(); ++cell)
                        repeats &= sequence[cell] == sequence[cell % length];
                    if (repeats) return std::vector<int>(sequence.begin(), sequence.begin() + length);
                }
                return sequence;
            };
            for (std::size_t j = 0; j < i; ++j)
                if (p.inputBars == factories[j].pattern.inputBars
                    && period(mapping) == period(audibleSourceCells(factories[j].pattern)))
                    throw std::runtime_error("duplicate repeating audio cycle: " + factories[i].name + " and " + factories[j].name);
        }
    }
}

Pattern partition(int bars, int grid, int colours = 3)
{
    auto p = Pattern::makeDefault();
    p.inputBars = bars;
    p.grid = grid;
    p.cells.fill(-1);
    p.order.fill(-1);
    p.orderCount = colours;
    for (int i = 0; i < p.activeCells(); ++i)
        p.cells[i] = std::min(colours - 1, i * colours / p.activeCells());
    for (int i = 0; i < colours; ++i) p.order[i] = i;
    require(p.validate(), "test partition validates");
    return p;
}

void slicesAreDeterministicAlignedAndKeepReferences()
{
    for (int bars : {1, 2, 4})
        for (int grid : {1, 2, 4})
            for (std::uint32_t seed = 0; seed < 2000; ++seed)
            {
                const auto original = partition(bars, grid);
                auto p = original, again = original;
                std::string reason = "old error";
                require(randomiseSlices(p, seed, &reason), "random slices succeeds");
                require(randomiseSlices(again, seed), "repeat random slices succeeds");
                require(p == again && p.validate(), "slice seed is deterministic and valid");
                require(reason.empty(), "successful slices clear error");
                require(p.order == original.order && p.orderCount == original.orderCount, "slices preserve complete order");
                require(p.inputBars == bars && p.grid == grid && p.outputBars == original.outputBars, "slices preserve timing");
                require(p.cells != original.cells, "slices produce a useful change");
                for (int c : original.order)
                    if (c >= 0) require(p.region(c).valid(), "slices preserve referenced colours");
                for (int cell = 0; cell < p.activeCells(); ++cell)
                {
                    require(p.cells[cell] >= 0, "slices partition complete source");
                    if (cell && p.cells[cell] != p.cells[cell - 1])
                        require(cell % grid == 0, "slice boundaries follow grid");
                }
            }

    for (int grid : {1, 2, 4})
    {
        const auto original = partition(1, grid, 8);
        auto p = original;
        std::string reason;
        const bool ok = randomiseSlices(p, 9, &reason);
        if (grid == 4)
            require(!ok && p == original && !reason.empty(), "impossible coarse partition rejects atomically");
        else
            require(ok && p.validate(), "all eight referenced colours survive a sufficient grid");
    }
}

void orderRandomisationPreservesSourceAndFillsLoop()
{
    for (int bars : {1, 2, 4})
        for (int grid : {1, 2, 4})
            for (int outputBars : {1, 2, 4, 8})
                for (std::uint32_t seed = 0; seed < 1000; ++seed)
                {
                    auto original = partition(bars, grid);
                    original.outputBars = outputBars;
                    original.orderCount = 6;
                    const std::array<int, 6> order {0, 0, 1, 2, 1, 2};
                    std::copy(order.begin(), order.end(), original.order.begin());
                    auto p = original, again = original;
                    require(shuffleOrder(p, seed) && shuffleOrder(again, seed), "shuffle succeeds");
                    require(p == again && p.validate(), "shuffle deterministic and valid");
                    require(p.cells == original.cells && p.inputBars == bars && p.outputBars == outputBars && p.grid == grid,
                            "shuffle preserves source and timing");
                    require(p.orderCount == original.orderCount && p.order != original.order, "shuffle changes a diverse order");
                    auto shuffled = p.order;
                    std::sort(shuffled.begin(), shuffled.begin() + p.orderCount);
                    require(std::equal(shuffled.begin(), shuffled.begin() + 6, std::array<int,6>{0,0,1,1,2,2}.begin()),
                            "shuffle preserves occurrence counts");
                    p = original; again = original;
                    require(generateOrder(p, seed) && generateOrder(again, seed), "new sequence succeeds");
                    require(p == again && p.validate(), "new sequence deterministic and valid");
                    require(p.cells == original.cells && p.inputBars == bars && p.outputBars == outputBars && p.grid == grid,
                            "new sequence preserves source and timing");
                    require(p.order != original.order || p.orderCount != original.orderCount, "new sequence changes diverse arrangement");
                    require(p.orderCount > 0 && p.orderCount <= maxOrder, "new sequence is bounded");
                    int duration = 0;
                    for (int i = 0; i < p.orderCount; ++i)
                    {
                        require(p.order[i] >= 0 && p.order[i] < 3, "new sequence uses only source colours");
                        const auto r = p.region(p.order[i]);
                        duration += r.end - r.first;
                    }
                    require(duration >= outputBars * cellsPerBar, "new sequence covers output loop");
                }
}

void emptySingletonAndMalformedCases()
{
    auto empty = partition(1, 4, 1);
    empty.cells.fill(-1); empty.order.fill(-1); empty.orderCount = 0;
    auto p = empty;
    require(shuffleOrder(p, 0) && p == empty, "empty shuffle is a valid no-op");
    std::string reason;
    require(!generateOrder(p, 0, &reason) && p == empty && !reason.empty(), "no regions rejects new sequence atomically");
    require(randomiseSlices(p, 0) && p.validate() && p.orderCount == 0, "empty source can receive slices");
    auto single = partition(1, 4, 1);
    require(shuffleOrder(single, 7), "one-occurrence shuffle succeeds");
    require(single.orderCount == 1 && single.order[0] == 0, "one-occurrence shuffle unchanged");
    single.orderCount = 8;
    std::fill_n(single.order.begin(), single.orderCount, 0);
    const auto sameColours = single;
    require(shuffleOrder(single, 5) && single == sameColours, "same-colour shuffle valid no-op");
    single.outputBars = 8;
    require(generateOrder(single, 17) && single.validate(), "single-colour sequence valid");
    require(single.orderCount == 8, "single-colour sequence repeats whole source to fill loop");
    auto shortest = partition(1, 1, 1);
    shortest.cells.fill(-1); shortest.cells[0] = 0; shortest.outputBars = 8;
    require(generateOrder(shortest, 2) && shortest.orderCount == maxOrder, "one-cell source covers maximum output within capacity");
    for (auto action : {randomiseSlices, shuffleOrder, generateOrder})
    {
        auto invalid = Pattern::makeDefault();
        invalid.order[0] = 7;
        const auto original = invalid;
        reason.clear();
        require(!action(invalid, 0, &reason) && invalid == original && !reason.empty(), "malformed input rejects atomically");
        auto valid = Pattern::makeDefault();
        reason = "old error";
        require(action(valid, 17, &reason) && reason.empty(), "successful random action clears prior error");
    }
}

void newSequenceChangesActiveOrderDespiteUnusedStorage()
{
    auto p = Pattern::makeDefault();
    require(generateOrder(p, 17), "generate initial sequence");
    // Pattern permits unused order slots; they must not disguise a visible no-op.
    require(p.orderCount < maxOrder, "fixture has unused order storage");
    p.order[p.orderCount] = 7;
    const auto original = p;
    require(generateOrder(p, 17), "regenerate sequence with same seed");
    require(p.orderCount != original.orderCount
            || !std::equal(p.order.begin(), p.order.begin() + p.orderCount, original.order.begin()),
            "new sequence changes visible references when unused storage differs");
}

void eightColourAndSparseReferenceStress()
{
    for (int bars : {1, 2, 4})
        for (int grid : {1, 2, 4})
            for (std::uint32_t seed = 0; seed < 1000; ++seed)
            {
                const auto original = partition(bars, grid, 8);
                auto p = original;
                const bool ok = randomiseSlices(p, seed);
                if (bars == 1 && grid == 4)
                    require(!ok && p == original, "every impossible coarse seed rejects atomically");
                else
                {
                    require(ok && p.validate(), "eight-colour seeded partition valid");
                    require(p.order == original.order && p.orderCount == 8, "all references retained exactly");
                    for (int c = 0; c < 8; ++c)
                    {
                        const auto r = p.region(c);
                        require(r.valid() && r.first % grid == 0 && r.end % grid == 0, "all eight grid-aligned regions survive");
                    }
                }
            }
    auto sparse = partition(1, 4, 1);
    for (int i = 0; i < sparse.activeCells(); ++i) sparse.cells[i] = 7;
    sparse.order[0] = 7;
    require(randomiseSlices(sparse, 8) && sparse.region(7).valid(), "high numbered referenced colour retained");
    sparse.order.fill(-1); sparse.orderCount = 0;
    require(generateOrder(sparse, 4) && sparse.validate(), "empty order can be generated from assigned regions");
}

void randomisationKeepsRestsSafeAndExplicit()
{
    auto original = partition(1, 1, 3);
    original.order.fill(-1);
    original.orderCount = 8;
    const std::array<int,8> initial {0,8,1,9,2,10,11,12};
    std::copy(initial.begin(), initial.end(), original.order.begin());
    original.outputBars = 8;
    require(original.validate(), "test order contains five supported rest lengths");
    bool generatedRest = false;
    for (std::uint32_t seed = 0; seed < 2000; ++seed)
    {
        auto slices = original;
        require(randomiseSlices(slices, seed) && slices.validate(), "rest tokens never index source-colour arrays");
        require(slices.order == original.order && slices.orderCount == original.orderCount,
                "random slices retain exact rests and order");
        for (int c = 0; c < 3; ++c)
            require(slices.region(c).valid(), "random slices keep every referenced audible colour");
        auto shuffled = original;
        require(shuffleOrder(shuffled, seed) && shuffled.validate(), "rest shuffle remains valid");
        auto sorted = shuffled.order;
        std::sort(sorted.begin(), sorted.begin() + shuffled.orderCount);
        require(std::equal(sorted.begin(), sorted.begin() + 8,
                           std::array<int,8>{0,1,2,8,9,10,11,12}.begin()),
                "shuffle retains each rest length and occurrence");
        auto generated = original;
        require(generateOrder(generated, seed) && generated.validate(), "new sequence can reuse existing rest choices");
        require(generated.orderCount > 0 && generated.order[0] < colourCount,
                "random generated arrangements begin with an audible source phrase");
        for (int i = 0; i < generated.orderCount; ++i)
        {
            require(std::find(initial.begin(), initial.end(), generated.order[i]) != initial.end(),
                    "generated sequence only uses assigned colours and existing rest lengths");
            generatedRest |= generated.order[i] >= colourCount;
        }
        auto noRests = partition(1, 1, 3);
        require(generateOrder(noRests, seed), "ordinary sequence still generates");
        for (int i = 0; i < noRests.orderCount; ++i)
            require(noRests.order[i] < colourCount, "old patterns never acquire surprise silence");
    }
    require(generatedRest, "existing silence is available to sequence generation");

    auto allRest = original;
    allRest.cells.fill(-1);
    allRest.order.fill(-1);
    allRest.order[0] = 8; allRest.order[1] = 10; allRest.orderCount = 2;
    for (std::uint32_t seed = 0; seed < 100; ++seed)
    {
        auto generated = allRest;
        require(generateOrder(generated, seed) && generated.validate(), "all-rest patterns generate from existing rest durations");
        int duration = 0;
        for (int i = 0; i < generated.orderCount; ++i)
        {
            require(generated.order[i] == 8 || generated.order[i] == 10, "all-rest generation keeps only present rest lengths");
            duration += generated.order[i] == 8 ? 1 : 4;
        }
        require(duration >= 128, "all-rest generation covers the complete output loop");
    }
}

void generatedRestFallbackDoesNotLeaveHiddenOrderEntries()
{
    auto p = partition(1, 1, 1);
    p.cells.fill(-1);
    p.outputBars = 1;
    p.order.fill(-1);
    p.order[0] = 8; p.order[1] = 12; p.orderCount = 2;
    for (std::uint32_t seed = 0; seed < 100; ++seed)
    {
        auto generated = p;
        require(generateOrder(generated, seed), "mixed-duration rest order generates");
        const auto expanded = expandSequence(generated);
        std::array<bool, maxOrder> reached {};
        for (int i = 0; i < expanded.count; ++i) reached[expanded.segments[i].orderIndex] = true;
        for (int i = 0; i < generated.orderCount; ++i)
            if (!reached[i]) throw std::runtime_error("generated rest fallback hides an order token for seed " + std::to_string(seed));
    }
}

void shufflingKeepsPlayLengthsWithTheirOccurrences()
{
    auto original = Pattern::makeDefault();
    original.orderCount = 8;
    const std::array<int, 8> tokens {0,0,1,2,1,2,restToken(1),restToken(4)};
    const std::array<int, 8> lengths {0,8,2,12,0,4,0,0};
    std::copy(tokens.begin(), tokens.end(), original.order.begin());
    std::copy(lengths.begin(), lengths.end(), original.orderLengths.begin());
    require(original.validate(), "length-bearing shuffle fixture validates");
    const std::vector<std::pair<int, int>> expected
        {{0,0},{0,8},{1,0},{1,2},{2,4},{2,12},{restToken(1),0},{restToken(4),0}};
    for (std::uint32_t seed = 0; seed < 1000; ++seed)
    {
        auto shuffled = original, again = original;
        require(shuffleOrder(shuffled, seed) && shuffleOrder(again, seed),
                "length-bearing shuffle remains valid with rests");
        require(shuffled == again, "length-bearing shuffle is seed deterministic");
        require(shuffled.cells == original.cells && shuffled.orderCount == 8,
                "length-bearing shuffle preserves source and occurrence count");
        std::vector<std::pair<int, int>> pairs;
        for (int i = 0; i < shuffled.orderCount; ++i)
        {
            pairs.emplace_back(shuffled.order[i], shuffled.orderLengths[i]);
            if (isRest(shuffled.order[i]))
                require(shuffled.orderLengths[i] == 0, "shuffled rests never acquire a play length");
        }
        std::sort(pairs.begin(), pairs.end());
        require(pairs == expected, "shuffle retains every token and its own play length as a pair");
    }
}

void sameColourDifferentPlayLengthsShuffleVisibly()
{
    auto original = Pattern::makeDefault();
    original.order.fill(-1);
    original.orderCount = 2;
    original.order[0] = original.order[1] = 0;
    original.orderLengths[1] = 8;
    require(original.validate(), "same-colour shuffle fixture validates");
    // These seeds exercise both a random swap and the no-op fallback for two
    // occurrences. Their colours match, but their audible lengths differ.
    for (std::uint32_t seed = 0; seed < 128; ++seed)
    {
        auto shuffled = original;
        require(shuffleOrder(shuffled, seed), "same-colour play lengths can shuffle");
        require(shuffled.order == original.order && shuffled.orderLengths[0] == 8
                && shuffled.orderLengths[1] == 0,
                "same-colour shuffle changes the audible sequence even when the permutation repeats");
    }
}

void generatedSequenceResetsAllPlayLengths()
{
    auto original = partition(1, 1, 1);
    original.outputBars = 1;
    original.orderCount = maxOrder;
    original.order.fill(0);
    original.orderLengths.fill(1);
    require(original.validate(), "full trimmed-order generation fixture validates");
    for (std::uint32_t seed = 0; seed < 100; ++seed)
    {
        auto generated = original;
        require(generateOrder(generated, seed), "new sequence accepts a previously trimmed order");
        require(generated.validate() && generated.orderCount == 1 && generated.orderLength(0) == 16,
                "new sequence uses a Full source phrase to fill the loop");
        require(std::all_of(generated.orderLengths.begin(), generated.orderLengths.end(),
                            [](int cells) { return cells == 0; }),
                "new sequence clears active and newly inactive play length overrides");
    }
}

void sourceChangesRetainCapsAndClampEffectiveLengths()
{
    auto resized = Pattern::makeDefault();
    resized.orderLengths[0] = 8;
    require(resized.resizeInput(1) && resized.orderLengths[0] == 8 && resized.orderLength(0) == 6,
            "shrinking the source retains its stored cap and plays only the remaining source");
    require(resized.resizeInput(2) && resized.orderLengths[0] == 8 && resized.orderLength(0) == 8,
            "growing the source restores the capped play length");

    auto original = partition(2, 1, 3);
    original.orderLengths[0] = 8;
    original.orderLengths[1] = 64;
    bool heardClampedPhrase = false;
    for (std::uint32_t seed = 0; seed < 1000; ++seed)
    {
        auto slices = original;
        require(randomiseSlices(slices, seed) && slices.validate(), "trimmed order supports random slices");
        require(slices.order == original.order && slices.orderLengths == original.orderLengths,
                "random slices retain each occurrence and its stored cap");
        const auto first = slices.region(0);
        const auto second = slices.region(1);
        const auto third = slices.region(2);
        require(slices.orderLength(0) == std::min(8, first.end - first.first)
                && slices.orderLength(1) == second.end - second.first
                && slices.orderLength(2) == third.end - third.first,
                "randomized source regions clamp caps while Full follows the complete region");
        heardClampedPhrase |= first.end - first.first < 8;
        const auto expanded = expandSequence(slices);
        for (int i = 0; i < expanded.count; ++i)
        {
            const auto& segment = expanded.segments[i];
            const auto region = slices.region(segment.colour);
            require(segment.length <= region.end - region.first,
                    "randomized trimmed playback never reads beyond its source region");
        }
    }
    require(heardClampedPhrase, "random slice fixture exercises a source shorter than its stored cap");
}
}
int main()
{
    try
    {
        originalFactoriesAreFrozen();
        releasedCatalogIncludingCategoriesIsFrozen();
        endShufflesOnlyPermuteTheFinalBeat();
        kickBuildsAccelerateTheSameKickOverTwoOutputBars();
        newKickBuildsAndEndShufflesAreAppendedInTheirCategories();
        factoriesAreDistinctAndValid();
        browserCategoriesContainTenNewPresetsForEachSourceLength();
        newCategoriesHaveTheirMusicalBehaviour();
        threeBeatFactoriesRepeatTheActualFirstThreeBeats();
        jungleFactoriesKeepAnchorsAndChopTheIntendedTail();
        sixBeatFactoriesRepeatExactlySixSourceBeats();
        rollFactoriesRetriggerTheActualKickAndSnareOnsets();
        gatedFactoriesCreateLiteralGapsWithoutUsingSourceColours();
        expandedFactoriesAreAudibleDistinctAndKeepLegacyIndices();
        slicesAreDeterministicAlignedAndKeepReferences();
        orderRandomisationPreservesSourceAndFillsLoop();
        emptySingletonAndMalformedCases();
        newSequenceChangesActiveOrderDespiteUnusedStorage();
        eightColourAndSparseReferenceStress();
        randomisationKeepsRestsSafeAndExplicit();
        generatedRestFallbackDoesNotLeaveHiddenOrderEntries();
        shufflingKeepsPlayLengthsWithTheirOccurrences();
        sameColourDifferentPlayLengthsShuffleVisibly();
        generatedSequenceResetsAllPlayLengths();
        sourceChangesRetainCapsAndClampEffectiveLengths();
        std::cout << "Library tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Library test failure: " << e.what() << '\n';
        return 1;
    }
}
