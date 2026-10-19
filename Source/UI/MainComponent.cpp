#include "MainComponent.h"
#include "Theme.h"

//==============================================================================
// StepRow

StepRow::StepRow (SequencerEngine& e, juce::String channelName)
    : engine (e), name (std::move (channelName))
{
}

juce::Rectangle<float> StepRow::getPadsArea() const
{
    auto area = getLocalBounds().toFloat().reduced (4.0f);
    area.removeFromLeft (juce::jmin (110.0f, area.getWidth() * 0.18f));   // spazio per il nome
    return area;
}

juce::Rectangle<float> StepRow::getPadBounds (int index) const
{
    auto area = getPadsArea();
    const float groupGap = 6.0f;
    const float padGap   = 3.0f;
    const int numGroups  = SequencerEngine::numSteps / 4;

    const float padWidth = (area.getWidth() - groupGap * (float) (numGroups - 1)
                                            - padGap * (float) (SequencerEngine::numSteps - numGroups))
                           / (float) SequencerEngine::numSteps;

    const int group = index / 4;
    const float x = area.getX()
                  + (float) index * padWidth
                  + (float) group * groupGap
                  + (float) (index - group) * padGap;

    return { x, area.getY(), padWidth, area.getHeight() };
}

void StepRow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (Theme::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    // Nome del canale
    auto nameArea = bounds.reduced (4.0f).withRight (getPadsArea().getX() - 6.0f);
    g.setColour (Theme::panelLight);
    g.fillRoundedRectangle (nameArea, 4.0f);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (juce::jlimit (12.0f, 20.0f, nameArea.getHeight() * 0.35f)));
    g.drawFittedText (name, nameArea.toNearestInt().reduced (4), juce::Justification::centred, 1);

    const int playing = engine.getCurrentStep();

    for (int i = 0; i < SequencerEngine::numSteps; ++i)
    {
        auto pad = getPadBounds (i);
        const bool on = engine.isStepOn (i);
        const bool groupA = (i / 4) % 2 == 0;

        g.setColour (on ? Theme::padOn : (groupA ? Theme::padOffA : Theme::padOffB));
        g.fillRoundedRectangle (pad, 4.0f);

        if (i == playing)
        {
            g.setColour (Theme::playhead);
            g.drawRoundedRectangle (pad.reduced (1.5f), 4.0f, 3.0f);
        }
    }
}

void StepRow::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < SequencerEngine::numSteps; ++i)
    {
        if (getPadBounds (i).contains (e.position))
        {
            engine.toggleStep (i);
            repaint();
            return;
        }
    }
}

//==============================================================================
// MainComponent

MainComponent::MainComponent()
{
    auto styleButton = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, Theme::panelLight);
        b.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        b.setColour (juce::TextButton::textColourOffId, Theme::text);
        b.setColour (juce::TextButton::textColourOnId, Theme::background);
    };

    styleButton (playButton);
    styleButton (bpmDownButton);
    styleButton (bpmUpButton);

    playButton.setClickingTogglesState (false);
    playButton.onClick = [this]
    {
        engine.setPlaying (! engine.isPlaying());
        updatePlayButton();
    };

    bpmDownButton.setButtonText ("-");
    bpmUpButton.setButtonText ("+");
    bpmDownButton.onClick = [this] { engine.setBpm (engine.getBpm() - 1.0); updateBpmLabel(); };
    bpmUpButton.onClick   = [this] { engine.setBpm (engine.getBpm() + 1.0); updateBpmLabel(); };
    bpmDownButton.setRepeatSpeed (400, 60);
    bpmUpButton.setRepeatSpeed (400, 60);

    titleLabel.setText ("DAWFL", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, Theme::accent);
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    bpmLabel.setColour (juce::Label::textColourId, Theme::text);
    bpmLabel.setColour (juce::Label::backgroundColourId, Theme::background);
    bpmLabel.setJustificationType (juce::Justification::centred);

    infoLabel.setColour (juce::Label::textColourId, Theme::textDim);
    infoLabel.setJustificationType (juce::Justification::centredLeft);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &bpmDownButton, &bpmUpButton,
                                                            &titleLabel, &bpmLabel, &infoLabel, &kickRow })
        addAndMakeVisible (c);

    // Avvio dell'audio: 0 ingressi, 2 uscite (stereo).
    audioError = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&engine);

    updatePlayButton();
    updateBpmLabel();
    updateInfoLabel();

    startTimerHz (30);
    setSize (900, 420);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    // Su Android rispetta notch e barre di sistema.
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = display->safeAreaInsets.subtractedFrom (getLocalBounds()).reduced (12);

    const int barHeight = juce::jlimit (44, 64, getHeight() / 7);

    auto bar = area.removeFromTop (barHeight);
    titleLabel.setFont (juce::FontOptions ((float) barHeight * 0.5f, juce::Font::bold));
    titleLabel.setBounds (bar.removeFromLeft (barHeight * 3));

    playButton.setBounds (bar.removeFromLeft (barHeight * 2).reduced (2));
    bar.removeFromLeft (16);
    bpmDownButton.setBounds (bar.removeFromLeft (barHeight).reduced (2));
    bpmLabel.setFont (juce::FontOptions ((float) barHeight * 0.42f));
    bpmLabel.setBounds (bar.removeFromLeft (barHeight * 3).reduced (2));
    bpmUpButton.setBounds (bar.removeFromLeft (barHeight).reduced (2));

    infoLabel.setFont (juce::FontOptions (14.0f));
    infoLabel.setBounds (area.removeFromBottom (24));

    area.removeFromTop (16);
    kickRow.setBounds (area.removeFromTop (juce::jlimit (56, 110, area.getHeight() / 3)));
}

void MainComponent::timerCallback()
{
    const int step = engine.getCurrentStep();

    if (step != lastDrawnStep)
    {
        lastDrawnStep = step;
        kickRow.repaint();
    }
}

void MainComponent::updatePlayButton()
{
    const bool on = engine.isPlaying();
    playButton.setButtonText (on ? "STOP" : "PLAY");
    playButton.setToggleState (on, juce::dontSendNotification);
}

void MainComponent::updateBpmLabel()
{
    bpmLabel.setText (juce::String (juce::roundToInt (engine.getBpm())) + " BPM", juce::dontSendNotification);
}

void MainComponent::updateInfoLabel()
{
    if (audioError.isNotEmpty())
    {
        infoLabel.setColour (juce::Label::textColourId, juce::Colours::red);
        infoLabel.setText ("Errore audio: " + audioError, juce::dontSendNotification);
        return;
    }

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        infoLabel.setText ("Nessun dispositivo audio attivo", juce::dontSendNotification);
        return;
    }

    const double sr = device->getCurrentSampleRate();
    const int buffer = device->getCurrentBufferSizeSamples();
    const double ms = sr > 0.0 ? 1000.0 * buffer / sr : 0.0;

    infoLabel.setText (device->getTypeName() + "  |  " + juce::String (juce::roundToInt (sr)) + " Hz  |  buffer "
                         + juce::String (buffer) + " campioni (" + juce::String (ms, 1) + " ms)",
                       juce::dontSendNotification);
}
