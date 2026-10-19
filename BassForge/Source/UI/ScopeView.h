#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "Palette.h"
#include "Modulation/ModMonitor.h"

namespace bf::ui
{
    /** Oscilloscopio dell'uscita (ultimi campioni dal thread audio, trigger sul fronte di salita). */
    class ScopeView : public juce::Component, private juce::Timer
    {
    public:
        explicit ScopeView(const ScopeBuffer& s) : scope(s) { startTimerHz(30); }
        ~ScopeView() override { stopTimer(); }

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xff0b0f14));
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(col::edge);
            g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);

            std::array<float, kCopy> buf{};
            scope.copyLatest(buf.data(), kCopy);
            int start = 0;
            for (int i = 1; i < kCopy - kShow; ++i) if (buf[(size_t) (i - 1)] < 0.0f && buf[(size_t) i] >= 0.0f) { start = i; break; }

            auto a = r.reduced(8.0f, 6.0f);
            g.setColour(col::edge.withAlpha(0.6f));
            g.drawHorizontalLine((int) a.getCentreY(), a.getX(), a.getRight());
            juce::Path p;
            for (int i = 0; i < kShow; ++i)
            {
                const float x = a.getX() + a.getWidth() * (float) i / (float) (kShow - 1);
                const float y = a.getCentreY() - juce::jlimit(-1.0f, 1.0f, buf[(size_t) (start + i)]) * a.getHeight() * 0.5f;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            g.setColour(col::cyan.withAlpha(0.22f));
            g.strokePath(p, juce::PathStrokeType(4.0f));
            g.setColour(col::cyan);
            g.strokePath(p, juce::PathStrokeType(1.4f));
        }

    private:
        static constexpr int kCopy = 2048, kShow = 1024;
        void timerCallback() override { if (isShowing()) repaint(); }
        const ScopeBuffer& scope;
    };
}
