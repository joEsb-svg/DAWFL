#pragma once

#include <JuceHeader.h>
#include "PolyForge.h"
#include "UI/KnobKit.h"
#include "UI/CustomLookAndFeel.h"

//==============================================================================
/**
    Interfaccia di PolyForge, pensata per il telefono (stesso stile della famiglia "Forge"):
    schede OSC / FILTRO / ENV / LFO / FX, pannelli con manopole grandi, disegno degli inviluppi.
    Si adatta alla dimensione che le da' l'host (orizzontale: pannelli affiancati; verticale: impilati).
*/
class PolyForgeEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit PolyForgeEditor (PolyForgeAudioProcessor&);
    ~PolyForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Mostra una scheda (0..4). */
    void showTab (int index);
    int getTab() const          { return tab; }

private:
    struct Section
    {
        std::unique_ptr<bf::ui::Panel> panel;
        std::vector<juce::Component*> items;
        juce::Component* view = nullptr;     // disegno (inviluppo), sopra o a sinistra delle manopole
        float weight = 1.0f;
    };

    class AdsrView;

    Section& addSection (int tabIndex, const juce::String& title, float weight = 1.0f);
    void knob (Section&, const char* id, const juce::String& title);
    void combo (Section&, const char* id, const juce::String& title);
    void toggle (Section&, const char* id, const juce::String& title);
    void timerCallback() override;

    PolyForgeAudioProcessor& proc;
    bf::ui::CustomLookAndFeel laf;
    std::array<juce::TextButton, 5> tabs;
    std::array<std::vector<std::unique_ptr<Section>>, 5> sections;
    std::vector<std::unique_ptr<juce::Component>> owned;
    int tab = 0;
    int headerHeight = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyForgeEditor)
};
