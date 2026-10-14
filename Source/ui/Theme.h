#pragma once
#include <fjord_base/ui/Theme.h>
#include <fjord_base/ui/Controls.h>
#include <fjord_base/ui/Keyboard.h>
#include <fjord_base/ui/PluginView.h>

// Hamar's UI builds on the shared family UI of fjord-base (palette from the Brand, Manrope, controls, header,
// presets). Only Hamar's own colour helpers live here: ivory and brass (the accent and the brand gradient), the wood
// of case and soundboard, the strings.
namespace hamar::ui
{
    using namespace fjord::base::ui;
    using APVTS = juce::AudioProcessorValueTreeState;

    inline const std::array<juce::Colour, 3>& brassColours (bool dark) { return gradientColours (dark); }
    inline juce::Colour brassAt (bool dark, float x) { return gradientAt (dark, x); }

    // The case (polished black in the dark theme, a warm dark wood in the light one)
    inline juce::Colour caseColour (bool dark)    { return dark ? juce::Colour (0xff070b11) : juce::Colour (0xff2a1d16); }
    inline juce::Colour caseEdge (bool dark)      { return dark ? juce::Colour (0xff2a3446) : juce::Colour (0xff4b3628); }
    // Spruce of the soundboard, toned towards the panel so it never shouts
    inline juce::Colour boardColour (bool dark)   { return dark ? juce::Colour (0xff2b2418) : juce::Colour (0xfff1e2c4); }
    inline juce::Colour boardGrain (bool dark)    { return dark ? juce::Colour (0xff3a3020) : juce::Colour (0xffe4cfa7); }
    // Plain steel and copper-wound strings
    inline juce::Colour steelColour (bool dark)   { return dark ? juce::Colour (0xff8e9aa8) : juce::Colour (0xff8a949e); }
    inline juce::Colour copperColour (bool dark)  { return dark ? juce::Colour (0xffc48a52) : juce::Colour (0xffa86a35); }
    inline juce::Colour feltColour (bool dark)    { return dark ? juce::Colour (0xffd9d2c3) : juce::Colour (0xfff6f1e6); }

    // A colour per instrument category (grand, upright, electric, historic, dance, fantasy), readable in both themes
    inline juce::Colour categoryColour (int category, bool dark)
    {
        static const juce::uint32 darkC[]  = { 0xffe6c88a, 0xffd9a77a, 0xff8fc7e6, 0xffc9b38a, 0xffe68ab4, 0xffa99ff0 };
        static const juce::uint32 lightC[] = { 0xff8a6526, 0xff8f5a33, 0xff2f7396, 0xff7a6438, 0xffa83f72, 0xff6a5bc0 };
        return juce::Colour ((dark ? darkC : lightC)[std::clamp (category, 0, 5)]);
    }

    inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
}
