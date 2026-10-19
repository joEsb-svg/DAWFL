#pragma once

#include <JuceHeader.h>
#include "../Engine/SequencerEngine.h"
#include "DawLookAndFeel.h"
#include "Widgets.h"
#include "ChannelRow.h"

//==============================================================================
/** La schermata principale: barra del trasporto + channel rack. */
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Salva lo stato corrente nel file di autosalvataggio (chiamato anche quando l'app va in background). */
    void saveSession();

private:
    void timerCallback() override;
    juce::Rectangle<int> getContentArea() const;

    // Menu e finestre
    void showFileMenu();
    void showPatternMenu();
    void showKitMenu();
    void askProjectName (const juce::String& title, std::function<void (juce::String)> onName);
    void saveProjectAs (const juce::String& name, bool confirmOverwrite);
    void openProject (const juce::String& name);
    void deleteProject (const juce::String& name);
    void newProject();
    void showAudioInfo();
    void showMessage (const juce::String& title, const juce::String& message);

    void syncControlsFromEngine();
    void updateViews();
    juce::String getKitLabel() const;

    SequencerEngine engine;
    juce::AudioDeviceManager deviceManager;
    juce::String audioError;
    juce::String projectName;
    juce::String lastSavedJson;
    int autosaveCounter = 0;

    DawLookAndFeel lookAndFeel;

    juce::TextButton fileButton { "MENU" }, kitButton;
    PlayButton playButton;
    LcdSelector bpmDisplay, patternDisplay;
    SegmentedControl modeSwitch  { { "STEP", "VEL" } };
    SegmentedControl lengthSwitch { { "16", "32" } };
    SegmentedControl pageSwitch  { { "1-16", "17-32" } };
    juce::Slider swingKnob, masterKnob;
    LevelMeter meter;
    StepRuler ruler;
    juce::OwnedArray<ChannelRow> rows;

    juce::Rectangle<float> headerArea, logoArea, swingCaption, masterCaption, kitCaption, rackArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
