#pragma once
#include "ParameterIDs.h"

namespace bf
{
    // Costruisce l'intero albero di parametri (chiamare solo dal message thread / costruttore).
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
