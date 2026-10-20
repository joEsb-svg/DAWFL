#pragma once

#include <JuceHeader.h>
#include "DrumForge.h"
#include "../Common/ForgeEditorBase.h"

//==============================================================================
/**
    Interfaccia di DrumForge:
    - PAD: i 16 pad (tocca per suonare e selezionare) e le impostazioni del pad selezionato
    - KIT: i kit di fabbrica, la saturazione e il riverbero
*/
class DrumForgeEditor : public ForgeEditorBase
{
public:
    explicit DrumForgeEditor (DrumForgeAudioProcessor&);
    ~DrumForgeEditor() override;

    void selectPad (int pad);
    int getSelectedPad() const      { return selected; }

private:
    class PadGrid;
    class KitList;
    class SampleButton;

    void refreshViews() override;
    void showSampleMenu (int pad, juce::Component& target);
    void importSample (int pad);

    DrumForgeAudioProcessor& proc;
    PadGrid* grid = nullptr;
    KitList* kits = nullptr;
    std::array<Section*, df::numPads> padSections {};
    std::array<SampleButton*, df::numPads> sampleButtons {};
    std::array<juce::String, df::numPads> shownNames;
    int selected = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
