#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/** Lo "stile" grafico dei controlli standard JUCE (manopole, pulsanti, menu, finestre). */
class DawLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DawLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, Theme::accent);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::accent);
        setColour (juce::TextButton::buttonColourId, Theme::raised);
        setColour (juce::Label::textColourId, Theme::text);

        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::raisedHi);
        setColour (juce::PopupMenu::highlightedTextColourId, Theme::textBright);

        setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::AlertWindow::outlineColourId, Theme::border);
        setColour (juce::TextEditor::backgroundColourId, Theme::lcdBg);
        setColour (juce::TextEditor::textColourId, Theme::textBright);
        setColour (juce::TextEditor::outlineColourId, Theme::border);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
        setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, Theme::accent);
    }

    //==============================================================================
    // Menu a tendina grandi, comodi da toccare col dito
    juce::Font getPopupMenuFont() override    { return Theme::font (17.0f); }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight,
                                    int& idealWidth, int& idealHeight) override
    {
        juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, idealWidth, idealHeight);
        if (! isSeparator)
            idealHeight = juce::jmax (idealHeight, 40);
        idealWidth = juce::jmax (idealWidth, 220);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::cyan.withAlpha (0.25f));
        g.drawRect (0, 0, width, height);
    }

    //==============================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto  centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float track  = juce::jmax (2.0f, size * 0.075f);
        const float arcR   = radius - track * 0.5f;
        const auto  colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const float angle  = startAngle + pos * (endAngle - startAngle);

        // Traccia e valore: un anello sottile
        juce::Path bgArc, valueArc;
        bgArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        // Manopole "bipolari" (pitch, tono, pan): l'arco parte dal centro
        const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        valueArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (juce::Colour (0xff1c2630));
        g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (slider.isEnabled())
        {
            g.setColour (colour.withAlpha (0.22f));   // alone
            g.strokePath (valueArc, juce::PathStrokeType (track * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (colour);
        g.strokePath (valueArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Corpo della manopola: scuro, opaco, con un filo di luce in alto
        const float knobR = radius - track * 2.0f;
        auto knob = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (Theme::raisedHi, knob.getCentreX(), knob.getY(),
                                                 Theme::panel, knob.getCentreX(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (Theme::border);
        g.drawEllipse (knob, 1.0f);

        // Tacca
        g.setColour (Theme::textBright);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.35f, angle),
                      centre.getPointOnCircumference (knobR * 0.9f, angle) },
                    juce::jmax (1.5f, size * 0.055f));
    }

    //==============================================================================
    /** Fader verticale: binario sottile e cappuccio chiaro con una riga del colore del canale. */
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }
        auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float cx = r.getCentreX();
        auto trackR = juce::Rectangle<float> (cx - 2.5f, r.getY() + 6.0f, 5.0f, r.getHeight() - 12.0f);
        g.setColour (Theme::lcdBg);
        g.fillRoundedRectangle (trackR, 2.5f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (trackR, 2.5f, 1.0f);
        const auto colour = slider.findColour (juce::Slider::thumbColourId);
        g.setColour (colour.withAlpha (0.5f));
        g.fillRoundedRectangle (trackR.withTop (sliderPos).reduced (1.0f, 0.0f), 2.0f);

        const float capW = juce::jmin (36.0f, r.getWidth() - 4.0f), capH = 16.0f;
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, sliderPos });
        g.setColour (colour.withAlpha (0.25f));
        g.fillRoundedRectangle (cap.expanded (3.0f), 7.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9eef2), cap.getX(), cap.getY(), juce::Colour (0xff8f9ba6), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (colour);
        g.fillRect (cap.reduced (5.0f, 0.0f).withHeight (2.0f).withCentre (cap.getCentre()));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool isHighlighted, bool isDown) override
    {
        auto r = button.getLocalBounds().toFloat();
        auto base = Theme::raised;

        if (isDown)             base = base.darker (0.25f);
        else if (isHighlighted) base = base.brighter (0.06f);

        Theme::drawRaised (g, r, base);

        if (button.getToggleState())
            Theme::drawActiveGlow (g, r);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return Theme::font (juce::jlimit (12.0f, 16.0f, (float) buttonHeight * 0.42f), true);
    }

    juce::Font getAlertWindowMessageFont() override   { return Theme::font (16.0f); }
    juce::Font getAlertWindowTitleFont() override     { return Theme::font (18.0f, true); }
};
