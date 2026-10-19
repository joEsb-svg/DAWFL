#include "ChannelRow.h"
#include "Theme.h"

ChannelRow::RowClipboard ChannelRow::clipboard;

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
    nameArea = left.reduced (0.0f, 1.0f);
    menuArea = nameArea.withLeft (nameArea.getRight() - juce::jmin (30.0f, nameArea.getWidth() * 0.28f));
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

void ChannelRow::setView (int firstStepToShow, EditMode newMode)
{
    if (firstStepToShow != firstStep || newMode != mode)
    {
        firstStep = firstStepToShow;
        mode = newMode;
        repaint();
    }
}

//==============================================================================
void ChannelRow::paint (juce::Graphics& g)
{
    auto& ch = engine.getChannel (channel);
    const bool muted = ch.muted.load();
    const int length = engine.getPatternLength();
    const bool showPlayhead = engine.getPlayingPattern() == engine.getCurrentPattern();

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

        g.setColour (colour.withAlpha (muted ? 0.3f : 0.9f));
        g.fillRect (r.withWidth (3.0f).reduced (0.0f, 3.0f).translated (3.0f, 0.0f));

        auto textArea = r.withTrimmedLeft (11.0f).withRight (menuArea.getX());
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

        g.setColour (Theme::border.withAlpha (0.8f));
        g.drawVerticalLine ((int) menuArea.getX(), menuArea.getY() + 5.0f, menuArea.getBottom() - 5.0f);

        const float a = juce::jmin (menuArea.getWidth(), menuArea.getHeight()) * 0.16f;
        auto c = menuArea.getCentre();
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

        if (step >= length)   // oltre la lunghezza del pattern
        {
            g.setColour (Theme::panel);
            g.fillRoundedRectangle (pad, 2.0f);
            continue;
        }

        const int vel  = engine.getStepVelocity (channel, step);
        const bool isCurrent = showPlayhead && step == current;
        auto off = (col / 4) % 2 == 0 ? Theme::stepOffA : Theme::stepOffB;
        if (isCurrent)
            off = off.brighter (0.25f);

        const auto onColour = (isCurrent ? Theme::stepOnHi : Theme::stepOn).withMultipliedAlpha (muted ? 0.45f : 1.0f);

        if (mode == EditMode::velocity || mode == EditMode::probability)
        {
            // Barra verticale: velocity (grigio) o probabilita' (con accento)
            Theme::drawSunken (g, pad, off.darker (0.35f), 2.0f);

            if (vel > 0)
            {
                const float amount = mode == EditMode::velocity ? (float) vel / 127.0f
                                                                : (float) engine.getStepProbability (channel, step) / 100.0f;
                auto bar = pad.reduced (3.0f);
                bar = bar.removeFromBottom (juce::jmax (2.0f, bar.getHeight() * amount));
                g.setColour (mode == EditMode::velocity ? onColour : onColour.interpolatedWith (Theme::accent, 0.35f));
                g.fillRect (bar);
                g.setColour (Theme::accent);
                g.fillRect (bar.removeFromTop (2.0f));
            }
            continue;
        }

        if (vel > 0)
        {
            auto on = off.interpolatedWith (onColour, 0.45f + 0.55f * (float) vel / 127.0f);
            Theme::drawRaised (g, pad, on, 2.0f);
        }
        else
        {
            Theme::drawRaised (g, pad, off, 2.0f);
        }

        if (vel > 0)
        {
            // Segni sullo step: tacche per il ratchet, puntino per la probabilita' < 100%
            const int rat = engine.getStepRatchet (channel, step);
            const bool showRatchet = rat > 1 || mode == EditMode::ratchet;

            if (showRatchet)
            {
                auto inner = pad.reduced (pad.getWidth() * 0.22f, pad.getHeight() * 0.3f);
                const float segH = inner.getHeight() / (float) SequencerEngine::maxRatchet;

                for (int i = 0; i < rat; ++i)
                {
                    g.setColour (Theme::bg.withAlpha (0.75f));
                    g.fillRect (inner.withY (inner.getBottom() - segH * (float) (i + 1)).withHeight (segH - 2.0f));
                }
            }

            if (engine.getStepProbability (channel, step) < 100)
            {
                g.setColour (Theme::accent);
                g.fillEllipse (pad.getRight() - 7.0f, pad.getY() + 3.0f, 4.0f, 4.0f);
            }
        }
    }
}

//==============================================================================
void ChannelRow::mouseDown (const juce::MouseEvent& e)
{
    lastColumn = -1;
    pressOnName = false;

    if (muteArea.expanded (2.0f).contains (e.position))
    {
        auto& m = engine.getChannel (channel).muted;
        m.store (! m.load());
        if (onTap) onTap();
        repaint();
        return;
    }

    if (menuArea.contains (e.position))
    {
        showRowMenu();
        return;
    }

    if (nameArea.contains (e.position))
    {
        engine.previewChannel (channel);
        pressOnName = true;   // se resta premuto a lungo, al rilascio apre l'editor
        return;
    }

    const int col = columnAt (e.position);
    if (col < 0 || firstStep + col >= engine.getPatternLength())
        return;

    const int step = firstStep + col;
    lastColumn = col;

    switch (mode)
    {
        case EditMode::velocity:
        case EditMode::probability:
            applyVerticalValueAt (e.position);
            return;

        case EditMode::ratchet:
            if (engine.isStepOn (channel, step))
                engine.setStepRatchet (channel, step, engine.getStepRatchet (channel, step) % SequencerEngine::maxRatchet + 1);
            break;

        case EditMode::step:
            paintValue = ! engine.isStepOn (channel, step);
            if (paintValue)
            {
                engine.setStepVelocity (channel, step, SequencerEngine::defaultVelocity);
                engine.setStepProbability (channel, step, 100);
                engine.setStepRatchet (channel, step, 1);
            }
            else
            {
                engine.setStepVelocity (channel, step, 0);
            }
            break;
    }

    if (onTap) onTap();
    repaint();
}

void ChannelRow::mouseDrag (const juce::MouseEvent& e)
{
    if (lastColumn < 0)
        return;

    if (mode == EditMode::velocity || mode == EditMode::probability)
    {
        applyVerticalValueAt (e.position);
        return;
    }

    if (mode != EditMode::step)
        return;

    const int col = columnAt ({ e.position.x, getPadsArea().getCentreY() });
    if (col >= 0 && col != lastColumn && firstStep + col < engine.getPatternLength())
    {
        const int step = firstStep + col;
        engine.setStepVelocity (channel, step, paintValue ? SequencerEngine::defaultVelocity : 0);
        if (paintValue)
        {
            engine.setStepProbability (channel, step, 100);
            engine.setStepRatchet (channel, step, 1);
        }
        lastColumn = col;
        if (onTap) onTap();
        repaint();
    }
}

void ChannelRow::mouseUp (const juce::MouseEvent& e)
{
    if (pressOnName && e.getLengthOfMousePress() > 450 && onOpenEditor)
        onOpenEditor (channel);

    pressOnName = false;
}

void ChannelRow::applyVerticalValueAt (juce::Point<float> p)
{
    // Il dito puo' scorrere sugli step: conta la colonna sotto il dito e la sua altezza nella riga.
    const int col = columnAt ({ p.x, getPadsArea().getCentreY() });
    if (col < 0 || firstStep + col >= engine.getPatternLength())
        return;

    const int step = firstStep + col;
    auto pad = getPadBounds (col).reduced (3.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - pad.getY()) / juce::jmax (1.0f, pad.getHeight()));

    if (mode == EditMode::velocity)
    {
        if (! engine.isStepOn (channel, step))
        {
            engine.setStepProbability (channel, step, 100);
            engine.setStepRatchet (channel, step, 1);
        }
        engine.setStepVelocity (channel, step, juce::jlimit (1, 127, juce::roundToInt (norm * 127.0f)));
    }
    else if (engine.isStepOn (channel, step))
    {
        // Probabilita' a scatti del 5%, minimo 5%
        engine.setStepProbability (channel, step, juce::jlimit (5, 100, juce::roundToInt (norm * 20.0f) * 5));
    }

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

    // Cambiamenti fatti altrove (kit globale, cambio pattern, caricamento progetto, undo)
    if (const int kit = engine.getChannelKit (channel); kit != shownKit)
    {
        shownKit = kit;
        needsRepaint = true;
    }

    if (const int pat = engine.getCurrentPattern() * 16 + engine.getPlayingPattern(); pat != shownPattern)
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
void ChannelRow::showRowMenu()
{
    juce::PopupMenu library, fill;

    const int selectedKit = engine.getChannelKit (channel);
    for (int k = 0; k < SoundLibrary::numKits; ++k)
        library.addItem (100 + k, SoundLibrary::getKitName (k), true, k == selectedKit);

    fill.addItem (201, "Ogni step");
    fill.addItem (202, "Ogni 2 step (ottavi)");
    fill.addItem (204, "Ogni 4 step (quarti)");
    fill.addItem (208, "Ogni 8 step");
    fill.addItem (210, "Controtempo (sugli ottavi pari)");

    juce::PopupMenu menu;
    menu.addSectionHeader (SequencerEngine::getChannelInfo (channel).name);
    menu.addItem (1, "Modifica suono...");
    menu.addSubMenu ("Libreria suoni", library);
    menu.addSeparator();
    menu.addItem (2, juce::String (juce::CharPointer_UTF8 ("Sposta a sinistra  \xe2\x97\x80")));
    menu.addItem (3, juce::String (juce::CharPointer_UTF8 ("Sposta a destra  \xe2\x96\xb6")));
    menu.addItem (4, "Inverti");
    menu.addSubMenu ("Riempi", fill);
    menu.addSeparator();
    menu.addItem (5, "Copia riga");
    menu.addItem (6, "Incolla riga", clipboard.valid);
    menu.addItem (7, "Svuota riga");

    juce::Component::SafePointer<ChannelRow> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (nameArea.toNearestInt())),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            auto& self = *safeThis;
                            auto& e = self.engine;
                            const int c = self.channel;

                            if (result == 1)        { if (self.onOpenEditor) self.onOpenEditor (c); }
                            else if (result == 2)   e.shiftRow (c, -1);
                            else if (result == 3)   e.shiftRow (c, 1);
                            else if (result == 4)   e.reverseRow (c);
                            else if (result == 5)
                            {
                                for (int s = 0; s < SequencerEngine::maxSteps; ++s)
                                {
                                    clipboard.velocity[s]    = (uint8_t) e.getStepVelocity (c, s);
                                    clipboard.probability[s] = (uint8_t) e.getStepProbability (c, s);
                                    clipboard.ratchet[s]     = (uint8_t) e.getStepRatchet (c, s);
                                }
                                clipboard.valid = true;
                            }
                            else if (result == 6)
                            {
                                for (int s = 0; s < SequencerEngine::maxSteps; ++s)
                                {
                                    e.setStepVelocity (c, s, clipboard.velocity[s]);
                                    e.setStepProbability (c, s, clipboard.probability[s]);
                                    e.setStepRatchet (c, s, clipboard.ratchet[s]);
                                }
                            }
                            else if (result == 7)   e.clearRow (c);
                            else if (result == 210) e.fillRow (c, 4, 2);
                            else if (result > 200)  e.fillRow (c, result - 200);
                            else if (result >= 100)
                            {
                                e.setChannelKit (c, result - 100);
                                e.previewChannel (c);
                            }

                            self.repaint();
                        });
}
