#pragma once

#include <JuceHeader.h>
#include <vector>
#include "PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/**
    Un controllo collegato a un parametro di BassForge, del tipo giusto:
    - numero   -> manopola (con valore e unita' sotto)
    - si/no    -> interruttore
    - scelta   -> pulsante che apre un menu

    I collegamenti usano le "attachment" di JUCE: gesti, undo dell'host e thread safety
    sono gestiti come in un plugin vero.
*/
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::String labelText)
        : label (std::move (labelText))
    {
        param = state.getParameter (paramId);
        jassert (param != nullptr);

        if (param == nullptr)
            return;

        if (label.isEmpty())
            label = shortName (param->getName (40));

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            kind = Kind::choice;
            choiceParam = choice;
            addAndMakeVisible (button);
            button.onClick = [this] { showChoiceMenu(); };
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { updateChoiceText(); });
            attachment->sendInitialUpdate();
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            kind = Kind::toggle;
            toggle.setClickingTogglesState (true);
            addAndMakeVisible (toggle);
            buttonAttachment = std::make_unique<juce::ButtonParameterAttachment> (*param, toggle);
            toggle.onStateChange = [this] { repaint(); };
        }
        else
        {
            kind = Kind::knob;
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setMouseDragSensitivity (200);
            slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff2fc6d6));
            addAndMakeVisible (slider);
            sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider);
            slider.setDoubleClickReturnValue (true, slider.getValue());   // doppio tocco = valore iniziale

            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                const auto& range = ranged->getNormalisableRange();
                if (range.start < 0.0f && range.end > 0.0f)
                    slider.getProperties().set ("bipolar", true);
                slider.setDoubleClickReturnValue (true, range.convertFrom0to1 (ranged->getDefaultValue()));
            }

            slider.onValueChange = [this] { repaint(); };
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawCaption (g, label, r.removeFromTop (14.0f));

        if (kind == Kind::knob && param != nullptr)
        {
            // Il testo del parametro include gia' l'unita' (es. "2.00 kHz", "0 st")
            auto text = param->getCurrentValueAsText();

            g.setColour (slider.isMouseButtonDown() ? Theme::accent : Theme::text);
            g.setFont (Theme::font (11.5f, true));
            g.drawFittedText (text, r.removeFromBottom (15.0f).toNearestInt(), juce::Justification::centred, 1);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (15);

        if (kind == Kind::knob)
        {
            r.removeFromBottom (15);
            const int s = juce::jmin (r.getWidth(), r.getHeight(), 72);
            slider.setBounds (r.withSizeKeepingCentre (s, s));
        }
        else
        {
            const int h = juce::jmin (r.getHeight(), 36);
            auto area = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 120), h);
            button.setBounds (area);
            toggle.setBounds (area);
        }
    }

private:
    enum class Kind { knob, toggle, choice };

    /** "Osc 1 WT Pos" -> "WT Pos": il nome della sezione e' gia' scritto sopra. */
    static juce::String shortName (juce::String n)
    {
        for (auto prefix : { "Osc ", "Filter ", "Env ", "LFO ", "Mod " })
            if (n.startsWith (prefix) && n.length() > juce::String (prefix).length() + 2)
                return n.substring (juce::String (prefix).length() + 2);

        for (auto prefix : { "Noise Filter ", "Dimension ", "Reverb ", "Sample ", "Delay ", "Drive ", "Noise ", "OTT ", "Sub " })
            if (n.startsWith (prefix))
                return n.substring (juce::String (prefix).length());

        return n;
    }

    void updateChoiceText()
    {
        if (choiceParam != nullptr)
            button.setButtonText (choiceParam->getCurrentChoiceName() + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
    }

    void showChoiceMenu()
    {
        if (choiceParam == nullptr)
            return;

        juce::PopupMenu menu;
        for (int i = 0; i < choiceParam->choices.size(); ++i)
            menu.addItem (i + 1, choiceParam->choices[i], true, i == choiceParam->getIndex());

        juce::Component::SafePointer<ParamControl> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button), [safeThis] (int result)
        {
            if (safeThis != nullptr && result > 0 && safeThis->attachment != nullptr)
                safeThis->attachment->setValueAsCompleteGesture ((float) (result - 1));
        });
    }

    juce::String label;
    juce::RangedAudioParameter* param = nullptr;
    juce::AudioParameterChoice* choiceParam = nullptr;
    Kind kind = Kind::knob;

    juce::Slider slider;
    juce::TextButton button;
    LedToggle toggle;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
/**
    Interfaccia di BassForge pensata per il telefono: schede grandi, sezioni da massimo 12 controlli,
    manopole a misura di dito. Per le cose "visive" (wavetable, editor del Performer, matrice completa)
    resta disponibile l'interfaccia originale ("Avanzato").
*/
class SynthMobileEditor : public juce::Component
{
public:
    explicit SynthMobileEditor (BassForgeAudioProcessor& p) : apvts (p.apvts)
    {
        buildPages();

        juce::StringArray tabNames;
        for (auto& t : tabs)
            tabNames.add (t.name);

        tabBar = std::make_unique<SegmentedControl> (tabNames);
        tabBar->onChange = [this] (int) { sectionIndex = 0; showSection(); };
        addAndMakeVisible (*tabBar);

        sectionBar = std::make_unique<SegmentedControl> (juce::StringArray { "-" });
        sectionBar->onChange = [this] (int i) { sectionIndex = i; showSection(); };
        addAndMakeVisible (*sectionBar);

        showSection();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        tabBar->setBounds (r.removeFromTop (32));
        r.removeFromTop (6);
        sectionBar->setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        controlsArea = r;
        layoutControls();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (controlsArea.toFloat().expanded (4.0f), Theme::corner);
    }

    void showTab (int tab, int section)
    {
        tabBar->setSelected (juce::jlimit (0, (int) tabs.size() - 1, tab));
        sectionIndex = juce::jlimit (0, (int) tabs[(size_t) tabBar->getSelected()].sections.size() - 1, section);
        showSection();
    }

private:
    struct Item    { juce::String id, label; };
    struct Section { juce::String name; std::vector<Item> items; };
    struct Tab     { juce::String name; std::vector<Section> sections; };

    void showSection()
    {
        const auto& tab = tabs[(size_t) tabBar->getSelected()];

        juce::StringArray names;
        for (auto& s : tab.sections)
            names.add (s.name);
        sectionBar->setOptions (names);
        sectionBar->setSelected (sectionIndex);
        sectionBar->setVisible (names.size() > 1);

        controls.clear();
        for (auto& item : tab.sections[(size_t) sectionIndex].items)
            addAndMakeVisible (controls.add (new ParamControl (apvts, item.id, item.label)));

        layoutControls();
    }

    void layoutControls()
    {
        if (controls.isEmpty() || controlsArea.isEmpty())
            return;

        // Colonne in base all'orientamento: in orizzontale fino a 6 per riga, in verticale 3
        const int n = controls.size();
        const bool portrait = controlsArea.getHeight() > controlsArea.getWidth();
        const int columns = portrait ? juce::jmin (3, n) : juce::jmin (6, n <= 6 ? n : (n + 1) / 2);
        const int rows = (n + columns - 1) / columns;

        const int cellW = controlsArea.getWidth() / columns;
        const int cellH = juce::jmin (130, controlsArea.getHeight() / rows);
        const int top = controlsArea.getY() + (controlsArea.getHeight() - cellH * rows) / 2;

        for (int i = 0; i < n; ++i)
            controls[i]->setBounds (juce::Rectangle<int> (controlsArea.getX() + (i % columns) * cellW, top + (i / columns) * cellH, cellW, cellH)
                                        .reduced (2, 5));
    }

    void buildPages()
    {
        using namespace bf;

        auto oscSection = [] (int i)
        {
            Section s { "Osc " + juce::String (i + 1), {} };
            for (auto [p, l] : { std::pair { "on", "Attivo" }, { "wt", "Wavetable" }, { "semi", "Semitoni" }, { "fine", "Fine" },
                                 { "level", "Livello" }, { "pan", "Pan" }, { "uni", "Unison" }, { "det", "Detune" },
                                 { "spread", "Spread" }, { "warp", "Warp" }, { "wamt", "Warp Amt" }, { "waux", "Warp Aux" } })
                s.items.push_back ({ ids::osc (i, p), l });
            return s;
        };

        Tab osc { "OSC", {} };
        for (int i = 0; i < kNumOsc; ++i)
            osc.sections.push_back (oscSection (i));
        osc.sections.push_back ({ "Sub", { { ids::subOn, "Attivo" }, { ids::subWave, "Onda" }, { ids::subOct, "Ottava" }, { ids::subLevel, "Livello" },
                                           { ids::subPhase, "Fase" }, { ids::subReset, "Phase Reset" }, { ids::subDirect, "Direct Out" }, { ids::subFollowMod, "Segue Mod" } } });
        osc.sections.push_back ({ "Noise", { { ids::noiseOn, "Attivo" }, { ids::noiseType, "Tipo" }, { ids::noiseLevel, "Livello" }, { ids::noiseWidth, "Ampiezza" },
                                             { ids::nfltOn, "Filtro" }, { ids::nfltType, "Tipo filtro" }, { ids::nfltCutoff, "Cutoff" }, { ids::nfltRes, "Risonanza" }, { ids::nfltTrack, "Keytrack" } } });
        osc.sections.push_back ({ "Sample", { { ids::smpOn, "Attivo" }, { ids::smpSlot, "Campione" }, { ids::smpLevel, "Livello" }, { ids::smpPitch, "Pitch" },
                                              { ids::smpPitchTrack, "Pitch Track" }, { ids::smpDecay, "Decay" }, { ids::smpLoop, "Loop" }, { ids::smpLoopStart, "Inizio loop" }, { ids::smpLoopEnd, "Fine loop" } } });

        Tab filt { "FILTRI", {} };
        for (int i = 0; i < kNumFilter; ++i)
        {
            Section s { "Filtro " + juce::String (i + 1), {} };
            for (auto [p, l] : { std::pair { "on", "Attivo" }, { "type", "Tipo" }, { "cutoff", "Cutoff" }, { "res", "Risonanza" },
                                 { "drive", "Drive" }, { "keytrack", "Keytrack" }, { "fb", "Feedback" }, { "tune", "Comb Tune" },
                                 { "vowel", "Vocale" }, { "fshift", "Formant" } })
                s.items.push_back ({ ids::filt (i, p), l });
            filt.sections.push_back (s);
        }
        filt.sections.push_back ({ "Routing", { { ids::filterRouting, "Routing" }, { ids::filterMorph, "Morph" } } });

        Tab env { "ENV", {} };
        for (int i = 0; i < kNumEnv; ++i)
        {
            Section s { i == 0 ? juce::String ("Env 1 (Volume)") : "Env " + juce::String (i + 1), {} };
            for (auto [p, l] : { std::pair { "a", "Attack" }, { "h", "Hold" }, { "d", "Decay" }, { "s", "Sustain" }, { "r", "Release" },
                                 { "ac", "Curva A" }, { "dc", "Curva D" }, { "rc", "Curva R" } })
                s.items.push_back ({ ids::env (i, p), l });
            env.sections.push_back (s);
        }

        Tab lfo { "LFO", {} };
        for (int i = 0; i < kNumLfo; ++i)
        {
            Section s { "LFO " + juce::String (i + 1), {} };
            for (auto [p, l] : { std::pair { "shape", "Forma" }, { "rate", "Velocita'" }, { "sync", "A tempo" }, { "div", "Divisione" },
                                 { "retrig", "Retrigger" }, { "phase", "Fase" }, { "fade", "Fade In" }, { "pmode", "Performer" } })
                s.items.push_back ({ ids::lfo (i, p), l });
            lfo.sections.push_back (s);
        }

        Tab mod { "MOD", {} };
        for (int first = 0; first < kNumSlots; first += 4)
        {
            Section s { "Slot " + juce::String (first + 1) + "-" + juce::String (first + 4), {} };
            for (int i = first; i < first + 4; ++i)
            {
                const auto n = juce::String (i + 1);
                s.items.push_back ({ ids::slot (i, "src"), "Sorgente " + n });
                s.items.push_back ({ ids::slot (i, "dst"), "Dest. " + n });
                s.items.push_back ({ ids::slot (i, "amt"), "Quantita' " + n });
            }
            mod.sections.push_back (s);
        }

        Tab macro { "MACRO", {} };
        for (int first = 0; first < kNumMacro; first += 6)
        {
            Section s { "Macro " + juce::String (first + 1) + "-" + juce::String (first + 6), {} };
            for (int i = first; i < first + 6; ++i)
                s.items.push_back ({ ids::macro (i), "Macro " + juce::String (i + 1) });
            macro.sections.push_back (s);
        }

        Tab fx { "FX", {} };
        fx.sections.push_back ({ "OTT", { { ids::ottOn, "Attivo" }, { ids::ottDepth, "Depth" }, { ids::ottTime, "Time" }, { ids::ottUp, "Upward" }, { ids::ottDown, "Downward" },
                                          { ids::ottLow, "Low" }, { ids::ottMid, "Mid" }, { ids::ottHigh, "High" }, { ids::ottMix, "Mix" } } });
        fx.sections.push_back ({ "Drive", { { ids::drvOn, "Attivo" }, { ids::drvType, "Tipo" }, { ids::drvAmount, "Quantita'" }, { ids::drvBias, "Bias" },
                                            { ids::drvStages, "Stadi" }, { ids::drvOs, "Oversampling" }, { ids::drvOut, "Uscita" }, { ids::drvMix, "Mix" } } });
        fx.sections.push_back ({ "Dimension", { { ids::dimOn, "Attivo" }, { ids::dimAmount, "Quantita'" }, { ids::dimWidth, "Ampiezza" }, { ids::dimSize, "Size" },
                                                { ids::dimRate, "Rate" }, { ids::dimMono, "Mono sotto" }, { ids::dimMix, "Mix" } } });
        fx.sections.push_back ({ "Delay", { { ids::dlyOn, "Attivo" }, { ids::dlySync, "A tempo" }, { ids::dlyDiv, "Divisione" }, { ids::dlyTime, "Tempo" }, { ids::dlyFb, "Feedback" },
                                            { ids::dlyPing, "Ping-Pong" }, { ids::dlyDamp, "Damping" }, { ids::dlyHpf, "HPF" }, { ids::dlyMix, "Mix" } } });
        fx.sections.push_back ({ "Reverb", { { ids::revOn, "Attivo" }, { ids::revSize, "Size" }, { ids::revDamp, "Damping" }, { ids::revWidth, "Ampiezza" },
                                             { ids::revPre, "Pre-Delay" }, { ids::revHpf, "HPF" }, { ids::revMix, "Mix" } } });
        fx.sections.push_back ({ "Master", { { ids::fxOrder, "Ordine FX" }, { ids::masterGain, "Volume" }, { ids::masterLimiter, "Limiter" }, { ids::pitchBendRange, "Pitch Bend" } } });

        tabs = { osc, filt, env, lfo, mod, macro, fx };
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Tab> tabs;
    int sectionIndex = 0;

    std::unique_ptr<SegmentedControl> tabBar, sectionBar;
    juce::OwnedArray<ParamControl> controls;
    juce::Rectangle<int> controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthMobileEditor)
};
