#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"
#include "Widgets.h"

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
        channel = c;
        repaint();
    }

    void setSnap (int ticks)        { snap = ticks; repaint(); }

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
            g.setColour (isBlackKey (pitch) ? Theme::bg.darker (0.25f) : Theme::panel);
            g.fillRect (row);
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
            g.setColour (bar ? Theme::textDim.withAlpha (0.6f) : beat ? Theme::raisedHi : Theme::raised.withAlpha (0.6f));
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

        // Note
        const auto colour = juce::Colour (engine.getChannel (channel).colour.load());
        for (auto& n : engine.getNotes (channel))
        {
            auto r = noteBounds (n);
            if (! r.intersects (grid.toFloat()))
                continue;

            const bool selected = dragIndex >= 0 && n == dragNote;
            auto fill = colour.interpolatedWith (Theme::stepOn, 0.35f).withMultipliedBrightness (0.65f + 0.35f * (float) n.velocity / 127.0f);
            Theme::drawRaised (g, r, selected ? fill.brighter (0.3f) : fill, 2.0f);

            // Maniglia per allungare
            g.setColour (Theme::bg.withAlpha (0.45f));
            g.fillRect (r.removeFromRight (juce::jmin (5.0f, r.getWidth() * 0.25f)).reduced (0.0f, 3.0f));
        }

        // Linea di riproduzione
        if (playTick >= 0)
        {
            const float x = xForTick (playTick);
            if (x >= (float) grid.getX())
            {
                g.setColour (Theme::accent);
                g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
            }
        }

        paintKeyboard (g);
    }

    void resized() override
    {
        clampScroll();
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
                dragMode = e.position.x > r.getRight() - handle ? DragMode::resize : DragMode::move;
                repaint();
                return;
            }
        }

        dragMode = DragMode::emptyArea;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto delta = e.position - downPos;
        if (! moved && delta.getDistanceFromOrigin() < 8.0f)
            return;

        moved = true;

        switch (dragMode)
        {
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
        if (moved || dragMode == DragMode::scrollKeyboard)
        {
            dragIndex = -1;
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

                if ((int) notes.size() < SequencerEngine::maxNotes)
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
    enum class DragMode { none, move, resize, emptyArea, scrollKeyboard };

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
    int lastLength = SequencerEngine::ticksPerStep * 2;  // 1/8
    float rowHeight = 20.0f;
    float scrollX = 0.0f, scrollY = 0.0f, startScrollX = 0.0f, startScrollY = 0.0f;
    int playTick = -1;

    DragMode dragMode = DragMode::none;
    int dragIndex = -1;
    bool moved = false;
    juce::Point<float> downPos;
    SequencerEngine::NoteData dragNote, originalNote;
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
    }

    std::function<void()> onClose;

    void setChannel (int c)
    {
        channel = c;
        grid.setChannel (c);
        channelButton.setButtonText (engine.getChannel (c).name
                                     + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
        grid.centreOnContent();
        repaint();
    }

    int getChannel() const          { return channel; }
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

        snapCaption = bar.removeFromLeft (52).toFloat();
        bar.removeFromLeft (6);
        snapSwitch.setBounds (bar.removeFromLeft (juce::jmin (260, bar.getWidth())).reduced (0, 1));

        r.removeFromTop (4);
        grid.setBounds (r);
    }

private:
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
    int channel = 0;

    // Griglia: 1/4, 1/8, 1/16, 1/32 e terzine di sedicesimo (in tick)
    const std::array<int, 5> snapTicks { 48, 24, 12, 6, 8 };
    SegmentedControl snapSwitch { { "1/4", "1/8", "1/16", "1/32", "1/16T" } };
    juce::TextButton channelButton, zoomOut { "-" }, zoomIn { "+" }, clearButton { "Svuota" }, closeButton { "Chiudi" };
    juce::Rectangle<float> colourMark, snapCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
