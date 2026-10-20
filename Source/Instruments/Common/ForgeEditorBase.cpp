#include "ForgeEditorBase.h"

namespace col = bf::ui::col;

//==============================================================================
ForgeEditorBase::AdsrView::AdsrView (juce::AudioProcessorValueTreeState& s, juce::String a, juce::String d,
                                     juce::String su, juce::String r, juce::Colour c)
    : apvts (s), ids { a, d, su, r }, colour (c) {}

float ForgeEditorBase::AdsrView::value (int i) const
{
    if (auto* v = apvts.getRawParameterValue (ids[(size_t) i]))
        return v->load();
    return 0.0f;
}

juce::uint64 ForgeEditorBase::AdsrView::signature() const
{
    juce::uint64 h = 0;
    for (int i = 0; i < 4; ++i) h = h * 1000003u + (juce::uint64) (value (i) * 100000.0f);
    return h;
}

void ForgeEditorBase::AdsrView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::edge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    const float A = value (0), D = value (1), S = value (2), R = value (3);
    // Tempi in scala "compressa" (radice): anche tempi molto diversi restano leggibili
    const float a = std::sqrt (A), d = std::sqrt (D), rel = std::sqrt (R), hold = 0.35f;
    const float total = a + d + hold + rel;
    auto in = r.reduced (8.0f, 8.0f).withTrimmedTop (10.0f);
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

    auto fmt = [] (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (s, 2) + " s"; };
    g.setColour (col::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawFittedText ("A " + fmt (A) + "  D " + fmt (D) + "  S " + juce::String (juce::roundToInt (S * 100)) + "%  R " + fmt (R),
                      r.reduced (8.0f, 3.0f).toNearestInt(), juce::Justification::topRight, 1);
}

//==============================================================================
ForgeEditorBase::ForgeEditorBase (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state,
                                  const juce::String& logoLeft, const juce::String& logoRight, juce::Colour accentColour,
                                  const juce::StringArray& tabNames)
    : juce::AudioProcessorEditor (&p), apvts (state), accent (accentColour), logoA (logoLeft), logoB (logoRight)
{
    setLookAndFeel (&laf);
    sections.resize ((size_t) tabNames.size());

    for (int i = 0; i < tabNames.size(); ++i)
    {
        auto* t = tabs.add (new juce::TextButton (tabNames[i]));
        t->setClickingTogglesState (true);
        t->setRadioGroupId (7301);
        t->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (t);
    }
    if (! tabs.isEmpty())
        tabs[0]->setToggleState (true, juce::dontSendNotification);

    setSize (900, 420);
    startTimerHz (15);
}

ForgeEditorBase::~ForgeEditorBase()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

ForgeEditorBase::Section& ForgeEditorBase::addSection (int tabIndex, const juce::String& title, float weight)
{
    auto s = std::make_unique<Section>();
    s->panel = std::make_unique<bf::ui::Panel> (title);
    s->weight = weight;
    addChildComponent (*s->panel);
    sections[(size_t) tabIndex].push_back (std::move (s));
    return *sections[(size_t) tabIndex].back();
}

void ForgeEditorBase::knob (Section& s, const juce::String& id, const juce::String& title)
{
    auto k = std::make_unique<bf::ui::ParamKnob> (apvts, id, title);
    s.panel->addAndMakeVisible (*k);
    s.items.push_back (k.get());
    owned.push_back (std::move (k));
}

void ForgeEditorBase::combo (Section& s, const juce::String& id, const juce::String& title)
{
    auto c = std::make_unique<bf::ui::ParamCombo> (apvts, id, title);
    s.panel->addAndMakeVisible (*c);
    s.items.push_back (c.get());
    owned.push_back (std::move (c));
}

void ForgeEditorBase::toggle (Section& s, const juce::String& id, const juce::String& title)
{
    auto t = std::make_unique<bf::ui::ParamToggle> (apvts, id, title);
    s.panel->addAndMakeVisible (*t);
    s.items.push_back (t.get());
    owned.push_back (std::move (t));
}

void ForgeEditorBase::setView (Section& s, std::unique_ptr<juce::Component> view, float share)
{
    s.view = view.get();
    s.viewShare = share;
    s.panel->addAndMakeVisible (*view);
    owned.push_back (std::move (view));
}

juce::Component& ForgeEditorBase::item (Section& s, std::unique_ptr<juce::Component> comp)
{
    auto& ref = *comp;
    s.panel->addAndMakeVisible (ref);
    s.items.push_back (comp.get());
    owned.push_back (std::move (comp));
    return ref;
}

void ForgeEditorBase::setHidden (Section& s, bool hidden)
{
    s.hidden = hidden;
    showTab (tab);
}

void ForgeEditorBase::adsr (Section& s, const juce::String& a, const juce::String& d, const juce::String& su,
                            const juce::String& r, juce::Colour c)
{
    setView (s, std::make_unique<AdsrView> (apvts, a, d, su, r, c));
}

void ForgeEditorBase::showTab (int index)
{
    tab = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), index);
    if (auto* t = tabs[tab])
        t->setToggleState (true, juce::dontSendNotification);
    for (size_t t = 0; t < sections.size(); ++t)
        for (auto& s : sections[t])
            s->panel->setVisible ((int) t == tab && ! s->hidden);
    resized();
}

void ForgeEditorBase::timerCallback()
{
    for (auto& c : owned)
        if (auto* v = dynamic_cast<AdsrView*> (c.get()))
            if (const auto sig = v->signature(); sig != v->shown && v->isShowing())
            {
                v->shown = sig;
                v->repaint();
            }
    refreshViews();
}

void ForgeEditorBase::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto head = getLocalBounds().removeFromTop (headerHeight);
    g.setGradientFill (juce::ColourGradient (col::panelHi, 0.0f, 0.0f, col::bg, 0.0f, (float) head.getHeight(), false));
    g.fillRect (head);
    g.setColour (col::edge);
    g.drawHorizontalLine (head.getBottom() - 1, 0.0f, (float) getWidth());

    auto logo = head.withWidth (124).translated (10, 0);
    const auto font = juce::Font (juce::FontOptions (18.0f, juce::Font::bold));
    g.setFont (font);
    g.setColour (col::text);
    g.drawText (logoA, logo, juce::Justification::centredLeft);
    const int wA = juce::GlyphArrangement::getStringWidthInt (font, logoA + " ");
    g.setColour (accent);
    g.drawText (logoB, logo.withTrimmedLeft (wA), juce::Justification::centredLeft);
    g.setColour (col::orange);
    g.fillRoundedRectangle ((float) logo.getX(), (float) logo.getBottom() - 7.0f, 108.0f, 2.0f, 1.0f);
}

void ForgeEditorBase::resized()
{
    auto b = getLocalBounds();
    auto head = b.removeFromTop (headerHeight).reduced (8, 5);
    head.removeFromLeft (128);
    if (! tabs.isEmpty())
    {
        const int tw = juce::jmin (100, head.getWidth() / tabs.size());
        for (auto* t : tabs)
            t->setBounds (head.removeFromLeft (tw).reduced (2, 0));
    }

    auto area = b.reduced (6);
    if (tab >= (int) sections.size())
        return;
    std::vector<Section*> list;
    for (auto& s : sections[(size_t) tab])
        if (! s->hidden)
            list.push_back (s.get());
    if (list.empty())
        return;

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
            if (s.items.empty())
                s.view->setBounds (c);
            else if (c.getWidth() > c.getHeight() * 2)
                s.view->setBounds (c.removeFromLeft ((int) ((float) c.getWidth() * s.viewShare)).reduced (0, 2));
            else
                s.view->setBounds (c.removeFromTop ((int) ((float) c.getHeight() * s.viewShare)).reduced (0, 2));
        }

        const int n = (int) s.items.size();
        if (n == 0)
            continue;
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
