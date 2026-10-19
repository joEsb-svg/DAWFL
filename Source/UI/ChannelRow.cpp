#include "ChannelRow.h"
#include "Theme.h"
#include "../Instruments/Instruments.h"

ChannelRow::RowClipboard ChannelRow::clipboard;

ChannelRow::ChannelRow (SequencerEngine& e, int channelIndex)
    : engine (e),
      channel (channelIndex)
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
float ChannelRow::getHeaderWidth() const
{
    if (compact)
        return juce::jlimit (110.0f, 150.0f, (float) getWidth() * 0.33f);

    return juce::jlimit (170.0f, 240.0f, (float) getWidth() * 0.25f);
}

void ChannelRow::setLayout (int stepsToShow, bool compactHeader)
{
    if (stepsToShow != visibleSteps || compactHeader != compact)
    {
        visibleSteps = stepsToShow;
        compact = compactHeader;
        volumeKnob.setVisible (! compact);   // in verticale il volume si regola dall'editor del canale
        resized();
        repaint();
    }
}

void ChannelRow::resized()
{
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto left = r.removeFromLeft (getHeaderWidth());

    muteArea = left.removeFromLeft (juce::jmin (26.0f, h * 0.7f));
    if (! compact)
    {
        const float knob = juce::jmin (h, 46.0f);
        volumeKnob.setBounds (left.removeFromLeft (knob).withSizeKeepingCentre (knob, knob).reduced (1.0f).toNearestInt());
        left.removeFromLeft (4.0f);
    }
    nameArea = left.reduced (0.0f, 1.0f);
    menuArea = nameArea.withLeft (nameArea.getRight() - juce::jmin (30.0f, nameArea.getWidth() * 0.28f));
}

juce::Rectangle<float> ChannelRow::getPadsArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (getHeaderWidth() + 8.0f);
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

        const auto colour = getColour();
        const bool soloed = ch.solo.load();
        g.setColour (colour.withAlpha (muted ? 0.3f : 0.9f));
        g.fillRect (r.withWidth (3.0f).reduced (0.0f, 3.0f).translated (3.0f, 0.0f));

        auto textArea = r.withTrimmedLeft (11.0f).withRight (menuArea.getX());
        const float nameH = juce::jlimit (12.0f, 15.0f, r.getHeight() * 0.36f);
        auto nameLine = textArea.removeFromTop (textArea.getHeight() * 0.55f);

        g.setColour (muted ? Theme::textDim : Theme::textBright);
        g.setFont (Theme::font (nameH, true));
        g.drawFittedText (ch.name, nameLine.toNearestInt(), juce::Justification::bottomLeft, 1);

        if (soloed)   // "S" del solo, in alto a destra nel pulsante
        {
            auto badge = juce::Rectangle<float> (15.0f, 13.0f).withPosition (menuArea.getX() - 18.0f, r.getY() + 4.0f);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (Theme::bg);
            g.setFont (Theme::font (10.5f, true));
            g.drawText ("S", badge, juce::Justification::centred, false);
        }

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (juce::jmax (10.5f, nameH * 0.8f)));
        g.drawFittedText (getSoundLabel (engine, channel), textArea.toNearestInt(),
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

    // Canale con note nel piano roll: al posto degli step, un'anteprima delle note
    if (engine.hasNotes (channel))
    {
        paintNotesPreview (g, muted);
        return;
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
        auto off = (step / 4) % 2 == 0 ? Theme::stepOffA : Theme::stepOffB;
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
        // Tocco breve = ascolta; pressione lunga = menu del canale; trascinamento = scorre la lista
        pressOnName = true;
        nameDragging = false;
        longPressDone = false;
        startTimer (450);
        return;
    }

    if (engine.hasNotes (channel))
    {
        if (getPadsArea().contains (e.position) && onOpenPianoRoll)
            onOpenPianoRoll (channel);
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
    if (pressOnName)
    {
        if (! nameDragging && ! longPressDone && std::abs (e.getDistanceFromDragStartY()) > 8)
        {
            nameDragging = true;
            stopTimer();
            if (onScrollDrag) onScrollDrag (0.0f, true);
        }

        if (nameDragging && onScrollDrag)
            onScrollDrag ((float) e.getDistanceFromDragStartY(), false);
        return;
    }

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

void ChannelRow::mouseUp (const juce::MouseEvent&)
{
    stopTimer();
    const bool wasName = pressOnName, dragged = nameDragging, longPress = longPressDone;
    pressOnName = nameDragging = longPressDone = false;

    if (! wasName || dragged)
        return;

    if (longPress)
    {
        showRowMenu();   // il menu si apre al rilascio: cosi' il dito non sceglie una voce per sbaglio
        return;
    }

    engine.previewChannel (channel);
    if (onTap) onTap();
}

void ChannelRow::timerCallback()
{
    stopTimer();
    if (pressOnName && ! nameDragging)
    {
        longPressDone = true;
        flash = 1.0f;          // il pulsante si illumina: "rilascia per il menu"
        repaint();
        if (onLongPress) onLongPress();
    }
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

    // Nome, colore, solo, muto cambiati altrove (menu, undo, caricamento)
    {
        auto& ch = engine.getChannel (channel);
        const auto look = (juce::uint64) ch.name.hashCode64() * 31u + ch.colour.load() * 7u
                        + (ch.solo.load() ? 3u : 0u) + (ch.muted.load() ? 5u : 0u) + (juce::uint64) ch.sampleName.hashCode64();
        if (look != shownLook)
        {
            shownLook = look;
            needsRepaint = true;
        }
    }

    // Cambiamenti fatti altrove (kit globale, cambio pattern, caricamento progetto, undo)
    if (const int kit = engine.getChannelKit (channel); kit != shownKit)
    {
        shownKit = kit;
        needsRepaint = true;
    }

    if (engine.isSynth (channel))
        if (const auto label = getSoundLabel (engine, channel); label != shownLabel)
        {
            shownLabel = label;
            needsRepaint = true;
        }

    if (const void* smp = engine.getChannel (channel).sample.load(); smp != shownSample)
    {
        shownSample = smp;
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

    // Note cambiate (dal piano roll, undo, caricamento)
    juce::uint64 hash = (juce::uint64) engine.getCurrentPattern() + 1;
    for (auto& n : engine.getNotes (channel))
        hash = hash * 1099511628211ull + (juce::uint64) SequencerEngine::packNote (n);
    if (hash != shownNotesHash)
    {
        shownNotesHash = hash;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

//==============================================================================
void ChannelRow::showRowMenu()
{
    auto& chan = engine.getChannel (channel);
    const bool synth = engine.isSynth (channel);
    const int numChannels = engine.getNumChannels();

    juce::PopupMenu fill, steps, colours;
    fill.addItem (201, "Ogni step");
    fill.addItem (202, "Ogni 2 step (ottavi)");
    fill.addItem (204, "Ogni 4 step (quarti)");
    fill.addItem (208, "Ogni 8 step");
    fill.addItem (210, "Controtempo (sugli ottavi pari)");

    steps.addItem (2, juce::String (juce::CharPointer_UTF8 ("Sposta a sinistra  \xe2\x97\x80")));
    steps.addItem (3, juce::String (juce::CharPointer_UTF8 ("Sposta a destra  \xe2\x96\xb6")));
    steps.addItem (4, "Inverti");
    steps.addSubMenu ("Riempi", fill);
    steps.addSeparator();
    steps.addItem (5, "Copia riga");
    steps.addItem (6, "Incolla riga", clipboard.valid);
    steps.addItem (7, engine.hasNotes (channel) ? "Svuota riga (step e note)" : "Svuota riga");

    for (int i = 0; i < numColours; ++i)
        colours.addColouredItem (colourBase + i, getPaletteName (i), getPaletteColour (i), true,
                                 getPaletteColour (i).getARGB() == chan.colour.load());

    juce::PopupMenu menu;
    menu.addSectionHeader (chan.name);
    menu.addItem (changeSound, synth ? "Preset..." : "Cambia suono...");
    const auto* info = Instruments::find (chan.instrumentKind);
    menu.addItem (editSound, synth ? "Apri " + juce::String (info != nullptr ? info->name : "il synth") + "..."
                                   : juce::String ("Modifica suono e mix..."));
    menu.addItem (openPianoRoll, "Piano roll...");
    menu.addSeparator();
    menu.addItem (15, "Muto", true, chan.muted.load());
    menu.addItem (solo, "Solo (ascolta solo questo)", true, chan.solo.load());
    menu.addSeparator();
    menu.addItem (clone, "Clona canale", numChannels < SequencerEngine::maxChannels);
    menu.addItem (rename, "Rinomina...");
    menu.addSubMenu ("Colore", colours);
    menu.addItem (moveUp, juce::String (juce::CharPointer_UTF8 ("Sposta su  \xe2\x96\xb2")), channel > 0);
    menu.addItem (moveDown, juce::String (juce::CharPointer_UTF8 ("Sposta gi\xc3\xb9  \xe2\x96\xbc")), channel < numChannels - 1);
    menu.addSeparator();
    menu.addSubMenu ("Step", steps);
    menu.addSeparator();
    menu.addItem (remove, "Elimina canale", numChannels > 1);

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

                            if (result == 2)        e.shiftRow (c, -1);
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
                            else if (result == 7)   { e.clearRow (c); e.setNotes (c, {}); }
                            else if (result == 15)
                            {
                                auto& m = e.getChannel (c).muted;
                                m.store (! m.load());
                            }
                            else if (result == 210) e.fillRow (c, 4, 2);
                            else if (result > 200 && result < 300)  e.fillRow (c, result - 200);
                            else if (result >= colourBase && result < colourBase + numColours)
                                e.getChannel (c).colour.store (getPaletteColour (result - colourBase).getARGB());
                            else if (self.onCommand)
                            {
                                self.onCommand (c, result);   // gestiti dalla schermata principale
                                return;                       // (la riga potrebbe essere gia' stata ricreata)
                            }

                            self.repaint();
                        });
}

juce::Colour ChannelRow::getPaletteColour (int i)
{
    static const juce::uint32 colours[numColours] =
    {
        0xffe8833a, 0xffd2555a, 0xffd8b14a, 0xff4fb3a9, 0xff5b8fd6, 0xff9a78d0,
        0xff2fc6d6, 0xff6fbf5a, 0xffe36fae, 0xffc49360, 0xff8aa0b8, 0xffe0e0e0
    };
    return juce::Colour (colours[juce::jlimit (0, numColours - 1, i)]);
}

juce::String ChannelRow::getPaletteName (int i)
{
    static const char* names[numColours] =
    {
        "Arancione", "Rosso", "Giallo", "Verde acqua", "Blu", "Viola",
        "Ciano", "Verde", "Rosa", "Legno", "Grigio", "Bianco"
    };
    return names[juce::jlimit (0, numColours - 1, i)];
}

juce::String ChannelRow::getSoundLabel (SequencerEngine& engine, int channel)
{
    if (engine.isSynth (channel))
    {
        return Instruments::presetName (engine.getInstrument (channel));
    }

    auto& ch = engine.getChannel (channel);
    if (ch.sample.load() != nullptr)
    {
        // Libreria: il nome del suono (o la categoria, se il canale si chiama gia' come il suono)
        if (auto* entry = SampleStore::findLibraryEntry (ch.sampleName); entry != nullptr && SampleStore::isLibrary (ch.sampleName))
        {
            const auto soundName = juce::String (juce::CharPointer_UTF8 (entry->name));
            return soundName == ch.name ? juce::String (juce::CharPointer_UTF8 (entry->category)) : soundName;
        }
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaa ")) + SampleStore::displayName (ch.sampleName);
    }

    return SoundLibrary::getKitName (engine.getChannelKit (channel));
}

void ChannelRow::setPlayTick (int tick)
{
    if (tick != playTick)
    {
        playTick = tick;
        if (engine.hasNotes (channel))
            repaint (getPadsArea().toNearestInt().expanded (2));
    }
}

void ChannelRow::paintNotesPreview (juce::Graphics& g, bool muted)
{
    auto area = getPadsArea();
    Theme::drawSunken (g, area, Theme::bg, 3.0f);

    const auto notes = engine.getNotes (channel);
    const int lengthTicks = engine.getPatternLength() * SequencerEngine::ticksPerStep;
    const int firstTick = firstStep * SequencerEngine::ticksPerStep;
    const int visibleTicks = visibleSteps * SequencerEngine::ticksPerStep;

    // Linee dei quarti
    for (int t = 0; t < visibleTicks; t += 4 * SequencerEngine::ticksPerStep)
    {
        g.setColour (Theme::raised);
        g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) t / (float) visibleTicks), area.getY() + 2.0f, area.getBottom() - 2.0f);
    }

    // Gamma di altezze da mostrare (almeno un'ottava)
    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    if (hi - lo < 12) { const int mid = (lo + hi) / 2; lo = mid - 6; hi = mid + 6; }

    auto inner = area.reduced (2.0f, 4.0f);
    const float rowH = inner.getHeight() / (float) (hi - lo + 1);
    const auto noteColour = getColour().interpolatedWith (Theme::stepOn, 0.35f).withMultipliedAlpha (muted ? 0.4f : 1.0f);

    for (auto& n : notes)
    {
        if (n.start + n.length <= firstTick || n.start >= firstTick + visibleTicks)
            continue;

        const float x0 = inner.getX() + inner.getWidth() * (float) (n.start - firstTick) / (float) visibleTicks;
        const float x1 = inner.getX() + inner.getWidth() * (float) (n.start + n.length - firstTick) / (float) visibleTicks;
        const float y = inner.getBottom() - (float) (n.pitch - lo + 1) * rowH;
        g.setColour (noteColour);
        g.fillRoundedRectangle (juce::Rectangle<float> (x0, y, juce::jmax (2.0f, x1 - x0 - 1.0f), juce::jmax (2.0f, rowH - 1.0f))
                                    .getIntersection (inner), 1.0f);
    }

    // Oltre la fine del pattern
    if (firstTick + visibleTicks > lengthTicks)
    {
        const float x = inner.getX() + inner.getWidth() * (float) (lengthTicks - firstTick) / (float) visibleTicks;
        g.setColour (Theme::panel);
        g.fillRect (juce::Rectangle<float> (juce::jmax (x, area.getX()), area.getY(), area.getRight() - juce::jmax (x, area.getX()), area.getHeight()));
    }

    // Posizione di riproduzione
    if (playTick >= firstTick && playTick < firstTick + visibleTicks && engine.getPlayingPattern() == engine.getCurrentPattern())
    {
        g.setColour (Theme::accent);
        g.fillRect (inner.getX() + inner.getWidth() * (float) (playTick - firstTick) / (float) visibleTicks, area.getY(), 1.5f, area.getHeight());
    }

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (10.0f, true));
    g.drawText ("PIANO ROLL", area.reduced (6.0f, 3.0f), juce::Justification::topRight, false);
}
