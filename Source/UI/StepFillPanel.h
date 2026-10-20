#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"

//==============================================================================
/**
    Riempimento automatico di una riga (si apre tenendo premuto uno step, come in FL Studio):
    - "Ogni ..." : un colpo ogni 1, 2, 3, 4, 8 o 16 step (dall'inizio, oppure dallo step premuto)
    - "Ritmi"    : figure pronte (cassa dritta, rullante su 2 e 4, hi-hat in controtempo, tresillo...)
    - "Modifica" : svuota, inverti, sposta, casuale
    La riga viene sostituita (non sommata); l'undo riporta indietro.
*/
class StepFillPanel : public juce::Component
{
public:
    StepFillPanel (SequencerEngine& e, int channelIndex, int pressedStep)
        : engine (e), channel (channelIndex), step (pressedStep)
    {
        fromHere.setButtonText ("Dallo step " + juce::String (step + 1));
        fromHere.setClickingTogglesState (true);
        addAndMakeVisible (fromHere);

        struct Every { int n; const char* text; };
        static const Every every[] = { { 1, "Ogni step\n1/16" }, { 2, "Ogni 2\n1/8" }, { 3, "Ogni 3\nterzinato" },
                                       { 4, "Ogni 4\n1/4 (4/4)" }, { 8, "Ogni 8\n2/4" }, { 16, "Ogni 16\n1 a battuta" } };
        for (auto& ev : every)
        {
            const int n = ev.n;
            addButton (everyButtons, ev.text, [this, n] { fillEvery (n); });
        }

        struct Rhythm { const char* name; const char* pattern; };
        // x = colpo, o = colpo leggero (ghost), . = vuoto  (16 step, ripetuti se il pattern e' da 32)
        static const Rhythm rhythms[] =
        {
            { "Cassa dritta\n4/4",           "x...x...x...x..." },
            { "Cassa\nmezzo tempo",          "x.......x......." },
            { "Rullante/clap\n2 e 4",       "....x.......x..." },
            { "Rullante\ntrap (sul 3)",      "........x......." },
            { "Hi-hat\nottavi",              "x.o.x.o.x.o.x.o." },
            { "Hi-hat\ncontrotempo",         "..x...x...x...x." },
            { "Hi-hat\nsedicesimi",          "xoooxoooxoooxooo" },
            { "Hi-hat trap\ncon rullate",    "x.o.x.o.x.o.xoxo" },
            { "Tresillo\n3-3-2",             "x..x..x.x..x..x." },
            { "Dembow\n(reggaeton)",         "...x..x....x..x." },
            { "Clave\n3-2",                  "x..x...x..x.x..." },
            { "Breakbeat\ncassa",            "x.x.......x....." },
            { "Breakbeat\nrullante",         "....x..o.o..x..o" },
            { "Galoppo\n(DnB)",              "x.........x....." },
        };
        for (auto& r : rhythms)
        {
            const juce::String pattern (r.pattern);
            addButton (rhythmButtons, r.name, [this, pattern] { fillPattern (pattern); });
        }

        addButton (editButtons, "Svuota", [this] { engine.clearRow (channel); done(); });
        addButton (editButtons, "Inverti", [this] { engine.reverseRow (channel); done(); });
        addButton (editButtons, juce::String (juce::CharPointer_UTF8 ("Sposta \xe2\x97\x80")), [this] { engine.shiftRow (channel, -1); done (false); });
        addButton (editButtons, juce::String (juce::CharPointer_UTF8 ("Sposta \xe2\x96\xb6")), [this] { engine.shiftRow (channel, 1); done (false); });
        addButton (editButtons, "Casuale", [this] { randomise(); });

        setSize (520, 470);
    }

    std::function<void()> onChanged, onClose;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (15.0f, true));
        g.drawFittedText ("Riempi \"" + engine.getChannel (channel).name + "\"", titleArea, juce::Justification::centredLeft, 1);
        Theme::drawCaption (g, "Ogni...", everyCaption.toFloat(), juce::Justification::centredLeft);
        Theme::drawCaption (g, "Ritmi pronti", rhythmCaption.toFloat(), juce::Justification::centredLeft);
        Theme::drawCaption (g, "Modifica la riga", editCaption.toFloat(), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (30);
        fromHere.setBounds (top.removeFromRight (juce::jmin (150, top.getWidth() / 2)).reduced (0, 2));
        titleArea = top;
        r.removeFromTop (4);

        const int cols = columnsFor (getWidth());
        auto grid = [&r, cols] (juce::OwnedArray<juce::TextButton>& buttons, juce::Rectangle<int>& caption, int rowH, int colsHere)
        {
            caption = r.removeFromTop (16);
            const int rows = (buttons.size() + colsHere - 1) / colsHere;
            auto area = r.removeFromTop (rows * rowH);
            const int w = area.getWidth() / colsHere;
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setBounds (juce::Rectangle<int> (area.getX() + (i % colsHere) * w, area.getY() + (i / colsHere) * rowH, w, rowH).reduced (2));
            r.removeFromTop (6);
        };
        grid (everyButtons, everyCaption, 42, cols >= 5 ? 6 : 3);
        grid (rhythmButtons, rhythmCaption, 42, cols);
        grid (editButtons, editCaption, 36, 5);
    }

    /** Altezza giusta per la larghezza data (per la finestrella che la contiene). */
    int preferredHeight (int width) const
    {
        const int cols = columnsFor (width);
        const int everyRows = cols >= 5 ? 1 : 2;
        return 8 + 34 + 16 + everyRows * 42 + 6 + 16 + ((rhythmButtons.size() + cols - 1) / cols) * 42 + 6 + 16 + 36 + 14;
    }

    static int columnsFor (int width)   { return width >= 540 ? 5 : width >= 420 ? 4 : 3; }

private:
    void addButton (juce::OwnedArray<juce::TextButton>& list, const juce::String& text, std::function<void()> fn)
    {
        auto* b = list.add (new juce::TextButton (text));
        b->onClick = std::move (fn);
        addAndMakeVisible (b);
    }

    int length() const  { return engine.getPatternLength(); }

    void setStep (int s, int velocity)
    {
        engine.setStepVelocity (channel, s, velocity);
        if (velocity > 0)
        {
            engine.setStepProbability (channel, s, 100);
            engine.setStepRatchet (channel, s, 1);
        }
    }

    void fillEvery (int n)
    {
        const int offset = fromHere.getToggleState() ? step % n : 0;
        for (int s = 0; s < length(); ++s)
            setStep (s, (s >= offset && (s - offset) % n == 0) ? SequencerEngine::defaultVelocity : 0);
        done();
    }

    void fillPattern (const juce::String& pattern)
    {
        const int shift = fromHere.getToggleState() ? step : 0;
        for (int s = 0; s < length(); ++s)
        {
            const auto ch = pattern[(s - shift + 32) % pattern.length()];
            setStep (s, ch == 'x' ? SequencerEngine::defaultVelocity : ch == 'o' ? 60 : 0);
        }
        done();
    }

    void randomise()
    {
        juce::Random rnd;
        for (int s = 0; s < length(); ++s)
        {
            // Piu' probabili i colpi sui quarti, poi sugli ottavi
            const float chance = s % 4 == 0 ? 0.6f : s % 2 == 0 ? 0.35f : 0.18f;
            setStep (s, rnd.nextFloat() < chance ? juce::jlimit (40, 120, 70 + rnd.nextInt (51)) : 0);
        }
        if (onChanged) onChanged();   // resta aperto: si puo' riprovare
    }

    void done (bool close = true)
    {
        if (onChanged) onChanged();
        if (close && onClose) onClose();
    }

    SequencerEngine& engine;
    const int channel, step;
    juce::TextButton fromHere;
    juce::OwnedArray<juce::TextButton> everyButtons, rhythmButtons, editButtons;
    juce::Rectangle<int> titleArea, everyCaption, rhythmCaption, editCaption;
};


//==============================================================================
/** Finestrella al centro dello schermo (lo sfondo si scurisce; tocca fuori per chiudere). */
class FillOverlay : public juce::Component
{
public:
    explicit FillOverlay (std::unique_ptr<StepFillPanel> p) : panel (std::move (p))
    {
        viewport.setViewedComponent (panel.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (viewport);
        panel->onClose = [this] { close(); };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        g.setColour (Theme::border);
        g.drawRect (viewport.getBounds().expanded (1), 1);
    }

    void resized() override
    {
        const int w = juce::jmin (620, getWidth() - 24);
        panel->setSize (w, panel->preferredHeight (w));
        const int h = juce::jmin (panel->getHeight(), getHeight() - 24);
        viewport.setBounds (juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre()));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! viewport.getBounds().contains (e.getPosition()))
            close();
    }

    /** Lo schermo ruota (verticale/orizzontale): la finestrella si adatta. */
    void parentSizeChanged() override
    {
        if (auto* p = getParentComponent())
            setBounds (p->getLocalBounds());
    }

    void close()
    {
        setVisible (false);
        juce::Component::SafePointer<FillOverlay> safe (this);
        juce::MessageManager::callAsync ([safe] { delete safe.getComponent(); });
    }

private:
    std::unique_ptr<StepFillPanel> panel;
    juce::Viewport viewport;
};
