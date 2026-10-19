#pragma once

#include <JuceHeader.h>
#include "FMForge.h"
#include "../Common/ForgeEditorBase.h"

//==============================================================================
/**
    Interfaccia di FMForge: ALGO (disegno dell'algoritmo, timbro, tono, voce),
    OP 1..4 (rapporto, livello, velocity, inviluppo), LFO/FX.
*/
class FMForgeEditor : public ForgeEditorBase
{
public:
    explicit FMForgeEditor (FMForgeAudioProcessor&);

private:
    class AlgorithmView;
    void refreshViews() override;
    AlgorithmView* algoView = nullptr;
};
