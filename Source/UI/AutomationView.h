#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "SoundBrowser.h"   // TileList
#include "Theme.h"

//==============================================================================
/** Nomi e valori leggibili delle automazioni. */
namespace AutomationText
{
    inline juce::String dbText (float gain)
    {
        if (gain <= 0.0001f) return "-inf";
        const float db = juce::Decibels::gainToDecibels (gain);
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    inline juce::String panText (float v)
    {
        const int x = juce::roundToInt ((-1.0f + 2.0f * v) * 100.0f);
        return x == 0 ? juce::String ("Centro") : x < 0 ? "S " + juce::String (-x) : "D " + juce::String (x);
    }

    /** Da dove viene il bersaglio ("Canale 3: Snare", "Mixer 2: Kick", "Master"). */
    inline juce::String source (SequencerEngine& e, const automation::Target& t)
    {
        using namespace automation;
        switch (t.kind)
        {
            case master:          return "Master";
            case channelParam:
            case instrumentParam: return t.a < e.getNumChannels() ? e.getChannel (t.a).name : juce::String ("Canale");
            case trackParam:
            case effectParam:     return t.a == 0 ? juce::String ("Mixer: Master") : "Mixer " + juce::String (t.a) + ": " + e.getTrackName (t.a);
            default:              return {};
        }
    }

    inline juce::String parameter (SequencerEngine& e, const automation::Target& t)
    {
        using namespace automation;
        switch (t.kind)
        {
            case master:       return "Volume";
            case channelParam: return channelParamName (t.b);
            case trackParam:   return trackParamName (t.b);
            case effectParam:
            {
                const auto& slot = e.getTrack (t.a).slots[(size_t) juce::jlimit (0, mixer::numSlots - 1, t.b)];
                const auto& inf = mixer::info (slot.type.load());
                return juce::String (inf.name) + ": " + (t.c < inf.numParams ? juce::String (inf.params[(size_t) t.c].name) : juce::String ("?"));
            }
            case instrumentParam:
                if (auto* proc = t.a < e.getNumChannels() ? e.getInstrument (t.a) : nullptr)
                    if (t.b >= 0 && t.b < proc->getParameters().size())
                        return proc->getParameters()[t.b]->getName (40);
                return "Parametro";
            default: return {};
        }
    }

    inline juce::String value (SequencerEngine& e, const automation::Target& t, float v)
    {
        using namespace automation;
        switch (t.kind)
        {
            case master: return dbText (volumeGain (v, 3.5f));
            case channelParam:
                switch (t.b)
                {
                    case chVolume: return dbText (volumeGain (v, 0.0f));
                    case chPan:    return panText (v);
                    case chPitch:  return juce::String (-12.0f + 24.0f * v, 1) + " st";
                    default:       return juce::String (juce::roundToInt (v * 100.0f)) + "%";
                }
            case trackParam:
                switch (t.b)
                {
                    case trVolume: return dbText (volumeGain (v, 4.0f));
                    case trPan:    return panText (v);
                    default:       return juce::String (-15.0f + 30.0f * v, 1) + " dB";
                }
            case effectParam:
            {
                const auto& slot = e.getTrack (t.a).slots[(size_t) juce::jlimit (0, mixer::numSlots - 1, t.b)];
                const auto& inf = mixer::info (slot.type.load());
                if (t.c < inf.numParams)
                    return mixer::formatValue (inf.params[(size_t) t.c], mixer::fromNormalised (inf.params[(size_t) t.c], v));
                return {};
            }
            case instrumentParam:
                if (auto* proc = t.a < e.getNumChannels() ? e.getInstrument (t.a) : nullptr)
                    if (t.b >= 0 && t.b < proc->getParameters().size())
                    {
                        auto* p = proc->getParameters()[t.b];
                        return (p->getText (v, 24) + " " + p->getLabel()).trim();
                    }
                return juce::String (juce::roundToInt (v * 100.0f)) + "%";
            default: return {};
        }
    }

    /** Il valore attuale del controllo (0..1), per partire da li' con la nuova curva. */
    inline float current (SequencerEngine& e, const automation::Target& t)
    {
        using namespace automation;
        switch (t.kind)
        {
            case master: return volumeNorm (e.getMasterVolume(), 3.5f);
            case channelParam:
            {
                auto& ch = e.getChannel (t.a);
                switch (t.b)
                {
                    case chVolume: return volumeNorm (ch.volume.load(), 0.0f);
                    case chPan:    return (ch.pan.load() + 1.0f) * 0.5f;
                    case chFilter: return ch.filter.load();
                    case chReverb: return ch.reverb.load();
                    case chDelay:  return ch.delay.load();
                    case chPitch:  return (ch.pitch.load() + 12.0f) / 24.0f;
                    default:       return 0.5f;
                }
            }
            case trackParam:
            {
                auto& tr = e.getTrack (t.a);
                switch (t.b)
                {
                    case trVolume: return volumeNorm (tr.volume.load(), 4.0f);
                    case trPan:    return (tr.pan.load() + 1.0f) * 0.5f;
                    case trEqLow:  return (tr.eqLow.load() + 15.0f) / 30.0f;
                    case trEqMid:  return (tr.eqMid.load() + 15.0f) / 30.0f;
                    case trEqHigh: return (tr.eqHigh.load() + 15.0f) / 30.0f;
                    default:       return 0.5f;
                }
            }
            case effectParam:
            {
                auto& slot = e.getTrack (t.a).slots[(size_t) juce::jlimit (0, mixer::numSlots - 1, t.b)];
                const auto& inf = mixer::info (slot.type.load());
                return t.c < inf.numParams ? mixer::toNormalised (inf.params[(size_t) t.c], slot.params[(size_t) t.c].load()) : 0.5f;
            }
            case instrumentParam:
                if (auto* proc = e.getInstrument (t.a))
                    if (t.b >= 0 && t.b < proc->getParameters().size())
                        return proc->getParameters()[t.b]->getValue();
                return 0.5f;
            default: return 0.5f;
        }
    }

    inline juce::Colour colour (SequencerEngine& e, const automation::Target& t)
    {
        using namespace automation;
        if (t.kind == channelParam || t.kind == instrumentParam)
            return t.a < e.getNumChannels() ? juce::Colour (e.getChannel (t.a).colour.load()) : Theme::accent;
        if (t.kind == trackParam || t.kind == effectParam)
            return e.getTrackColour (t.a);
        return Theme::accent;
    }
}

//==============================================================================
/**
    Le curve di automazione, sotto la stessa linea del tempo della playlist.

    - Tocca uno spazio vuoto della curva: aggiunge un punto
    - Trascina un punto: lo sposta (tempo agganciato agli step, valore libero)
    - Tocca un punto: lo cancella
    - Trascina sul vuoto: scorre (in orizzontale il tempo, in verticale le curve)
    - Tocca il nome a sinistra: menu (accendi/spegni, forme pronte, elimina)
*/
class AutomationView : public juce::Component
{
public:
    static constexpr int headerWidth = 118, rulerHeight = 22;

    AutomationView (SequencerEngine& e, std::function<float()> getBarWidth, std::function<float()> getScroll, std::function<void (float)> setScroll)
        : engine (e), barWidthFn (std::move (getBarWidth)), scrollFn (std::move (getScroll)), setScrollFn (std::move (setScroll)) {}

    std::function<void()> onAddLane, onEdit;

    void refresh()
    {
        const int step = engine.getSongStep();
        int used = 0;
        for (int i = 0; i < automation::maxLanes; ++i) used += engine.isLaneUsed (i) ? 1 : 0;
        if (step != shownStep || used != shownLanes)
        {
            shownStep = step;
            shownLanes = used;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        const auto lanes = usedLanes();
        const float bw = barWidthFn(), sx = scrollFn();
        const float gx = (float) headerWidth;

        // Righello
        g.setColour (Theme::panel);
        g.fillRect (0, 0, getWidth(), rulerHeight);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (headerWidth, 0, getWidth() - headerWidth, getHeight());
            const int firstBar = (int) (sx / bw);
            for (int b = firstBar; b < firstBar + (int) (getWidth() / bw) + 2; ++b)
            {
                const float x = gx + (float) b * bw - sx;
                g.setColour (Theme::border.brighter (0.15f));
                g.drawVerticalLine ((int) x, (float) rulerHeight, (float) getHeight());
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (11.0f, true));
                if (bw >= 30.0f || b % 2 == 0)
                    g.drawText (juce::String (b + 1), (int) x + 3, 2, 40, rulerHeight - 4, juce::Justification::centredLeft, false);
            }
            // Oltre la fine della canzone: piu' scuro
            if (const int songEnd = engine.getSongLengthSteps(); songEnd > 0)
            {
                const float x = gx + (float) songEnd / SequencerEngine::stepsPerBar * bw - sx;
                if (x < (float) getWidth())
                {
                    g.setColour (juce::Colours::black.withAlpha (0.35f));
                    g.fillRect (juce::Rectangle<float> (juce::jmax (gx, x), (float) rulerHeight, (float) getWidth() - juce::jmax (gx, x), (float) getHeight()));
                    g.setColour (Theme::accent.withAlpha (0.5f));
                    g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
                }
            }

            if (shownStep >= 0)
            {
                const float x = gx + (float) shownStep / SequencerEngine::stepsPerBar * bw - sx;
                g.setColour (Theme::accent);
                g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
            }
        }
        g.setColour (Theme::panel);
        g.fillRect (0, 0, headerWidth, rulerHeight);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.0f, true));
        g.drawText ("AUTOMAZIONI", 0, 0, headerWidth, rulerHeight, juce::Justification::centred, false);

        if (lanes.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (14.0f));
            g.drawFittedText ("Nessuna automazione.\nTocca \"+ Automazione\" e scegli cosa far muovere da solo nella canzone: "
                              "il volume, il filtro di un canale, un effetto del mixer, una manopola di un synth...\n"
                              "Le automazioni suonano in modalita' SONG.",
                              getLocalBounds().withTrimmedTop (rulerHeight + 20).reduced (30, 0), juce::Justification::centredTop, 6);
            return;
        }

        for (size_t k = 0; k < lanes.size(); ++k)
            paintLane (g, lanes[k], laneBounds ((int) k));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        downPos = e.position;
        startScrollX = scrollFn();
        startScrollY = scrollY;
        dragMode = DragMode::pending;
        dragLane = laneAt (e.position.y);
        dragPoint = -1;
        moved = false;

        if (dragLane >= 0 && e.position.x >= (float) headerWidth)
        {
            const auto pts = engine.getLanePoints (dragLane);
            const auto area = laneBounds (laneIndex (dragLane));
            for (int i = 0; i < (int) pts.size(); ++i)
                if (pointPos (pts[(size_t) i], area).getDistanceFrom (e.position) < 20.0f)
                {
                    dragPoint = i;
                    dragMode = DragMode::point;
                    break;
                }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto delta = e.position - downPos;
        if (delta.getDistanceFromOrigin() > 8.0f)
            moved = true;
        if (! moved)
            return;

        if (dragMode == DragMode::point)
        {
            auto pts = engine.getLanePoints (dragLane);
            if (dragPoint < 0 || dragPoint >= (int) pts.size())
                return;
            const auto area = laneBounds (laneIndex (dragLane));
            int tick = tickAt (e.position.x);
            // Resta tra i vicini (l'ordine nel tempo non cambia)
            if (dragPoint > 0)                     tick = juce::jmax (tick, pts[(size_t) dragPoint - 1].tick + 1);
            if (dragPoint + 1 < (int) pts.size())  tick = juce::jmin (tick, pts[(size_t) dragPoint + 1].tick - 1);
            pts[(size_t) dragPoint] = { tick, valueAt (e.position.y, area) };
            engine.setLanePoints (dragLane, pts);
            dragValueText = AutomationText::value (engine, engine.getLane (dragLane).target(), pts[(size_t) dragPoint].value);
            repaint();
            return;
        }

        // Scorrimento: si decide la direzione col primo movimento
        if (dragMode == DragMode::pending)
            dragMode = std::abs (delta.x) >= std::abs (delta.y) ? DragMode::scrollX : DragMode::scrollY;
        if (dragMode == DragMode::scrollX)
            setScrollFn (startScrollX - delta.x);
        else if (dragMode == DragMode::scrollY)
            scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, contentHeight() - (float) (getHeight() - rulerHeight)), startScrollY - delta.y);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        dragValueText.clear();
        if (moved)
        {
            if (dragMode == DragMode::point && onEdit) onEdit();
            repaint();
            return;
        }

        if (dragLane < 0)
            return;

        if (e.position.x < (float) headerWidth)
        {
            showLaneMenu (dragLane);
            return;
        }

        auto pts = engine.getLanePoints (dragLane);
        if (dragMode == DragMode::point && dragPoint >= 0 && dragPoint < (int) pts.size())
            pts.erase (pts.begin() + dragPoint);   // tocco su un punto: via
        else
            pts.push_back ({ tickAt (e.position.x), valueAt (e.position.y, laneBounds (laneIndex (dragLane))) });
        engine.setLanePoints (dragLane, pts);
        if (onEdit) onEdit();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        setScrollFn (scrollFn() - w.deltaX * 300.0f);
        scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, contentHeight() - (float) (getHeight() - rulerHeight)), scrollY - w.deltaY * 200.0f);
        repaint();
    }

private:
    enum class DragMode { none, pending, point, scrollX, scrollY };

    std::vector<int> usedLanes() const
    {
        std::vector<int> v;
        for (int i = 0; i < automation::maxLanes; ++i)
            if (engine.isLaneUsed (i))
                v.push_back (i);
        return v;
    }

    int laneIndex (int lane) const
    {
        const auto v = usedLanes();
        for (size_t k = 0; k < v.size(); ++k)
            if (v[k] == lane) return (int) k;
        return -1;
    }

    float laneHeight() const    { return juce::jlimit (72.0f, 120.0f, (float) (getHeight() - rulerHeight) / 3.0f); }
    float contentHeight() const { return laneHeight() * (float) usedLanes().size(); }

    juce::Rectangle<float> laneBounds (int k) const
    {
        return { 0.0f, (float) rulerHeight + laneHeight() * (float) k - scrollY, (float) getWidth(), laneHeight() };
    }

    int laneAt (float y) const
    {
        if (y < (float) rulerHeight)
            return -1;
        const int k = (int) std::floor ((y - (float) rulerHeight + scrollY) / laneHeight());
        const auto v = usedLanes();
        return k >= 0 && k < (int) v.size() ? v[(size_t) k] : -1;
    }

    int tickAt (float x) const
    {
        const float ticksPerPixel = (float) (SequencerEngine::stepsPerBar * SequencerEngine::ticksPerStep) / barWidthFn();
        const int tick = (int) std::round ((x - (float) headerWidth + scrollFn()) * ticksPerPixel / SequencerEngine::ticksPerStep)
                         * SequencerEngine::ticksPerStep;   // agganciato agli step
        return juce::jlimit (0, SequencerEngine::maxBars * SequencerEngine::stepsPerBar * SequencerEngine::ticksPerStep, tick);
    }

    float xForTick (double tick) const
    {
        return (float) headerWidth + (float) (tick / (SequencerEngine::stepsPerBar * SequencerEngine::ticksPerStep)) * barWidthFn() - scrollFn();
    }

    static juce::Rectangle<float> curveArea (juce::Rectangle<float> lane)    { return lane.withTrimmedLeft ((float) headerWidth).reduced (0.0f, 10.0f); }

    float valueAt (float y, juce::Rectangle<float> lane) const
    {
        const auto a = curveArea (lane);
        return juce::jlimit (0.0f, 1.0f, (a.getBottom() - y) / a.getHeight());
    }

    juce::Point<float> pointPos (const automation::Point& p, juce::Rectangle<float> lane) const
    {
        const auto a = curveArea (lane);
        return { xForTick (p.tick), a.getBottom() - p.value * a.getHeight() };
    }

    void paintLane (juce::Graphics& g, int lane, juce::Rectangle<float> r)
    {
        if (r.getBottom() < (float) rulerHeight || r.getY() > (float) getHeight())
            return;
        auto& l = engine.getLane (lane);
        const auto target = l.target();
        const auto colour = AutomationText::colour (engine, target);
        const bool on = l.enabled.load();

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (0, rulerHeight, getWidth(), getHeight() - rulerHeight);

        g.setColour (Theme::panel.withAlpha (0.6f));
        g.fillRect (r.withTrimmedLeft ((float) headerWidth));
        g.setColour (Theme::border);
        g.drawHorizontalLine ((int) r.getBottom() - 1, 0.0f, (float) getWidth());

        // Curva
        {
            juce::Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (r.withTrimmedLeft ((float) headerWidth).toNearestInt());
            const auto pts = engine.getLanePoints (lane);
            const auto area = curveArea (r);
            g.setColour (Theme::border.brighter (0.2f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

            if (! pts.empty())
            {
                juce::Path p;
                auto first = pointPos (pts.front(), r);
                p.startNewSubPath ((float) headerWidth, first.y);
                for (auto& pt : pts)
                    p.lineTo (pointPos (pt, r));
                p.lineTo ((float) getWidth(), pointPos (pts.back(), r).y);

                juce::Path fill (p);
                fill.lineTo ((float) getWidth(), area.getBottom());
                fill.lineTo ((float) headerWidth, area.getBottom());
                fill.closeSubPath();
                g.setColour (colour.withAlpha (on ? 0.18f : 0.06f));
                g.fillPath (fill);
                g.setColour (colour.withAlpha (on ? 1.0f : 0.35f));
                g.strokePath (p, juce::PathStrokeType (2.0f));

                for (size_t i = 0; i < pts.size(); ++i)
                {
                    const auto c = pointPos (pts[i], r);
                    const bool dragging = lane == dragLane && (int) i == dragPoint && moved;
                    g.setColour (Theme::bg);
                    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
                    g.setColour (dragging ? Theme::textBright : colour);
                    g.drawEllipse (juce::Rectangle<float> (dragging ? 14.0f : 11.0f, dragging ? 14.0f : 11.0f).withCentre (c), 2.0f);
                }
            }
            else
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (12.0f));
                g.drawText ("Tocca qui per aggiungere i punti", area.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
            }

            if (lane == dragLane && dragValueText.isNotEmpty())
            {
                g.setColour (Theme::textBright);
                g.setFont (Theme::font (12.0f, true));
                g.drawText (dragValueText, area.withHeight (16.0f).translated (6.0f, -8.0f).withWidth (160.0f), juce::Justification::centredLeft, false);
            }
        }

        // Nome
        auto head = r.withWidth ((float) headerWidth).reduced (3.0f, 2.0f);
        Theme::drawRaised (g, head, Theme::raised);
        g.setColour (colour.withAlpha (on ? 1.0f : 0.4f));
        g.fillRect (head.removeFromLeft (4.0f).reduced (0.0f, 5.0f).translated (3.0f, 0.0f));
        head.removeFromLeft (6.0f);
        g.setColour (on ? Theme::textBright : Theme::textDim);
        g.setFont (Theme::font (12.0f, true));
        g.drawFittedText (AutomationText::parameter (engine, target), head.removeFromTop (head.getHeight() * 0.55f).reduced (3.0f, 0.0f).toNearestInt(),
                          juce::Justification::bottomLeft, 2, 0.8f);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.5f));
        g.drawFittedText (AutomationText::source (engine, target) + (on ? "" : " (spenta)"), head.reduced (3.0f, 0.0f).toNearestInt(),
                          juce::Justification::topLeft, 2, 0.8f);
    }

    void showLaneMenu (int lane)
    {
        auto& l = engine.getLane (lane);
        juce::PopupMenu shapes;
        static const int lengths[] = { 1, 2, 4, 8, 16 };
        for (int len : lengths)
        {
            juce::PopupMenu sub;
            sub.addItem (100 + len, "Sale");
            sub.addItem (200 + len, "Scende");
            sub.addItem (300 + len, "Sale e scende");
            sub.addItem (400 + len, "Onde (una per battuta)");
            shapes.addSubMenu (juce::String (len) + (len == 1 ? " battuta" : " battute"), sub);
        }

        juce::PopupMenu menu;
        menu.addSectionHeader (AutomationText::parameter (engine, l.target()) + "  -  " + AutomationText::source (engine, l.target()));
        menu.addItem (1, "Accesa", true, l.enabled.load());
        menu.addSubMenu ("Forme pronte (dalla battuta " + juce::String (engine.getSongStartBar() + 1) + ")", shapes);
        menu.addItem (2, "Togli tutti i punti", l.count.load() > 0);
        menu.addSeparator();
        menu.addItem (3, "Elimina l'automazione");

        juce::Component::SafePointer<AutomationView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options(), [safe, lane] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            auto& eng = safe->engine;
            auto& ln = eng.getLane (lane);
            if (result == 1)      ln.enabled.store (! ln.enabled.load());
            else if (result == 2) eng.setLanePoints (lane, {});
            else if (result == 3) eng.removeLane (lane);
            else if (result >= 100)
            {
                const int shape = result / 100, bars = result % 100;
                const int start = eng.getSongStartBar() * 192, len = bars * 192;
                auto pts = eng.getLanePoints (lane);
                // Si tolgono i punti dentro il tratto, poi si mette la forma
                pts.erase (std::remove_if (pts.begin(), pts.end(), [=] (auto& p) { return p.tick >= start && p.tick <= start + len; }), pts.end());
                auto add = [&pts] (int t, float v) { pts.push_back ({ t, v }); };
                if (shape == 1)      { add (start, 0.0f); add (start + len, 1.0f); }
                else if (shape == 2) { add (start, 1.0f); add (start + len, 0.0f); }
                else if (shape == 3) { add (start, 0.0f); add (start + len / 2, 1.0f); add (start + len, 0.0f); }
                else
                    for (int b = 0; b < bars; ++b)
                    {
                        add (start + b * 192, 0.15f);
                        add (start + b * 192 + 96, 0.85f);
                        if (b == bars - 1) add (start + len, 0.15f);
                    }
                eng.setLanePoints (lane, pts);
            }
            if (safe->onEdit) safe->onEdit();
            safe->repaint();
        });
    }

    SequencerEngine& engine;
    std::function<float()> barWidthFn, scrollFn;
    std::function<void (float)> setScrollFn;
    float scrollY = 0.0f, startScrollX = 0.0f, startScrollY = 0.0f;
    int shownStep = -1, shownLanes = -1, dragLane = -1, dragPoint = -1;
    bool moved = false;
    DragMode dragMode = DragMode::none;
    juce::Point<float> downPos;
    juce::String dragValueText;
};

//==============================================================================
/** Scelta di cosa automatizzare, a tutto schermo: a sinistra da dove (canale, mixer, master), a destra il controllo. */
class AutomationTargetPicker : public juce::Component
{
public:
    explicit AutomationTargetPicker (SequencerEngine& e) : engine (e)
    {
        addAndMakeVisible (cancelButton);
        cancelButton.onClick = [this] { setVisible (false); };
        for (auto* vp : { &sourceView, &paramView })
        {
            vp->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
            vp->setScrollBarsShown (true, false);
            vp->setScrollBarThickness (6);
            addAndMakeVisible (vp);
        }
        sourceView.setViewedComponent (&sourceList, false);
        paramView.setViewedComponent (&paramList, false);
        sourceList.onTap = [this] (int i) { showSource (i); };
        paramList.onTap = [this] (int i)
        {
            if (i >= 0 && i < (int) shownTargets.size() && onChoose)
                onChoose (shownTargets[(size_t) i]);
            setVisible (false);
        };
    }

    std::function<void (automation::Target)> onChoose;

    void open()
    {
        sources.clear();
        std::vector<TileList::Item> items;
        sources.push_back ({ automation::master, 0 });
        items.push_back ({ "Master", "Volume generale", Theme::accent, true });
        for (int c = 0; c < engine.getNumChannels(); ++c)
        {
            sources.push_back ({ automation::channelParam, c });
            items.push_back ({ engine.getChannel (c).name, engine.isSynth (c) ? "Canale e manopole del synth" : "Canale",
                               juce::Colour (engine.getChannel (c).colour.load()), true });
        }
        for (int t = 0; t < mixer::numTracks; ++t)
            if (t == 0 ? hasEffects (0) : engine.isTrackUsed (t))
            {
                sources.push_back ({ automation::trackParam, t });
                items.push_back ({ t == 0 ? juce::String ("Mixer: Master") : "Mixer " + juce::String (t) + ": " + engine.getTrackName (t),
                                   "Volume, pan, EQ, effetti", engine.getTrackColour (t), true });
            }
        sourceList.setItems (items, -1);
        showSource (0);
        setVisible (true);
        toFront (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (16.0f, true));
        g.drawFittedText ("Cosa vuoi far muovere da solo?", titleArea, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (34);
        cancelButton.setBounds (top.removeFromRight (90).reduced (0, 2));
        titleArea = top;
        r.removeFromTop (6);
        const bool narrow = r.getWidth() < 520;
        auto left = r.removeFromLeft (juce::jlimit (120, 220, (int) (r.getWidth() * (narrow ? 0.4f : 0.3f))));
        r.removeFromLeft (8);
        sourceView.setBounds (left);
        sourceList.setLayout (1, 50);
        sourceList.setVisibleWidth (left.getWidth() - 8);
        paramView.setBounds (r);
        paramList.setLayout (juce::jmax (1, (r.getWidth() - 8) / 170), 46);
        paramList.setVisibleWidth (r.getWidth() - 8);
    }

private:
    struct Source { int kind, index; };

    bool hasEffects (int t) const
    {
        for (auto& s : engine.getTrack (t).slots)
            if (s.type.load() != mixer::none) return true;
        return false;
    }

    void showSource (int i)
    {
        if (i < 0 || i >= (int) sources.size())
            return;
        sourceList.setSelected (i);
        shownTargets.clear();
        std::vector<TileList::Item> items;
        const auto src = sources[(size_t) i];
        auto add = [&] (automation::Target t, const juce::String& detail)
        {
            shownTargets.push_back (t);
            items.push_back ({ AutomationText::parameter (engine, t), detail, AutomationText::colour (engine, t), true });
        };

        if (src.kind == automation::master)
            add ({ automation::master, 0, 0, 0 }, {});
        else if (src.kind == automation::channelParam)
        {
            for (int p = 0; p < automation::numChannelParams; ++p)
                add ({ automation::channelParam, src.index, p, 0 }, "Canale");
            if (auto* proc = engine.getInstrument (src.index))
                for (int p = 0; p < proc->getParameters().size(); ++p)
                    if (proc->getParameters()[p]->isAutomatable())
                        add ({ automation::instrumentParam, src.index, p, 0 }, proc->getName());
        }
        else
        {
            const int t = src.index;
            for (int p = 0; p < automation::numTrackParams; ++p)
                if (t > 0 || p >= automation::trEqLow)
                    add ({ automation::trackParam, t, p, 0 }, "Traccia");
            for (int s = 0; s < mixer::numSlots; ++s)
            {
                const auto& inf = mixer::info (engine.getTrack (t).slots[(size_t) s].type.load());
                for (int p = 0; p < inf.numParams; ++p)
                    if (inf.params[(size_t) p].unit != mixer::Unit::track)
                        add ({ automation::effectParam, t, s, p }, "Effetto " + juce::String (s + 1));
            }
        }
        paramList.setItems (items, -1);
        paramView.setViewPosition (0, 0);
    }

    SequencerEngine& engine;
    std::vector<Source> sources;
    std::vector<automation::Target> shownTargets;
    juce::Viewport sourceView, paramView;
    TileList sourceList, paramList;
    juce::TextButton cancelButton { "Annulla" };
    juce::Rectangle<int> titleArea;
};
