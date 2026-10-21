#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"
#include "ChannelRow.h"

//==============================================================================
/** Manopola con etichetta sopra e valore sotto (il valore si aggiorna mentre giri). */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::String labelText, double min, double max, double defaultValue,
                 std::function<juce::String (double)> formatter)
        : label (std::move (labelText)), format (std::move (formatter))
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (min, max);
        slider.setDoubleClickReturnValue (true, defaultValue);
        slider.setMouseDragSensitivity (180);
        slider.onValueChange = [this] { repaint(); if (onChange) onChange (slider.getValue()); };
        slider.onDragEnd     = [this] { if (onRelease) onRelease(); };
        addAndMakeVisible (slider);
    }

    std::function<void (double)> onChange;
    std::function<void()> onRelease;
    juce::Slider slider;

    void setValue (double v)        { slider.setValue (v, juce::dontSendNotification); repaint(); }
    void setColour (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (14);
        r.removeFromBottom (16);
        slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawCaption (g, label, r.removeFromTop (14));
        g.setColour (slider.isMouseButtonDown() ? Theme::accent : Theme::text);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (format ? format (slider.getValue()) : juce::String (slider.getValue(), 2),
                    r.removeFromBottom (16), juce::Justification::centred, false);
    }

private:
    juce::String label;
    std::function<juce::String (double)> format;
};

//==============================================================================
/**
    Pannello "impostazioni del canale" (come in FL Studio): modella il suono del preset
    e regola come entra nel mix. Si apre sopra il channel rack.
*/
class ChannelEditor : public juce::Component
{
public:
    explicit ChannelEditor (SequencerEngine& e) : engine (e)
    {

        add (pitch,    [this] (double v) { ch().pitch.store ((float) v);    ch().touchSound(); }, true);
        add (decay,    [this] (double v) { ch().decay.store ((float) v);    ch().touchSound(); }, true);
        add (tone,     [this] (double v) { ch().tone.store ((float) v);     ch().touchSound(); }, true);
        add (drive,    [this] (double v) { ch().drive.store ((float) v);    ch().touchSound(); }, true);
        add (humanize, [this] (double v) { ch().humanize.store ((float) v); }, false);
        add (volume,   [this] (double v) { ch().volume.store ((float) v);   }, false);
        add (pan,      [this] (double v) { ch().pan.store ((float) v);      }, false);
        add (filter,   [this] (double v) { ch().filter.store ((float) v);   }, true);
        add (reverb,   [this] (double v) { ch().reverb.store ((float) v);   }, true);
        add (delay,    [this] (double v) { ch().delay.store ((float) v);    }, true);

        for (auto* b : { &prevButton, &nextButton, &kitButton, &previewButton, &resetButton, &closeButton })
            addAndMakeVisible (b);

        prevButton.onClick    = [this] { const int n = engine.getNumChannels(); setChannel ((channel + n - 1) % n); };
        nextButton.onClick    = [this] { setChannel ((channel + 1) % engine.getNumChannels()); };
        previewButton.onClick = [this] { engine.previewChannel (channel); };
        resetButton.onClick   = [this]
        {
            const int kit = engine.getChannelKit (channel);
            const float vol = ch().volume.load();
            const bool muted = ch().muted.load();
            engine.resetChannel (channel, vol);
            engine.setChannelKit (channel, kit);
            ch().muted.store (muted);
            refreshFromEngine();
            engine.previewChannel (channel);
        };
        closeButton.onClick   = [this] { if (onClose) onClose(); };

        for (auto* k : { &pitch, &tone, &pan })
            k->slider.getProperties().set ("bipolar", true);
        kitButton.onClick     = [this] { showKitMenu(); };
    }

    std::function<void()> onClose;
    std::function<juce::PopupMenu (int channel)> buildSoundMenu;
    std::function<void (int channel, int result)> handleSoundMenu;
    std::function<void (int channel)> onChangeSound;      // apre il browser dei suoni

    void setChannel (int newChannel)
    {
        channel = juce::jlimit (0, engine.getNumChannels() - 1, newChannel);
        const auto colour = juce::Colour (ch().colour.load());

        for (auto* k : soundKnobs)
            k->setColour (colour);

        // Per gli strumenti il suono si modella dentro il synth: qui restano solo mix ed effetti
        const bool synth = engine.isSynth (channel);
        for (auto* k : { &pitch, &decay, &tone, &drive, &humanize })
            k->setVisible (! synth);
        resetButton.setVisible (! synth);
        filter.setVisible (! synth);

        refreshFromEngine();
        repaint();
    }

    int getChannel() const      { return channel; }

    /** Rilegge i valori dal motore (dopo undo, caricamento, ecc.). */
    void refreshFromEngine()
    {
        auto& c = ch();
        pitch.setValue (c.pitch.load());
        decay.setValue (c.decay.load());
        tone.setValue (c.tone.load());
        drive.setValue (c.drive.load());
        humanize.setValue (c.humanize.load());
        volume.setValue (c.volume.load());
        pan.setValue (c.pan.load());
        filter.setValue (c.filter.load());
        reverb.setValue (c.reverb.load());
        delay.setValue (c.delay.load());
        kitButton.setButtonText (ChannelRow::getSoundLabel (engine, channel) + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawGlassPanel (g, r);

        // Intestazione: colore e nome del canale
        auto title = titleArea;
        g.setColour (juce::Colour (engine.getChannel (channel).colour.load()));
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 4.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (17.0f, true));
        g.drawText (engine.getChannel (channel).name, title, juce::Justification::centredLeft, false);

        // Separatori delle sezioni
        Theme::drawCaption (g, engine.isSynth (channel) ? "Suono: apri il synth dal menu del canale" : "Suono",
                            soundCaption, juce::Justification::centredLeft);
        Theme::drawCaption (g, "Mix ed effetti", mixCaption, juce::Justification::centredLeft);
        g.setColour (Theme::border);
        g.drawHorizontalLine ((int) soundCaption.getY(), soundCaption.getX(), getWidth() - 10.0f);
        g.drawHorizontalLine ((int) mixCaption.getY(), mixCaption.getX(), getWidth() - 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);

        auto top = r.removeFromTop (34);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        top.removeFromRight (6);
        resetButton.setBounds (top.removeFromRight (70).reduced (0, 2));
        top.removeFromRight (6);
        previewButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        top.removeFromRight (12);
        nextButton.setBounds (top.removeFromRight (34).reduced (0, 2));
        top.removeFromRight (4);
        kitButton.setBounds (top.removeFromRight (130).reduced (0, 2));
        top.removeFromRight (4);
        prevButton.setBounds (top.removeFromRight (34).reduced (0, 2));
        titleArea = top.toFloat();

        r.removeFromTop (4);
        const int rowH = (r.getHeight() - 32) / 2;

        soundCaption = r.removeFromTop (16).toFloat();
        layoutRow (r.removeFromTop (rowH), { &pitch, &decay, &tone, &drive, &humanize });
        mixCaption = r.removeFromTop (16).toFloat();
        layoutRow (r.removeFromTop (rowH), { &volume, &pan, &filter, &reverb, &delay });
    }

private:
    SequencerEngine::Channel& ch()  { return engine.getChannel (channel); }

    void add (LabeledKnob& k, std::function<void (double)> setter, bool previewOnRelease)
    {
        k.onChange = std::move (setter);
        if (previewOnRelease)
            k.onRelease = [this] { engine.previewChannel (channel); };
        addAndMakeVisible (k);

        if (&k == &pitch || &k == &decay || &k == &tone || &k == &drive)
            soundKnobs.add (&k);
        else
            k.setColour (Theme::text.darker (0.2f));
    }

    static void layoutRow (juce::Rectangle<int> area, std::initializer_list<LabeledKnob*> knobs)
    {
        const int w = area.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
            k->setBounds (area.removeFromLeft (w).reduced (4, 0));
    }

    void showKitMenu()
    {
        if (! engine.isSynth (channel))
        {
            if (onChangeSound)
                onChangeSound (channel);
            return;
        }

        if (! buildSoundMenu)
            return;

        auto menu = buildSoundMenu (channel);
        juce::Component::SafePointer<ChannelEditor> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitButton), [safeThis] (int result)
        {
            if (safeThis == nullptr || result <= 0 || ! safeThis->handleSoundMenu)
                return;
            safeThis->handleSoundMenu (safeThis->channel, result);
            safeThis->refreshFromEngine();
        });
    }

    static juce::String signedFmt (double v, const juce::String& unit, int decimals = 0)
    {
        return (v > 0.0001 ? "+" : "") + juce::String (v, decimals) + unit;
    }

    SequencerEngine& engine;
    int channel = 0;

    LabeledKnob pitch    { "Pitch",    -12.0, 12.0, 0.0, [] (double v) { return signedFmt (std::round (v * 10.0) / 10.0, " st", 1); } };
    LabeledKnob decay    { "Decay",    0.25,  3.0,  1.0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } };
    LabeledKnob tone     { "Tono",     -1.0,  1.0,  0.0, [] (double v) { return signedFmt (juce::roundToInt (v * 100.0), ""); } };
    LabeledKnob drive    { "Drive",    0.0,   1.0,  0.0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } };
    LabeledKnob humanize { "Humanize", 0.0,   1.0,  0.15, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } };
    LabeledKnob volume   { "Volume",   0.0,   1.0,  0.8, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } };
    LabeledKnob pan      { "Pan",      -1.0,  1.0,  0.0, [] (double v)
                           {
                               const int p = juce::roundToInt (v * 100.0);
                               return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
                           } };
    LabeledKnob filter   { "Filtro",   0.0,   1.0,  1.0, [] (double v)
                           {
                               if (v >= 0.999) return juce::String ("Aperto");
                               const double hz = 120.0 * std::pow (20000.0 / 120.0, v);
                               return hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
                           } };
    LabeledKnob reverb   { "Riverbero", 0.0,  1.0,  0.0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } };
    LabeledKnob delay    { "Delay",    0.0,   1.0,  0.0, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } };

    juce::Array<LabeledKnob*> soundKnobs;

    juce::TextButton prevButton { "<" }, nextButton { ">" }, kitButton, previewButton { "Ascolta" },
                     resetButton { "Reset" }, closeButton { "Chiudi" };

    juce::Rectangle<float> titleArea, soundCaption, mixCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelEditor)
};
