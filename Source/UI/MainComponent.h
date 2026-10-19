#pragma once

#include <JuceHeader.h>
#include "../Engine/SequencerEngine.h"

//==============================================================================
/** Una riga del channel rack: nome del canale + 16 pad toccabili. */
class StepRow : public juce::Component
{
public:
    StepRow (SequencerEngine& e, juce::String channelName);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getPadBounds (int index) const;
    juce::Rectangle<float> getPadsArea() const;

    SequencerEngine& engine;
    juce::String name;
};

//==============================================================================
/** La schermata principale: barra del trasporto + channel rack + info audio. */
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updatePlayButton();
    void updateBpmLabel();
    void updateInfoLabel();

    SequencerEngine engine;
    juce::AudioDeviceManager deviceManager;
    juce::String audioError;
    int lastDrawnStep = -2;

    juce::TextButton playButton, bpmDownButton, bpmUpButton;
    juce::Label titleLabel, bpmLabel, infoLabel;
    StepRow kickRow { engine, "Kick" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
