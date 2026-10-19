#pragma once

#include <JuceHeader.h>
#include "../Engine/SequencerEngine.h"

//==============================================================================
/**
    Una riga del channel rack:
    [LED mute] [nome (tocca per sentire)] [manopola volume] [16 pad]

    I pad si possono "dipingere": tocca un pad e trascina il dito sugli altri.
*/
class ChannelRow : public juce::Component
{
public:
    ChannelRow (SequencerEngine& engine, int channelIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    /** Chiamato dal timer della schermata: aggiorna playhead e "lampeggio". */
    void refresh (int currentStep);

    /** Posizione del pad i (coordinate locali della riga). */
    juce::Rectangle<float> getPadBounds (int index) const;

private:
    juce::Rectangle<float> getPadsArea() const;
    int padIndexAt (juce::Point<float>) const;

    SequencerEngine& engine;
    const int channel;
    const juce::Colour colour;

    juce::Slider volumeKnob;
    juce::Rectangle<float> muteArea, nameArea;

    int  current = -1;
    float flash = 0.0f;
    bool paintValue = true;
    int  lastPaintedPad = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
};
