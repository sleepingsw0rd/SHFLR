#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace desk
{
inline const juce::Colour chassis{0xffdedbd2}, panel{0xffe9e6dd}, key{0xffefede5};
inline const juce::Colour ink{0xff252622}, muted{0xff585a52}, rule{0xffaaa89d}, accent{0xffa43c1c};
inline const juce::Colour display{0xff252823}, waveform{0xffd8ddd0},
                          displayText{0xfff1eee4}, displayMuted{0xffb6baac};

// Stable phrase identifiers, in the same order as the colour-assignment menu.
inline const std::array<juce::Colour, 8> slices{{
    juce::Colour{0xffc7aa4d}, juce::Colour{0xffce7763}, juce::Colour{0xff82a6c1},
    juce::Colour{0xffce9764}, juce::Colour{0xffce8eaa}, juce::Colour{0xffa58dc3},
    juce::Colour{0xff77aaa2}, juce::Colour{0xff94b178}}};

inline juce::Colour sliceInk(int index)
{
    return slices[static_cast<size_t>(index)].interpolatedWith(ink, 0.56f);
}

inline juce::Font font(float size, bool bold = false)
{
    return juce::Font(juce::FontOptions("Segoe UI", size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono(float size)
{
    return juce::Font(juce::FontOptions("Consolas", size, juce::Font::plain));
}
}
