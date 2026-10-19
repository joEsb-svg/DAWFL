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
    volumeKnob.setColour (juce::Slider::rotarySliderFillColourId, colour);
    volumeKnob.onValueChange = [this] { engine.getChannel (channel).volume.store ((float) volumeKnob.getValue()); };
    addAndMakeVisible (volumeKnob);
}

//==============================================================================
void ChannelRow::resized()
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const float h = r.getHeight();
    const float leftWidth = juce::jlimit (150.0f, 230.0f, r.getWidth() * 0.24f);
    auto left = r.removeFromLeft (leftWidth);

    muteArea = left.removeFromLeft (juce::jmin (32.0f, h * 0.8f));
    left.removeFromLeft (4.0f);
    volumeKnob.setBounds (left.removeFromRight (h).toNearestInt());
    left.removeFromRight (4.0f);
    nameArea  = left;
    soundArea = left.removeFromRight (juce::jmin (34.0f, left.getWidth() * 0.3f));
}

juce::Rectangle<float> ChannelRow::getPadsArea() const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const float leftWidth = juce::jlimit (150.0f, 230.0f, r.getWidth() * 0.24f);
    r.removeFromLeft (leftWidth + 8.0f);
    return r;
}

juce::Rectangle<float> ChannelRow::getPadBounds (int index) const
{
    auto area = getPadsArea();
    const float groupGap = 7.0f;
    const float padGap   = 3.0f;
    const int numSteps   = SequencerEngine::numSteps;
    const int numGroups  = numSteps / 4;

    const float padWidth = (area.getWidth() - groupGap * (float) (numGroups - 1)
                                            - padGap * (float) (numSteps - numGroups)) / (float) numSteps;
    const int group = index / 4;
    const float x = area.getX() + (float) index * padWidth + (float) group * groupGap + (float) (index - group) * padGap;

    return { x, area.getY(), padWidth, area.getHeight() };
}

int ChannelRow::padIndexAt (juce::Point<float> p) const
{
    for (int i = 0; i < SequencerEngine::numSteps; ++i)
        if (getPadBounds (i).expanded (2.0f, 0.0f).contains (p))
            return i;

    return -1;
}

//==============================================================================
void ChannelRow::paint (juce::Graphics& g)
{
    auto& ch = engine.getChannel (channel);
    const bool muted = ch.muted.load();

    // Sfondo della riga
    auto bg = getLocalBounds().toFloat();
    g.setColour (Theme::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (bg, 8.0f);

    // LED di mute (verde = canale attivo, come su FL)
    {
        const float d = juce::jmin (muteArea.getWidth(), muteArea.getHeight()) * 0.55f;
        auto led = juce::Rectangle<float> (d, d).withCentre (muteArea.getCentre());

        if (! muted)
        {
            g.setColour (Theme::lcdText.withAlpha (0.25f));
            g.fillEllipse (led.expanded (d * 0.25f));
        }

        g.setColour (muted ? Theme::bgBottom : Theme::lcdText);
        g.fillEllipse (led);
        g.setColour (Theme::outline);
        g.drawEllipse (led, 1.0f);
    }

    // Tasto con il nome: si illumina quando lo strumento suona
    {
        auto r = nameArea;
        const float corner = 6.0f;
        auto base = Theme::panelHi.interpolatedWith (colour, 0.12f + 0.45f * flash);

        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), r.getTopLeft(),
                                                 base.darker (0.2f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, corner);

        // Striscia colorata a sinistra
        g.setColour (colour.withAlpha (muted ? 0.35f : 1.0f));
        g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);

        // Nome dello strumento + suono scelto dalla libreria
        auto textArea = r.withTrimmedLeft (10.0f).withRight (soundArea.getX());
        const float nameH = juce::jlimit (12.0f, 17.0f, r.getHeight() * 0.34f);
        auto nameLine = textArea.removeFromTop (textArea.getHeight() * 0.56f);

        g.setColour (muted ? Theme::textDim : Theme::text);
        g.setFont (Theme::font (nameH, true));
        g.drawFittedText (SequencerEngine::getChannelInfo (channel).name,
                          nameLine.toNearestInt(), juce::Justification::bottomLeft, 1);

        g.setColour (colour.withAlpha (muted ? 0.4f : 0.9f).brighter (0.3f));
        g.setFont (Theme::font (juce::jmax (11.0f, nameH * 0.8f)));
        g.drawFittedText (SoundLibrary::getKitName (engine.getChannelKit (channel)),
                          textArea.toNearestInt(), juce::Justification::topLeft, 1);

        // Freccia: apre la libreria dei suoni
        auto arrow = soundArea.reduced (soundArea.getWidth() * 0.3f, soundArea.getHeight() * 0.38f);
        juce::Path tri;
        tri.addTriangle (arrow.getX(), arrow.getY(), arrow.getRight(), arrow.getY(), arrow.getCentreX(), arrow.getBottom());
        g.setColour (Theme::bgBottom.withAlpha (0.35f));
        g.fillRoundedRectangle (soundArea.reduced (3.0f), 5.0f);
        g.setColour (Theme::text.withAlpha (0.8f));
        g.fillPath (tri);
    }

    // Pad
    for (int i = 0; i < SequencerEngine::numSteps; ++i)
    {
        auto pad = getPadBounds (i);
        const bool on = engine.isStepOn (channel, i);
        const bool isCurrent = i == current;
        const float corner = juce::jmin (6.0f, pad.getWidth() * 0.2f);
        auto c = muted ? colour.withSaturation (0.15f).darker (0.3f) : colour;

        if (on)
        {
            // Alone luminoso + gradiente
            g.setColour (c.withAlpha (isCurrent ? 0.55f : 0.28f));
            g.fillRoundedRectangle (pad.expanded (isCurrent ? 3.0f : 1.5f), corner + 2.0f);

            auto top = isCurrent ? c.brighter (0.8f) : c.brighter (0.3f);
            g.setGradientFill (juce::ColourGradient (top, pad.getTopLeft(), c.darker (0.25f), pad.getBottomLeft(), false));
            g.fillRoundedRectangle (pad, corner);

            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.fillRoundedRectangle (pad.reduced (3.0f).withHeight (juce::jmax (2.0f, pad.getHeight() * 0.12f)), 2.0f);
        }
        else
        {
            auto off = (i / 4) % 2 == 0 ? Theme::padOffA : Theme::padOffB;
            if (isCurrent)
                off = off.brighter (0.45f);

            g.setGradientFill (juce::ColourGradient (off.brighter (0.08f), pad.getTopLeft(), off.darker (0.15f), pad.getBottomLeft(), false));
            g.fillRoundedRectangle (pad, corner);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawRoundedRectangle (pad.reduced (0.5f), corner, 1.0f);
        }
    }
}

//==============================================================================
void ChannelRow::mouseDown (const juce::MouseEvent& e)
{
    lastPaintedPad = -1;

    if (muteArea.expanded (4.0f).contains (e.position))
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

    const int pad = padIndexAt (e.position);
    if (pad >= 0)
    {
        paintValue = ! engine.isStepOn (channel, pad);
        engine.setStep (channel, pad, paintValue);
        lastPaintedPad = pad;
        repaint();
    }
}

void ChannelRow::mouseDrag (const juce::MouseEvent& e)
{
    if (lastPaintedPad < 0)
        return;

    const int pad = padIndexAt (e.position);
    if (pad >= 0 && pad != lastPaintedPad)
    {
        engine.setStep (channel, pad, paintValue);
        lastPaintedPad = pad;
        repaint();
    }
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
        flash *= 0.82f;
        needsRepaint = true;
    }
    else if (flash > 0.0f)
    {
        flash = 0.0f;
        needsRepaint = true;
    }

    if (const int kit = engine.getChannelKit (channel); kit != shownKit)
    {
        shownKit = kit;
        needsRepaint = true;
    }

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
    menu.addSectionHeader (juce::String (SequencerEngine::getChannelInfo (channel).name) + " - libreria suoni");

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
