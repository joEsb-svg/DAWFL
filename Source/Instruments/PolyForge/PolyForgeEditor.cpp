#include "PolyForgeEditor.h"

namespace col = bf::ui::col;
static const juce::Colour violet { 0xffb07cff };

//==============================================================================
/** Disegno di un inviluppo ADSR letto dai parametri. */
class PolyForgeEditor::AdsrView : public juce::Component
{
public:
    AdsrView (juce::AudioProcessorValueTreeState& s, const char* a, const char* d, const char* su, const char* r, juce::Colour c)
        : apvts (s), ids { a, d, su, r }, colour (c) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        const float A = value (0), D = value (1), S = value (2), R = value (3);
        // Scala dei tempi "compressa" (radice): anche tempi molto diversi restano leggibili
        const float a = std::sqrt (A), d = std::sqrt (D), rel = std::sqrt (R), hold = 0.35f;
        const float total = a + d + hold + rel;
        auto in = r.reduced (8.0f, 8.0f);
        auto x = [&] (float t) { return in.getX() + in.getWidth() * t / total; };
        auto y = [&] (float v) { return in.getBottom() - in.getHeight() * v; };

        juce::Path p;
        p.startNewSubPath (x (0), y (0));
        p.lineTo (x (a), y (1));
        p.quadraticTo (x (a + d * 0.3f), y (S + (1 - S) * 0.15f), x (a + d), y (S));
        p.lineTo (x (a + d + hold), y (S));
        p.quadraticTo (x (a + d + hold + rel * 0.3f), y (S * 0.15f), x (total), y (0));

        juce::Path fill (p);
        fill.lineTo (x (0), y (0));
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (col::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("A " + fmt (A) + "   D " + fmt (D) + "   S " + juce::String (juce::roundToInt (S * 100)) + "%   R " + fmt (R),
                    r.reduced (8.0f, 4.0f), juce::Justification::topRight, false);
    }

    juce::uint64 signature() const
    {
        juce::uint64 h = 0;
        for (int i = 0; i < 4; ++i) h = h * 1000003u + (juce::uint64) (value (i) * 100000.0f);
        return h;
    }

    juce::uint64 shown = 0;

private:
    float value (int i) const   { return apvts.getRawParameterValue (ids[(size_t) i])->load(); }
    static juce::String fmt (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (s, 2) + " s"; }

    juce::AudioProcessorValueTreeState& apvts;
    std::array<const char*, 4> ids;
    juce::Colour colour;
};

//==============================================================================
PolyForgeEditor::PolyForgeEditor (PolyForgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    using namespace pf::ids;

    const char* names[5] = { "OSC", "FILTRO", "ENV", "LFO", "FX" };
    for (int i = 0; i < 5; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (names[i]);
        t.setClickingTogglesState (true);
        t.setRadioGroupId (7301);
        t.onClick = [this, i] { showTab (i); };
        addAndMakeVisible (t);
    }

    // --- OSC
    {
        auto& s1 = addSection (0, "Oscillatore 1", 1.2f);
        combo (s1, osc1Wave, "Onda");
        knob (s1, osc1Lvl, "Livello");
        knob (s1, osc1Uni, "Unison");
        knob (s1, osc1Det, "Detune");
        knob (s1, osc1Pw, "PW (quadra)");

        auto& s2 = addSection (0, "Oscillatore 2", 1.5f);
        combo (s2, osc2Wave, "Onda");
        knob (s2, osc2Lvl, "Livello");
        knob (s2, osc2Semi, "Semitoni");
        knob (s2, osc2Fine, "Fine");
        knob (s2, osc2Uni, "Unison");
        knob (s2, osc2Det, "Detune");

        auto& s3 = addSection (0, "Sub e rumore", 0.75f);
        knob (s3, subLvl, "Sub");
        knob (s3, noiseLvl, "Rumore");
        knob (s3, spread, "Stereo");
    }

    // --- FILTRO
    {
        auto& f = addSection (1, "Filtro", 1.2f);
        combo (f, fltType, "Tipo");
        knob (f, cutoff, "Cutoff");
        knob (f, reso, "Risonanza");
        knob (f, fltDrive, "Drive");
        knob (f, keyTrack, "Keytrack");
        knob (f, fltVel, "Velocity");

        auto& e = addSection (1, "Inviluppo del filtro", 1.3f);
        knob (e, fltEnv, "Quantita'");
        knob (e, fltA, "Attacco");
        knob (e, fltD, "Decay");
        knob (e, fltS, "Sustain");
        knob (e, fltR, "Rilascio");
        auto view = std::make_unique<AdsrView> (proc.apvts, fltA, fltD, fltS, fltR, col::orange);
        e.view = view.get();
        e.panel->addAndMakeVisible (*view);
        owned.push_back (std::move (view));
    }

    // --- ENV
    {
        auto& a = addSection (2, "Volume (ADSR)", 1.6f);
        knob (a, ampA, "Attacco");
        knob (a, ampD, "Decay");
        knob (a, ampS, "Sustain");
        knob (a, ampR, "Rilascio");
        knob (a, velSens, "Velocity");
        auto view = std::make_unique<AdsrView> (proc.apvts, ampA, ampD, ampS, ampR, col::cyan);
        a.view = view.get();
        a.panel->addAndMakeVisible (*view);
        owned.push_back (std::move (view));

        auto& v = addSection (2, "Voce", 0.6f);
        combo (v, mode, "Modo");
        knob (v, glide, "Glide");
    }

    // --- LFO
    {
        auto& l = addSection (3, "LFO", 1.0f);
        combo (l, lfoWave, "Forma");
        knob (l, lfoRate, "Velocita'");
        toggle (l, lfoSync, "A tempo");
        combo (l, lfoDiv, "Divisione");

        auto& d = addSection (3, "Cosa muove", 1.0f);
        knob (d, lfoPitch, "Vibrato");
        knob (d, lfoCut, "Filtro");
        knob (d, lfoAmp, "Volume");
        knob (d, lfoPwm, "PW");
    }

    // --- FX
    {
        auto& c = addSection (4, "Chorus", 0.5f);
        knob (c, chorus, "Mix");

        auto& d = addSection (4, "Delay", 1.0f);
        knob (d, dlyMix, "Mix");
        combo (d, dlyTime, "Tempo");
        knob (d, dlyFb, "Feedback");

        auto& r = addSection (4, "Riverbero", 0.8f);
        knob (r, revMix, "Mix");
        knob (r, revSize, "Stanza");

        auto& o = addSection (4, "Uscita", 0.5f);
        knob (o, master, "Volume");
    }

    tabs[0].setToggleState (true, juce::dontSendNotification);
    showTab (0);
    setSize (900, 420);
    startTimerHz (15);
}

PolyForgeEditor::~PolyForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

PolyForgeEditor::Section& PolyForgeEditor::addSection (int tabIndex, const juce::String& title, float weight)
{
    auto s = std::make_unique<Section>();
    s->panel = std::make_unique<bf::ui::Panel> (title);
    s->weight = weight;
    addChildComponent (*s->panel);
    sections[(size_t) tabIndex].push_back (std::move (s));
    return *sections[(size_t) tabIndex].back();
}

void PolyForgeEditor::knob (Section& s, const char* id, const juce::String& title)
{
    auto k = std::make_unique<bf::ui::ParamKnob> (proc.apvts, id, title);
    s.panel->addAndMakeVisible (*k);
    s.items.push_back (k.get());
    owned.push_back (std::move (k));
}

void PolyForgeEditor::combo (Section& s, const char* id, const juce::String& title)
{
    auto c = std::make_unique<bf::ui::ParamCombo> (proc.apvts, id, title);
    s.panel->addAndMakeVisible (*c);
    s.items.push_back (c.get());
    owned.push_back (std::move (c));
}

void PolyForgeEditor::toggle (Section& s, const char* id, const juce::String& title)
{
    auto t = std::make_unique<bf::ui::ParamToggle> (proc.apvts, id, title);
    s.panel->addAndMakeVisible (*t);
    s.items.push_back (t.get());
    owned.push_back (std::move (t));
}

void PolyForgeEditor::showTab (int index)
{
    tab = juce::jlimit (0, 4, index);
    tabs[(size_t) tab].setToggleState (true, juce::dontSendNotification);
    for (int t = 0; t < 5; ++t)
        for (auto& s : sections[(size_t) t])
            s->panel->setVisible (t == tab);
    resized();
}

void PolyForgeEditor::timerCallback()
{
    // Ridisegna gli inviluppi quando cambiano i parametri
    for (auto& c : owned)
        if (auto* v = dynamic_cast<AdsrView*> (c.get()))
            if (const auto sig = v->signature(); sig != v->shown && v->isShowing())
            {
                v->shown = sig;
                v->repaint();
            }
}

void PolyForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto head = getLocalBounds().removeFromTop (headerHeight);
    g.setGradientFill (juce::ColourGradient (col::panelHi, 0.0f, 0.0f, col::bg, 0.0f, (float) head.getHeight(), false));
    g.fillRect (head);
    g.setColour (col::edge);
    g.drawHorizontalLine (head.getBottom() - 1, 0.0f, (float) getWidth());

    auto logo = head.withWidth (120).translated (10, 0);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.setColour (col::text);
    g.drawText ("POLY", logo, juce::Justification::centredLeft);
    g.setColour (violet);
    g.drawText ("FORGE", logo.withTrimmedLeft (50), juce::Justification::centredLeft);
    g.setColour (col::orange);
    g.fillRoundedRectangle ((float) logo.getX(), (float) logo.getBottom() - 7.0f, 108.0f, 2.0f, 1.0f);
}

void PolyForgeEditor::resized()
{
    auto b = getLocalBounds();
    auto head = b.removeFromTop (headerHeight).reduced (8, 5);
    head.removeFromLeft (128);
    const int tw = juce::jmin (100, head.getWidth() / 5);
    for (auto& t : tabs)
        t.setBounds (head.removeFromLeft (tw).reduced (2, 0));

    auto area = b.reduced (6);
    auto& list = sections[(size_t) tab];
    if (list.empty())
        return;

    // Orizzontale: pannelli affiancati (larghezza in proporzione); verticale: uno sotto l'altro
    const bool horizontal = area.getWidth() >= area.getHeight();
    float totalWeight = 0.0f;
    for (auto& s : list) totalWeight += s->weight;

    const int total = horizontal ? area.getWidth() : area.getHeight();
    int pos = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        auto& s = *list[i];
        const int size = i + 1 == list.size() ? total - pos : juce::roundToInt ((float) total * s.weight / totalWeight);
        auto r = horizontal ? juce::Rectangle<int> (area.getX() + pos, area.getY(), size, area.getHeight())
                            : juce::Rectangle<int> (area.getX(), area.getY() + pos, area.getWidth(), size);
        pos += size;
        s.panel->setBounds (r.reduced (3));

        auto c = s.panel->content();
        if (s.view != nullptr)
        {
            // Disegno dell'inviluppo: sopra le manopole se il pannello e' alto, a sinistra se e' largo
            if (c.getWidth() > c.getHeight() * 2)
                s.view->setBounds (c.removeFromLeft ((int) ((float) c.getWidth() * 0.38f)).reduced (0, 2));
            else
                s.view->setBounds (c.removeFromTop ((int) ((float) c.getHeight() * 0.38f)).reduced (0, 2));
        }

        // Colonne scelte perche' le celle siano circa quadrate
        const int n = (int) s.items.size();
        int best = 1; double bestScore = 1e9;
        for (int cols = 1; cols <= n; ++cols)
        {
            const int rows = (n + cols - 1) / cols;
            const double cell = ((double) c.getWidth() / cols) / ((double) c.getHeight() / rows);
            const double score = std::abs (std::log (cell / 0.9)) + 0.35 * (double) (cols * rows - n) / (double) cols;
            if (score < bestScore) { bestScore = score; best = cols; }
        }
        bf::ui::layoutGrid (c, s.items, best);
    }
}
