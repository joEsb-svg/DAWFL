#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"
#include "Widgets.h"
#include "Playlist.h"

//==============================================================================
/**
    Modalita' live: si suona "a pattern", come con un controller.
    - in alto un pad per ogni pattern: toccandolo parte quel pattern (a fine giro, o subito)
    - sotto un pulsante per ogni canale: accende e spegne il canale al volo
    Il pattern in riproduzione ha la barra di avanzamento, quello in attesa lampeggia.
*/
class LivePanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit LivePanel (SequencerEngine& e) : engine (e)
    {
        addAndMakeVisible (quantSwitch);
        quantSwitch.setSelected (0);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
        stopButton.onClick = [this] { engine.setPlaying (false); };
        addAndMakeVisible (stopButton);
    }

    std::function<void()> onClose, onChanged;

    void visibilityChanged() override
    {
        if (isVisible()) startTimerHz (30);
        else             stopTimer();
    }

    /** Per i test: tocco su un pad. */
    void launchForTest (int pattern)    { launch (pattern); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Theme::accent);
        g.fillRect (titleArea.withWidth (4).toFloat().reduced (0.0f, 6.0f));
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (17.0f, true));
        g.drawText ("Live", titleArea.withTrimmedLeft (14), juce::Justification::centredLeft);

        Theme::drawCaption (g, "Pattern  (tocca per lanciarlo)", patternCaption.toFloat(), juce::Justification::centredLeft);
        Theme::drawCaption (g, "Canali  (tocca per spegnere / accendere)", channelCaption.toFloat(), juce::Justification::centredLeft);

        const bool playing = engine.isPlaying() && ! engine.isSongMode();
        const int playingPattern = engine.getPlayingPattern(), queued = engine.getCurrentPattern();
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;

        for (int i = 0; i < (int) patternPads.size(); ++i)
        {
            const int p = patternPads[(size_t) i].pattern;
            auto r = patternPads[(size_t) i].bounds.toFloat().reduced (3.0f);
            const auto colour = PatternColours::get (p);
            const bool empty = engine.isPatternEmpty (p);
            const bool isPlaying = playing && p == playingPattern;
            const bool isQueued = playing && p == queued && p != playingPattern;

            Theme::drawRaised (g, r, isPlaying ? colour : colour.withMultipliedSaturation (0.55f).withMultipliedBrightness (empty ? 0.35f : 0.55f), 5.0f);

            if (isPlaying)
            {
                // Avanzamento nel giro
                const int len = juce::jmax (1, engine.getPattern (p).length.load());
                const float progress = (float) (juce::jmax (0, engine.getCurrentStep()) + 1) / (float) len;
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (r.getX() + 6.0f, r.getBottom() - 8.0f, (r.getWidth() - 12.0f) * progress, 3.0f);
            }
            if (isQueued && blink)
            {
                g.setColour (Theme::textBright);
                g.drawRoundedRectangle (r.reduced (1.5f), 5.0f, 3.0f);
            }

            g.setColour (isPlaying ? Theme::bg : Theme::textBright.withAlpha (empty ? 0.4f : 0.95f));
            g.setFont (Theme::font (juce::jmin (16.0f, r.getHeight() * 0.28f), true));
            g.drawFittedText (engine.getPatternName (p), r.reduced (8.0f, 6.0f).toNearestInt(), juce::Justification::centred, 2);
            if (isQueued)
            {
                g.setFont (Theme::font (11.0f));
                g.drawText ("in attesa", r.reduced (6.0f, 4.0f), juce::Justification::centredBottom);
            }
        }

        for (int i = 0; i < (int) channelPads.size(); ++i)
        {
            const int c = channelPads[(size_t) i].channel;
            if (c >= engine.getNumChannels())
                continue;
            auto r = channelPads[(size_t) i].bounds.toFloat().reduced (3.0f);
            auto& ch = engine.getChannel (c);
            const bool on = ! ch.muted.load();
            const auto colour = juce::Colour (ch.colour.load());
            const float activity = ch.activity.load();
            Theme::drawRaised (g, r, on ? colour.withMultipliedBrightness (0.55f + 0.45f * activity) : Theme::raised.darker (0.3f), 4.0f);
            g.setColour (on ? Theme::textBright : Theme::textDim);
            g.setFont (Theme::font (13.0f, on));
            g.drawFittedText (ch.name, r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::centred, 2);
            if (! on)
            {
                g.setFont (Theme::font (10.0f));
                g.drawText ("SPENTO", r.reduced (4.0f, 2.0f), juce::Justification::centredBottom);
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (36);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        top.removeFromRight (6);
        stopButton.setBounds (top.removeFromRight (70).reduced (0, 2));
        top.removeFromRight (6);
        if (getWidth() < 560)
        {
            // Telefono in verticale: la scelta del cambio va su una riga sua
            titleArea = top;
            r.removeFromTop (4);
            quantSwitch.setBounds (r.removeFromTop (32));
        }
        else
        {
            quantSwitch.setBounds (top.removeFromRight (juce::jmin (220, top.getWidth() / 2)).reduced (0, 3));
            titleArea = top;
        }
        r.removeFromTop (4);
        layoutPads (r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 12)
            return;
        for (auto& pad : patternPads)
            if (pad.bounds.contains (e.getPosition()))
            {
                launch (pad.pattern);
                return;
            }
        for (auto& pad : channelPads)
            if (pad.bounds.contains (e.getPosition()) && pad.channel < engine.getNumChannels())
            {
                auto& m = engine.getChannel (pad.channel).muted;
                m.store (! m.load());
                if (onChanged) onChanged();
                repaint();
                return;
            }
    }

private:
    struct PatternPad { int pattern; juce::Rectangle<int> bounds; };
    struct ChannelPad { int channel; juce::Rectangle<int> bounds; };

    void launch (int pattern)
    {
        if (engine.isSongMode())
            engine.setSongMode (false);
        const bool wasPlaying = engine.isPlaying();
        engine.selectPattern (pattern);
        if (! wasPlaying)
            engine.setPlaying (true);
        else if (quantSwitch.getSelected() == 1)
            engine.switchPatternNow();
        if (onChanged) onChanged();
        repaint();
    }

    void layoutPads (juce::Rectangle<int> area)
    {
        // Pattern: quelli usati (almeno 4 pad), canali: tutti
        const int numPatterns = juce::jlimit (4, SequencerEngine::numPatterns, PatternColours::count (engine));
        const int numChannels = engine.getNumChannels();
        lastPatternCount = numPatterns;
        lastChannelCount = numChannels;

        const int channelCols = juce::jmax (4, area.getWidth() / 110);
        const int channelRows = (numChannels + channelCols - 1) / channelCols;
        const int channelH = juce::jmin (54, juce::jmax (40, area.getHeight() / 5));
        auto channelArea = area.removeFromBottom (juce::jmin (channelRows * channelH, area.getHeight() / 2));
        channelCaption = area.removeFromBottom (18);
        area.removeFromBottom (4);
        patternCaption = area.removeFromTop (18);

        const int patternCols = juce::jmax (2, juce::jmin (8, area.getWidth() / 120));
        const int patternRows = (numPatterns + patternCols - 1) / patternCols;
        const int ph = juce::jlimit (48, 150, area.getHeight() / juce::jmax (1, patternRows));
        const int pw = area.getWidth() / patternCols;
        patternPads.clear();
        for (int p = 0; p < numPatterns; ++p)
            patternPads.push_back ({ p, { area.getX() + (p % patternCols) * pw, area.getY() + (p / patternCols) * ph, pw, ph } });
        patternContentHeight = patternRows * ph;

        const int cw = channelArea.getWidth() / channelCols;
        const int ch = channelRows > 0 ? channelArea.getHeight() / channelRows : channelH;
        channelPads.clear();
        for (int c = 0; c < numChannels; ++c)
            channelPads.push_back ({ c, { channelArea.getX() + (c % channelCols) * cw, channelArea.getY() + (c / channelCols) * ch, cw, ch } });
    }

    void timerCallback() override
    {
        // Pattern o canali aggiunti nel frattempo: si ridisegna la griglia
        if (juce::jlimit (4, SequencerEngine::numPatterns, PatternColours::count (engine)) != lastPatternCount
            || engine.getNumChannels() != lastChannelCount)
            resized();
        repaint();
    }

    SequencerEngine& engine;
    SegmentedControl quantSwitch { { "Fine giro", "Subito" } };
    juce::TextButton closeButton { "Chiudi" }, stopButton { "Stop" };
    juce::Rectangle<int> titleArea, patternCaption, channelCaption;
    std::vector<PatternPad> patternPads;
    std::vector<ChannelPad> channelPads;
    int lastPatternCount = -1, lastChannelCount = -1, patternContentHeight = 0;
};
