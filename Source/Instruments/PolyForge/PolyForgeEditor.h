#pragma once

#include <JuceHeader.h>
#include "PolyForge.h"
#include "../Common/ForgeEditorBase.h"

//==============================================================================
/** Interfaccia di PolyForge: schede OSC / FILTRO / ENV / LFO / FX (vedi ForgeEditorBase). */
class PolyForgeEditor : public ForgeEditorBase
{
public:
    explicit PolyForgeEditor (PolyForgeAudioProcessor&);
};
