#pragma once

#include <JuceHeader.h>
#include <functional>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

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
        for (auto* b : { &presetButton, &zoomButton, &mixButton, &playButton, &closeButton })
            addAndMakeVisible (b);

        zoomButton.setClickingTogglesState (true);
        zoomButton.onClick  = [this] { layoutEditor(); };
        closeButton.onClick = [this] { if (onClose) onClose(); };
        mixButton.onClick   = [this] { if (onOpenMix) onOpenMix(); };
        playButton.onClick  = [this] { if (onPreview) onPreview(); };
        presetButton.onClick = [this] { if (onShowPresets) onShowPresets (presetButton); };

        viewport.setScrollBarsShown (true, true);
        viewport.setScrollBarThickness (14);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);   // il dito serve alle manopole
        content.addAndMakeVisible (holder);
        viewport.setViewedComponent (&content, false);
        addAndMakeVisible (viewport);
    }

    ~SynthPanel() override   { closeEditor(); }

    std::function<void()> onClose, onOpenMix, onPreview;
    std::function<void (juce::Component&)> onShowPresets;

    void open()
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
            editor.reset();   // il distruttore avvisa il processore (editorBeingDeleted)
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
        bar.removeFromLeft (110);

        closeButton.setBounds (bar.removeFromRight (74));
        bar.removeFromRight (6);
        zoomButton.setBounds (bar.removeFromRight (70));
        bar.removeFromRight (6);
        mixButton.setBounds (bar.removeFromRight (60));
        bar.removeFromRight (6);
        playButton.setBounds (bar.removeFromRight (70));
        bar.removeFromRight (10);
        presetButton.setBounds (bar.removeFromLeft (juce::jmin (260, bar.getWidth())));

        viewport.setBounds (r);
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
    juce::Component content, holder;
    juce::Viewport viewport;
    juce::TextButton presetButton, zoomButton { "Zoom" }, mixButton { "Mix" }, playButton { "Ascolta" }, closeButton { "Chiudi" };
    juce::Rectangle<float> barArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};
