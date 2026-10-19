#include "ChannelRow.h"
#include "Theme.h"

ChannelRow::ChannelRow (SequencerEngine& e, int channelIndex)
    : engine (e),
      channel (channelIndex),
      colour (juce::Colour (SequencerEngine::getChannelInfo (channelIndex).colour))
{
    volumeKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    volumeKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeKnob.setRange (0.0, 1.0);
    volumeKnob.setValue (engine.getChannel (channel).volume.load(), juce::dontSendNotification);
    volumeKnob.setDoubleClickReturnValue (true, 0.8);
    volumeKnob.setMouseDragSensitivity (160);
    volumeKnob.setColour (juce::Slider::rotarySliderFillColourId, Theme::text.darker (0.25f));
    volumeKnob.onValueChange = [this] { engine.getChannel (channel).volume.store ((float) volumeKnob.getValue()); };
    addAndMakeVisible (volumeKnob);
}

//==============================================================================
float ChannelRow::getHeaderWidth (float rowWidth)
{
    return juce::jlimit (170.0f, 240.0f, rowWidth * 0.25f);
}

void ChannelRow::resized()
{
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto left = r.removeFromLeft (getHeaderWidth (r.getWidth()));

    muteArea = left.removeFromLeft (juce::jmin (26.0f, h * 0.7f));
    volumeKnob.setBounds (left.removeFromLeft (h).reduced (1.0f).toNearestInt());
    left.removeFromLeft (4.0f);
    nameArea  = left.reduced (0.0f, 1.0f);
    soundArea = nameArea.withLeft (nameArea.getRight() - juce::jmin (30.0f, nameArea.getWidth() * 0.28f));
}

juce::Rectangle<float> ChannelRow::getPadsArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (getHeaderWidth (r.getWidth()) + 8.0f);
    return r.reduced (0.0f, 1.0f);
}

juce::Rectangle<float> ChannelRow::getPadBounds (int column) const
{
    auto area = getPadsArea();
    const float groupGap = 6.0f;
    const float padGap   = 2.0f;
    const int numGroups  = visibleSteps / 4;

    const float padWidth = (area.getWidth() - groupGap * (float) (numGroups - 1)
                                            - padGap * (float) (visibleSteps - numGroups)) / (float) visibleSteps;
    const int group = column / 4;
    const float x = area.getX() + (float) column * padWidth + (float) group * groupGap + (float) (column - group) * padGap;

    return { x, area.getY(), padWidth, area.getHeight() };
}

int ChannelRow::columnAt (juce::Point<float> p) const
{
    for (int i = 0; i < visibleSteps; ++i)
        if (getPadBounds (i).expanded (1.5f, 4.0f).contains (p))
            return i;

    return -1;
}

void ChannelRow::setView (int firstStepToShow, bool velocityModeOn)
{
    if (firstStepToShow != firstStep || velocityModeOn != velocityMode)
    {
        firstStep = firstStepToShow;
        velocityMode = velocityModeOn;
        repaint();
    }
}

//==============================================================================
void ChannelRow::paint (juce::Graphics& g)
{
    auto& ch = engine.getChannel (channel);
    const bool muted = ch.muted.load();
    const int length = engine.getPatternLength();

    // LED di mute (verde = canale attivo)
    {
        const float d = juce::jmin (muteArea.getWidth(), muteArea.getHeight()) * 0.42f;
        auto led = juce::Rectangle<float> (d, d).withCentre (muteArea.getCentre());

        if (! muted)
        {
            g.setColour (Theme::led.withAlpha (0.18f));
            g.fillEllipse (led.expanded (d * 0.35f));
        }

        g.setColour (muted ? Theme::raised : Theme::led);
        g.fillEllipse (led);
        g.setColour (Theme::border);
        g.drawEllipse (led, 1.0f);
    }

    // Pulsante del canale: nome + suono scelto; si illumina quando lo strumento suona
    {
        auto r = nameArea;
        Theme::drawRaised (g, r, Theme::raised.interpolatedWith (Theme::raisedHi, flash));

        // Filo colorato a sinistra: unico tocco di colore del canale
        g.setColour (colour.withAlpha (muted ? 0.3f : 0.9f));
        g.fillRect (r.withWidth (3.0f).reduced (0.0f, 3.0f).translated (3.0f, 0.0f));

        auto textArea = r.withTrimmedLeft (11.0f).withRight (soundArea.getX());
        const float nameH = juce::jlimit (12.0f, 15.0f, r.getHeight() * 0.36f);
        auto nameLine = textArea.removeFromTop (textArea.getHeight() * 0.55f);

        g.setColour (muted ? Theme::textDim : Theme::textBright);
        g.setFont (Theme::font (nameH, true));
        g.drawFittedText (SequencerEngine::getChannelInfo (channel).name, nameLine.toNearestInt(),
                          juce::Justification::bottomLeft, 1);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (juce::jmax (10.5f, nameH * 0.8f)));
        g.drawFittedText (SoundLibrary::getKitName (engine.getChannelKit (channel)), textArea.toNearestInt(),
                          juce::Justification::topLeft, 1);

        // Separatore e freccia della libreria suoni
        g.setColour (Theme::border.withAlpha (0.8f));
        g.drawVerticalLine ((int) soundArea.getX(), soundArea.getY() + 5.0f, soundArea.getBottom() - 5.0f);

        const float a = juce::jmin (soundArea.getWidth(), soundArea.getHeight()) * 0.16f;
        auto c = soundArea.getCentre();
        juce::Path tri;
        tri.addTriangle (c.x - a, c.y - a * 0.5f, c.x + a, c.y - a * 0.5f, c.x, c.y + a * 0.7f);
        g.setColour (Theme::textDim);
        g.fillPath (tri);
    }

    // Step
    for (int col = 0; col < visibleSteps; ++col)
    {
        const int step = firstStep + col;
        auto pad = getPadBounds (col);
        const bool outside = step >= length;                       // oltre la lunghezza del pattern
        const int vel = outside ? 0 : engine.getStepVelocity (channel, step);
        const bool isCurrent = step == current;
        auto off = (col / 4) % 2 == 0 ? Theme::stepOffA : Theme::stepOffB;

        if (outside)
        {
            g.setColour (Theme::panel);
            g.fillRoundedRectangle (pad, 2.0f);
            continue;
        }

        if (isCurrent)
            off = off.brighter (0.25f);

        if (velocityMode)
        {
            // Sfondo dello step + barra verticale proporzionale alla velocity
            Theme::drawSunken (g, pad, off.darker (0.35f), 2.0f);

            if (vel > 0)
            {
                auto bar = pad.reduced (3.0f);
                bar = bar.removeFromBottom (bar.getHeight() * (float) vel / 127.0f);
                g.setColour ((isCurrent ? Theme::stepOnHi : Theme::stepOn).withAlpha (muted ? 0.4f : 1.0f));
                g.fillRect (bar);
                g.setColour (Theme::accent);
                g.fillRect (bar.removeFromTop (2.0f));
            }
            continue;
        }

        if (vel > 0)
        {
            // Acceso: piu' chiaro quanto piu' e' forte
            auto on = off.interpolatedWith (isCurrent ? Theme::stepOnHi : Theme::stepOn, 0.45f + 0.55f * (float) vel / 127.0f);
            if (muted)
                on = on.interpolatedWith (off, 0.6f);

            Theme::drawRaised (g, pad, on, 2.0f);
        }
        else
        {
            Theme::drawRaised (g, pad, off, 2.0f);
        }
    }
}

//==============================================================================
void ChannelRow::mouseDown (const juce::MouseEvent& e)
{
    lastColumn = -1;

    if (muteArea.expanded (2.0f).contains (e.position))
    {
        auto& m = engine.getChannel (channel).muted;
        m.store (! m.load());
        repaint();
        return;
    }

    if (soundArea.contains (e.position))
    {
        showSoundMenu();
        return;
    }

    if (nameArea.contains (e.position))
    {
        engine.previewChannel (channel);
        return;
    }

    const int col = columnAt (e.position);
    if (col < 0 || firstStep + col >= engine.getPatternLength())
        return;

    lastColumn = col;

    if (velocityMode)
    {
        applyVelocityAt (e.position);
        return;
    }

    const int step = firstStep + col;
    paintValue = ! engine.isStepOn (channel, step);
    engine.setStepVelocity (channel, step, paintValue ? SequencerEngine::defaultVelocity : 0);
    repaint();
}

void ChannelRow::mouseDrag (const juce::MouseEvent& e)
{
    if (lastColumn < 0)
        return;

    if (velocityMode)
    {
        applyVelocityAt (e.position);
        return;
    }

    const int col = columnAt ({ e.position.x, getPadsArea().getCentreY() });
    if (col >= 0 && col != lastColumn && firstStep + col < engine.getPatternLength())
    {
        engine.setStepVelocity (channel, firstStep + col, paintValue ? SequencerEngine::defaultVelocity : 0);
        lastColumn = col;
        repaint();
    }
}

void ChannelRow::applyVelocityAt (juce::Point<float> p)
{
    // In modalita' VEL il dito puo' scorrere sugli step: conta la colonna sotto il dito
    // e l'altezza del dito dentro la riga.
    const int col = columnAt ({ p.x, getPadsArea().getCentreY() });
    if (col < 0 || firstStep + col >= engine.getPatternLength())
        return;

    auto pad = getPadBounds (col).reduced (3.0f);
    const float norm = 1.0f - (p.y - pad.getY()) / juce::jmax (1.0f, pad.getHeight());
    const int vel = juce::jlimit (1, 127, juce::roundToInt (norm * 127.0f));

    engine.setStepVelocity (channel, firstStep + col, vel);
    lastColumn = col;
    repaint();
}

//==============================================================================
void ChannelRow::refresh (int currentStep)
{
    auto& activity = engine.getChannel (channel).activity;
    const float a = activity.load();
    bool needsRepaint = false;

    if (a > 0.0f)
    {
        flash = juce::jmax (flash, a);
        activity.store (0.0f);
    }

    if (flash > 0.01f)
    {
        flash *= 0.8f;
        needsRepaint = true;
    }
    else if (flash > 0.0f)
    {
        flash = 0.0f;
        needsRepaint = true;
    }

    // Cambiamenti fatti altrove (kit globale, cambio pattern, caricamento progetto)
    if (const int kit = engine.getChannelKit (channel); kit != shownKit)
    {
        shownKit = kit;
        needsRepaint = true;
    }

    if (const int pat = engine.getCurrentPattern(); pat != shownPattern)
    {
        shownPattern = pat;
        needsRepaint = true;
    }

    if (std::abs (volumeKnob.getValue() - engine.getChannel (channel).volume.load()) > 0.0001
         && ! volumeKnob.isMouseButtonDown())
        volumeKnob.setValue (engine.getChannel (channel).volume.load(), juce::dontSendNotification);

    if (currentStep != current)
    {
        current = currentStep;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

//==============================================================================
void ChannelRow::showSoundMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (SequencerEngine::getChannelInfo (channel).name) + "  -  libreria suoni");

    const int selectedKit = engine.getChannelKit (channel);
    for (int k = 0; k < SoundLibrary::numKits; ++k)
        menu.addItem (k + 1, SoundLibrary::getKitName (k), true, k == selectedKit);

    juce::Component::SafePointer<ChannelRow> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (nameArea.toNearestInt())),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            safeThis->engine.setChannelKit (safeThis->channel, result - 1);
                            safeThis->engine.previewChannel (safeThis->channel);
                            safeThis->repaint();
                        });
}
