#include "DrumForgeEditor.h"

namespace col = bf::ui::col;
static const juce::Colour dfAccent { 0xffff6b5b };

/** Colore del pad a partire dal nome del suono (cassa arancio, rullante rosso, hat verde acqua...). */
static juce::Colour padColour (const juce::String& name, int pad)
{
    const auto n = name.toLowerCase();
    if (n.contains ("kick") || n.contains ("cassa") || n.contains ("rumble") || (n == "808" && pad < 2))
        return juce::Colour (0xffe8833a);
    if (n.contains ("snare") || n.contains ("rullante") || n.contains ("spazzola"))  return juce::Colour (0xffd2555a);
    if (n.contains ("clap") || n.contains ("mani") || n.contains ("schiocco"))       return juce::Colour (0xffd8b14a);
    if (n.contains ("hat") || n.contains ("shaker") || n.contains ("maracas"))       return juce::Colour (0xff4fb3a9);
    if (n.contains ("tom") || n.contains ("conga") || n.contains ("bongo") || n.contains ("djembe")) return juce::Colour (0xff9a78d0);
    if (n.contains ("crash") || n.contains ("ride") || n.contains ("piatto") || n.contains ("splash") || n.contains ("china"))
        return juce::Colour (0xff5b8fd6);
    if (n.contains ("rim") || n.contains ("stick"))                                   return juce::Colour (0xffc94a4f);
    return juce::Colour (0xff8aa0b8);
}

//==============================================================================
class DrumForgeEditor::PadGrid : public juce::Component
{
public:
    PadGrid (DrumForgeEditor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        for (int p = 0; p < df::numPads; ++p)
        {
            auto r = padBounds (p);
            const auto name = editor.proc.getPadName (p);
            const auto colour = padColour (name, p);
            const float hit = editor.proc.getPadActivity (p);

            g.setColour (col::bg.interpolatedWith (colour, 0.22f + 0.6f * hit));
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (p == editor.selected ? col::text : colour.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, p == editor.selected ? 2.5f : 1.0f);

            g.setColour (colour.brighter (0.4f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (juce::String (p + 1), r.reduced (6.0f, 4.0f), juce::Justification::topLeft);

            g.setColour (col::text);
            g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 14.0f, r.getHeight() * 0.2f))));
            g.drawFittedText (name, r.reduced (5.0f, 4.0f).withTrimmedTop (12.0f).toNearestInt(), juce::Justification::centred, 2, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int p = 0; p < df::numPads; ++p)
            if (padBounds (p).contains (e.position))
            {
                // Piu' in alto si tocca il pad, piu' forte suona
                auto r = padBounds (p);
                const float vel = juce::jlimit (0.35f, 1.0f, 1.0f - 0.65f * (e.position.y - r.getY()) / r.getHeight() + 0.2f);
                editor.proc.hitPad (p, vel);
                editor.selectPad (p);
                return;
            }
    }

    juce::Rectangle<float> padBounds (int p) const
    {
        auto area = getLocalBounds().toFloat().reduced (2.0f);
        const float w = area.getWidth() / 4.0f, h = area.getHeight() / 4.0f;
        return { area.getX() + w * (float) (p % 4), area.getY() + h * (float) (p / 4), w, h };
    }

private:
    DrumForgeEditor& editor;
};

//==============================================================================
class DrumForgeEditor::KitList : public juce::Component
{
public:
    KitList (DrumForgeEditor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        const auto current = editor.proc.getPresets().currentName();
        for (int i = 0; i < count(); ++i)
        {
            auto r = cell (i).reduced (3.0f);
            const bool on = df::Presets::factoryName (i) == current;
            g.setColour (on ? dfAccent.withAlpha (0.35f) : col::panelHi);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (on ? dfAccent : col::edge);
            g.drawRoundedRectangle (r, 6.0f, on ? 2.0f : 1.0f);
            g.setColour (on ? col::text : col::textDim);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawFittedText (df::Presets::factoryName (i), r.reduced (4.0f).toNearestInt(), juce::Justification::centred, 2);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        for (int i = 0; i < count(); ++i)
            if (cell (i).contains (e.position))
            {
                editor.proc.getPresets().loadFactory (i);
                editor.proc.hitPad (0);
                editor.refreshViews();
                repaint();
                return;
            }
    }

private:
    int count() const       { return df::Presets::numFactory(); }
    int columns() const     { return getWidth() > getHeight() * 2 ? 6 : getWidth() > 420 ? 4 : 3; }

    juce::Rectangle<float> cell (int i) const
    {
        const int cols = columns(), rows = (count() + cols - 1) / cols;
        const float w = (float) getWidth() / (float) cols, h = juce::jmin (64.0f, (float) getHeight() / (float) rows);
        return { w * (float) (i % cols), h * (float) (i / cols), w, h };
    }

    DrumForgeEditor& editor;
};

//==============================================================================
class DrumForgeEditor::SampleButton : public juce::Component
{
public:
    SampleButton (DrumForgeEditor& e, int p) : editor (e), pad (p)
    {
        title.setText ("Suono", juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centredLeft);
        title.setColour (juce::Label::textColourId, col::textDim);
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);
        button.onClick = [this] { editor.showSampleMenu (pad, button); };
        addAndMakeVisible (button);
        refresh();
    }

    void refresh()
    {
        const auto name = SampleStore::displayName (editor.proc.getPadSample (pad));
        button.setButtonText ((name.isNotEmpty() ? name : juce::String ("(vuoto)")) + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (2, 0);
        title.setBounds (b.removeFromTop (14));
        button.setBounds (b.removeFromTop (juce::jmin (34, b.getHeight())));
    }

private:
    DrumForgeEditor& editor;
    int pad;
    juce::Label title;
    juce::TextButton button;
};

//==============================================================================
DrumForgeEditor::DrumForgeEditor (DrumForgeAudioProcessor& p)
    : ForgeEditorBase (p, p.apvts, "DRUM", "FORGE", dfAccent, { "PAD", "KIT" }), proc (p)
{
    // --- PAD
    auto& padsSection = addSection (0, "Pad", 1.15f);
    {
        auto g = std::make_unique<PadGrid> (*this);
        grid = g.get();
        setView (padsSection, std::move (g), 1.0f);
    }

    for (int i = 0; i < df::numPads; ++i)
    {
        auto& s = addSection (0, "Pad " + juce::String (i + 1), 1.0f);
        padSections[(size_t) i] = &s;
        sampleButtons[(size_t) i] = static_cast<SampleButton*> (&item (s, std::make_unique<SampleButton> (*this, i)));
        knob (s, df::padId (i, "Tune"), "Intonazione");
        knob (s, df::padId (i, "Decay"), "Decadimento");
        knob (s, df::padId (i, "Level"), "Volume");
        knob (s, df::padId (i, "Pan"), "Panorama");
        knob (s, df::padId (i, "Cutoff"), "Filtro");
        knob (s, df::padId (i, "Reverb"), "Riverbero");
        knob (s, df::padId (i, "Vel"), "Velocity");
        combo (s, df::padId (i, "Choke"), "Choke");
        s.hidden = i != 0;
    }

    // --- KIT
    auto& k = addSection (1, "Kit", 1.6f);
    {
        auto list = std::make_unique<KitList> (*this);
        kits = list.get();
        setView (k, std::move (list), 1.0f);
    }
    auto& out = addSection (1, "Uscita", 0.8f);
    knob (out, df::ids::master, "Volume");
    knob (out, df::ids::drive, "Saturazione");
    knob (out, df::ids::revMix, "Riverbero");
    knob (out, df::ids::revSize, "Stanza");

    showTab (0);
    refreshViews();
}

DrumForgeEditor::~DrumForgeEditor()
{
    picker.reset();
}

void DrumForgeEditor::selectPad (int pad)
{
    pad = juce::jlimit (0, df::numPads - 1, pad);
    if (pad != selected)
    {
        selected = pad;
        for (int i = 0; i < df::numPads; ++i)
            padSections[(size_t) i]->hidden = i != selected;
        showTab (getTab());
    }
    grid->repaint();
}

void DrumForgeEditor::refreshViews()
{
    // Nomi dei pad (cambiano con il kit o il suono scelto)
    bool changed = false;
    for (int i = 0; i < df::numPads; ++i)
    {
        const auto name = proc.getPadName (i);
        const auto key = name + "|" + proc.getPadSample (i);
        if (key != shownNames[(size_t) i])
        {
            shownNames[(size_t) i] = key;
            padSections[(size_t) i]->panel->setTitle ("Pad " + juce::String (i + 1) + "  " + name);
            sampleButtons[(size_t) i]->refresh();
            changed = true;
        }
    }
    if (changed)
    {
        grid->repaint();
        kits->repaint();
    }

    // Lampeggio dei pad suonati
    bool lit = false;
    for (int i = 0; i < df::numPads; ++i)
        lit = lit || proc.getPadActivity (i) > 0.02f;
    if (lit)
    {
        proc.decayPadActivity (0.72f);
        grid->repaint();
    }
}

//==============================================================================
/** Griglia di voci da toccare (scorre col dito dentro un Viewport). */
class DrumForgeEditor::TapGrid : public juce::Component
{
public:
    struct Item { juce::String text; juce::Colour colour; };
    std::function<void (int)> onTap;

    void setItems (std::vector<Item> list, int sel)     { items = std::move (list); selected = sel; updateSize(); repaint(); }
    void setSelected (int s)                             { selected = s; repaint(); }
    void setLayout (int cols, int h, int width)          { columns = juce::jmax (1, cols); itemH = h; w = width; updateSize(); }

    juce::Rectangle<int> itemBounds (int i) const
    {
        const int gap = 5, cw = (w - gap * (columns - 1)) / columns;
        return { (i % columns) * (cw + gap), (i / columns) * (itemH + gap), cw, itemH };
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) items.size(); ++i)
        {
            auto r = itemBounds (i).toFloat();
            if (! g.clipRegionIntersects (r.toNearestInt()))
                continue;
            const bool sel = i == selected;
            g.setColour (sel ? col::panelHi.brighter (0.15f) : col::panelHi);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (sel ? dfAccent : col::edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, sel ? 2.0f : 1.0f);
            g.setColour (items[(size_t) i].colour);
            g.fillRoundedRectangle (r.withWidth (4.0f).reduced (0.0f, 6.0f).translated (5.0f, 0.0f), 2.0f);
            g.setColour (sel ? col::text : col::text.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (14.0f, r.getHeight() * 0.38f), juce::Font::bold)));
            g.drawFittedText (items[(size_t) i].text, r.withTrimmedLeft (14.0f).reduced (2.0f, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 2, 0.85f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 10)
            return;   // era uno scorrimento
        for (int i = 0; i < (int) items.size(); ++i)
            if (itemBounds (i).contains (e.getMouseDownPosition()))
            {
                if (onTap) onTap (i);
                return;
            }
    }

private:
    void updateSize()
    {
        const int rows = ((int) items.size() + columns - 1) / columns;
        setSize (juce::jmax (1, w), juce::jmax (1, rows * (itemH + 5)));
    }

    std::vector<Item> items;
    int selected = -1, columns = 1, itemH = 44, w = 100;
};

//==============================================================================
/**
    Scelta del suono di un pad, a tutto schermo dentro DrumForge (al posto del menu a tendina):
    a sinistra le categorie (prima quelle di batteria), a destra i suoni. Un tocco = suono assegnato e ascoltato.
*/
class DrumForgeEditor::SamplePicker : public juce::Component
{
public:
    explicit SamplePicker (DrumForgeEditor& e) : editor (e)
    {
        for (auto* b : { &doneButton, &importButton })
            addAndMakeVisible (b);
        doneButton.onClick = [this] { setVisible (false); editor.refreshViews(); };
        importButton.onClick = [this] { editor.importSample (pad); };

        for (auto* vp : { &catView, &itemView })
        {
            vp->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
            vp->setScrollBarsShown (true, false);
            vp->setScrollBarThickness (5);
            addAndMakeVisible (vp);
        }
        catView.setViewedComponent (&cats, false);
        itemView.setViewedComponent (&grid, false);
        cats.onTap = [this] (int i) { showCategory (i); };
        grid.onTap = [this] (int i) { choose (i); };

        // Categorie: prima batteria e percussioni, poi l'elettronica, poi gli strumenti
        auto isDrum = [] (const juce::String& c) { return c.contains ("Batteria") || c.contains ("Percussioni") || c == "Drum machine"; };
        juce::StringArray drums, others;
        for (auto& en : LibraryIndex::entries)
        {
            const juce::String c (juce::CharPointer_UTF8 (en.category));
            auto& list = isDrum (c) ? drums : others;
            if (! list.contains (c)) list.add (c);
        }
        categories.addArray (drums);
        categories.addArray (others);
        categories.add (userCategory);
    }

    void openFor (int padIndex)
    {
        pad = padIndex;
        // Parte dalla categoria del suono attuale del pad
        int start = 0;
        if (auto* en = SampleStore::findLibraryEntry (editor.proc.getPadSample (pad)); en != nullptr && SampleStore::isLibrary (editor.proc.getPadSample (pad)))
            start = juce::jmax (0, categories.indexOf (juce::String (juce::CharPointer_UTF8 (en->category))));
        else if (editor.proc.getPadSample (pad).isNotEmpty())
            start = categories.size() - 1;
        std::vector<TapGrid::Item> list;
        for (auto& c : categories)
            list.push_back ({ c, c == userCategory ? dfAccent : col::textDim });
        cats.setItems (list, start);
        showCategory (start);
        setVisible (true);
        toFront (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawFittedText ("Suono del pad " + juce::String (pad + 1) + ":  " + editor.proc.getPadName (pad),
                          titleArea, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (36);
        doneButton.setBounds (top.removeFromRight (90).reduced (0, 3));
        top.removeFromRight (6);
        importButton.setBounds (top.removeFromRight (juce::jmin (150, top.getWidth() / 3)).reduced (0, 3));
        titleArea = top;
        r.removeFromTop (6);

        const bool narrow = r.getWidth() < 520;
        auto left = r.removeFromLeft (juce::jlimit (110, 190, (int) (r.getWidth() * (narrow ? 0.36f : 0.25f))));
        r.removeFromLeft (8);
        catView.setBounds (left);
        cats.setLayout (1, 40, left.getWidth() - 7);
        itemView.setBounds (r);
        grid.setLayout (juce::jmax (1, (r.getWidth() - 7) / (narrow ? 160 : 170)), 46, r.getWidth() - 7);
    }

private:
    void showCategory (int index)
    {
        cats.setSelected (index);
        shownNames.clear();
        std::vector<TapGrid::Item> list;
        const auto current = editor.proc.getPadSample (pad);
        int sel = -1;
        if (categories[index] == userCategory)
        {
            for (auto& f : editor.proc.getSampleStore().listSamples())
            {
                if (f == current) sel = (int) list.size();
                shownNames.add (f);
                list.push_back ({ juce::File (f).getFileNameWithoutExtension(), juce::Colour (0xffc49360) });
            }
            if (list.empty())
                list.push_back ({ "Nessun campione: tocca \"Importa un file\"", col::textDim });
        }
        else
            for (auto& en : LibraryIndex::entries)
                if (juce::String (juce::CharPointer_UTF8 (en.category)) == categories[index])
                {
                    const auto name = SampleStore::libraryName (en.id);
                    if (name == current) sel = (int) list.size();
                    shownNames.add (name);
                    list.push_back ({ juce::String (juce::CharPointer_UTF8 (en.name)), juce::Colour (en.colour) });
                }
        grid.setItems (list, sel);
        itemView.setViewPosition (0, 0);
    }

    void choose (int i)
    {
        if (i < 0 || i >= shownNames.size())
            return;
        if (editor.proc.setPadSample (pad, shownNames[i]))
        {
            grid.setSelected (i);
            editor.proc.hitPad (pad);
            editor.refreshViews();
            repaint();
        }
    }

    DrumForgeEditor& editor;
    int pad = 0;
    juce::StringArray categories, shownNames;
    inline static const juce::String userCategory { "Campioni importati" };
    juce::Viewport catView, itemView;
    TapGrid cats, grid;
    juce::TextButton doneButton { "Fatto" }, importButton { "Importa un file..." };
    juce::Rectangle<int> titleArea;
};

void DrumForgeEditor::showSampleMenu (int pad, juce::Component&)
{
    if (picker == nullptr)
    {
        picker = std::make_unique<SamplePicker> (*this);
        addChildComponent (*picker);
    }
    picker->setBounds (getLocalBounds());
    picker->openFor (pad);
}

void DrumForgeEditor::resized()
{
    ForgeEditorBase::resized();
    if (picker != nullptr)
        picker->setBounds (getLocalBounds());
}

void DrumForgeEditor::importSample (int pad)
{
    chooser = std::make_unique<juce::FileChooser> ("Scegli un campione", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<DrumForgeEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, pad] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getURLResults().isEmpty())
            return;
        juce::String error;
        const auto name = safe->proc.getSampleStore().importFromUrl (fc.getURLResults()[0], error);
        if (name.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "DrumForge", error);
            return;
        }
        safe->proc.setPadSample (pad, name);
        safe->proc.hitPad (pad);
        safe->refreshViews();
        if (safe->picker != nullptr && safe->picker->isVisible())
            safe->picker->openFor (pad);
    });
}
