#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "../Instruments/Instruments.h"
#include "../Instruments/Common/ForgeEditorBase.h"
#include "Theme.h"

//==============================================================================
/**
    Pannello a schermo intero con l'interfaccia di uno strumento (BassForge, PolyForge...).

    - L'editor dello strumento e' quello originale, adattato al telefono
    - In basso una tastiera per suonare e provare i preset (si puo' nascondere)
    - L'editor viene creato all'apertura e distrutto alla chiusura: quando e' chiuso non consuma nulla
*/
class SynthPanel : public juce::Component,
                   private juce::MidiKeyboardState::Listener
{
public:
    explicit SynthPanel (SequencerEngine& e) : engine (e)
    {
        for (auto* b : { &presetButton, &mixButton, &keysButton, &closeButton })
            addAndMakeVisible (b);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        mixButton.onClick   = [this] { if (onOpenMix) onOpenMix(); };
        presetButton.onClick = [this] { if (onShowPresets) onShowPresets (presetButton); };
        keysButton.setClickingTogglesState (true);
        keysButton.setToggleState (true, juce::dontSendNotification);
        keysButton.onClick = [this] { keyboardState.allNotesOff (1); resized(); };

        keyboard.setKeyWidth (34.0f);
        keyboard.setOctaveForMiddleC (4);   // come il piano roll: il Do centrale (nota 60) e' C4
        keyboard.setScrollButtonsVisible (true);
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour (0xff1fe5ff).withAlpha (0.55f));
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::transparentBlack);
        keyboard.setVelocity (0.85f, false);
        keyboardState.addListener (this);
        addAndMakeVisible (keyboard);
    }

    ~SynthPanel() override
    {
        keyboardState.removeListener (this);
        closeEditor();
    }

    std::function<void()> onClose, onOpenMix;
    std::function<void (juce::Component&)> onShowPresets;

    /** Apre lo strumento del canale indicato. */
    void open (int channelIndex)
    {
        if (channelIndex != channel || engine.getInstrument (channelIndex) != shownProcessor)
            closeEditor();

        channel = channelIndex;
        auto* proc = engine.getInstrument (channel);
        if (editor == nullptr && proc != nullptr)
        {
            shownProcessor = proc;
            editor.reset (proc->createEditorAndMakeActive());
            if (editor != nullptr)
            {
                Instruments::prepareEditor (editor.get());
                if (auto* bf = dynamic_cast<BassForgeAudioProcessorEditor*> (editor.get()))
                    bf->showPageSection (lastPage, lastSection, lastView);
                addAndMakeVisible (editor.get());
            }
        }

        // Tastiera: registro adatto allo strumento (bassi piu' in basso)
        keyboard.setLowestVisibleKey (dynamic_cast<BassForgeAudioProcessor*> (proc) != nullptr ? 24
                                      : dynamic_cast<DrumForgeAudioProcessor*> (proc) != nullptr ? df::firstNote : 48);
        refreshPresetName();
        resized();
        repaint();
    }

    int getChannel() const      { return channel; }
    juce::AudioProcessorEditor* getEditorForTest() const    { return editor.get(); }

    /** Solo per i test: pagina e sezione dell'editor di BassForge. */
    void showTab (int page, int section, int view)
    {
        lastPage = page; lastSection = section; lastView = view;
        if (auto* bf = dynamic_cast<BassForgeAudioProcessorEditor*> (editor.get()))
            bf->showPageSection (page, section, view);
    }

    void closeAll()     { keyboardState.allNotesOff (1); closeEditor(); }

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
            if (shownProcessor != nullptr)
                shownProcessor->editorBeingDeleted (editor.get());   // come fa una DAW: senza questo l'editor non si riapre
            editor.reset();
        }
        shownProcessor = nullptr;
    }

    void refreshPresetName()
    {
        const bool kit = dynamic_cast<DrumForgeAudioProcessor*> (engine.getInstrument (channel)) != nullptr;
        presetButton.setButtonText ((kit ? "Kit: " : "Preset: ") + Instruments::presetName (engine.getInstrument (channel))
                                    + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Theme::panel);
        g.fillRect (barArea);

        if (! compact && channel >= 0 && channel < engine.getNumChannels())
        {
            auto title = barArea.withWidth (titleWidth).reduced (10.0f, 0.0f);
            g.setColour (juce::Colour (engine.getChannel (channel).colour.load()));
            g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 9.0f));
            title.removeFromLeft (8.0f);
            g.setColour (Theme::textBright);
            g.setFont (Theme::font (15.0f, true));
            g.drawFittedText (engine.getChannel (channel).name, title.toNearestInt(), juce::Justification::centredLeft, 1);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (40);
        barArea = bar.toFloat();
        bar.reduce (8, 5);
        compact = getWidth() < 640;   // telefono in verticale: niente titolo
        if (! compact)
            bar.removeFromLeft ((int) titleWidth);

        closeButton.setBounds (bar.removeFromRight (compact ? 64 : 74));
        bar.removeFromRight (6);
        mixButton.setBounds (bar.removeFromRight (compact ? 44 : 60));
        bar.removeFromRight (6);
        keysButton.setBounds (bar.removeFromRight (compact ? 70 : 84));
        bar.removeFromRight (10);
        presetButton.setBounds (bar.removeFromLeft (juce::jmin (300, bar.getWidth())));

        const bool showKeys = keysButton.getToggleState();
        keyboard.setVisible (showKeys);
        if (showKeys)
            keyboard.setBounds (r.removeFromBottom (compact ? 70 : 58));

        if (editor != nullptr)
            editor->setBounds (r);
    }

private:
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override
    {
        engine.liveNote (channel, note, velocity);
    }

    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
    {
        engine.liveNote (channel, note, 0.0f);
    }

    SequencerEngine& engine;
    int channel = -1;
    juce::AudioProcessor* shownProcessor = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton presetButton, mixButton { "Mix" }, keysButton { "Tastiera" }, closeButton { "Chiudi" };
    juce::Rectangle<float> barArea;
    static constexpr float titleWidth = 150.0f;
    bool compact = false;
    int lastPage = 0, lastSection = 0, lastView = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};
