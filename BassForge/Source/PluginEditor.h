#pragma once
#include "PluginProcessor.h"
#include "UI/CustomLookAndFeel.h"
#include "UI/KnobKit.h"
#include "UI/ScopeView.h"
#include "UI/Pages.h"
#include "UI/PresetBar.h"

/**
 * Editor di BassForge: header (logo, schede, oscilloscopio, master), poi 4 pagine:
 *   OSCILLATORS (3 osc + vista wavetable 3D/2D, Sub, Noise, Sample)  |  FILTERS  |  MODULATORS  |  FX.
 * Ogni controllo e' collegato all'APVTS (SliderAttachment / ButtonAttachment / ComboBoxAttachment).
 * Un timer a 30 Hz aggiorna il modello degli anelli di modulazione (range dalla matrice + valore live dalle voci).
 * Ridimensionabile con proporzioni fisse; tutti i layout sono proporzionali.
 */
class BassForgeAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassForgeAudioProcessorEditor(BassForgeAudioProcessor&);
    ~BassForgeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage(int index);

    static constexpr int kBaseW = 1200, kBaseH = 780;

    BassForgeAudioProcessor& proc;
    bf::ui::CustomLookAndFeel laf;                       // dichiarato per primo: distrutto per ultimo
    bf::ui::ModRingModel ring;

    juce::TextButton tabs[4];
    bf::ui::ScopeView scope;
    bf::ui::ParamBar masterBar;
    juce::Label masterLabel;
    juce::TextButton limiterBtn { "LIMIT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterAtt;
    bf::ui::PresetBar presetBar;

    bf::ui::OscillatorPage oscPage;
    bf::ui::FilterPage     filterPage;
    bf::ui::ModulatorPage  modPage;
    bf::ui::FxPage         fxPage;
    juce::Component*       pages[4];
    int                    current = 0;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassForgeAudioProcessorEditor)
};
