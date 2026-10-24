#pragma once

#include <JuceHeader.h>
#include "FrenchRoulette.h"

/** Interfaccia di French Roulette (provvisoria: parametri generici). */
class FrenchRouletteEditor : public juce::GenericAudioProcessorEditor
{
public:
    explicit FrenchRouletteEditor (FrenchRouletteAudioProcessor& p) : juce::GenericAudioProcessorEditor (p) {}
};
