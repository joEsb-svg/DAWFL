#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"
#include "Widgets.h"
#include "MusicPanels.h"

//==============================================================================
/**
    Griglia del piano roll: tempo in orizzontale, altezza delle note in verticale.

    Gesti (pensati per il dito):
    - tocca uno spazio vuoto: aggiunge una nota (lunga come l'ultima usata)
    - tocca una nota: la cancella
    - trascina una nota: la sposta (agganciata alla griglia)
    - trascina il bordo destro di una nota: la allunga o accorcia
    - trascina su uno spazio vuoto: scorre la griglia
*/
class PianoRollGrid : public juce::Component
{
public:
    static constexpr int lowestNote  = 24;    // C1
    static constexpr int highestNote = 107;   // B7
    static constexpr int keyboardWidth = 46;

    explicit PianoRollGrid (SequencerEngine& e) : engine (e) {}

    std::function<void()> onEdit;   // per la vibrazione
    std::function<juce::String (int channel, int pitch)> rowLabel;   // nome della riga (i pad di DrumForge)

    void setChannel (int c)
    {
        if (c != channel) selection.clear();
        channel = c;
        repaint();
    }

    void setSnap (int ticks)        { snap = ticks; repaint(); }

    /** I canali batteria (DrumForge) non si mostrano come fantasmi: le loro note sono pad, non melodie. */
    bool isDrumKit (int c) const    { return engine.isSynth (c) && engine.getChannel (c).instrumentKind == "drumforge"; }

    void setRowHeight (float h)
    {
        const float centrePitch = pitchAtY ((float) getHeight() * 0.5f);
        rowHeight = juce::jlimit (12.0f, 40.0f, h);
        scrollToPitch (centrePitch);
    }

    float getRowHeight() const      { return rowHeight; }

    /** Mette la nota indicata al centro della vista. */
    void scrollToPitch (float pitch)
    {
        scrollY = (float) (highestNote - pitch) * rowHeight - (float) getHeight() * 0.5f;
        clampScroll();
        repaint();
    }

    /** Centra la vista sulle note esistenti (o sulla nota di base se non ce ne sono). */
    void centreOnContent()
    {
        auto notes = engine.getNotes (channel);
        if (notes.empty())
        {
            scrollToPitch ((float) engine.getChannelRootNote (channel));
            return;
        }

        float sum = 0.0f;
        for (auto& n : notes)
            sum += (float) n.pitch;
        scrollToPitch (sum / (float) notes.size());
    }

    void setPlayTick (int tick)
    {
        if (tick != playTick)
        {
            playTick = tick;
            repaint();
        }
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        auto grid = getGridArea();
        const int lengthTicks = engine.getPatternLength() * SequencerEngine::ticksPerStep;

        g.fillAll (Theme::bg);

        // Righe delle note (tasti neri piu' scuri)
        for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
        {
            const float y = yForPitch (pitch);
            if (y + rowHeight < 0 || y > (float) getHeight())
                continue;

            auto row = juce::Rectangle<float> ((float) grid.getX(), y, (float) grid.getWidth(), rowHeight);
            const int scaleType = engine.getScaleType(), scaleRoot = engine.getScaleRoot();
            if (scaleType > 0)
            {
                // Con una scala: le note della scala chiare, le altre scure, la tonica colorata
                const bool in = theory::inScale (pitch, scaleRoot, scaleType);
                g.setColour (in ? Theme::panel.brighter (0.05f) : Theme::bg.darker (0.35f));
                g.fillRect (row);
                if (in)
                {
                    g.setColour (Theme::cyan.withAlpha (0.035f));
                    g.fillRect (row);
                }
                if ((pitch - scaleRoot) % 12 == 0)
                {
                    g.setColour (Theme::accent.withAlpha (0.10f));
                    g.fillRect (row);
                }
            }
            else
            {
                g.setColour (isBlackKey (pitch) ? Theme::bg.darker (0.25f) : Theme::panel);
                g.fillRect (row);
            }
            g.setColour (Theme::border.withAlpha (pitch % 12 == 0 ? 1.0f : 0.45f));
            g.drawHorizontalLine ((int) (y + rowHeight), (float) grid.getX(), (float) grid.getRight());
        }

        // Linee verticali: step, quarti, battute
        for (int t = 0; t <= lengthTicks; t += SequencerEngine::ticksPerStep)
        {
            const float x = xForTick (t);
            if (x < (float) grid.getX() - 1 || x > (float) grid.getRight() + 1)
                continue;

            const bool bar  = t % (16 * SequencerEngine::ticksPerStep) == 0;
            const bool beat = t % (4 * SequencerEngine::ticksPerStep) == 0;
            g.setColour (bar ? Theme::cyan.withAlpha (0.28f) : beat ? Theme::cyan.withAlpha (0.12f) : Theme::raised.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        }

        // Oltre la fine del pattern
        const float endX = xForTick (lengthTicks);
        if (endX < (float) grid.getRight())
        {
            g.setColour (Theme::bg.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (endX, 0.0f, (float) grid.getRight() - endX, (float) getHeight()));
        }

        // Nomi delle righe (es. "Kick", "Snare" per i pad di una drum machine)
        if (rowLabel != nullptr && rowHeight >= 12.0f)
        {
            g.setFont (Theme::font (juce::jmin (12.0f, rowHeight - 3.0f)));
            for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
            {
                const float y = yForPitch (pitch);
                if (y + rowHeight < 0 || y > (float) getHeight())
                    continue;
                const auto label = rowLabel (channel, pitch);
                if (label.isEmpty())
                    continue;
                g.setColour (Theme::textDim.withAlpha (0.85f));
                g.drawText (label, juce::Rectangle<float> ((float) grid.getX() + 5.0f, y, 160.0f, rowHeight), juce::Justification::centredLeft, true);
            }
        }

        // Note "fantasma": quelle degli altri canali nello stesso pattern, appena accennate
        // (per scrivere la melodia sopra agli accordi e al basso senza andare a memoria)
        if (ghostNotes)
            for (int other = 0; other < engine.getNumChannels(); ++other)
            {
                if (other == channel || ! engine.hasNotes (other) || isDrumKit (other))
                    continue;
                const auto ghostColour = juce::Colour (engine.getChannel (other).colour.load());
                for (auto& n : engine.getNotes (other))
                {
                    auto r = noteBounds (n);
                    if (! r.intersects (grid.toFloat()))
                        continue;
                    g.setColour (ghostColour.withAlpha (0.22f));
                    g.fillRoundedRectangle (r, 2.0f);
                    g.setColour (ghostColour.withAlpha (0.6f));
                    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
                }
            }

        // Note
        const auto colour = juce::Colour (engine.getChannel (channel).colour.load());
        for (auto& n : engine.getNotes (channel))
        {
            auto r = noteBounds (n);
            if (! r.intersects (grid.toFloat()))
                continue;

            const bool selected = (dragIndex >= 0 && n == dragNote) || isSelected (n);
            // Nota luminosa col colore del canale (piu' viva se la velocity e' alta)
            const float level = 0.55f + 0.45f * (float) n.velocity / 127.0f;
            const float radius = juce::jmin (5.0f, r.getHeight() * 0.35f);
            g.setColour (colour.withAlpha (0.25f * level));
            g.fillRoundedRectangle (r.expanded (2.0f), radius + 2.0f);
            g.setGradientFill (juce::ColourGradient (colour.brighter (0.55f).withMultipliedBrightness (level), r.getX(), r.getY(),
                                                     colour.withMultipliedBrightness (0.75f * level + 0.25f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setColour (juce::Colours::white.withAlpha (selected ? 0.95f : 0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, selected ? 2.0f : 1.0f);

            // Maniglia per allungare
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.fillRoundedRectangle (r.removeFromRight (juce::jmin (5.0f, r.getWidth() * 0.25f)).reduced (1.5f, 3.0f), 1.0f);
        }

        // Linea di riproduzione
        if (playTick >= 0)
        {
            const float x = xForTick (playTick);
            if (x >= (float) grid.getX())
            {
                g.setColour (Theme::cyan.withAlpha (0.18f));
                g.fillRect (x - 4.0f, 0.0f, 8.0f, (float) getHeight());
                g.setColour (Theme::cyan);
                g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
            }
        }

        if (dragMode == DragMode::band && moved)
        {
            const auto band = juce::Rectangle<float> (downPos, lastPos);
            g.setColour (Theme::accent.withAlpha (0.15f));
            g.fillRect (band);
            g.setColour (Theme::accent);
            g.drawRect (band, 1.0f);
        }

        paintKeyboard (g);
        if (onPainted) onPainted();
    }

    void resized() override
    {
        clampScroll();
    }

    //==============================================================================
    // Selezione e strumenti (agiscono sulle note selezionate, o su tutte se non ce ne sono)

    // Per la corsia dei volumi (sotto la griglia)
    float tickToX (int tick) const  { return xForTick (tick); }
    int getChannel() const          { return channel; }
    bool hasSelection() const       { return ! selection.empty(); }
    bool noteSelected (const SequencerEngine::NoteData& n) const    { return isSelected (n); }
    std::function<void()> onPainted;

    /** Le note selezionate seguono le modifiche fatte da fuori (es. il volume cambiato nella corsia). */
    void refreshSelection (const std::vector<SequencerEngine::NoteData>& before, const std::vector<SequencerEngine::NoteData>& after)
    {
        for (auto& s : selection)
            for (size_t i = 0; i < before.size() && i < after.size(); ++i)
                if (before[i] == s) { s = after[i]; break; }
        repaint();
    }

    void setGhostNotes (bool on)    { ghostNotes = on; repaint(); }
    bool getGhostNotes() const      { return ghostNotes; }

    void setSelectMode (bool on)    { selectMode = on; if (! on) selection.clear(); repaint(); }

    void setChordMode (bool on)     { chordMode = on; if (on) setSelectMode (false); }
    bool isChordMode() const        { return chordMode; }
    void setChordType (int t)       { chordType = t; }
    int getChordType() const        { return chordType; }
    bool isSelectMode() const       { return selectMode; }
    int getSelectionSize() const    { return (int) selection.size(); }

    void selectAll()                { selection = engine.getNotes (channel); repaint(); }
    void clearSelection()           { selection.clear(); repaint(); }

    void copySelection()
    {
        auto notes = targets();
        clipboard().clear();
        if (notes.empty()) return;
        int first = notes.front().start;
        for (auto& n : notes) first = juce::jmin (first, n.start);
        for (auto n : notes) { n.start -= first; clipboard().push_back (n); }
    }

    /** Incolla all'inizio della parte visibile (agganciato alla griglia). */
    void paste()
    {
        if (clipboard().empty()) return;
        const int at = snapTickDown ((int) (scrollX / pixelsPerTick()));
        std::vector<SequencerEngine::NoteData> pasted;
        for (auto n : clipboard())
        {
            n.start += at;
            if (n.start < lengthTicks()) { n.length = juce::jmin (n.length, lengthTicks() - n.start); pasted.push_back (n); }
        }
        addNotes (pasted);
    }

    /** Copia le note subito dopo se stesse (es. una battuta -> la battuta dopo). */
    void duplicate()
    {
        auto notes = targets();
        if (notes.empty()) return;
        int first = notes.front().start, last = 0;
        for (auto& n : notes) { first = juce::jmin (first, n.start); last = juce::jmax (last, n.start + n.length); }
        const int span = juce::jmax (snap, (int) std::ceil ((double) (last - first) / snap) * snap);
        std::vector<SequencerEngine::NoteData> copies;
        for (auto n : notes)
        {
            n.start += span;
            if (n.start < lengthTicks()) { n.length = juce::jmin (n.length, lengthTicks() - n.start); copies.push_back (n); }
        }
        addNotes (copies);
    }

    /** Allinea l'inizio (e la durata) delle note alla griglia scelta. */
    void quantize()
    {
        transform ([this] (SequencerEngine::NoteData& n)
        {
            n.start = juce::jlimit (0, lengthTicks() - snap, snapTick (n.start));
            n.length = juce::jlimit (snap, lengthTicks() - n.start, snapTick (n.length));
        });
    }

    void transpose (int semitones)
    {
        transform ([semitones] (SequencerEngine::NoteData& n) { n.pitch = juce::jlimit (lowestNote, highestNote, n.pitch + semitones); });
        if (auto t = targets(); ! t.empty())
            engine.previewChannel (channel, t.front().pitch);
    }

    void changeVelocity (int amount)
    {
        transform ([amount] (SequencerEngine::NoteData& n) { n.velocity = juce::jlimit (1, 127, n.velocity + amount); });
    }

    /** Ogni nota dura fino all'inizio della successiva (suono continuo). */
    void legato()
    {
        auto notes = engine.getNotes (channel);
        const auto chosen = targets();
        for (auto& n : notes)
        {
            if (std::find (chosen.begin(), chosen.end(), n) == chosen.end()) continue;
            int next = lengthTicks();
            for (auto& m : notes)
                if (m.start > n.start) next = juce::jmin (next, m.start);
            const auto before = n;
            n.length = juce::jmax (snap / 2, next - n.start);
            for (auto& s : selection) if (s == before) s = n;
        }
        engine.setNotes (channel, notes);
        if (onEdit) onEdit();
        repaint();
    }

    void deleteSelection()
    {
        auto notes = engine.getNotes (channel);
        const auto chosen = targets();
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&chosen] (auto& n) { return std::find (chosen.begin(), chosen.end(), n) != chosen.end(); }),
                     notes.end());
        engine.setNotes (channel, notes);
        selection.clear();
        if (onEdit) onEdit();
        repaint();
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMode = DragMode::none;
        dragIndex = -1;
        moved = false;
        downPos = e.position;
        startScrollX = scrollX;
        startScrollY = scrollY;

        if (e.position.x < (float) keyboardWidth)
        {
            // Tastiera: tocca per sentire, trascina per scorrere
            dragMode = DragMode::scrollKeyboard;
            const int pitch = (int) pitchAtY (e.position.y);
            engine.previewChannel (channel, pitch);
            return;
        }

        auto notes = engine.getNotes (channel);
        for (int i = (int) notes.size(); --i >= 0;)
        {
            auto r = noteBounds (notes[(size_t) i]);
            if (r.expanded (0.0f, 2.0f).contains (e.position))
            {
                dragIndex = i;
                dragNote = notes[(size_t) i];
                originalNote = dragNote;
                const float handle = juce::jmax (14.0f, r.getWidth() * 0.3f);
                if (selectMode)
                {
                    // Trascinare una nota selezionata sposta tutte le selezionate
                    dragMode = isSelected (dragNote) ? DragMode::moveSelection : DragMode::move;
                    originalSelection = selection;
                    originalNotes = notes;
                }
                else
                    dragMode = e.position.x > r.getRight() - handle ? DragMode::resize : DragMode::move;
                repaint();
                return;
            }
        }

        dragMode = selectMode ? DragMode::band : DragMode::emptyArea;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto delta = e.position - downPos;
        if (! moved && delta.getDistanceFromOrigin() < 8.0f)
            return;

        moved = true;
        lastPos = e.position;

        switch (dragMode)
        {
            case DragMode::band:
                repaint();
                break;

            case DragMode::moveSelection:
            {
                const int dt = snapTick ((int) std::round (delta.x / pixelsPerTick()));
                const int dp = -(int) std::round (delta.y / rowHeight);
                auto notes = originalNotes;
                std::vector<SequencerEngine::NoteData> moved_;
                for (auto& n : notes)
                    if (std::find (originalSelection.begin(), originalSelection.end(), n) != originalSelection.end())
                    {
                        n.start = juce::jlimit (0, lengthTicks() - n.length, n.start + dt);
                        n.pitch = juce::jlimit (lowestNote, highestNote, n.pitch + dp);
                        moved_.push_back (n);
                    }
                engine.setNotes (channel, notes);
                selection = moved_;
                repaint();
                break;
            }

            case DragMode::scrollKeyboard:
            case DragMode::emptyArea:
                scrollX = startScrollX - (dragMode == DragMode::emptyArea ? delta.x : 0.0f);
                scrollY = startScrollY - delta.y;
                clampScroll();
                repaint();
                break;

            case DragMode::move:
            {
                auto n = originalNote;
                n.start = juce::jlimit (0, lengthTicks() - n.length,
                                        snapTick (originalNote.start + (int) std::round (delta.x / pixelsPerTick())));
                n.pitch = juce::jlimit (lowestNote, highestNote, originalNote.pitch - (int) std::round (delta.y / rowHeight));

                if (n.pitch != dragNote.pitch)
                    engine.previewChannel (channel, n.pitch);

                replaceDraggedNote (n);
                break;
            }

            case DragMode::resize:
            {
                auto n = originalNote;
                const int end = snapTick (originalNote.start + originalNote.length + (int) std::round (delta.x / pixelsPerTick()));
                n.length = juce::jlimit (snap, lengthTicks() - n.start, end - n.start);
                lastLength = n.length;
                replaceDraggedNote (n);
                break;
            }

            case DragMode::none:
                break;
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragMode == DragMode::band && moved)
        {
            // Selezione a riquadro
            const auto band = juce::Rectangle<float> (downPos, e.position);
            selection.clear();
            for (auto& n : engine.getNotes (channel))
                if (noteBounds (n).intersects (band))
                    selection.push_back (n);
            dragMode = DragMode::none;
            repaint();
            return;
        }

        if (moved || dragMode == DragMode::scrollKeyboard)
        {
            if (dragMode == DragMode::moveSelection && onEdit) onEdit();
            dragIndex = -1;
            dragMode = DragMode::none;
            repaint();
            return;
        }

        if (selectMode)
        {
            // Tocco: una nota entra o esce dalla selezione; sul vuoto si toglie la selezione
            if (dragIndex >= 0)
            {
                if (isSelected (dragNote))
                    selection.erase (std::remove (selection.begin(), selection.end(), dragNote), selection.end());
                else
                    selection.push_back (dragNote);
                engine.previewChannel (channel, dragNote.pitch);
            }
            else
                selection.clear();
            dragIndex = -1;
            dragMode = DragMode::none;
            repaint();
            return;
        }

        auto notes = engine.getNotes (channel);

        if (dragIndex >= 0 && dragIndex < (int) notes.size())
        {
            // Tocco su una nota: cancellala
            notes.erase (notes.begin() + dragIndex);
            engine.setNotes (channel, notes);
        }
        else if (dragMode == DragMode::emptyArea)
        {
            // Tocco su uno spazio vuoto: nuova nota
            const int tick = snapTickDown ((int) ((e.position.x - (float) keyboardWidth + scrollX) / pixelsPerTick()));
            if (tick < lengthTicks())
            {
                SequencerEngine::NoteData n;
                n.start = tick;
                n.length = juce::jmin (lastLength, lengthTicks() - tick);
                n.pitch = juce::jlimit (lowestNote, highestNote, (int) pitchAtY (e.position.y));
                n.velocity = SequencerEngine::defaultVelocity;

                if (chordMode)
                {
                    // Un tocco = un accordo intero, costruito sulla scala
                    for (int p : theory::chordAt (n.pitch, engine.getScaleRoot(), engine.getScaleType(), chordType))
                        if ((int) notes.size() < SequencerEngine::maxNotes && p >= 0 && p <= 127)
                        {
                            auto cn = n;
                            cn.pitch = p;
                            notes.push_back (cn);
                        }
                    engine.setNotes (channel, notes);
                    engine.previewChannel (channel, n.pitch);
                }
                else if ((int) notes.size() < SequencerEngine::maxNotes)
                {
                    notes.push_back (n);
                    engine.setNotes (channel, notes);
                    engine.previewChannel (channel, n.pitch);
                }
            }
        }

        if (onEdit)
            onEdit();

        dragIndex = -1;
        repaint();
    }

private:
    enum class DragMode { none, move, resize, emptyArea, scrollKeyboard, band, moveSelection };

    static std::vector<SequencerEngine::NoteData>& clipboard()  { static std::vector<SequencerEngine::NoteData> c; return c; }

    bool isSelected (const SequencerEngine::NoteData& n) const
    {
        return std::find (selection.begin(), selection.end(), n) != selection.end();
    }

    std::vector<SequencerEngine::NoteData> targets() const
    {
        return selection.empty() ? engine.getNotes (channel) : selection;
    }

    void addNotes (const std::vector<SequencerEngine::NoteData>& extra)
    {
        auto notes = engine.getNotes (channel);
        for (auto& n : extra)
            if ((int) notes.size() < SequencerEngine::maxNotes && std::find (notes.begin(), notes.end(), n) == notes.end())
                notes.push_back (n);
        engine.setNotes (channel, notes);
        selection = extra;   // le note nuove restano selezionate (si possono spostare subito)
        if (onEdit) onEdit();
        repaint();
    }

    template <typename Fn>
    void transform (Fn&& fn)
    {
        auto notes = engine.getNotes (channel);
        const auto chosen = targets();
        std::vector<SequencerEngine::NoteData> changed;
        for (auto& n : notes)
            if (std::find (chosen.begin(), chosen.end(), n) != chosen.end())
            {
                fn (n);
                changed.push_back (n);
            }
        engine.setNotes (channel, notes);
        if (! selection.empty())
            selection = changed;
        if (onEdit) onEdit();
        repaint();
    }

    juce::Rectangle<int> getGridArea() const    { return getLocalBounds().withTrimmedLeft (keyboardWidth); }

    int lengthTicks() const                     { return engine.getPatternLength() * SequencerEngine::ticksPerStep; }

    float pixelsPerTick() const
    {
        // 16 step riempiono la larghezza; con 32 step si scorre
        const float stepWidth = juce::jmax (22.0f, (float) getGridArea().getWidth() / 16.0f);
        return stepWidth / (float) SequencerEngine::ticksPerStep;
    }

    float xForTick (int tick) const             { return (float) keyboardWidth + (float) tick * pixelsPerTick() - scrollX; }
    float yForPitch (int pitch) const           { return (float) (highestNote - pitch) * rowHeight - scrollY; }
    float pitchAtY (float y) const              { return (float) highestNote - std::floor ((y + scrollY) / rowHeight); }

    juce::Rectangle<float> noteBounds (const SequencerEngine::NoteData& n) const
    {
        const float x0 = xForTick (n.start), x1 = xForTick (n.start + n.length);
        return { x0 + 1.0f, yForPitch (n.pitch) + 1.0f, juce::jmax (4.0f, x1 - x0 - 2.0f), rowHeight - 2.0f };
    }

    int snapTick (int tick) const               { return (int) std::round ((double) tick / snap) * snap; }
    int snapTickDown (int tick) const           { return (tick / snap) * snap; }

    void clampScroll()
    {
        const float contentH = (float) (highestNote - lowestNote + 1) * rowHeight;
        scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, contentH - (float) getHeight()), scrollY);

        const float contentW = (float) lengthTicks() * pixelsPerTick();
        scrollX = juce::jlimit (0.0f, juce::jmax (0.0f, contentW - (float) getGridArea().getWidth()), scrollX);
    }

    void replaceDraggedNote (const SequencerEngine::NoteData& n)
    {
        auto notes = engine.getNotes (channel);
        if (dragIndex < 0 || dragIndex >= (int) notes.size())
            return;

        notes[(size_t) dragIndex] = n;
        engine.setNotes (channel, notes);
        dragNote = n;
        repaint();
    }

    static bool isBlackKey (int pitch)
    {
        const int k = pitch % 12;
        return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
    }

    void paintKeyboard (juce::Graphics& g)
    {
        auto kb = getLocalBounds().removeFromLeft (keyboardWidth).toFloat();
        g.setColour (Theme::panel);
        g.fillRect (kb);

        for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
        {
            const float y = yForPitch (pitch);
            if (y + rowHeight < 0 || y > (float) getHeight())
                continue;

            auto key = juce::Rectangle<float> (kb.getX(), y, kb.getWidth(), rowHeight).reduced (0.0f, 0.5f);
            const bool black = isBlackKey (pitch);
            g.setColour (black ? juce::Colour (0xff23262a) : juce::Colour (0xffcfd5da));
            g.fillRect (black ? key.withWidth (kb.getWidth() * 0.62f) : key);

            if (pitch % 12 == 0)
            {
                g.setColour (Theme::bg);
                g.setFont (Theme::font (juce::jmin (11.0f, rowHeight - 2.0f), true));
                g.drawText ("C" + juce::String (pitch / 12 - 1), key.reduced (3.0f, 0.0f), juce::Justification::centredRight, false);
            }

            if (pitch == engine.getChannelRootNote (channel))
            {
                g.setColour (Theme::accent);
                g.fillRect (key.removeFromRight (3.0f));
            }
        }

        g.setColour (Theme::border);
        g.drawVerticalLine (keyboardWidth - 1, 0.0f, (float) getHeight());
    }

    SequencerEngine& engine;
    int channel = 0;
    int snap = SequencerEngine::ticksPerStep;           // 1/16
    bool ghostNotes = true;
    int lastLength = SequencerEngine::ticksPerStep * 2;  // 1/8
    float rowHeight = 20.0f;
    float scrollX = 0.0f, scrollY = 0.0f, startScrollX = 0.0f, startScrollY = 0.0f;
    int playTick = -1;

    DragMode dragMode = DragMode::none;
    int dragIndex = -1;
    bool moved = false, selectMode = false, chordMode = false;
    int chordType = theory::triad;
    juce::Point<float> downPos, lastPos;
    std::vector<SequencerEngine::NoteData> selection, originalSelection, originalNotes;
    SequencerEngine::NoteData dragNote, originalNote;
};

//==============================================================================
/**
    Corsia dei volumi sotto il piano roll (come in FL Studio): una barretta per ogni nota.
    Trascina il pallino per cambiare il volume; passando il dito su piu' note si "disegna" (crescendo).
    Se ci sono note selezionate, si modificano solo quelle.
*/
class VelocityLane : public juce::Component
{
public:
    VelocityLane (SequencerEngine& e, PianoRollGrid& g) : engine (e), grid (g) {}

    std::function<void()> onEdit;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b1015));
        g.fillRect (r);
        auto area = barArea();

        // Righe di riferimento (25, 50, 75, 100%)
        for (int i = 1; i <= 4; ++i)
        {
            const float y = area.getBottom() - area.getHeight() * (float) i / 4.0f;
            g.setColour (juce::Colours::white.withAlpha (i == 4 ? 0.08f : 0.04f));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        }

        // Etichetta a sinistra
        auto head = r.withWidth ((float) PianoRollGrid::keyboardWidth);
        g.setColour (juce::Colour (0xff070a0d));
        g.fillRect (head);
        g.setColour (juce::Colour (0xaa4fe3ff));
        g.setFont (Theme::font (9.0f, true).withExtraKerningFactor (0.12f));
        g.drawText ("VOL", head.reduced (2.0f), juce::Justification::centred, false);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.toNearestInt().withTop (0));
        const auto colour = juce::Colour (engine.getChannel (grid.getChannel()).colour.load());
        const bool anySelected = grid.hasSelection();
        for (auto& n : engine.getNotes (grid.getChannel()))
        {
            const float x = grid.tickToX (n.start) + 7.0f;
            if (x < area.getX() - 10.0f || x > area.getRight() + 10.0f)
                continue;
            const float h = area.getHeight() * (float) n.velocity / 127.0f;
            const bool sel = grid.noteSelected (n);
            const auto c = sel ? Theme::accent : (anySelected ? colour.withAlpha (0.4f) : colour);
            juce::Rectangle<float> bar (x - 2.5f, area.getBottom() - h, 5.0f, h);
            g.setGradientFill (juce::ColourGradient (c.brighter (0.6f), bar.getX(), bar.getY(), c.darker (0.2f), bar.getX(), bar.getBottom(), false));
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (c.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x, bar.getY() }));
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, bar.getY() }));
        }

        if (showValue >= 0)
        {
            const auto text = juce::String (showValue) + "  (" + juce::String (juce::roundToInt (showValue * 100.0f / 127.0f)) + "%)";
            auto tag = juce::Rectangle<float> (86.0f, 18.0f).withPosition (juce::jlimit (area.getX(), area.getRight() - 86.0f, showX + 8.0f), area.getY());
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (Theme::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (tag, 5.0f, 1.0f);
            g.setColour (juce::Colour (0xffffd6a8));
            g.setFont (Theme::font (11.0f));
            g.drawText (text, tag, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        before = engine.getNotes (grid.getChannel());
        lastPos = e.position;
        applyBetween (e.position, e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        applyBetween (lastPos, e.position);
        lastPos = e.position;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        grid.refreshSelection (before, engine.getNotes (grid.getChannel()));
        showValue = -1;
        repaint();
        if (onEdit) onEdit();
    }

private:
    juce::Rectangle<float> barArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft ((float) PianoRollGrid::keyboardWidth).reduced (0.0f, 8.0f).withTrimmedTop (4.0f);
    }

    int velocityAt (float y) const
    {
        const auto area = barArea();
        return juce::jlimit (1, 127, juce::roundToInt ((area.getBottom() - y) / area.getHeight() * 127.0f));
    }

    /** Cambia il volume delle note che iniziano tra due punti (il dito che passa sopra), con la linea tra i due. */
    void applyBetween (juce::Point<float> a, juce::Point<float> b)
    {
        auto notes = engine.getNotes (grid.getChannel());
        if (notes.empty())
            return;
        const bool onlySelected = grid.hasSelection();
        const float x0 = juce::jmin (a.x, b.x) - 10.0f, x1 = juce::jmax (a.x, b.x) + 10.0f;

        // Un tocco senza movimento: la nota piu' vicina
        int nearest = -1;
        if (a == b)
        {
            float best = 24.0f;
            for (size_t i = 0; i < notes.size(); ++i)
            {
                if (onlySelected && ! grid.noteSelected (before[i]))
                    continue;
                const float d = std::abs (grid.tickToX (notes[i].start) + 7.0f - a.x);
                if (d < best) { best = d; nearest = (int) i; }
            }
        }

        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const float x = grid.tickToX (notes[i].start) + 7.0f;
            const bool hit = a == b ? (int) i == nearest : (x >= x0 && x <= x1);
            if (! hit || (onlySelected && (i >= before.size() || ! grid.noteSelected (before[i]))))
                continue;
            const float t = std::abs (b.x - a.x) < 1.0f ? 1.0f : juce::jlimit (0.0f, 1.0f, (x - a.x) / (b.x - a.x));
            const int v = velocityAt (a.y + (b.y - a.y) * t);
            if (notes[i].velocity != v)
            {
                notes[i].velocity = v;
                changed = true;
            }
            showValue = v;
            showX = x;
        }
        if (changed)
        {
            engine.setNotes (grid.getChannel(), notes);
            grid.repaint();
        }
        repaint();
    }

    SequencerEngine& engine;
    PianoRollGrid& grid;
    std::vector<SequencerEngine::NoteData> before;
    juce::Point<float> lastPos;
    int showValue = -1;
    float showX = 0.0f;
};

//==============================================================================
/** Il piano roll completo: barra degli strumenti + griglia. Si apre sopra il channel rack. */
class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll (SequencerEngine& e) : engine (e), grid (e)
    {
        addAndMakeVisible (grid);

        snapSwitch.setSelected (2);
        snapSwitch.onChange = [this] (int i) { grid.setSnap (snapTicks[(size_t) i]); };
        addAndMakeVisible (snapSwitch);

        for (auto* b : { &channelButton, &zoomOut, &zoomIn, &clearButton, &closeButton })
            addAndMakeVisible (b);

        channelButton.onClick = [this] { showChannelMenu(); };
        zoomOut.onClick = [this] { grid.setRowHeight (grid.getRowHeight() - 4.0f); };
        zoomIn.onClick  = [this] { grid.setRowHeight (grid.getRowHeight() + 4.0f); };
        clearButton.onClick = [this]
        {
            engine.setNotes (channel, {});
            grid.repaint();
        };
        closeButton.onClick = [this] { if (onClose) onClose(); };

        // Strumenti (seconda riga, scorre col dito se lo schermo e' stretto)
        auto addTool = [this] (const juce::String& text, std::function<void()> fn, int width = 64)
        {
            auto* b = tools.add (new juce::TextButton (text));
            b->onClick = [this, fn] { fn(); updateTools(); };
            b->getProperties().set ("w", width);
            toolStrip.addAndMakeVisible (b);
            return b;
        };
        scaleButton = addTool ("Scala", [this] { showScalePanel(); }, 104);
        chordButton = addTool ("Accordi", [this] { grid.setChordMode (! grid.isChordMode()); }, 72);
        chordButton->setClickingTogglesState (true);
        chordTypeButton = addTool ("Triade", [this] { grid.setChordType ((grid.getChordType() + 1) % theory::numChordTypes); }, 76);
        addTool ("Progressioni", [this] { showProgressionPanel(); }, 100);
        ghostButton = addTool ("Fantasmi", [this] { grid.setGhostNotes (! grid.getGhostNotes()); }, 80);
        ghostButton->setClickingTogglesState (true);
        laneButton = addTool ("Volumi", [this] { laneShown() = ! laneShown(); resized(); }, 72);
        laneButton->setClickingTogglesState (true);
        addChildComponent (lane);
        grid.onPainted = [this] { if (lane.isVisible()) lane.repaint(); };
        selectButton = addTool ("Seleziona", [this] { grid.setSelectMode (! grid.isSelectMode()); grid.setChordMode (false); }, 82);
        selectButton->setClickingTogglesState (true);
        addTool ("Tutto", [this] { grid.setSelectMode (true); grid.selectAll(); });
        addTool ("Copia", [this] { grid.copySelection(); });
        addTool ("Incolla", [this] { grid.paste(); }, 68);
        addTool ("Duplica", [this] { grid.duplicate(); }, 68);
        addTool ("Quantizza", [this] { grid.quantize(); }, 82);
        addTool ("Legato", [this] { grid.legato(); });
        addTool ("-12", [this] { grid.transpose (-12); }, 46);
        addTool ("-1", [this] { grid.transpose (-1); }, 40);
        addTool ("+1", [this] { grid.transpose (1); }, 40);
        addTool ("+12", [this] { grid.transpose (12); }, 46);
        addTool ("Vel -", [this] { grid.changeVelocity (-12); }, 56);
        addTool ("Vel +", [this] { grid.changeVelocity (12); }, 56);
        addTool ("Elimina", [this] { grid.deleteSelection(); }, 70);
        grid.addMouseListener (this, false);   // dopo ogni tocco sulla griglia si aggiorna "Sel. (n)"
        toolView.setViewedComponent (&toolStrip, false);
        toolView.setScrollBarsShown (false, false);
        toolView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (toolView);
        updateTools();
    }

    std::function<void()> onClose;

    void mouseUp (const juce::MouseEvent&) override     { updateTools(); }

    void updateTools()
    {
        chordButton->setToggleState (grid.isChordMode(), juce::dontSendNotification);
        ghostButton->setToggleState (grid.getGhostNotes(), juce::dontSendNotification);
        laneButton->setToggleState (laneShown(), juce::dontSendNotification);
        chordTypeButton->setButtonText (theory::chordTypeName (grid.getChordType()));
        chordTypeButton->setEnabled (grid.isChordMode());
        const int st = engine.getScaleType();
        scaleButton->setButtonText (st > 0 ? juce::String (theory::noteName (engine.getScaleRoot())) + " "
                                             + juce::String (theory::scales()[(size_t) st].name).toLowerCase().substring (0, 9)
                                           : juce::String ("Scala..."));
        selectButton->setToggleState (grid.isSelectMode(), juce::dontSendNotification);
        const int sel = grid.getSelectionSize();
        selectButton->setButtonText (sel > 0 ? "Sel. (" + juce::String (sel) + ")" : juce::String ("Seleziona"));
    }

    void setChannel (int c)
    {
        channel = c;
        grid.setChannel (c);
        channelButton.setButtonText (engine.getChannel (c).name
                                     + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
        grid.centreOnContent();
        updateTools();
        repaint();
    }

    int getChannel() const          { return channel; }
    void showScalePanelForTest()        { showScalePanel(); }
    void showProgressionPanelForTest()  { showProgressionPanel(); }
    PianoRollGrid& getGrid()        { return grid; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), Theme::corner);

        g.setColour (juce::Colour (engine.getChannel (channel).colour.load()));
        g.fillRect (colourMark);

        Theme::drawCaption (g, "Griglia", snapCaption, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        auto bar = r.removeFromTop (30);

        colourMark = bar.removeFromLeft (4).reduced (0, 4).toFloat();
        bar.removeFromLeft (6);
        channelButton.setBounds (bar.removeFromLeft (120).reduced (0, 1));
        bar.removeFromLeft (10);

        closeButton.setBounds (bar.removeFromRight (72).reduced (0, 1));
        bar.removeFromRight (6);
        clearButton.setBounds (bar.removeFromRight (72).reduced (0, 1));
        bar.removeFromRight (10);
        zoomIn.setBounds (bar.removeFromRight (32).reduced (0, 1));
        bar.removeFromRight (3);
        zoomOut.setBounds (bar.removeFromRight (32).reduced (0, 1));
        bar.removeFromRight (10);

        if (bar.getWidth() >= 200)
        {
            snapCaption = bar.removeFromLeft (52).toFloat();
            bar.removeFromLeft (6);
            snapSwitch.setBounds (bar.removeFromLeft (juce::jmin (260, bar.getWidth())).reduced (0, 1));
        }
        else
        {
            // Schermo stretto (verticale): la griglia ha una riga sua
            r.removeFromTop (4);
            auto snapRow = r.removeFromTop (28);
            snapCaption = snapRow.removeFromLeft (52).toFloat();
            snapRow.removeFromLeft (6);
            snapSwitch.setBounds (snapRow.reduced (0, 1));
        }

        r.removeFromTop (4);
        auto toolRow = r.removeFromTop (30);
        toolView.setBounds (toolRow);
        int x = 0;
        for (auto* b : tools)
        {
            const int w = (int) b->getProperties()["w"];
            b->setBounds (x, 1, w, toolRow.getHeight() - 2);
            x += w + 4;
        }
        toolStrip.setSize (juce::jmax (x, toolRow.getWidth()), toolRow.getHeight());

        r.removeFromTop (4);
        // Corsia dei volumi sotto la griglia (si nasconde col pulsante "Volumi")
        lane.setVisible (laneShown() && r.getHeight() > 200);
        if (lane.isVisible())
        {
            lane.setBounds (r.removeFromBottom (juce::jlimit (64, 130, r.getHeight() / 4)));
            r.removeFromBottom (4);
        }
        grid.setBounds (r);
    }

private:
    void showScalePanel()
    {
        auto panel = std::make_unique<ScalePanel> (engine);
        auto* raw = panel.get();
        juce::Component::SafePointer<PianoRoll> safe (this);
        raw->onChanged = [safe] { if (safe != nullptr) { safe->grid.repaint(); safe->updateTools(); } };
        auto* overlay = CenteredOverlay::show (*this, std::move (panel), [raw] (int w) { return raw->preferredHeight (w); }, 560);
        juce::Component::SafePointer<CenteredOverlay> safeOverlay (overlay);
        raw->onDone = [safeOverlay] { if (safeOverlay != nullptr) safeOverlay->close(); };
    }

    void showProgressionPanel()
    {
        auto panel = std::make_unique<ProgressionPanel> (engine, channel);
        auto* raw = panel.get();
        juce::Component::SafePointer<PianoRoll> safe (this);
        raw->onApplied = [safe] { if (safe != nullptr) { safe->grid.centreOnContent(); safe->grid.repaint(); safe->updateTools(); } };
        auto* overlay = CenteredOverlay::show (*this, std::move (panel), [raw] (int w) { return raw->preferredHeight (w); }, 600);
        juce::Component::SafePointer<CenteredOverlay> safeOverlay (overlay);
        raw->onDone = [safeOverlay] { if (safeOverlay != nullptr) safeOverlay->close(); };
    }

    void showChannelMenu()
    {
        juce::PopupMenu menu;
        for (int c = 0; c < engine.getNumChannels(); ++c)
            menu.addItem (c + 1, engine.getChannel (c).name, true, c == channel);

        juce::Component::SafePointer<PianoRoll> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&channelButton), [safeThis] (int result)
        {
            if (safeThis != nullptr && result > 0)
                safeThis->setChannel (result - 1);
        });
    }

    SequencerEngine& engine;
    PianoRollGrid grid;
    VelocityLane lane { engine, grid };
    static bool& laneShown()        { static bool shown = true; return shown; }
    int channel = 0;

    // Griglia: 1/4, 1/8, 1/16, 1/32 e terzine di sedicesimo (in tick)
    const std::array<int, 5> snapTicks { 48, 24, 12, 6, 8 };
    SegmentedControl snapSwitch { { "1/4", "1/8", "1/16", "1/32", "1/16T" } };
    juce::TextButton channelButton, zoomOut { "-" }, zoomIn { "+" }, clearButton { "Svuota" }, closeButton { "Chiudi" };
    juce::Rectangle<float> colourMark, snapCaption;
    juce::Viewport toolView;
    juce::Component toolStrip;
    juce::OwnedArray<juce::TextButton> tools;
    juce::TextButton* selectButton = nullptr, *scaleButton = nullptr, *chordButton = nullptr, *chordTypeButton = nullptr, *ghostButton = nullptr, *laneButton = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
