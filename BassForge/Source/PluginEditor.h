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

    /** Layout per telefono (usato da JLoop): dimensione libera, header compatto, una sezione alla volta.
        Stesso aspetto e stessi controlli dell'editor normale. */
    void setCompactLayout(bool shouldBeCompact);
    bool isCompactLayout() const noexcept { return compact; }
    int compactHeaderHeight() const;
    /** Apre una pagina (0..3) e una sua sezione; per i modulatori `view` sceglie Env/LFO/Macro/Performer. */
    void showPageSection(int page, int section, int view = -1);
    int getCurrentPage() const noexcept { return current; }
    int getCurrentSection() const { return static_cast<const bf::ui::PageBase*>(pages[current])->getSection(); }
    int getModulatorView() const noexcept { return modPage.getView(); }
    void setPresetBarVisible(bool v) { presetBar.setVisible(v); resized(); }

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
    bool                   compact = false;

    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassForgeAudioProcessorEditor)
};
