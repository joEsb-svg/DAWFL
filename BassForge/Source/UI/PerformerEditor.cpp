#include "PerformerEditor.h"

namespace bf::ui
{
    PerformerEditor::PerformerEditor(PerformerBank& b, Apvts& a) : bank(b), apvts(a)
    {
        lengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        lengthSlider.setRange(1.0, (double) PerformerPattern::kMaxSteps, 1.0);
        lengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
        lengthSlider.onValueChange = [this] { pat().setLength((int) lengthSlider.getValue()); repaint(); };
        lengthLabel.setText("Steps", juce::dontSendNotification);
        lengthLabel.setColour(juce::Label::textColourId, col::textDim);

        curveSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        curveSlider.setRange(-1.0, 1.0, 0.001);
        curveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
        curveSlider.onValueChange = [this]
        {
            auto& p = pat();
            p.setStep(selected, p.getValue(selected), p.getInterp(selected), (float) curveSlider.getValue());
            repaint();
        };
        curveLabel.setText("Curve (step)", juce::dontSendNotification);
        curveLabel.setColour(juce::Label::textColourId, col::textDim);

        interpBox.addItemList({ "Linear", "Exponential", "Step", "Custom curve" }, 1);
        interpBox.onChange = [this]
        {
            auto& p = pat();
            p.setStep(selected, p.getValue(selected), (StepInterp) juce::jlimit(0, 3, interpBox.getSelectedItemIndex()), p.getCurve(selected));
            repaint();
        };

        for (auto* c : std::initializer_list<juce::Component*>{ &lengthSlider, &curveSlider, &interpBox, &lengthLabel, &curveLabel,
                                                                 &clearBtn, &rampUp, &rampDown, &sineBtn, &randBtn })
            addAndMakeVisible(*c);

        auto fill = [this](auto fn)
        {
            auto& p = pat();
            for (int i = 0; i < PerformerPattern::kMaxSteps; ++i)
                p.setStep(i, fn(i, p.getLength()), p.getInterp(i), p.getCurve(i));
            repaint();
        };
        clearBtn.onClick = [fill] { fill([](int, int) { return 0.0f; }); };
        rampUp.onClick   = [fill] { fill([](int i, int n) { return n > 1 ? -1.0f + 2.0f * (float) std::min(i, n - 1) / (float) (n - 1) : 0.0f; }); };
        rampDown.onClick = [fill] { fill([](int i, int n) { return n > 1 ? 1.0f - 2.0f * (float) std::min(i, n - 1) / (float) (n - 1) : 0.0f; }); };
        sineBtn.onClick  = [fill] { fill([](int i, int n) { return std::sin(juce::MathConstants<float>::twoPi * (float) i / (float) std::max(n, 1)); }); };
        randBtn.onClick  = [this, fill] { fill([this](int, int) { return rng.nextFloat() * 2.0f - 1.0f; }); };

        setTarget(0);
    }

    void PerformerEditor::setTarget(int i)
    {
        lfo = juce::jlimit(0, kNumLfo - 1, i);
        selected = juce::jmin(selected, pat().getLength() - 1);
        syncControls();
        repaint();
    }

    void PerformerEditor::syncControls()
    {
        lengthSlider.setValue((double) pat().getLength(), juce::dontSendNotification);
        interpBox.setSelectedItemIndex((int) pat().getInterp(selected), juce::dontSendNotification);
        curveSlider.setValue((double) pat().getCurve(selected), juce::dontSendNotification);
    }

    PlayMode PerformerEditor::playMode() const
    {
        if (auto* p = apvts.getRawParameterValue(ids::lfo(lfo, "pmode")))
            return (PlayMode) juce::jlimit(0, (int) PlayMode::Count - 1, (int) (p->load() + 0.5f));
        return PlayMode::Forward;
    }

    void PerformerEditor::resized()
    {
        auto b = getLocalBounds().reduced(4);
        auto top = b.removeFromTop(28);
        lengthLabel.setBounds(top.removeFromLeft(44));
        lengthSlider.setBounds(top.removeFromLeft(220));
        top.removeFromLeft(12);
        for (auto* bt : { &clearBtn, &rampUp, &rampDown, &sineBtn, &randBtn })
            bt->setBounds(top.removeFromLeft(86).reduced(2));
        auto bottom = b.removeFromBottom(30);
        interpBox.setBounds(bottom.removeFromLeft(170).reduced(0, 3));
        bottom.removeFromLeft(12);
        curveLabel.setBounds(bottom.removeFromLeft(90));
        curveSlider.setBounds(bottom.removeFromLeft(300));
    }

    juce::Rectangle<float> PerformerEditor::barsArea() const
    {
        auto b = getLocalBounds().toFloat().reduced(4.0f);
        b.removeFromTop(34.0f); b.removeFromBottom(34.0f + 24.0f);
        return b;
    }

    juce::Rectangle<float> PerformerEditor::interpArea() const
    {
        auto a = barsArea();
        return { a.getX(), a.getBottom() + 3.0f, a.getWidth(), 20.0f };
    }

    int PerformerEditor::stepAt(float x) const
    {
        const auto a = barsArea();
        return juce::jlimit(0, PerformerPattern::kMaxSteps - 1,
                            (int) std::floor((x - a.getX()) / (a.getWidth() / (float) PerformerPattern::kMaxSteps)));
    }

    float PerformerEditor::valueAt(float y, bool snap) const
    {
        const auto a = barsArea();
        float v = juce::jlimit(-1.0f, 1.0f, 1.0f - 2.0f * (y - a.getY()) / a.getHeight());
        if (snap) v = std::round(v * 8.0f) / 8.0f;
        return v;
    }

    void PerformerEditor::paintStep(int s, float y, bool snap)
    {
        auto& p = pat();
        p.setStep(s, valueAt(y, snap), p.getInterp(s), p.getCurve(s));
    }

    void PerformerEditor::mouseDown(const juce::MouseEvent& e)
    {
        const auto ia = interpArea();
        const int s = stepAt(e.position.x);
        if (ia.contains(e.position))
        {
            auto& p = pat();
            const int n = (int) StepInterp::Count;
            const int next = ((int) p.getInterp(s) + (e.mods.isShiftDown() ? n - 1 : 1)) % n;
            p.setStep(s, p.getValue(s), (StepInterp) next, p.getCurve(s));
            selected = s; syncControls(); repaint();
            return;
        }
        if (! barsArea().expanded(0.0f, 6.0f).contains(e.position)) return;
        selected = s; lastPainted = s;
        paintStep(s, e.position.y, e.mods.isShiftDown());
        syncControls(); repaint();
    }

    void PerformerEditor::mouseDrag(const juce::MouseEvent& e)
    {
        if (lastPainted < 0 || interpArea().contains(e.getMouseDownPosition().toFloat())) return;
        const int s = stepAt(e.position.x);
        // riempie anche gli step saltati se il mouse si muove veloce
        const int from = lastPainted, to = s, dir = to >= from ? 1 : -1;
        for (int i = from; i != to + dir; i += dir) paintStep(i, e.position.y, e.mods.isShiftDown());
        lastPainted = s; selected = s;
        syncControls(); repaint();
    }

    void PerformerEditor::mouseDoubleClick(const juce::MouseEvent& e)
    {
        if (! barsArea().contains(e.position)) return;
        auto& p = pat(); const int s = stepAt(e.position.x);
        p.setStep(s, 0.0f, p.getInterp(s), p.getCurve(s));
        selected = s; syncControls(); repaint();
    }

    void PerformerEditor::paint(juce::Graphics& g)
    {
        auto all = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0b0f14)); g.fillRoundedRectangle(all.reduced(1.0f), 8.0f);

        const auto a = barsArea();
        const int n = pat().getLength();
        const float sw = a.getWidth() / (float) PerformerPattern::kMaxSteps;
        const float cy = a.getCentreY();

        // griglia
        g.setColour(col::edge.withAlpha(0.35f));
        for (int q = -2; q <= 2; ++q) g.drawHorizontalLine((int) (cy - (float) q * a.getHeight() * 0.25f), a.getX(), a.getRight());
        g.setColour(col::edge.withAlpha(0.8f)); g.drawHorizontalLine((int) cy, a.getX(), a.getRight());

        // barre
        for (int i = 0; i < PerformerPattern::kMaxSteps; ++i)
        {
            const bool in = i < n;
            const float v = pat().getValue(i);
            const float y = cy - v * a.getHeight() * 0.5f;
            auto bar = juce::Rectangle<float>(a.getX() + (float) i * sw + 2.0f, juce::jmin(y, cy), sw - 4.0f, std::abs(y - cy)).withHeight(juce::jmax(1.5f, std::abs(y - cy)));
            const auto c = (v >= 0.0f ? col::cyan : col::orange).withAlpha(in ? 0.85f : 0.18f);
            g.setColour(c.withAlpha(in ? 0.22f : 0.06f)); g.fillRoundedRectangle(bar.expanded(2.0f, 0.0f), 3.0f);
            g.setColour(c); g.fillRoundedRectangle(bar, 2.0f);
            if (i == selected) { g.setColour(juce::Colours::white); g.drawRoundedRectangle(bar.expanded(1.5f), 3.0f, 1.3f); }

            // striscia interpolazione
            const char letters[4] = { 'L', 'E', 'S', 'C' };
            auto cell = juce::Rectangle<float>(a.getX() + (float) i * sw + 1.0f, interpArea().getY(), sw - 2.0f, interpArea().getHeight());
            g.setColour(in ? col::panelHi : col::panel); g.fillRoundedRectangle(cell, 3.0f);
            g.setColour(in ? col::text : col::textDim.withAlpha(0.4f));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(juce::String::charToString((juce::juce_wchar) letters[(int) pat().getInterp(i)]), cell, juce::Justification::centred);
        }

        // segnale reale (stessa funzione del motore audio)
        {
            const PlayMode mode = playMode();
            juce::Path path;
            const int px = juce::jmax(2, (int) (sw * (float) n));
            for (int i = 0; i <= px; ++i)
            {
                const double x = (double) n * (double) i / (double) px * (i == px ? 0.99999 : 1.0);
                const float v = pat().evaluate(x, mode, true);
                const float X = a.getX() + sw * (float) n * (float) i / (float) px, Y = cy - v * a.getHeight() * 0.5f;
                if (i == 0) path.startNewSubPath(X, Y); else path.lineTo(X, Y);
            }
            g.setColour(juce::Colours::white.withAlpha(0.18f)); g.strokePath(path, juce::PathStrokeType(4.0f));
            g.setColour(juce::Colours::white.withAlpha(0.9f));  g.strokePath(path, juce::PathStrokeType(1.4f));
            g.setColour(col::textDim); g.setFont(juce::Font(juce::FontOptions(11.0f)));
            const char* names[3] = { "Forward", "Ping-Pong", "One-Shot" };
            g.drawText(juce::String("LFO ") + juce::String(lfo + 1) + " · " + names[(int) mode], getLocalBounds().reduced(10, 36),
                       juce::Justification::topRight);
        }

        // fine pattern
        g.setColour(col::orange.withAlpha(0.7f));
        const float ex = a.getX() + sw * (float) n;
        g.drawVerticalLine((int) ex, a.getY(), a.getBottom());
    }
}
