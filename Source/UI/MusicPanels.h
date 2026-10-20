#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "../Engine/MusicTheory.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/**
    Finestrella al centro dello schermo per un pannello qualsiasi: lo sfondo si scurisce,
    tocca fuori per chiudere; scorre se il pannello e' piu' alto dello schermo; segue le rotazioni.
    Si cancella da sola quando si chiude.
*/
class CenteredOverlay : public juce::Component
{
public:
    CenteredOverlay (std::unique_ptr<juce::Component> content, std::function<int (int width)> heightFor, int maxWidth = 620)
        : panel (std::move (content)), preferredHeight (std::move (heightFor)), widthLimit (maxWidth)
    {
        viewport.setViewedComponent (panel.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (viewport);
    }

    /** Apre il pannello sopra tutto (nel componente piu' esterno della finestra). */
    static CenteredOverlay* show (juce::Component& anyComponent, std::unique_ptr<juce::Component> content,
                                  std::function<int (int)> heightFor, int maxWidth = 620)
    {
        auto* top = anyComponent.getTopLevelComponent();
        if (top == nullptr)
            return nullptr;
        auto* overlay = new CenteredOverlay (std::move (content), std::move (heightFor), maxWidth);
        top->addAndMakeVisible (overlay);
        overlay->setBounds (top->getLocalBounds());
        overlay->toFront (true);
        return overlay;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        g.setColour (Theme::border);
        g.drawRect (viewport.getBounds().expanded (1), 1);
    }

    void resized() override
    {
        const int w = juce::jmin (widthLimit, getWidth() - 24);
        panel->setSize (w, preferredHeight (w));
        const int h = juce::jmin (panel->getHeight(), getHeight() - 24);
        viewport.setBounds (juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre()));
    }

    void parentSizeChanged() override
    {
        if (auto* p = getParentComponent())
            setBounds (p->getLocalBounds());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! viewport.getBounds().contains (e.getPosition()))
            close();
    }

    void close()
    {
        setVisible (false);
        juce::Component::SafePointer<CenteredOverlay> safe (this);
        juce::MessageManager::callAsync ([safe] { delete safe.getComponent(); });
    }

private:
    std::unique_ptr<juce::Component> panel;
    std::function<int (int)> preferredHeight;
    int widthLimit;
    juce::Viewport viewport;
};

//==============================================================================
/** Scelta della scala del progetto: tonica (12 note) e tipo (maggiore, minore, pentatonica...). */
class ScalePanel : public juce::Component
{
public:
    explicit ScalePanel (SequencerEngine& e) : engine (e)
    {
        for (int r = 0; r < 12; ++r)
        {
            auto* b = roots.add (new juce::TextButton (theory::noteName (r)));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (8801);
            b->onClick = [this, r] { engine.setScale (r, juce::jmax (1, engine.getScaleType())); refresh(); };
            addAndMakeVisible (b);
        }
        for (int t = 0; t < (int) theory::scales().size(); ++t)
        {
            auto* b = types.add (new juce::TextButton (theory::scales()[(size_t) t].name));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (8802);
            b->onClick = [this, t] { engine.setScale (engine.getScaleRoot(), t); refresh(); };
            addAndMakeVisible (b);
        }
        doneButton.onClick = [this] { if (onDone) onDone(); };
        addAndMakeVisible (doneButton);
        refresh();
    }

    std::function<void()> onChanged, onDone;

    int preferredHeight (int width) const
    {
        const int cols = width >= 480 ? 4 : 3;
        return 10 + 34 + 18 + 2 * 40 + 10 + 18 + ((types.size() + cols - 1) / cols) * 40 + 14;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (15.0f, true));
        g.drawFittedText ("Scala: " + theory::scaleLabel (engine.getScaleRoot(), engine.getScaleType()), titleArea, juce::Justification::centredLeft, 1);
        Theme::drawCaption (g, "Tonica", rootCaption.toFloat(), juce::Justification::centredLeft);
        Theme::drawCaption (g, "Tipo", typeCaption.toFloat(), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 10);
        auto top = r.removeFromTop (34);
        doneButton.setBounds (top.removeFromRight (90).reduced (0, 2));
        titleArea = top;
        rootCaption = r.removeFromTop (18);
        auto rootArea = r.removeFromTop (80);
        const int rw = rootArea.getWidth() / 6;
        for (int i = 0; i < 12; ++i)
            roots[i]->setBounds (juce::Rectangle<int> (rootArea.getX() + (i % 6) * rw, rootArea.getY() + (i / 6) * 40, rw, 40).reduced (2));
        r.removeFromTop (10);
        typeCaption = r.removeFromTop (18);
        const int cols = getWidth() >= 480 ? 4 : 3;
        const int tw = r.getWidth() / cols;
        for (int i = 0; i < types.size(); ++i)
            types[i]->setBounds (juce::Rectangle<int> (r.getX() + (i % cols) * tw, r.getY() + (i / cols) * 40, tw, 40).reduced (2));
    }

private:
    void refresh()
    {
        for (int i = 0; i < 12; ++i)
            roots[i]->setToggleState (i == engine.getScaleRoot(), juce::dontSendNotification);
        for (int i = 0; i < types.size(); ++i)
            types[i]->setToggleState (i == engine.getScaleType(), juce::dontSendNotification);
        repaint();
        if (onChanged) onChanged();
    }

    SequencerEngine& engine;
    juce::OwnedArray<juce::TextButton> roots, types;
    juce::TextButton doneButton { "Fatto" };
    juce::Rectangle<int> titleArea, rootCaption, typeCaption;
};

//==============================================================================
/**
    Progressioni di accordi pronte: un tocco scrive 4 accordi nel pattern del canale,
    nella scala del progetto (se non c'e' una scala si usa La minore).
*/
class ProgressionPanel : public juce::Component
{
public:
    ProgressionPanel (SequencerEngine& e, int channelIndex) : engine (e), channel (channelIndex)
    {
        for (int i = 0; i < (int) theory::progressions().size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (theory::progressions()[(size_t) i].name));
            b->onClick = [this, i] { apply (i); };
            addAndMakeVisible (b);
        }
        styleSwitch.addItem ("Accordi", 1);
        styleSwitch.addItem ("Accordi + basso", 2);
        styleSwitch.addItem ("Solo basso", 3);
        styleSwitch.addItem ("Arpeggio", 4);
        styleSwitch.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (styleSwitch);
        octaveSwitch.addItem ("Ottava bassa", 1);
        octaveSwitch.addItem ("Ottava centrale", 2);
        octaveSwitch.addItem ("Ottava alta", 3);
        octaveSwitch.setSelectedId (2, juce::dontSendNotification);
        addAndMakeVisible (octaveSwitch);
        cancelButton.onClick = [this] { if (onDone) onDone(); };
        addAndMakeVisible (cancelButton);
    }

    std::function<void()> onApplied, onDone;

    int preferredHeight (int width) const
    {
        const int cols = width >= 480 ? 2 : 1;
        return 10 + 34 + 8 + 34 + 10 + ((buttons.size() + cols - 1) / cols) * 40 + 30;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (15.0f, true));
        const int type = engine.getScaleType() > 0 ? engine.getScaleType() : 2;
        const int root = engine.getScaleType() > 0 ? engine.getScaleRoot() : 9;
        g.drawFittedText ("Progressioni in " + theory::scaleLabel (root, type), titleArea, juce::Justification::centredLeft, 1);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.0f));
        g.drawFittedText ("4 accordi su tutto il pattern; le note che c'erano vengono sostituite (l'undo le riporta).",
                          noteArea, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 10);
        auto top = r.removeFromTop (34);
        cancelButton.setBounds (top.removeFromRight (90).reduced (0, 2));
        titleArea = top;
        r.removeFromTop (8);
        auto opts = r.removeFromTop (34);
        styleSwitch.setBounds (opts.removeFromLeft (opts.getWidth() / 2).reduced (2));
        octaveSwitch.setBounds (opts.reduced (2));
        r.removeFromTop (10);
        noteArea = r.removeFromBottom (26);
        const int cols = getWidth() >= 480 ? 2 : 1;
        const int w = r.getWidth() / cols;
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (juce::Rectangle<int> (r.getX() + (i % cols) * w, r.getY() + (i / cols) * 40, w, 40).reduced (2));
    }

private:
    void apply (int index)
    {
        const auto& prog = theory::progressions()[(size_t) index];
        int type = engine.getScaleType(), root = engine.getScaleRoot();
        if (type <= 0 || theory::scales()[(size_t) type].intervals.size() != 7)
        {
            // Serve una scala a 7 note: quella della progressione (maggiore o minore) sulla stessa tonica
            type = prog.minor ? 2 : 1;
            if (engine.getScaleType() <= 0) root = 9;
            engine.setScale (root, type);
        }

        // Progressione maggiore in una scala minore (o il contrario): si usa la relativa, che ha le stesse note
        const bool scaleIsMinor = theory::scales()[(size_t) type].intervals[2] == 3;
        if (prog.minor != scaleIsMinor)
        {
            root = (root + (prog.minor ? 9 : 3)) % 12;
            type = prog.minor ? 2 : 1;
        }

        const int length = engine.getPatternLength() * SequencerEngine::ticksPerStep;
        const int chordLength = length / 4;
        const int octaveShift = (octaveSwitch.getSelectedId() - 2) * 12;
        const int style = styleSwitch.getSelectedId();
        // Bassi piu' in basso; il resto attorno al Do centrale
        const int baseOctave = (style == 3 ? 2 : 4) + octaveShift / 12;

        std::vector<SequencerEngine::NoteData> notes;
        for (size_t k = 0; k < prog.degrees.size(); ++k)
        {
            const int degree = prog.degrees[k];
            const int start = (int) k * chordLength;
            int chordRoot = theory::pitchOfDegree (root, type, degree, baseOctave);   // 60 = Do4
            if (chordRoot > 12 * (baseOctave + 1) + 4) chordRoot -= 12;                // non troppo in alto
            const auto chord = theory::chordAt (chordRoot, root, type, theory::triad);

            if (style == 1 || style == 2)
                for (int p : chord)
                    notes.push_back ({ start, chordLength, juce::jlimit (0, 127, p), 92 });
            if (style == 2 || style == 3)
                notes.push_back ({ start, chordLength, juce::jlimit (0, 127, style == 2 ? chordRoot - 24 : chordRoot), 105 });
            if (style == 4)
            {
                // Arpeggio: le note dell'accordo in sedicesimi, su e giu'
                const int stepTicks = SequencerEngine::ticksPerStep;
                const int pattern[] = { 0, 1, 2, 1 };
                for (int t = 0, i = 0; t < chordLength; t += stepTicks, ++i)
                    notes.push_back ({ start + t, stepTicks, juce::jlimit (0, 127, chord[(size_t) pattern[i % 4]] + (i % 8 >= 4 ? 12 : 0)), 90 });
            }
        }
        engine.setNotes (channel, notes);
        engine.previewChannel (channel, notes.empty() ? 60 : notes.front().pitch);
        if (onApplied) onApplied();
        if (onDone) onDone();
    }

    SequencerEngine& engine;
    const int channel;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ComboBox styleSwitch, octaveSwitch;
    juce::TextButton cancelButton { "Annulla" };
    juce::Rectangle<int> titleArea, noteArea;
};


//==============================================================================
/** L'arpeggiatore di un canale strumento: acceso/spento, ordine delle note, velocita', ottave, durata. */
class ArpPanel : public juce::Component
{
public:
    ArpPanel (SequencerEngine& e, int channelIndex) : engine (e), channel (channelIndex)
    {
        auto& ch = engine.getChannel (channel);
        for (auto* s : { &onSwitch, &modeSwitch, &rateSwitch, &octaveSwitch, &gateSwitch })
            addAndMakeVisible (s);

        onSwitch.setSelected (ch.arpOn.load() ? 1 : 0);
        modeSwitch.setSelected (ch.arpMode.load());
        rateSwitch.setSelected (indexOf (rates, ch.arpRate.load(), 2));
        octaveSwitch.setSelected (juce::jlimit (0, 2, ch.arpOctaves.load() - 1));
        gateSwitch.setSelected (nearest (gates, ch.arpGate.load()));

        onSwitch.onChange     = [this] (int i) { engine.getChannel (channel).arpOn.store (i == 1); changed(); };
        modeSwitch.onChange   = [this] (int i) { engine.getChannel (channel).arpMode.store (i); changed(); };
        rateSwitch.onChange   = [this] (int i) { engine.getChannel (channel).arpRate.store (rates[(size_t) i]); changed(); };
        octaveSwitch.onChange = [this] (int i) { engine.getChannel (channel).arpOctaves.store (i + 1); changed(); };
        gateSwitch.onChange   = [this] (int i) { engine.getChannel (channel).arpGate.store (gates[(size_t) i]); changed(); };

        doneButton.onClick = [this] { if (onDone) onDone(); };
        addAndMakeVisible (doneButton);
    }

    std::function<void()> onChanged, onDone;

    int preferredHeight (int) const     { return 10 + 34 + 5 * (18 + 34 + 6) + 30; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (15.0f, true));
        g.drawFittedText ("Arpeggiatore: " + engine.getChannel (channel).name, titleArea, juce::Justification::centredLeft, 1);
        const char* captions[] = { "Arpeggiatore", "Ordine delle note", "Velocita'", "Ottave", "Durata delle note" };
        for (int i = 0; i < 5; ++i)
            Theme::drawCaption (g, captions[i], captionAreas[(size_t) i].toFloat(), juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.0f));
        g.drawFittedText ("Tieni un accordo (piano roll, step o tastiera): le note suonano una alla volta, a tempo, mentre il pattern gira.",
                          hintArea, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 10);
        auto top = r.removeFromTop (34);
        doneButton.setBounds (top.removeFromRight (90).reduced (0, 2));
        titleArea = top;
        SegmentedControl* rows[] = { &onSwitch, &modeSwitch, &rateSwitch, &octaveSwitch, &gateSwitch };
        for (int i = 0; i < 5; ++i)
        {
            captionAreas[(size_t) i] = r.removeFromTop (18);
            rows[i]->setBounds (r.removeFromTop (34));
            r.removeFromTop (6);
        }
        hintArea = r;
    }

private:
    static int indexOf (const std::vector<int>& v, int value, int fallback)
    {
        for (size_t i = 0; i < v.size(); ++i) if (v[i] == value) return (int) i;
        return fallback;
    }

    static int nearest (const std::vector<float>& v, float value)
    {
        int best = 0;
        for (size_t i = 0; i < v.size(); ++i)
            if (std::abs (v[i] - value) < std::abs (v[(size_t) best] - value)) best = (int) i;
        return best;
    }

    void changed()      { if (onChanged) onChanged(); }

    SequencerEngine& engine;
    const int channel;
    const std::vector<int> rates { 6, 8, 12, 24, 48 };            // 1/32, 1/16 terzina, 1/16, 1/8, 1/4
    const std::vector<float> gates { 0.25f, 0.5f, 0.8f, 1.0f };
    SegmentedControl onSwitch { { "Spento", "Acceso" } };
    SegmentedControl modeSwitch { { "Su", juce::String (juce::CharPointer_UTF8 ("Gi\xc3\xb9")), juce::String (juce::CharPointer_UTF8 ("Su e gi\xc3\xb9")), "Casuale", "Accordo" } };
    SegmentedControl rateSwitch { { "1/32", "1/16T", "1/16", "1/8", "1/4" } };
    SegmentedControl octaveSwitch { { "1 ottava", "2 ottave", "3 ottave" } };
    SegmentedControl gateSwitch { { "Corta", "Media", "Lunga", "Legata" } };
    juce::TextButton doneButton { "Fatto" };
    juce::Rectangle<int> titleArea, hintArea;
    std::array<juce::Rectangle<int>, 5> captionAreas;
};
