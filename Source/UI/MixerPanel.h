#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"
#include "Widgets.h"
#include "ChannelEditor.h"   // LabeledKnob

//==============================================================================
/** Conversione volume della traccia (guadagno) <-> dB del fader. */
namespace MixerFader
{
    inline constexpr double minDb = -60.0, maxDb = 4.0;
    inline double gainToDb (float g)     { return g <= 0.001f ? minDb : juce::jlimit (minDb, maxDb, (double) juce::Decibels::gainToDecibels (g)); }
    inline float dbToGain (double db)    { return db <= minDb + 0.01 ? 0.0f : juce::Decibels::decibelsToGain ((float) db); }
    inline juce::String text (double db) { return db <= minDb + 0.01 ? juce::String ("-inf") : (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB"; }
}

//==============================================================================
/** Una striscia del mixer: nome, effetti, panorama, fader con meter, muto e solo. */
class MixerStrip : public juce::Component
{
public:
    MixerStrip (SequencerEngine& e, int trackIndex) : engine (e), track (trackIndex)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (MixerFader::minDb, MixerFader::maxDb);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setColour (juce::Slider::trackColourId, Theme::raisedHi);
        fader.onValueChange = [this]
        {
            if (track == 0)
                engine.setMasterVolume (juce::jmin (1.5f, MixerFader::dbToGain (fader.getValue())));   // = la manopola Master
            else
                engine.getTrack (track).volume.store (MixerFader::dbToGain (fader.getValue()));
            repaint();
        };
        addAndMakeVisible (fader);

        pan.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setMouseDragSensitivity (160);
        pan.onValueChange = [this] { engine.getTrack (track).pan.store ((float) pan.getValue()); };
        addAndMakeVisible (pan);

        for (auto* b : { &muteButton, &soloButton })
        {
            b->setClickingTogglesState (true);
            addAndMakeVisible (b);
        }
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc94a4f));
        soloButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent.darker (0.2f));
        muteButton.onClick = [this] { engine.getTrack (track).muted.store (muteButton.getToggleState()); };
        soloButton.onClick = [this] { engine.getTrack (track).solo.store (soloButton.getToggleState()); };
        muteButton.setVisible (track > 0);
        soloButton.setVisible (track > 0);
        pan.setVisible (track > 0);

        refresh();
    }

    std::function<void (int track)> onOpenEffects, onNameTap;
    int getTrack() const    { return track; }

    void setSelected (bool s)       { if (selected != s) { selected = s; repaint(); } }

    /** Rilegge le impostazioni (undo, caricamento) senza disturbare un dito che sta muovendo. */
    void refresh()
    {
        auto& tr = engine.getTrack (track);
        const float vol = track == 0 ? engine.getMasterVolume() : tr.volume.load();
        if (! fader.isMouseButtonDown())
            fader.setValue (MixerFader::gainToDb (vol), juce::dontSendNotification);
        if (! pan.isMouseButtonDown())
            pan.setValue (tr.pan.load(), juce::dontSendNotification);
        muteButton.setToggleState (tr.muted.load(), juce::dontSendNotification);
        soloButton.setToggleState (tr.solo.load(), juce::dontSendNotification);

        juce::String fx;
        int count = 0;
        for (auto& s : tr.slots)
            if (const int type = s.type.load(); type != mixer::none)
            {
                ++count;
                fx << (fx.isEmpty() ? "" : "\n") << mixer::info (type).name << (s.bypass.load() ? " (off)" : "");
            }
        const bool eq = tr.eqLow.load() != 0.0f || tr.eqMid.load() != 0.0f || tr.eqHigh.load() != 0.0f;
        const auto name = engine.getTrackName (track);
        const auto colour = engine.getTrackColour (track);
        if (fx != fxText || name != nameText || colour != nameColour || eq != eqOn)
        {
            fxText = fx; nameText = name; nameColour = colour; eqOn = eq; numFx = count;
            repaint();
        }
    }

    void setLevel (float peak)
    {
        const float l = juce::jmax (peak, level * 0.82f);
        if (std::abs (l - level) > 0.002f)
        {
            level = l;
            repaint (meterArea.toNearestInt().expanded (1));
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
        Theme::drawRaised (g, r, selected ? Theme::raisedHi : Theme::raised);
        if (selected)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (r.reduced (0.5f), Theme::corner, 1.5f);
        }

        // Nome con il colore della traccia
        g.setColour (nameColour);
        g.fillRect (nameArea.removeFromTop (3.0f).withTrimmedLeft (2).withTrimmedRight (2));
        auto name = nameArea;
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.0f, true));
        g.drawText (track == 0 ? juce::String ("M") : juce::String (track), name.removeFromTop (12.0f), juce::Justification::centred, false);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (12.0f, true));
        g.drawFittedText (nameText, name.toNearestInt(), juce::Justification::centredTop, 2, 0.8f);

        // Effetti (tocca per aprirli)
        Theme::drawSunken (g, fxArea, Theme::lcdBg, 2.0f);
        g.setFont (Theme::font (10.0f));
        if (numFx == 0 && ! eqOn)
        {
            g.setColour (Theme::textDim);
            g.drawFittedText ("+ FX", fxArea.toNearestInt(), juce::Justification::centred, 1);
        }
        else
        {
            g.setColour (Theme::lcdText);
            auto lines = juce::StringArray::fromLines (fxText);
            if (eqOn) lines.insert (0, "EQ");
            auto area = fxArea.reduced (3.0f, 2.0f);
            const int maxLines = juce::jmax (1, (int) (area.getHeight() / 11.0f));
            for (int i = 0; i < juce::jmin (lines.size(), maxLines); ++i)
                g.drawText (i == maxLines - 1 && lines.size() > maxLines ? "+" + juce::String (lines.size() - i) + " ..." : lines[i],
                            area.removeFromTop (11.0f), juce::Justification::centredLeft, true);
        }

        // Valore del fader
        g.setColour (fader.isMouseButtonDown() ? Theme::accent : Theme::text);
        g.setFont (Theme::font (10.0f, true));
        g.drawText (MixerFader::text (fader.getValue()), valueArea, juce::Justification::centred, false);

        // Meter
        Theme::drawSunken (g, meterArea, Theme::lcdBg, 2.0f);
        auto inner = meterArea.reduced (1.5f);
        const float h = inner.getHeight() * juce::jlimit (0.0f, 1.0f, std::sqrt (level));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0483e), inner.getX(), inner.getY(),
                                                 Theme::led.darker (0.2f), inner.getX(), inner.getBottom(), false));
        g.fillRect (inner.removeFromBottom (h));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (5, 4);
        nameArea = r.removeFromTop (44).toFloat();
        r.removeFromTop (3);
        fxArea = r.removeFromTop (juce::jlimit (26, 70, getHeight() / 6)).toFloat();
        r.removeFromTop (3);

        auto buttons = r.removeFromBottom (track > 0 ? 26 : 0);
        if (track > 0)
        {
            muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
            soloButton.setBounds (buttons.reduced (1, 0));
            r.removeFromBottom (3);
        }

        if (track > 0)
            pan.setBounds (r.removeFromTop (34).withSizeKeepingCentre (34, 34));
        valueArea = r.removeFromTop (14).toFloat();

        auto meter = r.removeFromRight (8);
        meterArea = meter.toFloat().reduced (0.0f, 6.0f);
        fader.setBounds (r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        if (fxArea.contains (e.position))       { if (onOpenEffects) onOpenEffects (track); }
        else if (nameArea.contains (e.position)) { if (onNameTap) onNameTap (track); }
    }

private:
    SequencerEngine& engine;
    const int track;
    juce::Slider fader, pan;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Rectangle<float> nameArea, fxArea, meterArea, valueArea;
    juce::String fxText, nameText;
    juce::Colour nameColour;
    bool selected = false, eqOn = false;
    int numFx = 0;
    float level = 0.0f;
};

//==============================================================================
/** Effetti ed EQ di una traccia: 6 slot in fila, e le manopole dello slot scelto. */
class TrackEffectsView : public juce::Component
{
public:
    explicit TrackEffectsView (SequencerEngine& e) : engine (e)
    {
        eqKnobs.add (new LabeledKnob ("Bassi", -15.0, 15.0, 0.0, [] (double v) { return dbText (v); }));
        eqKnobs.add (new LabeledKnob ("Medi", -15.0, 15.0, 0.0, [] (double v) { return dbText (v); }));
        eqKnobs.add (new LabeledKnob ("Freq. medi", 100.0, 10000.0, 1000.0,
                                      [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }));
        eqKnobs.add (new LabeledKnob ("Alti", -15.0, 15.0, 0.0, [] (double v) { return dbText (v); }));
        eqKnobs[2]->slider.setSkewFactorFromMidPoint (1000.0);
        for (int i = 0; i < eqKnobs.size(); ++i)
        {
            eqKnobs[i]->onChange = [this, i] (double v)
            {
                auto& tr = engine.getTrack (track);
                (i == 0 ? tr.eqLow : i == 1 ? tr.eqMid : i == 2 ? tr.eqMidFreq : tr.eqHigh).store ((float) v);
            };
            addAndMakeVisible (eqKnobs[i]);
        }

        for (int s = 0; s < mixer::numSlots; ++s)
        {
            auto* row = slotRows.add (new SlotRow (*this, s));
            addAndMakeVisible (row);
        }
    }

    void setTrack (int t)
    {
        track = juce::jlimit (0, mixer::numTracks - 1, t);
        selectedSlot = 0;
        for (int s = 0; s < mixer::numSlots; ++s)
            if (engine.getTrack (track).slots[(size_t) s].type.load() != mixer::none)
            {
                selectedSlot = s;
                break;
            }
        refresh();
        rebuildParams();
    }

    int getTrack() const        { return track; }
    int getSelectedSlot() const { return selectedSlot; }

    void refresh()
    {
        auto& tr = engine.getTrack (track);
        const float values[4] { tr.eqLow.load(), tr.eqMid.load(), tr.eqMidFreq.load(), tr.eqHigh.load() };
        for (int i = 0; i < 4; ++i)
            if (! eqKnobs[i]->slider.isMouseButtonDown())
                eqKnobs[i]->setValue (values[i]);

        for (auto* row : slotRows)
            row->refresh();

        // Tipo cambiato altrove (undo): ricrea le manopole
        if (engine.getTrack (track).slots[(size_t) selectedSlot].type.load() != shownType)
            rebuildParams();

        for (int i = 0; i < paramKnobs.size(); ++i)
            if (auto* k = paramKnobs[i]; k != nullptr && ! k->slider.isMouseButtonDown())
            {
                const auto& def = mixer::info (shownType).params[(size_t) paramIndex[(size_t) i]];
                k->setValue (mixer::toNormalised (def, tr.slots[(size_t) selectedSlot].params[(size_t) paramIndex[(size_t) i]].load()));
            }
        for (auto* b : paramButtons)
            b->refresh();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawCaption (g, "Equalizzatore", eqCaption);
        Theme::drawCaption (g, "Effetti (dall'alto in basso)", slotsCaption);
        const auto type = engine.getTrack (track).slots[(size_t) selectedSlot].type.load();
        Theme::drawCaption (g, type == mixer::none ? juce::String ("Slot " + juce::String (selectedSlot + 1) + ": vuoto")
                                                   : "Slot " + juce::String (selectedSlot + 1) + ": " + mixer::info (type).name, paramsCaption);
        if (type == mixer::none)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (13.0f));
            g.drawFittedText ("Tocca il nome dello slot per scegliere un effetto.", paramsArea.toNearestInt(), juce::Justification::centred, 3);
        }
        else if (const juce::String detail (mixer::info (type).detail); detail.isNotEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (11.0f));
            g.drawFittedText (detail, detailArea.toNearestInt(), juce::Justification::centredLeft, 1);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const bool wide = r.getWidth() > r.getHeight() * 1.3f;

        juce::Rectangle<int> left, right;
        if (wide)
        {
            left = r.removeFromLeft (juce::jmin (300, r.getWidth() * 2 / 5));
            r.removeFromLeft (10);
            right = r;
        }
        else
        {
            left = r.removeFromTop (16 + mixer::numSlots * 44);
            r.removeFromTop (10);
            right = r;
        }

        // Sinistra: gli slot
        slotsCaption = left.removeFromTop (16).toFloat();
        const int rowH = juce::jlimit (30, 44, left.getHeight() / mixer::numSlots);
        for (auto* row : slotRows)
            row->setBounds (left.removeFromTop (rowH).reduced (0, 2));

        // Destra: EQ in alto, parametri sotto
        eqCaption = right.removeFromTop (16).toFloat();
        auto eq = right.removeFromTop (juce::jlimit (70, 110, right.getHeight() * 2 / 5));
        const int ew = eq.getWidth() / 4;
        for (auto* k : eqKnobs)
            k->setBounds (eq.removeFromLeft (ew).reduced (2, 0));
        right.removeFromTop (8);
        paramsCaption = right.removeFromTop (16).toFloat();
        detailArea = right.removeFromTop (16).toFloat();
        paramsArea = right;
        layoutParams();
    }

private:
    static juce::String dbText (double v)     { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; }

    //==========================================================================
    /** Una riga: numero, nome dell'effetto (tocca per sceglierlo), acceso/spento, su/giu'. */
    class SlotRow : public juce::Component
    {
    public:
        SlotRow (TrackEffectsView& v, int s) : view (v), slot (s)
        {
            nameButton.onClick = [this] { view.selectSlot (slot); view.showTypeMenu (slot, nameButton); };
            power.setClickingTogglesState (true);
            power.onClick = [this]
            {
                view.engine.getTrack (view.track).slots[(size_t) slot].bypass.store (! power.getToggleState());
                view.selectSlot (slot);
            };
            up.onClick = [this] { view.moveSlot (slot, slot - 1); };
            down.onClick = [this] { view.moveSlot (slot, slot + 1); };
            for (auto* c : std::initializer_list<juce::Component*> { &nameButton, &power, &up, &down })
                addAndMakeVisible (c);
        }

        void refresh()
        {
            const auto& s = view.engine.getTrack (view.track).slots[(size_t) slot];
            const int type = s.type.load();
            nameButton.setButtonText (juce::String (slot + 1) + "   " + (type == mixer::none ? juce::String ("(vuoto)") : juce::String (mixer::info (type).name)));
            repaint();
            nameButton.setColour (juce::TextButton::textColourOffId, type == mixer::none ? Theme::textDim : Theme::textBright);
            power.setToggleState (type != mixer::none && ! s.bypass.load(), juce::dontSendNotification);
            power.setEnabled (type != mixer::none);
            up.setEnabled (slot > 0 && type != mixer::none);
            down.setEnabled (slot < mixer::numSlots - 1 && type != mixer::none);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (view.selectedSlot == slot)
            {
                g.setColour (Theme::accent);
                g.drawRoundedRectangle (nameButton.getBounds().toFloat().reduced (0.5f), Theme::corner, 2.0f);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            down.setBounds (r.removeFromRight (30));
            r.removeFromRight (2);
            up.setBounds (r.removeFromRight (30));
            r.removeFromRight (4);
            power.setBounds (r.removeFromRight (66));
            r.removeFromRight (4);
            nameButton.setBounds (r);
        }

    private:
        TrackEffectsView& view;
        const int slot;
        juce::TextButton nameButton;
        LedToggle power;
        juce::TextButton up { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")) }, down { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")) };
    };

    /** Un parametro a scelta (tipo di filtro, tempo del delay, traccia sorgente): un pulsante con il menu. */
    class ChoiceParam : public juce::Component
    {
    public:
        ChoiceParam (TrackEffectsView& v, int p) : view (v), param (p)
        {
            button.onClick = [this] { showMenu(); };
            addAndMakeVisible (button);
            refresh();
        }

        void refresh()
        {
            const auto& def = currentDef();
            const float value = view.engine.getTrack (view.track).slots[(size_t) view.selectedSlot].params[(size_t) param].load();
            button.setButtonText (def.unit == mixer::Unit::track ? view.trackLabel (juce::roundToInt (value)) : mixer::formatValue (def, value));
        }

        void paint (juce::Graphics& g) override     { Theme::drawCaption (g, currentDef().name, getLocalBounds().toFloat().removeFromTop (14)); }
        void resized() override                     { button.setBounds (getLocalBounds().withTrimmedTop (16).withSizeKeepingCentre (getWidth() - 4, juce::jmin (34, getHeight() - 16))); }

    private:
        const mixer::ParamDef& currentDef() const
        {
            return mixer::info (view.engine.getTrack (view.track).slots[(size_t) view.selectedSlot].type.load()).params[(size_t) param];
        }

        void showMenu()
        {
            const auto& def = currentDef();
            const float value = view.engine.getTrack (view.track).slots[(size_t) view.selectedSlot].params[(size_t) param].load();
            juce::PopupMenu menu;
            for (int v = juce::roundToInt (def.minValue); v <= juce::roundToInt (def.maxValue); ++v)
            {
                if (def.unit == mixer::Unit::track)
                {
                    if (v == view.track || ! view.engine.isTrackUsed (v))
                        continue;
                    menu.addColouredItem (v + 1, view.trackLabel (v), view.engine.getTrackColour (v), true, v == juce::roundToInt (value));
                }
                else
                    menu.addItem (v + 1, mixer::formatValue (def, (float) v), true, v == juce::roundToInt (value));
            }
            if (menu.getNumItems() == 0)
                menu.addItem (-1, "(nessuna altra traccia in uso)", false);

            juce::Component::SafePointer<ChoiceParam> safe (this);
            const int slot = view.selectedSlot, track = view.track;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button), [safe, slot, track] (int result)
            {
                if (safe == nullptr || result <= 0)
                    return;
                safe->view.engine.getTrack (track).slots[(size_t) slot].params[(size_t) safe->param].store ((float) (result - 1));
                safe->refresh();
                if (safe->view.onChanged) safe->view.onChanged();
            });
        }

        TrackEffectsView& view;
        const int param;
        juce::TextButton button;

        friend class TrackEffectsView;
    };

    //==========================================================================
    juce::String trackLabel (int t) const   { return juce::String (t) + "  " + engine.getTrackName (t); }

    void selectSlot (int s)
    {
        if (s == selectedSlot)
            return;
        selectedSlot = s;
        refresh();
        rebuildParams();
    }

    void moveSlot (int from, int to)
    {
        if (to < 0 || to >= mixer::numSlots)
            return;
        engine.swapTrackEffects (track, from, to);
        selectedSlot = to;
        refresh();
        rebuildParams();
        if (onChanged) onChanged();
    }

    void showTypeMenu (int slot, juce::Component& target)
    {
        const int current = engine.getTrack (track).slots[(size_t) slot].type.load();
        juce::PopupMenu menu;
        menu.addSectionHeader ("Slot " + juce::String (slot + 1));
        for (int t = 1; t < mixer::numFxTypes; ++t)
            menu.addItem (t + 1, mixer::info (t).name, true, t == current);
        menu.addSeparator();
        menu.addItem (1, "Togli l'effetto", current != mixer::none);

        juce::Component::SafePointer<TrackEffectsView> safe (this);
        const int trackIndex = track;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, slot, trackIndex] (int result)
        {
            if (safe == nullptr || result <= 0 || safe->track != trackIndex)
                return;
            safe->engine.setTrackEffect (trackIndex, slot, result - 1);
            // Sidechain: parte dalla traccia della cassa, se c'e'
            if (result - 1 == mixer::sidechain)
                if (const int kick = safe->findKickTrack(); kick > 0)
                    safe->engine.getTrack (trackIndex).slots[(size_t) slot].params[0].store ((float) kick);
            safe->selectedSlot = slot;
            safe->refresh();
            safe->rebuildParams();
            if (safe->onChanged) safe->onChanged();
        });
    }

    int findKickTrack() const
    {
        for (int c = 0; c < engine.getNumChannels(); ++c)
        {
            const auto name = engine.getChannel (c).name.toLowerCase();
            const int t = engine.getChannelTrack (c);
            if (t > 0 && t != track && (name.contains ("kick") || name.contains ("cassa")))
                return t;
        }
        return 0;
    }

    void rebuildParams()
    {
        paramKnobs.clear();
        paramButtons.clear();
        paramIndex.clear();
        auto& slot = engine.getTrack (track).slots[(size_t) selectedSlot];
        shownType = slot.type.load();
        const auto& inf = mixer::info (shownType);

        for (int p = 0; p < inf.numParams; ++p)
        {
            const auto& def = inf.params[(size_t) p];
            if (def.unit == mixer::Unit::choice || def.unit == mixer::Unit::track)
            {
                auto* b = paramButtons.add (new ChoiceParam (*this, p));
                addAndMakeVisible (b);
                paramKnobs.add (nullptr);
            }
            else
            {
                auto* k = paramKnobs.add (new LabeledKnob (def.name, 0.0, 1.0, mixer::toNormalised (def, def.defaultValue),
                                                           [def] (double n) { return mixer::formatValue (def, mixer::fromNormalised (def, (float) n)); }));
                const int trackIndex = track, slotIndex = selectedSlot;
                k->onChange = [this, def, p, trackIndex, slotIndex] (double n)
                {
                    engine.getTrack (trackIndex).slots[(size_t) slotIndex].params[(size_t) p].store (mixer::fromNormalised (def, (float) n));
                };
                k->setValue (mixer::toNormalised (def, slot.params[(size_t) p].load()));
                k->setColour (Theme::accent);
                addAndMakeVisible (k);
            }
            paramIndex.push_back (p);
        }

        for (auto* row : slotRows)
            row->refresh();
        layoutParams();
        repaint();
    }

    void layoutParams()
    {
        const int n = (int) paramIndex.size();
        if (n == 0 || paramsArea.isEmpty())
            return;
        const int cols = paramsArea.getWidth() >= paramsArea.getHeight() * 2 ? n : juce::jmin (n, 3);
        const int rows = (n + cols - 1) / cols;
        const int w = paramsArea.getWidth() / cols, h = juce::jmin (120, paramsArea.getHeight() / rows);
        int button = 0;
        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<int> cell (paramsArea.getX() + (i % cols) * w, paramsArea.getY() + (i / cols) * h, w, h);
            if (auto* k = paramKnobs[i])
                k->setBounds (cell.reduced (3, 0));
            else if (auto* b = paramButtons[button++])
                b->setBounds (cell.reduced (3, 0));
        }
    }

public:
    std::function<void()> onChanged;

private:
    SequencerEngine& engine;
    int track = 1, selectedSlot = 0, shownType = -1;
    juce::OwnedArray<LabeledKnob> eqKnobs, paramKnobs;
    juce::OwnedArray<ChoiceParam> paramButtons;
    std::vector<int> paramIndex;
    juce::OwnedArray<SlotRow> slotRows;
    juce::Rectangle<float> eqCaption, slotsCaption, paramsCaption, detailArea;
    juce::Rectangle<int> paramsArea;
};

//==============================================================================
/**
    Il mixer: le strisce delle tracce (scorrono col dito), e la pagina degli effetti di una traccia.
    Si apre sopra il channel rack, come la playlist.
*/
class MixerPanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit MixerPanel (SequencerEngine& e) : engine (e), effects (e)
    {
        for (auto* b : { &closeButton, &allButton, &backButton })
            addAndMakeVisible (b);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        allButton.setClickingTogglesState (true);
        allButton.onClick = [this] { rebuildStrips(); };
        backButton.onClick = [this] { showEffects (-1); };

        viewport.setViewedComponent (&stripHolder, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);
        addChildComponent (effects);
        effects.onChanged = [this] { for (auto* s : strips) s->refresh(); };
    }

    std::function<void()> onClose;

    void open (int trackToShow = -1)
    {
        rebuildStrips();
        showEffects (-1);
        if (trackToShow >= 0)
            showEffects (trackToShow);
        startTimerHz (30);
    }

    void visibilityChanged() override
    {
        if (! isVisible())
            stopTimer();
    }

    /** Apre gli effetti di una traccia (-1 = torna alle strisce). */
    void showEffects (int track)
    {
        effectsTrack = track;
        effects.setVisible (track >= 0);
        viewport.setVisible (track < 0);
        backButton.setVisible (track >= 0);
        allButton.setVisible (track < 0);
        if (track >= 0)
        {
            effects.setTrack (track);
            selectStrip (track);
        }
        resized();
        repaint();
    }

    int getEffectsTrack() const     { return effectsTrack; }
    TrackEffectsView& getEffectsView()   { return effects; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), Theme::corner, 1.0f);

        auto title = titleArea;
        g.setColour (effectsTrack >= 0 ? engine.getTrackColour (effectsTrack) : Theme::accent);
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 6.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (16.0f, true));
        const auto text = effectsTrack < 0 ? juce::String ("Mixer")
                        : effectsTrack == 0 ? juce::String ("Master: effetti")
                                            : "Traccia " + juce::String (effectsTrack) + ": " + engine.getTrackName (effectsTrack);
        g.drawFittedText (text, title.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto bar = r.removeFromTop (32);
        const bool narrow = getWidth() < 520;
        closeButton.setBounds (bar.removeFromRight (narrow ? 64 : 76).reduced (0, 2));
        bar.removeFromRight (6);
        if (effectsTrack >= 0)
            backButton.setBounds (bar.removeFromRight (narrow ? 70 : 90).reduced (0, 2));
        else
            allButton.setBounds (bar.removeFromRight (narrow ? 70 : 110).reduced (0, 2));
        titleArea = bar.toFloat();
        r.removeFromTop (6);

        viewport.setBounds (r);
        effects.setBounds (r);
        layoutStrips();
    }

private:
    void rebuildStrips()
    {
        strips.clear();
        for (int t = 0; t < mixer::numTracks; ++t)
            if (allButton.getToggleState() || engine.isTrackUsed (t))
            {
                auto* s = strips.add (new MixerStrip (engine, t));
                s->onOpenEffects = [this] (int track) { showEffects (track); };
                s->onNameTap = [this] (int track) { showTrackMenu (track); };
                stripHolder.addAndMakeVisible (s);
            }
        allButton.setButtonText (allButton.getToggleState() ? "Solo usate" : "Tutte (" + juce::String (mixer::numTracks) + ")");
        shownSignature = signature();
        layoutStrips();
    }

    void layoutStrips()
    {
        const int h = juce::jmax (200, viewport.getHeight() - (viewport.isHorizontalScrollBarShown() ? 8 : 0));
        const int w = getWidth() < 520 ? 70 : 82;
        stripHolder.setSize (juce::jmax (viewport.getWidth(), w * strips.size()), h);
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setBounds (i * w, 0, w, h);
    }

    void selectStrip (int track)
    {
        for (auto* s : strips)
            s->setSelected (s->getTrack() == track);
    }

    /** Quali tracce sono in uso (cambia aggiungendo canali o caricando un progetto). */
    juce::uint64 signature() const
    {
        juce::uint64 h = allButton.getToggleState() ? 7 : 3;
        for (int t = 0; t < mixer::numTracks; ++t)
            h = h * 3 + (engine.isTrackUsed (t) ? 1u : 0u);
        return h;
    }

    void timerCallback() override
    {
        if (effectsTrack < 0 && signature() != shownSignature)
            rebuildStrips();

        for (auto* s : strips)
        {
            s->setLevel (engine.takeTrackPeak (s->getTrack()));
            if (++refreshCounter % 6 == 0)
                s->refresh();
        }
        if (effectsTrack >= 0 && refreshCounter % 6 == 0 && ! isMouseButtonDownAnywhere())
            effects.refresh();
    }

    void showTrackMenu (int track)
    {
        juce::PopupMenu colours;
        static const juce::uint32 palette[] = { 0xffe8833a, 0xffd2555a, 0xffd8b14a, 0xff4fb3a9, 0xff5b8fd6, 0xff9a78d0,
                                                0xff6fbf5a, 0xffe36fae, 0xff8aa0b8, 0xffc49360 };
        colours.addItem (100, "Automatico", true, engine.getTrack (track).colour == 0);
        for (int i = 0; i < (int) std::size (palette); ++i)
            colours.addColouredItem (101 + i, "Colore " + juce::String (i + 1), juce::Colour (palette[i]), true, engine.getTrack (track).colour == palette[i]);

        // Canali che suonano in questa traccia
        juce::String routed;
        for (int c = 0; c < engine.getNumChannels(); ++c)
            if (engine.getChannelTrack (c) == track)
                routed << (routed.isEmpty() ? "" : ", ") << engine.getChannel (c).name;

        juce::PopupMenu menu;
        menu.addSectionHeader (track == 0 ? juce::String ("Master") : "Traccia " + juce::String (track) + ": " + engine.getTrackName (track));
        if (routed.isNotEmpty())
            menu.addItem (-1, "Canali: " + routed, false);
        menu.addItem (1, "Effetti ed equalizzatore...");
        if (track > 0)
        {
            menu.addItem (2, "Rinomina...");
            menu.addSubMenu ("Colore", colours);
            menu.addSeparator();
            menu.addItem (3, "Azzera la traccia (volume, EQ, effetti)");
        }

        juce::Component::SafePointer<MixerPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options(), [safe, track] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& eng = safe->engine;
            if (result == 1)       safe->showEffects (track);
            else if (result == 2)
            {
                auto* w = new juce::AlertWindow ("Nome della traccia", {}, juce::MessageBoxIconType::NoIcon, safe.getComponent());
                w->addTextEditor ("text", eng.getTrack (track).name.isNotEmpty() ? eng.getTrack (track).name : eng.getTrackName (track));
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Automatico", 2);
                w->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, track] (int r)
                {
                    if (safe != nullptr && r > 0)
                    {
                        safe->engine.getTrack (track).name = r == 1 ? w->getTextEditorContents ("text").trim() : juce::String();
                        for (auto* s : safe->strips) s->refresh();
                    }
                }), true);
            }
            else if (result == 3)  { eng.resetTrack (track); safe->rebuildStrips(); }
            else if (result == 100) eng.getTrack (track).colour = 0;
            else if (result > 100)  eng.getTrack (track).colour = palette[result - 101];
            for (auto* s : safe->strips) s->refresh();
        });
    }

    SequencerEngine& engine;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<MixerStrip> strips;
    TrackEffectsView effects;
    juce::TextButton closeButton { "Chiudi" }, allButton, backButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80 Mixer")) };
    juce::Rectangle<float> titleArea;
    int effectsTrack = -1, refreshCounter = 0;
    juce::uint64 shownSignature = 0;
};
