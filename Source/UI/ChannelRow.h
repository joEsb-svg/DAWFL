#pragma once

#include <JuceHeader.h>
#include "../Engine/SequencerEngine.h"

//==============================================================================
/**
    Una riga del channel rack:
    [LED mute] [volume] [nome + suono ▾] [16 step]

    Modalita' STEP: tocca uno step per accenderlo/spegnerlo, trascina per "dipingerne" tanti.
    Modalita' VEL:  tocca/trascina in verticale su uno step per regolarne la forza (velocity).
*/
class ChannelRow : public juce::Component
{
public:
    ChannelRow (SequencerEngine& engine, int channelIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    /** Quale pagina di 16 step mostrare e in che modalita' di modifica. */
    void setView (int firstStepToShow, bool velocityModeOn);

    /** Chiamato dal timer della schermata: aggiorna playhead, lampeggio, cambi esterni. */
    void refresh (int currentStep);

    /** Posizione della colonna visibile i (0..15), in coordinate locali della riga. */
    juce::Rectangle<float> getPadBounds (int column) const;

    /** Larghezza della parte sinistra (LED, volume, nome), uguale per tutte le righe. */
    static float getHeaderWidth (float rowWidth);

    static constexpr int visibleSteps = 16;

private:
    juce::Rectangle<float> getPadsArea() const;
    int columnAt (juce::Point<float>) const;
    void applyVelocityAt (juce::Point<float>);
    void showSoundMenu();

    SequencerEngine& engine;
    const int channel;
    const juce::Colour colour;

    juce::Slider volumeKnob;
    juce::Rectangle<float> muteArea, nameArea, soundArea;

    int   firstStep = 0;
    bool  velocityMode = false;
    int   current = -1;
    float flash = 0.0f;
    bool  paintValue = true;
    int   lastColumn = -1;
    int   shownKit = -1;
    int   shownPattern = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
};
