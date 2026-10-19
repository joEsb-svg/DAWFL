#pragma once

#include <JuceHeader.h>
#include <functional>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"
#include "SynthMobileEditor.h"

//==============================================================================
/**
    Pannello a schermo intero con l'interfaccia di BassForge.

    L'editor di BassForge e' pensato per il PC (1200 x 780): qui viene scalato per stare
    sullo schermo ("Adatta"), oppure mostrato piu' grande dentro un'area scorrevole ("Zoom")
    per poter usare comodamente le manopole col dito.
    L'editor viene creato all'apertura e distrutto alla chiusura: quando e' chiuso non consuma nulla.
*/
class SynthPanel : public juce::Component
{
public:
    explicit SynthPanel (BassForgeAudioProcessor& p) : processor (p)
    {
        for (auto* b : { &presetButton, &mixButton, &playButton, &closeButton })
            addAndMakeVisible (b);

        zoomButton.setClickingTogglesState (true);
        zoomButton.onClick  = [this] { layoutEditor(); };
        advancedButton.setClickingTogglesState (true);
        advancedButton.onClick = [this] { setAdvanced (advancedButton.getToggleState()); };
        addAndMakeVisible (advancedButton);
        addChildComponent (zoomButton);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        mixButton.onClick   = [this] { if (onOpenMix) onOpenMix(); };
        playButton.onClick  = [this] { if (onPreview) onPreview(); };
        presetButton.onClick = [this] { if (onShowPresets) onShowPresets (presetButton); };

        viewport.setScrollBarsShown (true, true);
        viewport.setScrollBarThickness (14);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);   // il dito serve alle manopole
        content.addAndMakeVisible (holder);
        viewport.setViewedComponent (&content, false);
        addChildComponent (viewport);
    }

    ~SynthPanel() override   { closeEditor(); }

    /** Chiude tutto (alla chiusura del pannello): l'editor originale non resta in memoria. */
    void closeAll()
    {
        closeEditor();
        mobile.reset();
    }

    std::function<void()> onClose, onOpenMix, onPreview;
    std::function<void (juce::Component&)> onShowPresets;

    void open()
    {
        if (mobile == nullptr)
        {
            mobile = std::make_unique<SynthMobileEditor> (processor);
            addAndMakeVisible (*mobile);
            resized();
        }

        setAdvanced (advancedButton.getToggleState());
        refreshPresetName();
    }

    void showTab (int tab, int section)     { if (mobile != nullptr) mobile->showTab (tab, section); }

    /** Interfaccia originale di BassForge (wavetable, Performer, matrice grafica). */
    void setAdvanced (bool shouldShowOriginal)
    {
        advancedButton.setToggleState (shouldShowOriginal, juce::dontSendNotification);
        advancedButton.setButtonText (shouldShowOriginal ? "Semplice" : "Avanzato");
        zoomButton.setVisible (shouldShowOriginal);
        resized();
        viewport.setVisible (shouldShowOriginal);
        if (mobile != nullptr)
            mobile->setVisible (! shouldShowOriginal);

        if (shouldShowOriginal)
            openOriginalEditor();
        else
            closeEditor();
    }

    void openOriginalEditor()
    {
        if (editor == nullptr)
        {
            editor.reset (processor.createEditorAndMakeActive());
            if (editor != nullptr)
            {
                editor->setSize (editorWidth, editorHeight);
                holder.addAndMakeVisible (editor.get());
            }
        }

        refreshPresetName();
        layoutEditor();
    }

    void closeEditor()
    {
        if (editor != nullptr)
        {
            holder.removeChildComponent (editor.get());
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

        if (compact)
            return;

        auto title = barArea.reduced (10.0f, 0.0f);
        g.setColour (juce::Colour (0xff2fc6d6));
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 8.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (17.0f, true));
        g.drawText ("BassForge", title, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (40);
        barArea = bar.toFloat();
        bar.reduce (8, 5);
        compact = getWidth() < 640;              // telefono in verticale: niente titolo
        if (! compact)
            bar.removeFromLeft (110);

        closeButton.setBounds (bar.removeFromRight (74));
        bar.removeFromRight (6);
        advancedButton.setBounds (bar.removeFromRight (compact ? 74 : 84));
        bar.removeFromRight (6);
        if (zoomButton.isVisible())
        {
            zoomButton.setBounds (bar.removeFromRight (70));
            bar.removeFromRight (6);
        }
        mixButton.setBounds (bar.removeFromRight (compact ? 44 : 60));
        bar.removeFromRight (6);
        playButton.setBounds (bar.removeFromRight (compact ? 0 : 70));   // in verticale si ascolta dal channel rack
        playButton.setVisible (! compact);
        bar.removeFromRight (10);
        presetButton.setBounds (bar.removeFromLeft (juce::jmin (260, bar.getWidth())));

        viewport.setBounds (r);
        if (mobile != nullptr)
            mobile->setBounds (r);
        layoutEditor();
    }

private:
    // Dimensione di progetto dell'editor di BassForge (vedi PluginEditor.h)
    static constexpr int editorWidth = 1200, editorHeight = 780;

    void layoutEditor()
    {
        if (editor == nullptr)
            return;

        const float w = (float) editorWidth;
        const float h = (float) editorHeight;
        const auto area = viewport.getLocalBounds();

        // "Adatta": tutto l'editor visibile. "Zoom": almeno 80% della dimensione originale, si scorre.
        const float fit = juce::jmin ((float) area.getWidth() / w, (float) area.getHeight() / h);
        const float scale = zoomButton.getToggleState() ? juce::jmax (0.8f, fit) : fit;

        editor->setTransform (juce::AffineTransform::scale (scale));
        holder.setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));

        // Centrato quando sta tutto nello schermo
        content.setSize (juce::jmax (area.getWidth(), holder.getWidth()), juce::jmax (area.getHeight(), holder.getHeight()));
        holder.setTopLeftPosition ((content.getWidth() - holder.getWidth()) / 2, (content.getHeight() - holder.getHeight()) / 2);
        zoomButton.setButtonText (zoomButton.getToggleState() ? "Adatta" : "Zoom");
    }

    BassForgeAudioProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<SynthMobileEditor> mobile;
    juce::Component content, holder;
    juce::Viewport viewport;
    juce::TextButton presetButton, zoomButton { "Zoom" }, advancedButton { "Avanzato" }, mixButton { "Mix" }, playButton { "Ascolta" }, closeButton { "Chiudi" };
    juce::Rectangle<float> barArea;
    bool compact = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};
