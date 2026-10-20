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

DrumForgeEditor::~DrumForgeEditor() = default;

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

void DrumForgeEditor::showSampleMenu (int pad, juce::Component& target)
{
    // Libreria: Strumenti / Elettronica -> categorie -> suoni
    juce::PopupMenu menu;
    std::map<juce::String, std::map<juce::String, juce::PopupMenu>> groups;
    std::vector<juce::String> groupOrder;
    std::map<juce::String, std::vector<juce::String>> categoryOrder;
    const auto current = proc.getPadSample (pad);

    int index = 0;
    for (auto& e : LibraryIndex::entries)
    {
        ++index;
        const juce::String group (juce::CharPointer_UTF8 (e.group)), category (juce::CharPointer_UTF8 (e.category));
        if (std::find (groupOrder.begin(), groupOrder.end(), group) == groupOrder.end())
            groupOrder.push_back (group);
        auto& cats = categoryOrder[group];
        if (std::find (cats.begin(), cats.end(), category) == cats.end())
            cats.push_back (category);
        groups[group][category].addItem (index, juce::String (juce::CharPointer_UTF8 (e.name)), true,
                                         current == SampleStore::libraryName (e.id));
    }

    // Prima le categorie di batteria (le piu' usate per un pad)
    auto isDrumCategory = [] (const juce::String& c)
    {
        return c.contains ("Batteria") || c.contains ("Percussioni") || c == "Drum machine";
    };
    juce::PopupMenu drums;
    for (auto& g : groupOrder)
        for (auto& c : categoryOrder[g])
            if (isDrumCategory (c))
                drums.addSubMenu (c, groups[g][c]);
    menu.addSectionHeader ("Batteria");
    menu.addSubMenu ("Batteria e percussioni", drums);

    menu.addSectionHeader ("Libreria");
    for (auto& g : groupOrder)
    {
        juce::PopupMenu sub;
        for (auto& c : categoryOrder[g])
            if (! isDrumCategory (c))
                sub.addSubMenu (c, groups[g][c]);
        menu.addSubMenu (g, sub);
    }

    menu.addSectionHeader ("I miei campioni");
    const auto userSamples = proc.getSampleStore().listSamples();
    juce::PopupMenu mine;
    for (int i = 0; i < userSamples.size(); ++i)
        mine.addItem (5000 + i, juce::File (userSamples[i]).getFileNameWithoutExtension(), true, current == userSamples[i]);
    if (userSamples.isEmpty())
        mine.addItem (4999, "(nessun campione importato)", false);
    menu.addSubMenu ("Campioni importati", mine);
    menu.addItem (9000, juce::String (juce::CharPointer_UTF8 ("Importa un file audio\xe2\x80\xa6")));

    juce::Component::SafePointer<DrumForgeEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safe, pad, userSamples] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;
        auto& self = *safe;

        if (result == 9000)
        {
            self.importSample (pad);
            return;
        }

        juce::String name;
        if (result >= 5000)
            name = userSamples[result - 5000];
        else if (result <= (int) std::size (LibraryIndex::entries))
            name = SampleStore::libraryName (LibraryIndex::entries[result - 1].id);

        if (name.isNotEmpty() && self.proc.setPadSample (pad, name))
        {
            self.proc.hitPad (pad);
            self.refreshViews();
        }
    });
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
    });
}
