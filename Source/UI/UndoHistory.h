#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include "Theme.h"

//==============================================================================
/**
    Cronologia delle modifiche: ogni passo di undo ha un nome ("Step: Cassa (Pattern 1)",
    "Tempo: 128 BPM", "Mixer"...), ricavato confrontando il progetto prima e dopo.
    Il pannello mostra la lista e permette di tornare a un punto qualsiasi con un tocco.
*/
namespace undohistory
{
    inline juce::String toText (const juce::var& v)     { return juce::JSON::toString (v, true); }
    inline bool same (const juce::var& a, const juce::var& b)   { return toText (a) == toText (b); }

    inline juce::String channelName (const juce::var& channels, int c)
    {
        if (auto* arr = channels.getArray(); arr != nullptr && c >= 0 && c < arr->size())
            return (*arr)[c]["name"].toString();
        return "canale " + juce::String (c + 1);
    }

    inline juce::String patternName (const juce::var& patterns, int p)
    {
        if (auto* arr = patterns.getArray(); arr != nullptr && p < arr->size())
            if (const auto n = (*arr)[p]["name"].toString(); n.isNotEmpty())
                return n;
        return "Pattern " + juce::String (p + 1);
    }

    /** Il nome di una modifica, in italiano semplice. */
    inline juce::String describe (const juce::String& beforeText, const juce::String& afterText)
    {
        const auto before = juce::JSON::parse (beforeText), after = juce::JSON::parse (afterText);
        juce::StringArray parts;

        // Canali
        const auto chB = before["channels"], chA = after["channels"];
        const int nB = chB.getArray() != nullptr ? chB.getArray()->size() : 0;
        const int nA = chA.getArray() != nullptr ? chA.getArray()->size() : 0;
        if (nA > nB)
            parts.add ("Nuovo canale: " + channelName (chA, nA - 1));
        else if (nA < nB)
            parts.add ("Canale eliminato");
        else if (! same (chB, chA))
        {
            juce::StringArray changed;
            juce::String what;
            for (int c = 0; c < nA; ++c)
            {
                const auto& b = (*chB.getArray())[c];
                const auto& a = (*chA.getArray())[c];
                if (same (b, a))
                    continue;
                changed.add (a["name"].toString());
                auto differs = [&] (const char* key) { return ! same (b[key], a[key]); };
                if (differs ("name"))                                                     what = "Rinomina";
                else if (differs ("sample") || differs ("instrument") || differs ("drumType")
                         || differs ("kit") || differs ("type"))                          what = "Suono";
                else if (differs ("arp"))                                                 what = "Arpeggiatore";
                else if (differs ("volume") || differs ("pan") || differs ("muted") || differs ("solo")
                         || differs ("mixer"))                                            what = "Mix";
                else if (differs ("state"))                                               what = "Preset";
                else                                                                      what = "Regolazioni";
            }
            if (changed.size() == 1)
                parts.add (what + ": " + changed[0]);
            else if (changed.size() > 1)
            {
                // Stessi canali in ordine diverso?
                juce::StringArray namesB, namesA;
                for (int c = 0; c < nA; ++c) { namesB.add (channelName (chB, c)); namesA.add (channelName (chA, c)); }
                namesB.sort (false); namesA.sort (false);
                parts.add (namesB == namesA ? juce::String ("Canali spostati") : juce::String (changed.size()) + " canali");
            }
        }

        // Pattern: step, note, lunghezza
        const auto pB = before["patterns"], pA = after["patterns"];
        if (! same (pB, pA))
        {
            const int count = juce::jmax (pB.getArray() != nullptr ? pB.getArray()->size() : 0,
                                          pA.getArray() != nullptr ? pA.getArray()->size() : 0);
            int changedPatterns = 0;
            juce::String label;
            for (int p = 0; p < count; ++p)
            {
                const juce::var b = pB.getArray() != nullptr && p < pB.getArray()->size() ? (*pB.getArray())[p] : juce::var();
                const juce::var a = pA.getArray() != nullptr && p < pA.getArray()->size() ? (*pA.getArray())[p] : juce::var();
                if (same (b, a))
                    continue;
                ++changedPatterns;

                auto rowsChanged = [&] (const char* key)
                {
                    juce::Array<int> list;
                    auto rb = b[key], ra = a[key];
                    // Pattern prima vuoto (o svuotato): manca l'elenco delle righe; le righe "vuote" sono
                    // quelle uguali alla piu' comune dall'altra parte
                    if (rb.getArray() == nullptr || ra.getArray() == nullptr)
                    {
                        const auto& present = rb.getArray() != nullptr ? rb : ra;
                        if (present.getArray() == nullptr)
                            return list;
                        std::map<juce::String, int> counts;
                        for (auto& row : *present.getArray()) ++counts[toText (row)];
                        juce::String common; int best = -1;
                        for (auto& [text, n] : counts) if (n > best) { best = n; common = text; }
                        for (int c = 0; c < present.getArray()->size(); ++c)
                            if (toText ((*present.getArray())[c]) != common)
                                list.add (c);
                        return list;
                    }
                    const int rows = juce::jmax (rb.getArray() != nullptr ? rb.getArray()->size() : 0,
                                                 ra.getArray() != nullptr ? ra.getArray()->size() : 0);
                    for (int c = 0; c < rows; ++c)
                    {
                        const juce::var x = rb.getArray() != nullptr && c < rb.getArray()->size() ? (*rb.getArray())[c] : juce::var();
                        const juce::var y = ra.getArray() != nullptr && c < ra.getArray()->size() ? (*ra.getArray())[c] : juce::var();
                        if (! same (x, y)) list.add (c);
                    }
                    return list;
                };

                const auto steps = rowsChanged ("steps");
                const auto notes = rowsChanged ("notes");
                const auto extra = rowsChanged ("probability").size() + rowsChanged ("ratchet").size();
                const auto where = " (" + patternName (pA.isVoid() ? pB : pA, p) + ")";
                if (! notes.isEmpty())
                    label = (notes.size() == 1 ? "Note: " + channelName (chA, notes[0]) : juce::String ("Note")) + where;
                else if (! steps.isEmpty())
                    label = (steps.size() == 1 ? "Step: " + channelName (chA, steps[0]) : juce::String ("Step")) + where;
                else if (extra > 0)
                    label = "Probabilita' / rullate" + where;
                else if (! same (b["name"], a["name"]))
                    label = "Nome del pattern" + where;
                else if (! same (b["length"], a["length"]))
                    label = "Lunghezza" + where;
                else
                    label = "Pattern" + where;
            }
            if (changedPatterns == 1)
                parts.add (label);
            else if (changedPatterns > 1)
                parts.add (juce::String (changedPatterns) + " pattern");
        }

        if (! same (before["bpm"], after["bpm"]))
            parts.add ("Tempo: " + juce::String (juce::roundToInt ((double) after["bpm"])) + " BPM");
        if (! same (before["swing"], after["swing"]))      parts.add ("Swing");
        if (! same (before["scale"], after["scale"]))      parts.add ("Scala");
        if (! same (before["master"], after["master"]))    parts.add ("Volume master");
        if (! same (before["song"], after["song"]))        parts.add ("Playlist");
        if (! same (before["mixer"], after["mixer"]))      parts.add ("Mixer");
        if (! same (before["automation"], after["automation"])) parts.add ("Automazioni");
        if (! same (before["mastering"], after["mastering"]))   parts.add ("Mastering");

        if (parts.isEmpty())
            return "Modifica";
        return parts.size() == 1 ? parts[0] : parts[0] + " e altro";
    }
}

//==============================================================================
/**
    La lista delle modifiche: in alto le piu' recenti. Le voci "rifatte" (dopo un undo) sono in grigio:
    toccandole si va avanti. Il tocco su una voce porta il progetto a com'era DOPO quella modifica.
*/
class UndoHistoryPanel : public juce::Component
{
public:
    /** undoLabels: dalla piu' vecchia alla piu' recente; redoLabels: la prossima da rifare e' l'ultima. */
    UndoHistoryPanel (const juce::StringArray& undoLabels, const juce::StringArray& redoLabels)
    {
        // Ogni riga e' una modifica: toccandola il progetto torna com'era subito DOPO quella modifica.
        // In cima quelle annullate (da rifare), poi l'ultima fatta (= adesso), poi le piu' vecchie.
        for (int i = 0; i < redoLabels.size(); ++i)
            addRow (redoLabels[i], redoLabels.size() - i, false);
        for (int i = undoLabels.size() - 1; i >= 0; --i)
            addRow (undoLabels[i], -(undoLabels.size() - 1 - i), true);
        addRow ("Inizio (prima di queste modifiche)", -undoLabels.size(), true);

        doneButton.onClick = [this] { if (onDone) onDone(); };
        addAndMakeVisible (doneButton);
    }

    /** steps > 0 = rifai tanti passi, < 0 = annulla tanti passi. */
    std::function<void (int steps)> onJump;
    std::function<void()> onDone;

    int preferredHeight (int) const     { return 10 + 34 + 6 + rows.size() * rowHeight + 10; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (15.0f, true));
        g.drawFittedText ("Cronologia delle modifiche", titleArea, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 10);
        auto top = r.removeFromTop (34);
        doneButton.setBounds (top.removeFromRight (90).reduced (0, 2));
        titleArea = top;
        r.removeFromTop (6);
        for (auto* row : rows)
            row->setBounds (r.removeFromTop (rowHeight).reduced (0, 2));
    }

private:
    struct Row : public juce::Component
    {
        juce::String text;
        int moveTo = 0;          // passi da fare per arrivare qui
        bool done = true;        // modifica gia' fatta (non da rifare)
        std::function<void()> onTap;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const bool now = moveTo == 0;
            Theme::drawRaised (g, r, now ? Theme::accent.darker (0.55f) : Theme::raised);
            g.setColour (now ? Theme::textBright : done ? Theme::text : Theme::textDim);
            g.setFont (Theme::font (14.0f, now));
            g.drawFittedText (text, getLocalBounds().reduced (12, 0), juce::Justification::centredLeft, 1);
            if (! done || now)
            {
                g.setFont (Theme::font (11.0f));
                g.drawText (now ? "adesso" : "annullata", getLocalBounds().reduced (12, 0), juce::Justification::centredRight);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.getDistanceFromDragStart() < 10 && onTap)
                onTap();
        }
    };

    void addRow (const juce::String& text, int moveTo, bool done)
    {
        auto* row = rows.add (new Row());
        row->text = text;
        row->moveTo = moveTo;
        row->done = done;
        row->onTap = [this, row] { if (row->moveTo != 0 && onJump) onJump (row->moveTo); };
        addAndMakeVisible (row);
    }

    static constexpr int rowHeight = 42;
    juce::OwnedArray<Row> rows;
    juce::TextButton doneButton { "Fatto" };
    juce::Rectangle<int> titleArea;
};
