#pragma once

#include <JuceHeader.h>
#include <functional>
#include "Theme.h"

//==============================================================================
/** Pulsante Play/Stop disegnato a mano: triangolo o quadrato, con alone quando suona. */
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("Play") {}

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool playing = getToggleState();
        const auto colour = playing ? Theme::lcdText : Theme::accent;
        const float corner = r.getHeight() * 0.25f;

        if (playing)
        {
            g.setColour (colour.withAlpha (0.18f));
            g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        }

        auto top = Theme::panelHi.brighter (isHighlighted ? 0.12f : 0.06f);
        auto bottom = Theme::panel.darker (0.2f);
        if (isDown) { top = top.darker (0.2f); bottom = bottom.darker (0.2f); }

        g.setGradientFill (juce::ColourGradient (top, r.getTopLeft(), bottom, r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (playing ? colour : Theme::outline);
        g.drawRoundedRectangle (r, corner, playing ? 2.0f : 1.0f);

        const float s = r.getHeight() * 0.42f;
        auto icon = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
        juce::Path p;

        if (playing)
            p.addRoundedRectangle (icon.reduced (s * 0.06f), s * 0.12f);
        else
            p.addTriangle (icon.getX() + s * 0.12f, icon.getY(), icon.getX() + s * 0.12f, icon.getBottom(),
                           icon.getRight() + s * 0.05f, icon.getCentreY());

        g.setColour (colour);
        g.fillPath (p);
    }
};

//==============================================================================
/** Display stile LCD del tempo. Trascina su/giu' per cambiarlo, o tocca - / + ai lati. */
class BpmDisplay : public juce::Component
{
public:
    std::function<double()>     getBpm;
    std::function<void(double)> setBpm;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float corner = r.getHeight() * 0.22f;

        g.setColour (Theme::lcdBg);
        g.fillRoundedRectangle (r, corner);
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (r, corner, 1.0f);

        auto minus = getMinusArea().toFloat();
        auto plus  = getPlusArea().toFloat();
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (r.getHeight() * 0.55f, true));
        g.drawText ("-", minus, juce::Justification::centred);
        g.drawText ("+", plus,  juce::Justification::centred);

        auto middle = r.withLeft (minus.getRight()).withRight (plus.getX());
        const double bpm = getBpm ? getBpm() : 120.0;

        g.setColour (Theme::lcdText.withAlpha (0.12f));
        g.fillRoundedRectangle (middle.reduced (2.0f, r.getHeight() * 0.14f), 4.0f);

        g.setColour (Theme::lcdText);
        g.setFont (Theme::font (r.getHeight() * 0.5f, true));
        g.drawText (juce::String (juce::roundToInt (bpm)), middle.removeFromTop (middle.getHeight() * 0.72f),
                    juce::Justification::centredBottom);
        g.setColour (Theme::lcdText.withAlpha (0.6f));
        g.setFont (Theme::font (r.getHeight() * 0.2f, true));
        g.drawText ("BPM", middle, juce::Justification::centredTop);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartBpm = getBpm ? getBpm() : 120.0;
        dragged = false;
        downPos = e.getPosition();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = downPos.y - e.getPosition().y;
        if (std::abs (dy) > 6)
            dragged = true;

        if (dragged && setBpm)
        {
            setBpm (dragStartBpm + dy / 4.0);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragged || ! setBpm || ! getBpm)
            return;

        if (getMinusArea().contains (e.getPosition()))      setBpm (getBpm() - 1.0);
        else if (getPlusArea().contains (e.getPosition()))  setBpm (getBpm() + 1.0);
        repaint();
    }

private:
    juce::Rectangle<int> getMinusArea() const  { return getLocalBounds().removeFromLeft (getWidth() / 4); }
    juce::Rectangle<int> getPlusArea() const   { return getLocalBounds().removeFromRight (getWidth() / 4); }

    double dragStartBpm = 120.0;
    bool dragged = false;
    juce::Point<int> downPos;
};

//==============================================================================
/** Meter verticale a segmenti per il livello d'uscita. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newPeak)
    {
        level = juce::jmax (newPeak, level * 0.86f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::lcdBg);
        g.fillRoundedRectangle (r, 3.0f);

        const int segments = 12;
        const float gap = 2.0f;
        const float segH = (r.getHeight() - gap * (segments + 1)) / (float) segments;
        const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, level) * segments);

        for (int i = 0; i < segments; ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + gap, r.getBottom() - gap - (float) (i + 1) * (segH + gap) + gap,
                                               r.getWidth() - gap * 2.0f, segH);
            auto c = i >= segments - 1 ? juce::Colour (0xffff4d4d)
                   : i >= segments - 4 ? Theme::accent2
                                       : Theme::lcdText;
            g.setColour (i < lit ? c : c.withAlpha (0.1f));
            g.fillRoundedRectangle (seg, 1.5f);
        }
    }

private:
    float level = 0.0f;
};

//==============================================================================
/** Riga di 16 LED sopra il channel rack: mostra il punto di riproduzione. */
class StepLeds : public juce::Component
{
public:
    /** Funzione che dice dove sta ogni colonna (la fornisce il channel rack, cosi' sono allineati). */
    std::function<juce::Rectangle<float> (int)> getColumnBounds;

    void setCurrentStep (int step)
    {
        if (step != current)
        {
            current = step;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (! getColumnBounds)
            return;

        for (int i = 0; i < 16; ++i)
        {
            auto col = getColumnBounds (i).withY (0.0f).withHeight ((float) getHeight());
            const float d = juce::jmin (col.getHeight() * 0.5f, col.getWidth() * 0.4f);
            auto led = juce::Rectangle<float> (d * 1.8f, d * 0.7f).withCentre ({ col.getCentreX(), col.getHeight() * 0.62f });
            const bool on = i == current;
            const bool beat = i % 4 == 0;

            if (on)
            {
                g.setColour (Theme::lcdText.withAlpha (0.3f));
                g.fillRoundedRectangle (led.expanded (3.0f), 4.0f);
            }

            g.setColour (on ? Theme::lcdText : (beat ? Theme::outline.brighter (0.3f) : Theme::outline));
            g.fillRoundedRectangle (led, led.getHeight() * 0.5f);

            if (beat)
            {
                g.setColour (on ? Theme::lcdText : Theme::textDim);
                g.setFont (Theme::font (juce::jmin (12.0f, col.getHeight() * 0.42f), true));
                g.drawText (juce::String (i / 4 + 1), col.withHeight (col.getHeight() * 0.42f).translated (0.0f, -1.0f),
                            juce::Justification::centredLeft);
            }
        }
    }

private:
    int current = -1;
};
