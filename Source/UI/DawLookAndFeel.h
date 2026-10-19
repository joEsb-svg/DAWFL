#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/** Lo "stile" grafico dei controlli standard JUCE (manopole, pulsanti). */
class DawLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DawLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::bgBottom);
        setColour (juce::Label::textColourId, Theme::text);

        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, Theme::bgBottom);
    }

    // Menu a tendina grandi, comodi da toccare col dito
    juce::Font getPopupMenuFont() override    { return Theme::font (18.0f); }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight,
                                    int& idealWidth, int& idealHeight) override
    {
        juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, idealWidth, idealHeight);
        if (! isSeparator)
            idealHeight = juce::jmax (idealHeight, 42);
        idealWidth = juce::jmax (idealWidth, 200);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto area          = bounds.withSizeKeepingCentre (size, size);
        const float radius = size * 0.5f;
        const float track  = juce::jmax (2.0f, size * 0.11f);
        const auto centre  = area.getCentre();
        const auto colour  = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const float angle  = startAngle + pos * (endAngle - startAngle);

        // Arco di sfondo
        juce::Path bgArc;
        bgArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::bgBottom);
        g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Arco del valore, con un leggero alone
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (valueArc, juce::PathStrokeType (track * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (valueArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Corpo della manopola
        const float knobR = radius - track * 2.2f;
        auto knob = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (Theme::panelHi.brighter (0.25f), knob.getTopLeft(),
                                                 Theme::panel.darker (0.4f), knob.getBottomRight(), false));
        g.fillEllipse (knob);
        g.setColour (Theme::outline);
        g.drawEllipse (knob, 1.0f);

        // Indicatore
        const auto tip = centre.getPointOnCircumference (knobR * 0.75f, angle);
        g.setColour (Theme::text);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.2f, angle), tip }, juce::jmax (1.5f, size * 0.06f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool isHighlighted, bool isDown) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (1.0f);
        const float corner = juce::jmin (8.0f, r.getHeight() * 0.25f);
        const bool on = button.getToggleState();

        auto top    = on ? Theme::accent.brighter (0.2f) : Theme::panelHi.brighter (0.08f);
        auto bottom = on ? Theme::accent.darker (0.25f)  : Theme::panel;

        if (isDown)             { top = top.darker (0.2f); bottom = bottom.darker (0.2f); }
        else if (isHighlighted) { top = top.brighter (0.05f); }

        g.setGradientFill (juce::ColourGradient (top, r.getTopLeft(), bottom, r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? Theme::accent.brighter (0.4f) : Theme::outline);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return Theme::font (juce::jmin (22.0f, (float) buttonHeight * 0.55f), true);
    }
};
