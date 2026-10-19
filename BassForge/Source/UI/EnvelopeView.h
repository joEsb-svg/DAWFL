#pragma once
#include "ModRing.h"
#include "Modulation/ModCurve.h"

namespace bf::ui
{
    /**
     * Editor grafico di un AHDSR: disegna la forma (con le tre curve) leggendo i parametri APVTS e permette di
     * trascinare i 4 nodi (fine Attack, fine Hold, fine Decay = livello Sustain, fine Release).
     * Asse tempi compresso (larghezza ~ sqrt(t)) cosi' si vedono sia 1 ms sia 10 s; la scala e' FISSA, quindi
     * trascinare un nodo non sposta gli altri. Le modifiche passano da begin/endChangeGesture (automazione/undo host).
     */
    class EnvelopeView : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
    {
    public:
        EnvelopeView(Apvts& apvts, int envIndex) : state(apvts)
        {
            const char* n[8] = { "a", "h", "d", "s", "r", "ac", "dc", "rc" };
            for (int i = 0; i < 8; ++i)
            {
                id[(size_t) i] = ids::env(envIndex, n[i]);
                par[(size_t) i] = apvts.getParameter(id[(size_t) i]);
                apvts.addParameterListener(id[(size_t) i], this);
            }
        }
        ~EnvelopeView() override { for (const auto& s : id) state.removeParameterListener(s, this); cancelPendingUpdate(); }

        void paint(juce::Graphics& g) override
        {
            auto all = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xff0b0f14)); g.fillRoundedRectangle(all, 6.0f);
            g.setColour(col::edge); g.drawRoundedRectangle(all.reduced(0.5f), 6.0f, 1.0f);

            const auto P = values();
            const auto area = plot();
            const auto x = nodeXs(P, area);          // x0 (inizio), xa, xh, xd, xs, xr
            auto Y = [&](float lv) { return area.getBottom() - lv * area.getHeight(); };

            juce::Path env, fill;
            env.startNewSubPath(x[0], Y(0.0f));
            auto seg = [&](float xa, float xb, auto levelAt)
            {
                const int n = juce::jmax(2, (int) (xb - xa) / 2);
                for (int i = 1; i <= n; ++i) { const float t = (float) i / (float) n; env.lineTo(xa + (xb - xa) * t, Y(levelAt(t))); }
            };
            seg(x[0], x[1], [&](float t) { return curveWarp(t, P.ac); });                                         // attack
            seg(x[1], x[2], [&](float)   { return 1.0f; });                                                      // hold
            seg(x[2], x[3], [&](float t) { return P.s + (1.0f - P.s) * curveWarp(1.0f - t, P.dc); });             // decay
            seg(x[3], x[4], [&](float)   { return P.s; });                                                       // sustain
            seg(x[4], x[5], [&](float t) { return P.s * curveWarp(1.0f - t, P.rc); });                            // release

            fill = env; fill.lineTo(x[5], Y(0.0f)); fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(col::cyan.withAlpha(0.28f), 0, area.getY(), col::cyan.withAlpha(0.02f), 0, area.getBottom(), false));
            g.fillPath(fill);
            g.setColour(col::cyan.withAlpha(0.25f)); g.strokePath(env, juce::PathStrokeType(5.0f));
            g.setColour(col::cyan);                  g.strokePath(env, juce::PathStrokeType(1.8f));

            const float ny[4] = { Y(1.0f), Y(1.0f), Y(P.s), Y(0.0f) };
            const float nx[4] = { x[1], x[2], x[3], x[5] };
            for (int i = 0; i < 4; ++i)
            {
                const bool act = i == drag;
                g.setColour(act ? col::orange : juce::Colours::white);
                g.fillEllipse(nx[i] - 4.5f, ny[i] - 4.5f, 9.0f, 9.0f);
                g.setColour(col::orange); g.drawEllipse(nx[i] - 4.5f, ny[i] - 4.5f, 9.0f, 9.0f, 1.2f);
            }
            g.setColour(col::textDim);
            g.setFont(juce::Font(juce::FontOptions(10.5f)));
            g.drawText("A " + fmt(P.a) + "  H " + fmt(P.h) + "  D " + fmt(P.d) + "  S " + juce::String(P.s, 2) + "  R " + fmt(P.r),
                       getLocalBounds().reduced(8, 3), juce::Justification::topRight);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            const auto P = values(); const auto area = plot(); const auto x = nodeXs(P, area);
            const float ny[4] = { area.getBottom() - area.getHeight(), area.getBottom() - area.getHeight(),
                                  area.getBottom() - P.s * area.getHeight(), area.getBottom() };
            const float nx[4] = { x[1], x[2], x[3], x[5] };
            drag = -1; float best = 14.0f * 14.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float dx = e.position.x - nx[i], dy = e.position.y - ny[i], d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; drag = i; }
            }
            if (drag < 0) return;
            const int touched[4][2] = { { 0, -1 }, { 1, -1 }, { 2, 3 }, { 4, -1 } };
            for (int k : touched[drag]) if (k >= 0 && par[(size_t) k] != nullptr) par[(size_t) k]->beginChangeGesture();
            mouseDrag(e);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (drag < 0) return;
            const auto P = values(); const auto area = plot(); const auto x = nodeXs(P, area);
            auto timeFor = [&](float w) { return timeOfWidth(w, unit(area)); };
            switch (drag)
            {
                case 0: setReal(0, timeFor(e.position.x - x[0])); break;
                case 1: setReal(1, timeFor(e.position.x - x[1])); break;
                case 2: setReal(2, timeFor(e.position.x - x[2]));
                        setReal(3, juce::jlimit(0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight())); break;
                case 3: setReal(4, timeFor(e.position.x - x[4])); break;
                default: break;
            }
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            if (drag < 0) return;
            const int touched[4][2] = { { 0, -1 }, { 1, -1 }, { 2, 3 }, { 4, -1 } };
            for (int k : touched[drag]) if (k >= 0 && par[(size_t) k] != nullptr) par[(size_t) k]->endChangeGesture();
            drag = -1; repaint();
        }

        /** Rotella sul disegno: curva dello stadio sotto il cursore (attack / decay / release) piu' log <-> exp. */
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const auto P = values(); const auto x = nodeXs(P, plot());
            const int idx = e.position.x < x[1] ? 5 : (e.position.x < x[3] ? 6 : 7);
            if (par[(size_t) idx] == nullptr) return;
            const float cur = par[(size_t) idx]->convertFrom0to1(par[(size_t) idx]->getValue());
            par[(size_t) idx]->beginChangeGesture();
            setReal(idx, juce::jlimit(-1.0f, 1.0f, cur + w.deltaY * 0.5f));
            par[(size_t) idx]->endChangeGesture();
        }

    private:
        struct Vals { float a, h, d, s, r, ac, dc, rc; };

        Vals values() const
        {
            auto v = [&](int i) { return par[(size_t) i] != nullptr ? par[(size_t) i]->convertFrom0to1(par[(size_t) i]->getValue()) : 0.0f; };
            return { v(0), v(1), v(2), v(3), v(4), v(5), v(6), v(7) };
        }
        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(10.0f, 14.0f).withTrimmedTop(4.0f); }
        // Larghezza ~ f(t) = (log10(1000 t + 1) / 4)^0.6: 1 ms .. 10 s su una scala leggibile; f(10 s) = 1. Scala FISSA.
        static float unit(juce::Rectangle<float> area) { return area.getWidth() / (4.0f + 0.6f); }
        static float segW(float t, float u)
        {
            const float f = std::pow(juce::jlimit(0.0f, 1.0f, std::log10(1000.0f * juce::jmax(t, 0.0f) + 1.0f) / 4.0f), 0.6f);
            return f * u;
        }
        static float timeOfWidth(float w, float u)
        {
            const float f = juce::jlimit(0.0f, 1.0f, w / u);
            return (std::pow(10.0f, 4.0f * std::pow(f, 1.0f / 0.6f)) - 1.0f) / 1000.0f;
        }
        static std::array<float, 6> nodeXs(const Vals& P, juce::Rectangle<float> area)
        {
            const float u = unit(area);
            std::array<float, 6> x{};
            x[0] = area.getX();
            x[1] = x[0] + segW(P.a, u);
            x[2] = x[1] + segW(P.h, u);
            x[3] = x[2] + segW(P.d, u);
            x[4] = x[3] + 0.6f * u;             // sustain: larghezza fissa
            x[5] = x[4] + segW(P.r, u);
            return x;
        }
        static juce::String fmt(float t) { return t < 1.0f ? juce::String(juce::roundToInt(t * 1000.0f)) + "ms" : juce::String(t, 2) + "s"; }

        void setReal(int i, float real)
        {
            if (par[(size_t) i] == nullptr) return;
            par[(size_t) i]->setValueNotifyingHost(par[(size_t) i]->convertTo0to1(real));
        }
        void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }     // puo' arrivare da qualunque thread
        void handleAsyncUpdate() override { repaint(); }

        Apvts& state;
        std::array<juce::String, 8> id;
        std::array<juce::RangedAudioParameter*, 8> par{};
        int drag = -1;
    };
}
