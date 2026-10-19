#pragma once

#include <JuceHeader.h>
#include <functional>

//==============================================================================
/**
    Schermata di presentazione all'avvio: il logo JLoop appare su nero con un leggero
    ingrandimento e un bagliore, resta un attimo e poi sfuma rivelando l'app.
    Un tocco la salta.
*/
class JLoopSplash : public juce::Component,
                     private juce::Timer
{
public:
    JLoopSplash()
    {
        logo = juce::ImageCache::getFromMemory (BinaryData::splash_logo_png, BinaryData::splash_logo_pngSize);
        setOpaque (false);
        setInterceptsMouseClicks (true, false);
        start = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }

    std::function<void()> onFinished;

    void mouseDown (const juce::MouseEvent&) override
    {
        // Salta: passa subito alla dissolvenza finale
        const double now = elapsed();
        if (now < fadeOutStart)
            start -= (fadeOutStart - now) * 1000.0;
    }

    void paint (juce::Graphics& g) override
    {
        const double t = elapsed();
        const float screenAlpha = t < fadeOutStart ? 1.0f : (float) juce::jlimit (0.0, 1.0, 1.0 - (t - fadeOutStart) / fadeOutTime);
        g.setColour (juce::Colours::black.withAlpha (screenAlpha));
        g.fillAll();

        if (! logo.isValid())
            return;

        // Comparsa: dissolvenza + leggero zoom (0.9 -> 1.0) con rallentamento finale
        const float in = (float) juce::jlimit (0.0, 1.0, (t - 0.15) / fadeInTime);
        const float ease = 1.0f - std::pow (1.0f - in, 3.0f);
        const float scale = 0.9f + 0.1f * ease + (t > fadeOutStart ? 0.06f * (1.0f - screenAlpha) : 0.0f);
        const float alpha = ease * screenAlpha;

        const float side = (float) juce::jmin (getWidth(), getHeight()) * 0.62f * scale;
        const float aspect = (float) logo.getWidth() / (float) logo.getHeight();
        auto area = juce::Rectangle<float> (side * aspect, side).withCentre (getLocalBounds().toFloat().getCentre());

        // Bagliore che "respira" mentre il logo e' fermo
        const float pulse = 0.5f + 0.5f * std::sin ((float) t * 3.2f);
        const float glowR = side * (0.75f + 0.08f * pulse);
        juce::ColourGradient glow (juce::Colour (0xffff7a1a).withAlpha (0.20f * alpha), area.getCentreX(), area.getCentreY(),
                                   juce::Colour (0xffff7a1a).withAlpha (0.0f), area.getCentreX() + glowR, area.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (area.withSizeKeepingCentre (glowR * 2.0f, glowR * 2.0f));

        g.setOpacity (alpha);
        g.drawImage (logo, area, juce::RectanglePlacement::centred);
    }

private:
    double elapsed() const   { return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0; }

    void timerCallback() override
    {
        repaint();
        if (elapsed() >= fadeOutStart + fadeOutTime)
        {
            stopTimer();
            setVisible (false);
            if (onFinished)
                juce::MessageManager::callAsync (onFinished);   // chi la possiede la elimina fuori da questo callback
        }
    }

    static constexpr double fadeInTime = 0.7, fadeOutStart = 2.2, fadeOutTime = 2.55;
    juce::Image logo;
    double start = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JLoopSplash)
};
