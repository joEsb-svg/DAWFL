#pragma once

#include <JuceHeader.h>
#include <functional>
#include "Theme.h"

//==============================================================================
/** Pulsante Play/Stop: triangolo quando fermo, quadrato (e LED verde) quando suona. */
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("Play") {}

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto r = getLocalBounds().toFloat();
        const bool playing = getToggleState();

        auto base = Theme::raised;
        if (isDown)             base = base.darker (0.25f);
        else if (isHighlighted) base = base.brighter (0.06f);
        Theme::drawRaised (g, r, base);

        const float s = juce::jmin (r.getHeight(), r.getWidth()) * 0.36f;
        auto icon = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
        juce::Path p;

        if (playing)
            p.addRectangle (icon.reduced (s * 0.08f));
        else
            p.addTriangle (icon.getX() + s * 0.1f, icon.getY(), icon.getX() + s * 0.1f, icon.getBottom(),
                           icon.getRight(), icon.getCentreY());

        g.setColour (playing ? Theme::led : Theme::textBright);
        g.fillPath (p);

        if (playing)
        {
            g.setColour (Theme::led);
            g.fillRect (r.reduced (5.0f, 0.0f).removeFromBottom (2.0f));
        }
    }
};

//==============================================================================
/**
    Display LCD con frecce ai lati.
    - BPM: trascina su/giu' sul numero, oppure tocca le frecce
    - Pattern: tocca le frecce per scorrere, il centro per il menu
*/
class LcdSelector : public juce::Component
{
public:
    std::function<juce::String()> getText;
    std::function<void (int delta)> onStep;          // freccia sinistra/destra
    std::function<void (double delta)> onDrag;       // trascinamento verticale (unita' per pixel / 4)
    std::function<void()> onCentreTap;
    juce::String caption;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawSunken (g, r);

        auto left = getLeftArea().toFloat(), right = getRightArea().toFloat();
        drawArrow (g, left, true);
        drawArrow (g, right, false);

        auto middle = r.withLeft (left.getRight()).withRight (right.getX());

        if (caption.isNotEmpty())
            Theme::drawCaption (g, caption, middle.removeFromTop (r.getHeight() * 0.32f).translated (0.0f, 2.0f));

        g.setColour (Theme::lcdText);
        g.setFont (Theme::font (juce::jmin (22.0f, r.getHeight() * (caption.isNotEmpty() ? 0.46f : 0.55f)), true));
        g.drawText (getText ? getText() : juce::String(), middle, juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        downY = e.position.y;
        accumulated = 0.0;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! onDrag)
            return;

        const double dy = downY - e.position.y;
        if (! dragged && std::abs (dy) > 6.0)
            dragged = true;

        if (dragged)
        {
            onDrag ((dy - accumulated) / 4.0);
            accumulated = dy;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragged)
            return;

        const auto p = e.getPosition();
        if (getLeftArea().contains (p))         { if (onStep) onStep (-1); }
        else if (getRightArea().contains (p))   { if (onStep) onStep (1); }
        else if (onCentreTap)                   onCentreTap();
        repaint();
    }

private:
    juce::Rectangle<int> getLeftArea() const   { return getLocalBounds().removeFromLeft (juce::jmin (34, getWidth() / 4)); }
    juce::Rectangle<int> getRightArea() const  { return getLocalBounds().removeFromRight (juce::jmin (34, getWidth() / 4)); }

    static void drawArrow (juce::Graphics& g, juce::Rectangle<float> area, bool pointLeft)
    {
        const float s = juce::jmin (area.getWidth(), area.getHeight()) * 0.24f;
        auto c = area.getCentre();
        juce::Path p;
        if (pointLeft) p.addTriangle (c.x + s * 0.5f, c.y - s, c.x + s * 0.5f, c.y + s, c.x - s * 0.6f, c.y);
        else           p.addTriangle (c.x - s * 0.5f, c.y - s, c.x - s * 0.5f, c.y + s, c.x + s * 0.6f, c.y);
        g.setColour (Theme::textDim);
        g.fillPath (p);
    }

    bool dragged = false;
    float downY = 0.0f;
    double accumulated = 0.0;
};

//==============================================================================
/** Selettore a segmenti (es. "16 | 32", "STEP | VEL"). */
class SegmentedControl : public juce::Component
{
public:
    SegmentedControl (juce::StringArray optionNames) : options (std::move (optionNames)) {}

    std::function<void (int)> onChange;

    void setSelected (int index)
    {
        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    int getSelected() const     { return selected; }
    int getNumOptions() const   { return options.size(); }

    void setOptions (const juce::StringArray& newOptions)
    {
        if (newOptions != options)
        {
            options = newOptions;
            selected = juce::jlimit (0, juce::jmax (0, options.size() - 1), selected);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawSunken (g, r, Theme::panel.darker (0.3f));

        const float w = r.getWidth() / (float) options.size();
        for (int i = 0; i < options.size(); ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight());

            if (i == selected)
                Theme::drawRaised (g, seg.reduced (2.0f), Theme::raisedHi);

            g.setColour (i == selected ? Theme::accent : Theme::textDim);
            g.setFont (Theme::font (juce::jmin (12.0f, r.getHeight() * 0.5f), true).withExtraKerningFactor (0.05f));
            g.drawText (options[i], seg, juce::Justification::centred, false);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;

        const int i = juce::jlimit (0, options.size() - 1, e.x * options.size() / juce::jmax (1, getWidth()));
        setSelected (i);
        if (onChange)
            onChange (i);
    }

private:
    juce::StringArray options;
    int selected = 0;
};

//==============================================================================
/** Meter verticale sottile per il livello d'uscita. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newPeak)
    {
        const float newLevel = juce::jmax (newPeak, level * 0.85f);
        if (std::abs (newLevel - level) > 0.002f)
        {
            level = newLevel;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawSunken (g, r, Theme::lcdBg, 2.0f);

        auto inner = r.reduced (2.0f);
        const float h = inner.getHeight() * juce::jlimit (0.0f, 1.0f, level);
        auto bar = inner.removeFromBottom (h);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0483e), inner.getX(), r.getY(),
                                                 Theme::led.darker (0.2f), inner.getX(), r.getBottom(), false));
        g.fillRect (bar);
    }

private:
    float level = 0.0f;
};

//==============================================================================
/** Righello sopra gli step: numeri dei quarti, tacche, e la posizione di riproduzione. */
class StepRuler : public juce::Component
{
public:
    /** Dove sta la colonna i della pagina visibile, fornita dal channel rack. */
    std::function<juce::Rectangle<float> (int)> getColumnBounds;

    void setState (int newPlayingColumn, int newFirstStep, int newNumColumns)
    {
        if (newPlayingColumn != playingColumn || newFirstStep != firstStep || newNumColumns != numColumns)
        {
            playingColumn = newPlayingColumn;
            firstStep = newFirstStep;
            numColumns = newNumColumns;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (! getColumnBounds)
            return;

        const float h = (float) getHeight();

        for (int i = 0; i < numColumns; ++i)
        {
            auto col = getColumnBounds (i).withY (0.0f).withHeight (h);
            const int step = firstStep + i;
            const bool beat = step % 4 == 0;

            if (i == playingColumn)
            {
                g.setColour (Theme::accent);
                g.fillRect (col.removeFromBottom (3.0f));
            }
            else
            {
                g.setColour (beat ? Theme::textDim : Theme::raisedHi);
                g.fillRect (col.withTop (h - (beat ? 5.0f : 3.0f)).withSizeKeepingCentre (1.0f, beat ? 5.0f : 3.0f));
            }

            if (beat)
            {
                g.setColour (i == playingColumn ? Theme::accent : Theme::textDim);
                g.setFont (Theme::font (11.0f, true));
                g.drawText (juce::String (step / 4 + 1), col.withHeight (h - 5.0f).withTrimmedLeft (2.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }

private:
    int playingColumn = -1;
    int firstStep = 0;
    int numColumns = 16;
};

//==============================================================================
/** Pulsante con un'icona disegnata (undo, redo, metronomo). */
class IconButton : public juce::Button
{
public:
    enum class Icon { undo, redo, metronome };

    explicit IconButton (Icon i) : juce::Button ("icon"), icon (i) {}

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto r = getLocalBounds().toFloat();
        auto base = Theme::raised;
        if (isDown)             base = base.darker (0.25f);
        else if (isHighlighted) base = base.brighter (0.06f);
        Theme::drawRaised (g, r, base);

        const bool on = getToggleState();
        const auto colour = ! isEnabled() ? Theme::textDim.withAlpha (0.4f) : (on ? Theme::accent : Theme::textBright);
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        auto box = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
        g.setColour (colour);

        if (icon == Icon::metronome)
        {
            juce::Path p;
            p.addTriangle (box.getCentreX(), box.getY(), box.getX() + s * 0.12f, box.getBottom(), box.getRight() - s * 0.12f, box.getBottom());
            g.strokePath (p, juce::PathStrokeType (1.6f));
            g.drawLine (box.getCentreX(), box.getBottom() - s * 0.2f, box.getRight() - s * 0.05f, box.getY() + s * 0.15f, 1.8f);
        }
        else
        {
            // Freccia curva: arco + punta
            const bool undo = icon == Icon::undo;
            juce::Path arc;
            const float rad = s * 0.42f;
            const auto c = box.getCentre().translated (0.0f, s * 0.08f);
            arc.addCentredArc (c.x, c.y, rad, rad, 0.0f,
                               undo ? -juce::MathConstants<float>::halfPi * 1.4f : juce::MathConstants<float>::halfPi * 1.4f,
                               undo ? juce::MathConstants<float>::halfPi * 0.9f : -juce::MathConstants<float>::halfPi * 0.9f, true);
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto start = arc.getPointAlongPath (0.0f);
            const float dir = undo ? -1.0f : 1.0f;
            juce::Path head;
            head.addTriangle (start.x + dir * s * 0.02f - dir * s * 0.24f, start.y,
                              start.x + dir * s * 0.16f, start.y - s * 0.17f,
                              start.x + dir * s * 0.16f, start.y + s * 0.17f);
            g.fillPath (head);
        }
    }

private:
    Icon icon;
};
