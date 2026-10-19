#pragma once

#include <JuceHeader.h>
#include <functional>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

//==============================================================================
/**
    Pannello a schermo intero con l'interfaccia originale di BassForge.

    L'editor e' lo stesso del plugin per PC (stessi pannelli, manopole, vista wavetable,
    inviluppi, Performer), ma in "layout compatto": occupa esattamente lo schermo del telefono
    e mostra una sezione alla volta (OSC 1 / OSC 2 / ... ) invece di rimpicciolire tutto.
    L'editor viene creato all'apertura e distrutto alla chiusura: quando e' chiuso non consuma nulla.
*/
class SynthPanel : public juce::Component
{
public:
    explicit SynthPanel (BassForgeAudioProcessor& p) : processor (p)
    {
        for (auto* b : { &presetButton, &mixButton, &playButton, &closeButton })
            addAndMakeVisible (b);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        mixButton.onClick   = [this] { if (onOpenMix) onOpenMix(); };
        playButton.onClick  = [this] { if (onPreview) onPreview(); };
        presetButton.onClick = [this] { if (onShowPresets) onShowPresets (presetButton); };
    }

    ~SynthPanel() override   { closeEditor(); }

    /** Chiude tutto (alla chiusura del pannello): l'editor non resta in memoria. */
    void closeAll()    { closeEditor(); }

    std::function<void()> onClose, onOpenMix, onPreview;
    std::function<void (juce::Component&)> onShowPresets;

    void open()
    {
        if (editor == nullptr)
        {
            editor.reset (processor.createEditorAndMakeActive());
            if (auto* bf = dynamic_cast<BassForgeAudioProcessorEditor*> (editor.get()))
            {
                bf->setCompactLayout (true);
                bf->setPresetBarVisible (false);   // i preset si scelgono dal pulsante in alto
                // Riprende la pagina e la sezione dove l'utente era rimasto
                bf->showPageSection (lastPage, lastSection, lastView);
            }
            if (editor != nullptr)
                addAndMakeVisible (editor.get());
        }

        refreshPresetName();
        resized();
    }

    void showTab (int page, int section, int view)
    {
        lastPage = page; lastSection = section; lastView = view;
        if (auto* bf = dynamic_cast<BassForgeAudioProcessorEditor*> (editor.get()))
            bf->showPageSection (page, section, view);
    }

    void closeEditor()
    {
        if (auto* bf = dynamic_cast<BassForgeAudioProcessorEditor*> (editor.get()))
        {
            lastPage = bf->getCurrentPage();
            lastSection = bf->getCurrentSection();
            lastView = bf->getModulatorView();
        }

        if (editor != nullptr)
        {
            removeChildComponent (editor.get());
            processor.editorBeingDeleted (editor.get());   // come fa una DAW: senza questo l'editor non si riapre
            editor.reset();
        }
    }

    void refreshPresetName()
    {
        presetButton.setButtonText ("Preset: " + processor.getPresets().currentName()
                                    + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Theme::panel);
        g.fillRect (barArea);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (40);
        barArea = bar.toFloat();
        bar.reduce (8, 5);
        const bool compact = getWidth() < 640;   // telefono in verticale

        closeButton.setBounds (bar.removeFromRight (74));
        bar.removeFromRight (6);
        mixButton.setBounds (bar.removeFromRight (compact ? 44 : 60));
        bar.removeFromRight (6);
        playButton.setBounds (bar.removeFromRight (compact ? 70 : 80));
        bar.removeFromRight (10);
        presetButton.setBounds (bar.removeFromLeft (juce::jmin (300, bar.getWidth())));

        if (editor != nullptr)
            editor->setBounds (r);
    }

private:
    BassForgeAudioProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::TextButton presetButton, mixButton { "Mix" }, playButton { "Ascolta" }, closeButton { "Chiudi" };
    juce::Rectangle<float> barArea;
    int lastPage = 0, lastSection = 0, lastView = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};
