#include "PluginEditor.h"

namespace ui = bf::ui;

BassForgeAudioProcessorEditor::BassForgeAudioProcessorEditor(BassForgeAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p),
      ring(p.apvts, p.getModMonitor()),
      scope(p.getScope()),
      masterBar(p.apvts, bf::ids::masterGain),
      presetBar(p.getPresets()),
      oscPage(p.apvts, ring, p.getWavetables(), p.getModMonitor()),
      filterPage(p.apvts, ring),
      modPage(p.apvts, ring, p.getPerformers()),
      fxPage(p.apvts, ring)
{
    setLookAndFeel(&laf);

    const char* names[4] = { "OSCILLATORS", "FILTERS", "MODULATORS", "FX" };
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setButtonText(names[i]);
        tabs[i].setClickingTogglesState(true);
        tabs[i].setRadioGroupId(1001);
        tabs[i].onClick = [this, i] { showPage(i); };
        addAndMakeVisible(tabs[i]);
    }
    addAndMakeVisible(scope);
    masterLabel.setText("MASTER", juce::dontSendNotification);
    masterLabel.setJustificationType(juce::Justification::centredRight);
    masterLabel.setColour(juce::Label::textColourId, ui::col::textDim);
    addAndMakeVisible(masterLabel);
    addAndMakeVisible(masterBar);
    limiterBtn.setClickingTogglesState(true);
    limiterBtn.setTooltip("Master limiter (brickwall -0.3 dBFS)");
    limiterAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, bf::ids::masterLimiter, limiterBtn);
    addAndMakeVisible(limiterBtn);
    addAndMakeVisible(presetBar);

    pages[0] = &oscPage; pages[1] = &filterPage; pages[2] = &modPage; pages[3] = &fxPage;
    for (auto* pg : pages) addChildComponent(*pg);
    tabs[0].setToggleState(true, juce::dontSendNotification);
    showPage(0);

    constrainer.setFixedAspectRatio((double) kBaseW / (double) kBaseH);
    constrainer.setSizeLimits(kBaseW * 3 / 4, kBaseH * 3 / 4, kBaseW * 3 / 2, kBaseH * 3 / 2);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(kBaseW, kBaseH);

    startTimerHz(30);
}

BassForgeAudioProcessorEditor::~BassForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void BassForgeAudioProcessorEditor::timerCallback()
{
    ring.update();
}

void BassForgeAudioProcessorEditor::showPage(int index)
{
    current = juce::jlimit(0, 3, index);
    for (int i = 0; i < 4; ++i) pages[i]->setVisible(i == current);
}

void BassForgeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::col::bg);
    auto head = getLocalBounds().removeFromTop(getHeight() / 11);
    g.setGradientFill(juce::ColourGradient(ui::col::panelHi, 0.0f, 0.0f, ui::col::bg, 0.0f, (float) head.getHeight(), false));
    g.fillRect(head);
    g.setColour(ui::col::edge);
    g.drawHorizontalLine(head.getBottom() - 1, 0.0f, (float) getWidth());

    const float s = (float) getHeight() / (float) kBaseH;
    g.setColour(ui::col::text);
    g.setFont(juce::Font(juce::FontOptions(26.0f * s, juce::Font::bold)));
    g.drawText("BASS", (int) (18 * s), 0, (int) (80 * s), head.getHeight(), juce::Justification::centredLeft);
    g.setColour(ui::col::cyan);
    g.drawText("FORGE", (int) (84 * s), 0, (int) (110 * s), head.getHeight(), juce::Justification::centredLeft);
    g.setColour(ui::col::orange);
    g.fillRoundedRectangle(18 * s, (float) head.getBottom() - 6.0f * s, 176.0f * s, 2.5f * s, 1.2f * s);
}

void BassForgeAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    auto head = b.removeFromTop(getHeight() / 11);
    const float s = (float) getHeight() / (float) kBaseH;

    auto h = head.reduced((int) (12 * s), (int) (6 * s));
    h.removeFromLeft((int) (200 * s));                         // logo
    masterBar.setBounds(h.removeFromRight((int) (150 * s)).withSizeKeepingCentre((int) (150 * s), (int) (26 * s)));
    masterLabel.setBounds(h.removeFromRight((int) (58 * s)));
    limiterBtn.setBounds(h.removeFromRight((int) (62 * s)).withSizeKeepingCentre((int) (56 * s), (int) (24 * s)));
    h.removeFromRight((int) (10 * s));
    scope.setBounds(h.removeFromRight((int) (190 * s)).reduced(0, (int) (2 * s)));
    h.removeFromRight((int) (14 * s));
    for (auto& t : tabs) t.setBounds(h.removeFromLeft((int) (140 * s)));

    auto sub = b.removeFromTop((int) (34 * s)).reduced((int) (14 * s), (int) (2 * s));
    presetBar.setBounds(sub.removeFromLeft((int) (500 * s)));
    for (auto* pg : pages) pg->setBounds(b);
}
