#include "MainComponent.h"
#include "Theme.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);

    // Trasporto
    playButton.onClick = [this]
    {
        engine.setPlaying (! engine.isPlaying());
        playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);
    };
    addAndMakeVisible (playButton);

    bpmDisplay.getBpm = [this] { return engine.getBpm(); };
    bpmDisplay.setBpm = [this] (double v) { engine.setBpm (v); };
    addAndMakeVisible (bpmDisplay);

    // Volume master
    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterKnob.setRange (0.0, 1.5);
    masterKnob.setValue (engine.getMasterVolume(), juce::dontSendNotification);
    masterKnob.setDoubleClickReturnValue (true, 1.0);
    masterKnob.setMouseDragSensitivity (160);
    masterKnob.onValueChange = [this] { engine.setMasterVolume ((float) masterKnob.getValue()); };
    addAndMakeVisible (masterKnob);

    addAndMakeVisible (meter);

    // Channel rack
    for (int c = 0; c < SequencerEngine::numChannels; ++c)
        addAndMakeVisible (rows.add (new ChannelRow (engine, c)));

    stepLeds.getColumnBounds = [this] (int i)
    {
        auto* row = rows.getFirst();
        auto r = row->getPadBounds (i).translated ((float) row->getX(), 0.0f);
        return r.translated (-(float) stepLeds.getX(), 0.0f);
    };
    addAndMakeVisible (stepLeds);

    infoLabel.setColour (juce::Label::textColourId, Theme::textDim);
    infoLabel.setJustificationType (juce::Justification::centredLeft);
    infoLabel.setFont (Theme::font (13.0f));
    addAndMakeVisible (infoLabel);

    // Avvio dell'audio: 0 ingressi, 2 uscite (stereo).
    audioError = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&engine);
    updateInfoLabel();

    startTimerHz (40);
    setSize (900, 440);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Rectangle<int> MainComponent::getContentArea() const
{
    auto area = getLocalBounds();

    // Su Android rispetta notch e barre di sistema.
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = display->safeAreaInsets.subtractedFrom (area);

    return area.reduced (10, 8);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (Theme::bgTop, 0.0f, 0.0f, Theme::bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Barra superiore
    g.setColour (Theme::panel.withAlpha (0.9f));
    g.fillRoundedRectangle (headerArea, 10.0f);
    g.setColour (Theme::outline.withAlpha (0.6f));
    g.drawRoundedRectangle (headerArea, 10.0f, 1.0f);

    // Logo con gradiente
    {
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (Theme::font (logoArea.getHeight() * 0.62f, true), "DAWFL",
                              logoArea.getX(), logoArea.getY(), logoArea.getWidth(), logoArea.getHeight(),
                              juce::Justification::centredLeft, 1);
        juce::Path p;
        glyphs.createPath (p);

        auto b = p.getBounds();
        g.setColour (Theme::accent.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setGradientFill (juce::ColourGradient (Theme::accent2, b.getX(), b.getY(), Theme::accent, b.getRight(), b.getBottom(), false));
        g.fillPath (p);
    }

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (11.0f, true));
    g.drawText ("MASTER", masterLabelArea, juce::Justification::centred);
}

void MainComponent::resized()
{
    auto area = getContentArea();

    // --- Barra superiore
    const int headerH = juce::jlimit (48, 68, area.getHeight() / 6);
    auto header = area.removeFromTop (headerH);
    headerArea = header.toFloat();
    header.reduce (10, 6);

    logoArea = header.removeFromLeft (juce::roundToInt (headerH * 2.6)).toFloat();
    header.removeFromLeft (8);

    const int ctlH = header.getHeight();
    playButton.setBounds (header.removeFromLeft (juce::roundToInt (ctlH * 1.5)));
    header.removeFromLeft (10);
    bpmDisplay.setBounds (header.removeFromLeft (juce::roundToInt (ctlH * 2.6)));

    meter.setBounds (header.removeFromRight (14));
    header.removeFromRight (8);
    masterKnob.setBounds (header.removeFromRight (ctlH));
    masterLabelArea = header.removeFromRight (52).toFloat();

    // --- Riga in basso con le info audio
    area.removeFromTop (6);
    infoLabel.setBounds (area.removeFromBottom (18));

    // --- LED dei passi + righe del channel rack
    stepLeds.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    const int gap = 5;
    const int rowH = (area.getHeight() - gap * (SequencerEngine::numChannels - 1)) / SequencerEngine::numChannels;

    for (auto* row : rows)
    {
        row->setBounds (area.removeFromTop (rowH));
        area.removeFromTop (gap);
    }

    stepLeds.repaint();
}

//==============================================================================
void MainComponent::timerCallback()
{
    const int step = engine.getCurrentStep();

    for (auto* row : rows)
        row->refresh (step);

    stepLeds.setCurrentStep (step);
    meter.setLevel (engine.takeOutputPeak());

    if (playButton.getToggleState() != engine.isPlaying())
        playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);
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
