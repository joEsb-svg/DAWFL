#pragma once

#include <JuceHeader.h>

/** Colori e misure dell'interfaccia, tutti in un posto solo. */
namespace Theme
{
    const juce::Colour bgTop        { 0xff1d2129 };
    const juce::Colour bgBottom     { 0xff12141a };
    const juce::Colour panel        { 0xff232833 };
    const juce::Colour panelHi      { 0xff2d3340 };
    const juce::Colour outline      { 0xff3a4150 };
    const juce::Colour text         { 0xffeef1f6 };
    const juce::Colour textDim      { 0xff8a93a5 };
    const juce::Colour accent       { 0xffff8a1f };   // arancione "fruity"
    const juce::Colour accent2      { 0xffffc93c };
    const juce::Colour lcdBg        { 0xff0c0f12 };
    const juce::Colour lcdText      { 0xff7dffb0 };
    const juce::Colour padOffA      { 0xff2f3541 };   // gruppi di 4 step alternati
    const juce::Colour padOffB      { 0xff3b3340 };
    const juce::Colour playhead     { 0xffffffff };

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}
