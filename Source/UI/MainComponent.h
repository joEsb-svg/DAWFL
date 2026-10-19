#pragma once

#include <JuceHeader.h>
#include "../Engine/SequencerEngine.h"
#include "DawLookAndFeel.h"
#include "Widgets.h"
#include "ChannelRow.h"

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
    void updateInfoLabel();
    juce::Rectangle<int> getContentArea() const;

    SequencerEngine engine;
    juce::AudioDeviceManager deviceManager;
    juce::String audioError;

    DawLookAndFeel lookAndFeel;

    PlayButton playButton;
    BpmDisplay bpmDisplay;
    juce::Slider masterKnob;
    LevelMeter meter;
    StepLeds stepLeds;
    juce::Label infoLabel;
    juce::OwnedArray<ChannelRow> rows;

    juce::Rectangle<float> headerArea, logoArea, masterLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
