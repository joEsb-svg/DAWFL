#include "FMForgeEditor.h"

namespace col = bf::ui::col;
static const juce::Colour fmAccent { 0xff7dffb0 };

//==============================================================================
/** Disegno dell'algoritmo: gli operatori come blocchi, chi modula chi, le uscite in basso. */
class FMForgeEditor::AlgorithmView : public juce::Component
{
public:
    explicit AlgorithmView (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    juce::uint64 signature() const
    {
        juce::uint64 h = (juce::uint64) value ("algo") + 1;
        h = h * 131 + (juce::uint64) (value ("feedback") * 100);
        for (int o = 0; o < fm::numOps; ++o)
            h = h * 1009 + (juce::uint64) value (fm::opId (o, "Ratio")) * 211 + (juce::uint64) (value (fm::opId (o, "Level")) * 100);
        return h;
    }

    juce::uint64 shown = 0;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        const auto& alg = fm::algorithm ((int) value ("algo"));

        // Profondita': 0 = portante (esce), 1 = modula una portante, ...
        int depth[fm::numOps] {};
        for (int pass = 0; pass < 4; ++pass)
            for (int o = 0; o < fm::numOps; ++o)
                for (int t = 0; t < fm::numOps; ++t)
                    if ((alg.modulators[(size_t) t] >> o) & 1)
                        depth[o] = juce::jmax (depth[o], depth[t] + 1);
        int maxDepth = 0;
        for (int d : depth) maxDepth = juce::jmax (maxDepth, d);

        auto area = r.reduced (12.0f, 14.0f).withTrimmedBottom (14.0f);
        const float boxW = juce::jmin (56.0f, area.getWidth() / 5.0f), boxH = juce::jmin (38.0f, area.getHeight() / (float) (maxDepth + 2));
        const float rowStep = maxDepth > 0 ? (area.getHeight() - boxH) / (float) maxDepth : 0.0f;

        // Posizioni: portanti distribuite in basso, modulatori sopra i loro bersagli
        float x[fm::numOps] {};
        int numCarriers = 0;
        for (int o = 0; o < fm::numOps; ++o) if ((alg.carriers >> o) & 1) ++numCarriers;
        int k = 0;
        for (int o = 0; o < fm::numOps; ++o)
            if ((alg.carriers >> o) & 1)
                x[o] = area.getX() + area.getWidth() * ((float) k++ + 0.5f) / (float) numCarriers;
        for (int d = 1; d <= maxDepth; ++d)
        {
            std::vector<int> atDepth;
            for (int o = 0; o < fm::numOps; ++o)
                if (depth[o] == d && ! ((alg.carriers >> o) & 1))
                {
                    float sum = 0.0f; int n = 0;
                    for (int t = 0; t < fm::numOps; ++t)
                        if ((alg.modulators[(size_t) t] >> o) & 1) { sum += x[t]; ++n; }
                    x[o] = n > 0 ? sum / (float) n : area.getCentreX();
                    atDepth.push_back (o);
                }
            // Due operatori sullo stesso punto: si allargano
            for (size_t i = 0; i < atDepth.size(); ++i)
                for (size_t j = i + 1; j < atDepth.size(); ++j)
                    if (std::abs (x[atDepth[i]] - x[atDepth[j]]) < boxW * 1.2f)
                    {
                        x[atDepth[i]] -= boxW * 0.65f;
                        x[atDepth[j]] += boxW * 0.65f;
                    }
        }

        auto box = [&] (int o)
        {
            const float yTop = area.getBottom() - boxH - (float) depth[o] * rowStep;
            return juce::Rectangle<float> (x[o] - boxW * 0.5f, yTop, boxW, boxH);
        };

        // Collegamenti
        g.setColour (col::textDim);
        for (int t = 0; t < fm::numOps; ++t)
            for (int m = 0; m < fm::numOps; ++m)
                if ((alg.modulators[(size_t) t] >> m) & 1)
                {
                    const auto from = box (m), to = box (t);
                    g.drawArrow ({ from.getCentreX(), from.getBottom(), to.getCentreX(), to.getY() }, 1.6f, 7.0f, 7.0f);
                }
        // Uscite
        const float outY = area.getBottom() + 12.0f;
        for (int o = 0; o < fm::numOps; ++o)
            if ((alg.carriers >> o) & 1)
            {
                g.setColour (col::cyan.withAlpha (0.7f));
                g.drawLine (x[o], box (o).getBottom(), x[o], outY - 2.0f, 1.6f);
            }
        g.setColour (col::cyan.withAlpha (0.7f));
        g.drawLine (area.getX(), outY, area.getRight(), outY, 1.6f);

        // Feedback dell'operatore 4
        if (value ("feedback") > 0.01f)
        {
            const auto b = box (3);
            juce::Path loop;
            loop.startNewSubPath (b.getRight(), b.getCentreY());
            loop.cubicTo (b.getRight() + 16.0f, b.getCentreY(), b.getRight() + 16.0f, b.getY() - 12.0f, b.getCentreX(), b.getY() - 10.0f);
            loop.lineTo (b.getCentreX(), b.getY());
            g.setColour (col::orange);
            g.strokePath (loop, juce::PathStrokeType (1.5f));
        }

        // Blocchi
        for (int o = 0; o < fm::numOps; ++o)
        {
            const auto b = box (o);
            const bool carrier = (alg.carriers >> o) & 1;
            const auto c = carrier ? col::cyan : col::orange;
            const float level = value (fm::opId (o, "Level"));
            g.setColour (col::panelHi);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (c.withAlpha (0.25f + 0.75f * level));
            g.drawRoundedRectangle (b, 5.0f, 2.0f);
            g.fillRect (b.withTrimmedTop (b.getHeight() - 3.0f).withWidth (b.getWidth() * level).reduced (3.0f, 0.0f));

            g.setColour (col::text);
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (15.0f, boxH * 0.42f), juce::Font::bold)));
            g.drawText (juce::String (o + 1), b.withTrimmedBottom (b.getHeight() * 0.42f), juce::Justification::centred);
            g.setColour (col::textDim);
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (11.0f, boxH * 0.3f))));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + fm::ratioNames()[(int) value (fm::opId (o, "Ratio"))],
                        b.withTrimmedTop (b.getHeight() * 0.5f), juce::Justification::centred);
        }

        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("azzurro = esce (portante)   arancio = modula", r.reduced (8.0f, 3.0f), juce::Justification::topLeft);
    }

private:
    float value (const juce::String& id) const
    {
        if (auto* v = apvts.getRawParameterValue (id))
            return v->load();
        return 0.0f;
    }

    juce::AudioProcessorValueTreeState& apvts;
};

//==============================================================================
FMForgeEditor::FMForgeEditor (FMForgeAudioProcessor& p)
    : ForgeEditorBase (p, p.apvts, "FM", "FORGE", fmAccent, { "ALGO", "OP1", "OP2", "OP3", "OP4", "FX" })
{
    // --- Algoritmo e voce
    auto& a = addSection (0, "Algoritmo", 2.0f);
    combo (a, "algo", "Algoritmo");
    knob (a, "feedback", "Feedback");
    knob (a, "bright", "Timbro");
    knob (a, "tone", "Tono");
    auto view = std::make_unique<AlgorithmView> (p.apvts);
    algoView = view.get();
    setView (a, std::move (view), 0.55f);

    auto& v = addSection (0, "Voce", 0.6f);
    combo (v, "mode", "Modo");
    knob (v, "glide", "Glide");

    // --- Operatori
    for (int o = 0; o < fm::numOps; ++o)
    {
        auto& op = addSection (1 + o, "Operatore " + juce::String (o + 1), 1.0f);
        combo (op, fm::opId (o, "Ratio"), "Rapporto");
        knob (op, fm::opId (o, "Fine"), "Fine");
        knob (op, fm::opId (o, "Level"), "Livello");
        knob (op, fm::opId (o, "Vel"), "Velocity");

        auto& e = addSection (1 + o, "Inviluppo operatore " + juce::String (o + 1), 1.6f);
        knob (e, fm::opId (o, "A"), "Attacco");
        knob (e, fm::opId (o, "D"), "Decay");
        knob (e, fm::opId (o, "S"), "Sustain");
        knob (e, fm::opId (o, "R"), "Rilascio");
        adsr (e, fm::opId (o, "A"), fm::opId (o, "D"), fm::opId (o, "S"), fm::opId (o, "R"), col::cyan);
    }

    // --- LFO ed effetti
    auto& l = addSection (5, "LFO", 1.0f);
    combo (l, "lfoWave", "Forma");
    knob (l, "lfoRate", "Velocita'");
    toggle (l, "lfoSync", "A tempo");
    combo (l, "lfoDiv", "Divisione");
    knob (l, "lfoVib", "Vibrato");
    knob (l, "lfoTrem", "Tremolo");
    knob (l, "lfoBright", "Timbro");

    auto& fx = addSection (5, "Effetti", 1.3f);
    knob (fx, "chorus", "Chorus");
    knob (fx, "dlyMix", "Delay");
    combo (fx, "dlyTime", "Tempo");
    knob (fx, "dlyFb", "Feedback");
    knob (fx, "revMix", "Riverbero");
    knob (fx, "revSize", "Stanza");
    knob (fx, "master", "Volume");

    showTab (0);
}

void FMForgeEditor::refreshViews()
{
    if (algoView != nullptr && algoView->isShowing())
        if (const auto sig = algoView->signature(); sig != algoView->shown)
        {
            algoView->shown = sig;
            algoView->repaint();
        }
}
