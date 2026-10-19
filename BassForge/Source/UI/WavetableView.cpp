#include "WavetableView.h"

namespace bf::ui
{
    WavetableView::WavetableView(const WavetableBank& b, Apvts& apvts, const ModMonitor& mon, int oscIndex)
        : bank(b), monitor(mon), wtParam(apvts.getRawParameterValue(ids::osc(oscIndex, "wt"))),
          dest(DstOsc1Wt + oscIndex), oscIdx(oscIndex)
    {
        setOpaque(false);
        startTimerHz(30);
    }

    void WavetableView::timerCallback()
    {
        if (! isShowing()) return;
        const float base = wtParam->load(std::memory_order_relaxed);
        const float live = juce::jlimit(0.0f, 1.0f, base + monitor.get(dest));
        if (std::abs(base - shownBase) > 1.0e-4f || std::abs(live - shownLive) > 1.0e-4f)
        {
            shownBase = base; shownLive = live;
            repaint();
        }
    }

    // forma d'onda interpolata linearmente tra i due frame vicini (mip 0), n punti su un ciclo
    void WavetableView::fillWave(float pos01, float* out, int n) const
    {
        const int nf = bank.getNumFrames();
        const float fp = juce::jlimit(0.0f, 1.0f, pos01) * (float) (nf - 1);
        const int f0 = std::min((int) fp, nf - 1), f1 = std::min(f0 + 1, nf - 1);
        const float t = fp - (float) f0;
        const float* A = bank.getTable(f0, 0);
        const float* B = bank.getTable(f1, 0);
        for (int i = 0; i < n; ++i)
        {
            const int k = (i * WavetableBank::kTableSize) / n;
            out[i] = A[k] + (B[k] - A[k]) * t;
        }
    }

    void WavetableView::buildPath(juce::Path& p, const float* w, int n, juce::Rectangle<float> r, float baselineY, float amp) const
    {
        p.clear();
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float y = baselineY - juce::jlimit(-1.2f, 1.2f, w[i]) * amp;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
    }

    void WavetableView::paint(juce::Graphics& g)
    {
        auto all = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0b0f14));
        g.fillRoundedRectangle(all, 8.0f);
        g.setColour(col::edge);
        g.drawRoundedRectangle(all.reduced(0.5f), 8.0f, 1.0f);
        if (! bank.isReady()) return;

        const float base = wtParam->load(std::memory_order_relaxed);
        const float live = juce::jlimit(0.0f, 1.0f, base + monitor.get(dest));
        const bool modulated = std::abs(live - base) > 0.003f;

        auto inner = all.reduced(8.0f);
        auto area3d = inner.removeFromLeft(inner.getWidth() * 0.58f).reduced(0, 14.0f);
        auto area2d = inner.reduced(6.0f, 14.0f);

        // ---------------- cascata 3D ----------------
        constexpr int kPts = 96;
        const int nf = bank.getNumFrames();
        const int nDraw = juce::jlimit(2, 30, nf);
        const float amp = area3d.getHeight() * 0.17f;
        const float sx = area3d.getWidth() * 0.20f / (float) (nDraw - 1);
        const float sy = (area3d.getHeight() - 2.0f * amp) / (float) (nDraw - 1);
        const juce::Rectangle<float> wr(area3d.getX(), 0.0f, area3d.getWidth() - sx * (float) (nDraw - 1), 0.0f);
        std::array<float, kPts> wv{};
        juce::Path line, fill;

        auto frameBase = [&](float idxF) { return area3d.getBottom() - amp - idxF * sy; };
        auto frameX    = [&](float idxF) { return juce::Rectangle<float>(wr.getX() + idxF * sx, 0, wr.getWidth(), 0); };

        for (int k = nDraw - 1; k >= 0; --k)                     // dal fondo al davanti
        {
            fillWave((float) k / (float) (nDraw - 1), wv.data(), kPts);
            const float by = frameBase((float) k);
            buildPath(line, wv.data(), kPts, frameX((float) k), by, amp);
            fill = line;
            fill.lineTo(wr.getX() + (float) k * sx + wr.getWidth(), by + amp * 0.6f);
            fill.lineTo(wr.getX() + (float) k * sx, by + amp * 0.6f);
            fill.closeSubPath();
            g.setColour(juce::Colour(0xff0b0f14));
            g.fillPath(fill);
            const float depth = (float) k / (float) (nDraw - 1);
            g.setColour(col::cyanDim.interpolatedWith(col::textDim, 0.25f).withAlpha(0.95f - depth * 0.55f));
            g.strokePath(line, juce::PathStrokeType(1.0f));
        }

        auto drawCursor = [&](float pos, juce::Colour c, float thick)
        {
            const float idxF = pos * (float) (nDraw - 1);
            fillWave(pos, wv.data(), kPts);
            buildPath(line, wv.data(), kPts, frameX(idxF), frameBase(idxF), amp);
            g.setColour(c.withAlpha(0.25f));
            g.strokePath(line, juce::PathStrokeType(thick + 4.0f));
            g.setColour(c);
            g.strokePath(line, juce::PathStrokeType(thick));
        };
        drawCursor(base, col::cyan, 1.8f);
        if (modulated) drawCursor(live, col::orange, 1.8f);

        // ---------------- 2D ----------------
        g.setColour(col::edge.withAlpha(0.6f));
        const float cy = area2d.getCentreY();
        g.drawHorizontalLine((int) cy, area2d.getX(), area2d.getRight());
        g.setColour(col::edge.withAlpha(0.25f));
        g.drawHorizontalLine((int) (cy - area2d.getHeight() * 0.25f), area2d.getX(), area2d.getRight());
        g.drawHorizontalLine((int) (cy + area2d.getHeight() * 0.25f), area2d.getX(), area2d.getRight());
        for (int q = 1; q < 4; ++q) g.drawVerticalLine((int) (area2d.getX() + area2d.getWidth() * (float) q * 0.25f), area2d.getY(), area2d.getBottom());

        constexpr int kPts2 = 256;
        std::array<float, kPts2> w2{};
        juce::Path p2;
        const float a2 = area2d.getHeight() * 0.45f;
        if (modulated)
        {
            fillWave(live, w2.data(), kPts2);
            buildPath(p2, w2.data(), kPts2, area2d, cy, a2);
            g.setColour(col::orange.withAlpha(0.9f));
            g.strokePath(p2, juce::PathStrokeType(1.6f));
        }
        fillWave(base, w2.data(), kPts2);
        buildPath(p2, w2.data(), kPts2, area2d, cy, a2);
        g.setColour(col::cyan.withAlpha(0.2f));
        g.strokePath(p2, juce::PathStrokeType(5.0f));
        g.setColour(col::cyan);
        g.strokePath(p2, juce::PathStrokeType(1.8f));

        // ---------------- testi ----------------
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.setColour(col::textDim);
        g.drawText("OSC " + juce::String(oscIdx + 1) + " WAVETABLE", all.reduced(10.0f, 4.0f).removeFromTop(14.0f), juce::Justification::centredLeft);
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.setColour(col::cyan);
        juce::String t = "WT " + juce::String(base, 2);
        if (modulated) t << "  >  " << juce::String(live, 2);
        g.drawText(t, all.reduced(10.0f, 4.0f).removeFromTop(14.0f), juce::Justification::centredRight);
    }
}
