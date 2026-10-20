#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** Colori dei pattern nella playlist (come in FL Studio, ogni pattern ha il suo). */
namespace PatternColours
{
    inline juce::Colour get (int pattern)
    {
        static const juce::uint32 colours[] =
        {
            0xff5b8fd6, 0xffe8833a, 0xff6fbf5a, 0xffd2555a, 0xff9a78d0, 0xffd8b14a, 0xff4fb3a9, 0xffe36fae,
            0xff8fb84a, 0xffc9744a, 0xff5aa6c9, 0xffb06cc4, 0xffd99a5b, 0xff57b98a, 0xffcf6a7f, 0xff7f8fd6
        };
        constexpr int n = (int) std::size (colours);
        const int p = juce::jmax (0, pattern);
        // dopo i primi 16 i colori si ripetono, un po' piu' chiari o scuri
        const auto base = juce::Colour (colours[p % n]);
        const int round = (p / n) % 3;
        return round == 0 ? base : round == 1 ? base.brighter (0.25f) : base.darker (0.2f);
    }

    inline juce::String name (const SequencerEngine& engine, int pattern)   { return engine.getPatternName (pattern); }

    /** Quanti pattern mostrare nei menu: quelli usati, con un nome, nella playlist, e quello aperto. */
    inline int count (const SequencerEngine& engine)
    {
        int n = juce::jmax (engine.getUsedPatternCount(), engine.getCurrentPattern() + 1);
        for (const auto& clip : engine.getClips())
            n = juce::jmax (n, clip.pattern + 1);
        for (int p = n; p < SequencerEngine::numPatterns; ++p)
            if (engine.getCustomPatternName (p).isNotEmpty())
                n = p + 1;
        return juce::jmin (n, SequencerEngine::numPatterns);
    }
}

//==============================================================================
/**
    La griglia della playlist: tracce in verticale, battute in orizzontale.

    - Tocca una cella vuota: mette il pattern scelto (il "pennello")
    - Tocca una clip: la toglie
    - Trascina una clip: la sposta (battuta e traccia)
    - Tieni premuta una clip: menu (modifica pattern, cambia, duplica, elimina)
    - Trascina sul vuoto: scorre la griglia
    - Tocca il righello: la canzone parte da quella battuta
*/
class PlaylistGrid : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int headerWidth = 54, rulerHeight = 24;

    explicit PlaylistGrid (SequencerEngine& e) : engine (e) {}

    std::function<void()> onEdit;                       // per la vibrazione
    std::function<void (int pattern)> onEditPattern;    // "Modifica pattern"
    std::function<void (int pattern)> onBrushChanged;

    void setBrush (int pattern)     { brush = juce::jlimit (0, SequencerEngine::numPatterns - 1, pattern); repaint(); }
    int getBrush() const            { return brush; }

    void setBarWidth (float w)
    {
        const float centreBar = (scrollX + gridWidth() * 0.5f) / barWidth;
        barWidth = juce::jlimit (24.0f, 120.0f, w);
        scrollX = centreBar * barWidth - gridWidth() * 0.5f;
        clampScroll();
        repaint();
    }

    float getBarWidth() const       { return barWidth; }

    /** Chiamato dal timer della schermata: playhead e scorrimento automatico. */
    void refresh()
    {
        const int step = engine.getSongStep();
        if (step != shownStep)
        {
            shownStep = step;

            // Segue il playhead quando esce dalla vista (se l'utente non sta toccando la griglia)
            if (step >= 0 && ! isMouseButtonDown())
            {
                const float x = (float) step / SequencerEngine::stepsPerBar * barWidth;
                if (x < scrollX || x > scrollX + gridWidth() - 10.0f)
                {
                    scrollX = x - 20.0f;
                    clampScroll();
                }
            }
            repaint();
        }

        // Modifiche fatte altrove (undo, caricamento progetto, pattern cambiati)
        juce::uint64 hash = (juce::uint64) engine.getSongStartBar() + 7;
        for (auto& c : engine.getClips())
            hash = hash * 1099511628211ull + (juce::uint64) SequencerEngine::packClip (c);
        const int shown = PatternColours::count (engine);
        hash = hash * 131 + (juce::uint64) shown;
        for (int p = 0; p < shown; ++p)
            hash = hash * 31 + (juce::uint64) engine.getPattern (p).length.load() + (engine.isPatternEmpty (p) ? 1u : 0u)
                 + (juce::uint64) engine.getCustomPatternName (p).hashCode64();
        if (hash != shownHash)
        {
            shownHash = hash;
            repaint();
        }
    }

    void resized() override
    {
        trackHeight = juce::jlimit (34.0f, 56.0f, (float) (getHeight() - rulerHeight) / 8.0f);
        clampScroll();
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        const auto clips = engine.getClips();
        const int bars = visibleBars();
        const float gx = (float) headerWidth, gy = (float) rulerHeight;

        // Tracce (righe alternate)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (headerWidth, rulerHeight, getWidth() - headerWidth, getHeight() - rulerHeight);

            for (int t = 0; t < SequencerEngine::numTracks; ++t)
            {
                const float y = gy + (float) t * trackHeight - scrollY;
                g.setColour (t % 2 == 0 ? Theme::panel : Theme::panel.darker (0.12f));
                g.fillRect (gx, y, (float) getWidth() - gx, trackHeight);
            }

            // Righe delle battute (piu' marcate ogni 4)
            const int firstBar = (int) (scrollX / barWidth);
            for (int b = firstBar; b <= firstBar + bars + 1; ++b)
            {
                const float x = gx + (float) b * barWidth - scrollX;
                g.setColour (b % 4 == 0 ? Theme::raisedHi : Theme::raised.darker (0.15f));
                g.drawVerticalLine ((int) x, gy, (float) getHeight());
            }

            // Fine della canzone
            const int songEnd = engine.getSongLengthSteps();
            if (songEnd > 0)
            {
                const float x = gx + (float) songEnd / SequencerEngine::stepsPerBar * barWidth - scrollX;
                g.setColour (Theme::bg.withAlpha (0.45f));
                g.fillRect (x, gy, (float) getWidth() - x, (float) getHeight() - gy);
            }

            // Clip
            for (int i = 0; i < (int) clips.size(); ++i)
            {
                if (i == dragIndex && dragging)
                    continue;
                paintClip (g, clipBounds (clips[(size_t) i]), clips[(size_t) i].pattern, longPressed && i == dragIndex);
            }

            if (dragging && dragIndex >= 0 && dragIndex < (int) clips.size())
            {
                auto c = clips[(size_t) dragIndex];
                c.bar = dragBar; c.track = dragTrack;
                paintClip (g, clipBounds (c), c.pattern, true);
            }

            // Playhead
            if (shownStep >= 0)
            {
                const float x = gx + (float) shownStep / SequencerEngine::stepsPerBar * barWidth - scrollX;
                g.setColour (Theme::accent);
                g.fillRect (x - 0.75f, gy, 1.5f, (float) getHeight() - gy);
            }

            if (clips.empty())
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (14.0f));
                g.drawFittedText ("Tocca la griglia per mettere il pattern scelto in alto (" + PatternColours::name (engine, brush) + ").\n"
                                  "Tocca una clip per toglierla, trascinala per spostarla, tienila premuta per il menu.",
                                  juce::Rectangle<int> (headerWidth + 12, rulerHeight + (int) trackHeight * 2, getWidth() - headerWidth - 24, 60),
                                  juce::Justification::centredLeft, 3);
            }
        }

        // Righello con i numeri delle battute e il punto di partenza
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (headerWidth, 0, getWidth() - headerWidth, rulerHeight);
            g.setColour (Theme::panel.darker (0.3f));
            g.fillRect (0, 0, getWidth(), rulerHeight);

            const int firstBar = (int) (scrollX / barWidth);
            const int labelEvery = barWidth < 36.0f ? 4 : (barWidth < 60.0f ? 2 : 1);
            g.setFont (Theme::font (11.0f, true));
            for (int b = firstBar; b <= firstBar + bars + 1; ++b)
            {
                const float x = gx + (float) b * barWidth - scrollX;
                g.setColour (Theme::raisedHi);
                g.drawVerticalLine ((int) x, (float) rulerHeight - 7.0f, (float) rulerHeight);
                if (b % labelEvery == 0)
                {
                    g.setColour (Theme::textDim);
                    g.drawText (juce::String (b + 1), (int) x + 3, 2, 40, rulerHeight - 6, juce::Justification::centredLeft, false);
                }
            }

            const float sx = gx + (float) engine.getSongStartBar() * barWidth - scrollX;
            juce::Path tri;
            tri.addTriangle (sx, (float) rulerHeight - 1.0f, sx, 4.0f, sx + 9.0f, (float) rulerHeight * 0.5f + 1.5f);
            g.setColour (Theme::accent);
            g.fillPath (tri);
        }

        // Intestazioni delle tracce
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, rulerHeight, headerWidth, getHeight() - rulerHeight);
            for (int t = 0; t < SequencerEngine::numTracks; ++t)
            {
                auto r = juce::Rectangle<float> (0.0f, gy + (float) t * trackHeight - scrollY, gx - 2.0f, trackHeight).reduced (2.0f, 1.5f);
                Theme::drawRaised (g, r, Theme::raised);
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (11.0f, true));
                g.drawText ("Tr " + juce::String (t + 1), r, juce::Justification::centred, false);
            }
        }

        g.setColour (Theme::panel.darker (0.3f));
        g.fillRect (0, 0, headerWidth, rulerHeight);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.0f, true));
        g.drawText ("BATT.", 0, 0, headerWidth, rulerHeight, juce::Justification::centred, false);
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        downPos = e.position;
        startScrollX = scrollX;
        startScrollY = scrollY;
        dragging = scrolling = longPressed = false;
        dragIndex = clipAt (e.position);
        onRuler = e.position.y < (float) rulerHeight && e.position.x > (float) headerWidth;

        if (dragIndex >= 0)
            startTimer (500);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (longPressed)
            return;

        const auto delta = e.position - downPos;
        if (! dragging && ! scrolling && delta.getDistanceFromOrigin() < 8.0f)
            return;

        stopTimer();

        if (dragIndex >= 0 && ! scrolling)
        {
            const auto clips = engine.getClips();
            if (dragIndex >= (int) clips.size())
                return;

            dragging = true;
            const auto& c = clips[(size_t) dragIndex];
            dragBar   = juce::jlimit (0, SequencerEngine::maxBars - 1, c.bar + juce::roundToInt (delta.x / barWidth));
            dragTrack = juce::jlimit (0, SequencerEngine::numTracks - 1, c.track + juce::roundToInt (delta.y / trackHeight));
            repaint();
            return;
        }

        scrolling = true;
        scrollX = startScrollX - delta.x;
        if (! onRuler)
            scrollY = startScrollY - delta.y;
        clampScroll();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();

        if (longPressed)
        {
            showClipMenu (dragIndex);   // il menu si apre al rilascio (il dito non sceglie una voce per sbaglio)
            longPressed = false;
            dragIndex = -1;
            repaint();
            return;
        }

        auto clips = engine.getClips();

        if (dragging)
        {
            if (dragIndex >= 0 && dragIndex < (int) clips.size())
            {
                clips[(size_t) dragIndex].bar = dragBar;
                clips[(size_t) dragIndex].track = dragTrack;
                engine.setClips (clips);
                if (onEdit) onEdit();
            }
            dragging = false;
            dragIndex = -1;
            repaint();
            return;
        }

        if (scrolling)
            return;

        if (onRuler)
        {
            engine.setSongPosition (barAt (e.position.x));
            if (onEdit) onEdit();
            repaint();
            return;
        }

        if (e.position.x < (float) headerWidth || e.position.y < (float) rulerHeight)
            return;

        if (dragIndex >= 0 && dragIndex < (int) clips.size())
        {
            clips.erase (clips.begin() + dragIndex);   // tocco su una clip: la toglie
        }
        else
        {
            const int track = trackAt (e.position.y);
            if (track < 0 || (int) clips.size() >= SequencerEngine::maxClips)
                return;
            clips.push_back ({ track, barAt (e.position.x), brush });
        }

        engine.setClips (clips);
        dragIndex = -1;
        if (onEdit) onEdit();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scrollX -= (w.deltaX != 0.0f ? w.deltaX : 0.0f) * 300.0f;
        scrollY -= w.deltaY * 200.0f;
        clampScroll();
        repaint();
    }

    void showClipMenu (int index)
    {
        auto clips = engine.getClips();
        if (index < 0 || index >= (int) clips.size())
            return;

        const auto clip = clips[(size_t) index];
        juce::PopupMenu change;
        for (int p = 0; p < PatternColours::count (engine); ++p)
            change.addColouredItem (100 + p, PatternColours::name (engine, p), PatternColours::get (p), true, p == clip.pattern);

        juce::PopupMenu menu;
        menu.addSectionHeader (PatternColours::name (engine, clip.pattern) + "  -  battuta " + juce::String (clip.bar + 1));
        menu.addItem (1, "Modifica questo pattern");
        menu.addItem (2, "Usa come pennello");
        menu.addSubMenu ("Cambia in", change);
        menu.addItem (3, "Duplica a destra");
        menu.addSeparator();
        menu.addItem (4, "Elimina");

        juce::Component::SafePointer<PlaylistGrid> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                      .withTargetScreenArea (localAreaToGlobal (clipBounds (clip).toNearestInt())),
                            [safeThis, index, clip] (int result)
                            {
                                if (safeThis == nullptr || result <= 0)
                                    return;

                                auto& self = *safeThis;
                                auto list = self.engine.getClips();
                                if (index >= (int) list.size() || ! (list[(size_t) index] == clip))
                                    return;

                                if (result == 1)
                                {
                                    if (self.onEditPattern) self.onEditPattern (clip.pattern);
                                    return;
                                }
                                if (result == 2)
                                {
                                    self.setBrush (clip.pattern);
                                    if (self.onBrushChanged) self.onBrushChanged (clip.pattern);
                                    return;
                                }

                                if (result == 3)
                                {
                                    auto copy = clip;
                                    copy.bar = juce::jmin (SequencerEngine::maxBars - 1, clip.bar + self.clipBars (clip.pattern));
                                    list.push_back (copy);
                                }
                                else if (result == 4)
                                    list.erase (list.begin() + index);
                                else if (result >= 100)
                                    list[(size_t) index].pattern = result - 100;

                                self.engine.setClips (list);
                                if (self.onEdit) self.onEdit();
                                self.repaint();
                            });
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (dragIndex >= 0 && ! dragging && ! scrolling)
        {
            longPressed = true;   // vibra subito; il menu si apre quando si alza il dito
            if (onEdit) onEdit();
            repaint();
        }
    }

    float gridWidth() const     { return (float) (getWidth() - headerWidth); }
    int visibleBars() const     { return (int) (gridWidth() / barWidth) + 1; }

    int clipBars (int pattern) const
    {
        return juce::jmax (1, (engine.getPattern (pattern).length.load() + SequencerEngine::stepsPerBar - 1) / SequencerEngine::stepsPerBar);
    }

    juce::Rectangle<float> clipBounds (const SequencerEngine::Clip& c) const
    {
        return { (float) headerWidth + (float) c.bar * barWidth - scrollX + 1.0f,
                 (float) rulerHeight + (float) c.track * trackHeight - scrollY + 2.0f,
                 (float) clipBars (c.pattern) * barWidth - 2.0f, trackHeight - 4.0f };
    }

    int clipAt (juce::Point<float> p) const
    {
        if (p.x < (float) headerWidth || p.y < (float) rulerHeight)
            return -1;

        const auto clips = engine.getClips();
        for (int i = (int) clips.size() - 1; i >= 0; --i)   // quella disegnata sopra per ultima
            if (clipBounds (clips[(size_t) i]).contains (p))
                return i;
        return -1;
    }

    int barAt (float x) const
    {
        return juce::jlimit (0, SequencerEngine::maxBars - 1, (int) std::floor ((x - (float) headerWidth + scrollX) / barWidth));
    }

    int trackAt (float y) const
    {
        const int t = (int) std::floor ((y - (float) rulerHeight + scrollY) / trackHeight);
        return t >= 0 && t < SequencerEngine::numTracks ? t : -1;
    }

    void clampScroll()
    {
        scrollX = juce::jlimit (0.0f, juce::jmax (0.0f, (float) SequencerEngine::maxBars * barWidth - gridWidth()), scrollX);
        scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, (float) SequencerEngine::numTracks * trackHeight - (float) (getHeight() - rulerHeight)), scrollY);
    }

    void paintClip (juce::Graphics& g, juce::Rectangle<float> r, int pattern, bool ghost)
    {
        const auto colour = PatternColours::get (pattern);
        g.setColour (colour.withAlpha (ghost ? 0.5f : 0.85f).darker (0.35f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colour.withAlpha (ghost ? 0.8f : 1.0f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, ghost ? 2.0f : 1.0f);

        // Anteprima: gli step del pattern (qualsiasi canale) come piccole tacche
        auto& pat = engine.getPattern (pattern);
        const int len = pat.length.load();
        auto inner = r.reduced (4.0f, 3.0f);
        auto label = inner.removeFromTop (juce::jmin (14.0f, inner.getHeight() * 0.5f));
        const float sw = inner.getWidth() / (float) len;
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        for (int s = 0; s < len; ++s)
        {
            bool on = false;
            for (int c = 0; c < engine.getNumChannels() && ! on; ++c)
                on = pat.velocity[(size_t) c][(size_t) s].load() > 0 || pat.noteCount[(size_t) c].load() > 0;
            if (on && sw >= 1.5f)
                g.fillRect (inner.getX() + (float) s * sw + sw * 0.2f, inner.getBottom() - inner.getHeight() * 0.6f, juce::jmax (1.0f, sw * 0.6f), inner.getHeight() * 0.6f);
        }

        g.setColour (Theme::textBright);
        g.setFont (Theme::font (11.0f, true));
        g.drawText (r.getWidth() > 64.0f ? PatternColours::name (engine, pattern) : juce::String (pattern + 1), label, juce::Justification::centredLeft, true);
    }

    SequencerEngine& engine;
    int brush = 0;
    float barWidth = 56.0f, trackHeight = 40.0f, scrollX = 0.0f, scrollY = 0.0f;
    float startScrollX = 0.0f, startScrollY = 0.0f;
    juce::Point<float> downPos;
    int dragIndex = -1, dragBar = 0, dragTrack = 0, shownStep = -2;
    bool dragging = false, scrolling = false, longPressed = false, onRuler = false;
    juce::uint64 shownHash = 0;
};

//==============================================================================
/** La playlist: barra (pennello, zoom, svuota, chiudi) + griglia. Si apre sopra il channel rack. */
class Playlist : public juce::Component
{
public:
    explicit Playlist (SequencerEngine& e) : engine (e), grid (e)
    {
        for (auto* b : { &brushButton, &zoomOut, &zoomIn, &clearButton, &closeButton })
            addAndMakeVisible (b);

        brushButton.onClick = [this] { showBrushMenu(); };
        zoomOut.onClick = [this] { grid.setBarWidth (grid.getBarWidth() - 12.0f); };
        zoomIn.onClick  = [this] { grid.setBarWidth (grid.getBarWidth() + 12.0f); };
        clearButton.onClick = [this]
        {
            engine.setClips ({});
            engine.setSongPosition (0);
            grid.repaint();
        };
        closeButton.onClick = [this] { if (onClose) onClose(); };
        grid.onBrushChanged = [this] (int) { updateBrushButton(); };
        addAndMakeVisible (grid);
        updateBrushButton();
    }

    std::function<void()> onClose;
    PlaylistGrid& getGrid()         { return grid; }

    /** Il pennello parte dal pattern che si sta modificando. */
    void setBrush (int pattern)     { grid.setBrush (pattern); updateBrushButton(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), Theme::corner, 1.0f);

        auto title = titleArea;
        g.setColour (Theme::accent);
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 6.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (16.0f, true));
        g.drawFittedText ("Playlist", title.toNearestInt(), juce::Justification::centredLeft, 1);
        Theme::drawCaption (g, "Pennello", brushCaption, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto bar = r.removeFromTop (32);
        const bool narrow = getWidth() < 520;

        closeButton.setBounds (bar.removeFromRight (narrow ? 64 : 76).reduced (0, 2));
        bar.removeFromRight (6);
        clearButton.setBounds (bar.removeFromRight (narrow ? 60 : 70).reduced (0, 2));
        bar.removeFromRight (8);
        zoomIn.setBounds (bar.removeFromRight (34).reduced (0, 2));
        bar.removeFromRight (3);
        zoomOut.setBounds (bar.removeFromRight (34).reduced (0, 2));
        bar.removeFromRight (8);

        titleArea = bar.removeFromLeft (narrow ? 0 : 90).toFloat();
        brushButton.setBounds (bar.removeFromRight (juce::jmin (150, bar.getWidth())).reduced (0, 2));
        brushCaption = bar.toFloat().withTrimmedRight (6.0f);
        if (narrow) brushCaption = {};

        r.removeFromTop (4);
        grid.setBounds (r);
    }

private:
    void updateBrushButton()
    {
        brushButton.setButtonText (PatternColours::name (engine, grid.getBrush()) + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
        brushButton.setColour (juce::TextButton::buttonColourId, PatternColours::get (grid.getBrush()).darker (0.55f));
    }

    void showBrushMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Pattern da mettere nella playlist");
        const int shown = PatternColours::count (engine);
        for (int p = 0; p < shown; ++p)
            menu.addColouredItem (p + 1, PatternColours::name (engine, p) + (engine.isPatternEmpty (p) ? "  (vuoto)" : ""),
                                  PatternColours::get (p), true, p == grid.getBrush());
        if (shown < SequencerEngine::numPatterns && grid.getBrush() < shown)
            menu.addColouredItem (shown + 1, "Pattern " + juce::String (shown + 1) + "  (nuovo)", PatternColours::get (shown), true, false);

        juce::Component::SafePointer<Playlist> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&brushButton), [safeThis] (int result)
        {
            if (safeThis != nullptr && result > 0)
                safeThis->setBrush (result - 1);
        });
    }

    SequencerEngine& engine;
    PlaylistGrid grid;
    juce::TextButton brushButton, zoomOut { "-" }, zoomIn { "+" }, clearButton { "Svuota" }, closeButton { "Chiudi" };
    juce::Rectangle<float> titleArea, brushCaption;
};
