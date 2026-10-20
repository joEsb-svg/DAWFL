#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "../Engine/SequencerEngine.h"
#include "../Engine/SampleStore.h"
#include "../Instruments/Instruments.h"
#include "Theme.h"
#include "Widgets.h"

//==============================================================================
/** Il suono scelto nel browser. */
struct SoundChoice
{
    enum Kind { none, library, userSample, importFile, drumSynth, instrument, drumForgeKit, kitChannels };

    Kind kind = none;
    juce::String id;            // libreria: id; campione: nome del file
    juce::String name;          // nome da dare al canale
    juce::uint32 colour = 0xff8aa0b8;
    int drumType = 0, kit = 0;
};

//==============================================================================
/**
    Una lista di "piastrelle" da toccare, dentro un Viewport che scorre col dito.
    Il tocco conta solo se il dito non si e' mosso (altrimenti era uno scorrimento).
*/
class TileList : public juce::Component
{
public:
    struct Item
    {
        juce::String text, detail;
        juce::Colour colour;
        bool enabled = true;
    };

    std::function<void (int index)> onTap;

    void setItems (std::vector<Item> newItems, int newSelected)
    {
        items = std::move (newItems);
        selected = newSelected;
        updateSize();
        repaint();
    }

    void setSelected (int index)        { selected = index; repaint(); }
    int getSelected() const             { return selected; }
    const std::vector<Item>& getItems() const   { return items; }

    void setLayout (int numColumns, int rowHeight)
    {
        columns = juce::jmax (1, numColumns);
        itemHeight = rowHeight;
        updateSize();
    }

    /** Larghezza visibile (quella del Viewport): l'altezza cresce con il numero di voci. */
    void setVisibleWidth (int w)        { width = w; updateSize(); }

    juce::Rectangle<int> getItemBounds (int i) const
    {
        const int gap = 5;
        const int w = (width - gap * (columns - 1)) / columns;
        return { (i % columns) * (w + gap), (i / columns) * (itemHeight + gap), w, itemHeight };
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) items.size(); ++i)
        {
            auto r = getItemBounds (i).toFloat();
            if (! g.clipRegionIntersects (r.toNearestInt()))
                continue;

            const auto& it = items[(size_t) i];
            const bool sel = i == selected;
            Theme::drawRaised (g, r, sel ? Theme::raisedHi : Theme::raised);

            if (sel)
            {
                g.setColour (Theme::accent);
                g.drawRoundedRectangle (r.reduced (1.0f), Theme::corner, 1.6f);
            }

            g.setColour (it.colour.withMultipliedAlpha (it.enabled ? 0.95f : 0.3f));
            g.fillRect (r.withWidth (3.0f).reduced (0.0f, 5.0f).translated (4.0f, 0.0f));

            auto text = r.withTrimmedLeft (13.0f).reduced (2.0f, 0.0f);
            const bool twoLines = it.detail.isNotEmpty() && r.getHeight() >= 38.0f;
            g.setColour (it.enabled ? (sel ? Theme::textBright : Theme::text) : Theme::textDim.withAlpha (0.6f));
            g.setFont (Theme::font (juce::jmin (14.0f, r.getHeight() * 0.36f), true));

            if (twoLines)
            {
                auto top = text.removeFromTop (text.getHeight() * 0.56f);
                g.drawFittedText (it.text, top.toNearestInt(), juce::Justification::bottomLeft, 1);
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (11.0f));
                g.drawFittedText (it.detail, text.toNearestInt(), juce::Justification::topLeft, 1);
            }
            else
            {
                g.drawFittedText (it.text, text.toNearestInt(), juce::Justification::centredLeft, 1);
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 10 || e.getLengthOfMousePress() > 800)
            return;

        for (int i = 0; i < (int) items.size(); ++i)
            if (getItemBounds (i).contains (e.getMouseDownPosition()))
            {
                if (items[(size_t) i].enabled && onTap)
                    onTap (i);
                return;
            }
    }

private:
    void updateSize()
    {
        const int rows = ((int) items.size() + columns - 1) / columns;
        setSize (width, juce::jmax (1, rows * (itemHeight + 5)));
    }

    std::vector<Item> items;
    int selected = -1, columns = 1, itemHeight = 44, width = 100;
};

//==============================================================================
/**
    Il browser dei suoni: si apre sopra il channel rack per aggiungere uno strumento
    o cambiare il suono di un canale.

    - Strumenti:   pianoforti, chitarre, bassi, archi, fiati, voci, percussioni, batteria acustica...
    - Elettronica: EDM, House, Techno, Electro, Drum and Bass, Dubstep, Trap, synth, effetti
    - Sintesi:     i synth (BassForge, PolyForge) e la batteria sintetizzata di JLoop (kit per genere)
    - Campioni:    i file importati dall'utente

    Tocca un suono per ascoltarlo; "Aggiungi" (o "Usa questo suono") per confermare.
*/
class SoundBrowser : public juce::Component
{
public:
    SoundBrowser (SequencerEngine& e, SampleStore& store) : engine (e), samples (store)
    {
        for (auto* b : { &closeButton, &confirmButton })
            addAndMakeVisible (b);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        confirmButton.onClick = [this] { confirm(); };

        tabs.onChange = [this] (int) { showTab (tabs.getSelected(), 0); };
        addAndMakeVisible (tabs);

        for (auto* vp : { &categoryView, &tileView })
        {
            vp->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
            vp->setScrollBarsShown (true, false);
            vp->setScrollBarThickness (6);
            addAndMakeVisible (vp);
        }

        categoryView.setViewedComponent (&categoryList, false);
        tileView.setViewedComponent (&tileList, false);
        categoryList.onTap = [this] (int i) { showCategory (i); };
        tileList.onTap     = [this] (int i) { select (i); };

        buildLibraryCategories();
    }

    std::function<void (int channel, const SoundChoice&)> onChoose;
    std::function<void()> onClose;
    std::function<void()> onTap;          // per la vibrazione

    /** Apre il browser: channel = canale di cui cambiare suono, -1 = aggiungere un canale nuovo. */
    void openFor (int channel)
    {
        targetChannel = channel;
        chosen = {};
        confirmButton.setButtonText (channel < 0 ? "Aggiungi" : "Usa questo suono");

        // Parte dalla categoria del suono attuale del canale, se si sa qual e'
        int tab = lastTab, category = lastCategory[(size_t) lastTab];
        if (channel >= 0)
        {
            auto& ch = engine.getChannel (channel);
            if (engine.isSynth (channel))
            {
                tab = 2; category = 0;
            }
            else if (auto* entry = SampleStore::findLibraryEntry (ch.sampleName); entry != nullptr && SampleStore::isLibrary (ch.sampleName))
            {
                tab = juce::String (entry->group) == "Strumenti" ? 0 : 1;
                category = juce::jmax (0, groups[(size_t) tab].indexOf (juce::String (juce::CharPointer_UTF8 (entry->category))));
            }
            else if (ch.sample.load() != nullptr)
            {
                tab = 3; category = 0;
            }
            else
            {
                tab = 2; category = engine.getChannelKit (channel) + firstDrumSynthCategory;
            }
        }

        tabs.setSelected (tab);
        showTab (tab, category);
        repaint();
    }

    int getTargetChannel() const    { return targetChannel; }

    /** Solo per i test: apre una scheda e una categoria. */
    void showTabForTest (int tab, int category)    { tabs.setSelected (tab); showTab (tab, category); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), Theme::corner, 1.0f);

        auto title = titleArea;
        g.setColour (Theme::accent);
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 6.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (17.0f, true));
        const auto heading = targetChannel < 0 ? juce::String ("Aggiungi strumento")
                                               : "Suono di \"" + engine.getChannel (targetChannel).name + "\"";
        g.drawFittedText (heading, title.toNearestInt(), juce::Justification::centredLeft, 1);

        // In basso: il suono selezionato
        g.setColour (chosen.kind == SoundChoice::none ? Theme::textDim : Theme::text);
        g.setFont (Theme::font (13.0f, chosen.kind != SoundChoice::none));
        g.drawFittedText (chosen.kind == SoundChoice::none ? juce::String ("Tocca un suono per ascoltarlo")
                                                           : juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaa  ")) + chosen.name,
                          footerText.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto top = r.removeFromTop (34);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        titleArea = top.toFloat();

        r.removeFromTop (4);
        tabs.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);

        auto footer = r.removeFromBottom (40);
        confirmButton.setBounds (footer.removeFromRight (juce::jmin (170, footer.getWidth() / 2)).reduced (0, 3));
        footerText = footer.toFloat().withTrimmedRight (8.0f);
        r.removeFromBottom (4);

        const bool narrow = r.getWidth() < 520;
        auto left = r.removeFromLeft (juce::jlimit (110, 190, (int) (r.getWidth() * (narrow ? 0.36f : 0.26f))));
        r.removeFromLeft (8);

        categoryView.setBounds (left);
        categoryList.setLayout (1, narrow ? 40 : 36);
        categoryList.setVisibleWidth (left.getWidth() - 8);

        tileView.setBounds (r);
        const int columns = juce::jmax (1, (r.getWidth() - 8) / (narrow ? 200 : 170));
        tileList.setLayout (columns, 46);
        tileList.setVisibleWidth (r.getWidth() - 8);
    }

private:
    //==============================================================================
    void buildLibraryCategories()
    {
        for (auto& e : LibraryIndex::entries)
        {
            const int g = juce::String (e.group) == "Strumenti" ? 0 : 1;
            const auto cat = juce::String (juce::CharPointer_UTF8 (e.category));
            if (! groups[(size_t) g].contains (cat))
                groups[(size_t) g].add (cat);
        }
    }

    void showTab (int tab, int category)
    {
        lastTab = tab;
        std::vector<TileList::Item> cats;

        if (tab <= 1)
        {
            for (auto& c : groups[(size_t) tab])
                cats.push_back ({ c, {}, Theme::textDim, true });
        }
        else if (tab == 2)
        {
            cats.push_back ({ "Synth", {}, juce::Colour (0xffb07cff), true });
            cats.push_back ({ "Kit DrumForge", {}, juce::Colour (0xffff6b5b), true });
            cats.push_back ({ "Kit su canali separati", {}, juce::Colour (0xffff6b5b), true });
            for (int k = 0; k < SoundLibrary::numKits; ++k)
                cats.push_back ({ juce::String ("Batteria ") + SoundLibrary::getKitName (k), {}, Theme::textDim, true });
        }
        else
        {
            cats.push_back ({ "Campioni importati", {}, Theme::textDim, true });
        }

        categoryList.setItems (cats, -1);
        showCategory (juce::jlimit (0, (int) cats.size() - 1, category));
    }

    void showCategory (int index)
    {
        const int tab = tabs.getSelected();
        lastCategory[(size_t) tab] = index;
        categoryList.setSelected (index);
        choices.clear();
        std::vector<TileList::Item> tiles;

        if (tab <= 1)
        {
            const auto cat = groups[(size_t) tab][index];
            for (auto& e : LibraryIndex::entries)
            {
                const auto entryCat = juce::String (juce::CharPointer_UTF8 (e.category));
                const int g = juce::String (e.group) == "Strumenti" ? 0 : 1;
                if (g != tab || entryCat != cat)
                    continue;

                SoundChoice c;
                c.kind = SoundChoice::library;
                c.id = e.id;
                c.name = juce::String (juce::CharPointer_UTF8 (e.name));
                c.colour = e.colour;
                choices.push_back (c);
                tiles.push_back ({ c.name, {}, juce::Colour (e.colour), true });
            }
        }
        else if (tab == 2 && (index == 1 || index == 2))
        {
            // I kit di DrumForge: in un solo canale (la drum machine) o un canale per suono
            for (int k = 0; k < df::Presets::numFactory(); ++k)
            {
                SoundChoice c;
                c.kind = index == 1 ? SoundChoice::drumForgeKit : SoundChoice::kitChannels;
                c.id = "drumforge";
                c.kit = k;
                c.name = "Kit " + df::Presets::factoryName (k);
                c.colour = 0xffff6b5b;
                choices.push_back (c);
                tiles.push_back ({ c.name, index == 1 ? juce::String ("16 pad in DrumForge") : juce::String (kitChannelPads.size()) + " canali",
                                   juce::Colour (c.colour), true });
            }
        }
        else if (tab == 2 && index > 0)
        {
            index -= firstDrumSynthCategory;   // 0 = Synth, 1-2 = kit di DrumForge, poi i kit della batteria sintetizzata
            for (int t = 0; t < SequencerEngine::numDrumTypes; ++t)
            {
                SoundChoice c;
                c.kind = SoundChoice::drumSynth;
                c.drumType = t;
                c.kit = index;
                c.name = SequencerEngine::getDrumTypeInfo (t).name;
                c.colour = SequencerEngine::getDrumTypeInfo (t).colour;
                choices.push_back (c);
                tiles.push_back ({ c.name, SoundLibrary::getKitName (index), juce::Colour (c.colour), true });
            }
        }
        else if (tab == 2)
        {
            for (auto& info : Instruments::list())
            {
                SoundChoice c;
                c.kind = SoundChoice::instrument;
                c.id = info.id;
                c.name = info.name;
                c.colour = info.colour;
                choices.push_back (c);
                tiles.push_back ({ info.name, juce::String (juce::CharPointer_UTF8 (info.detail)), juce::Colour (info.colour), true });
            }
        }
        else
        {
            SoundChoice imp;
            imp.kind = SoundChoice::importFile;
            imp.name = "Importa un file audio...";
            choices.push_back (imp);
            tiles.push_back ({ "+ Importa...", "WAV, AIFF, FLAC, OGG, MP3", Theme::accent, true });

            for (auto& f : samples.listSamples())
            {
                SoundChoice c;
                c.kind = SoundChoice::userSample;
                c.id = f;
                c.name = SampleStore::displayName (f);
                c.colour = 0xffc49360;
                choices.push_back (c);
                tiles.push_back ({ c.name, {}, juce::Colour (c.colour), true });
            }
        }

        // Evidenzia il suono attuale del canale
        int current = -1;
        if (targetChannel >= 0)
            for (int i = 0; i < (int) choices.size(); ++i)
                if (isCurrentSound (choices[(size_t) i]))
                    current = i;

        tileList.setItems (tiles, current);
        tileView.setViewPosition (0, current >= 0 ? juce::jmax (0, tileList.getItemBounds (current).getY() - 40) : 0);
        chosen = {};
        confirmButton.setEnabled (false);
        repaint();
    }

    bool isCurrentSound (const SoundChoice& c) const
    {
        auto& ch = engine.getChannel (targetChannel);
        switch (c.kind)
        {
            case SoundChoice::library:    return ch.sampleName == SampleStore::libraryName (c.id);
            case SoundChoice::userSample: return ch.sampleName == c.id;
            case SoundChoice::drumSynth:  return engine.isDrumSynthChannel (targetChannel) && ! engine.isSynth (targetChannel)
                                                 && ch.drumType.load() == c.drumType && ch.kit.load() == c.kit;
            case SoundChoice::instrument: return engine.isSynth (targetChannel) && ch.instrumentKind == c.id;
            default: return false;
        }
    }

    void select (int index)
    {
        if (index < 0 || index >= (int) choices.size())
            return;

        tileList.setSelected (index);
        chosen = choices[(size_t) index];
        if (onTap) onTap();

        // Importa: niente anteprima, si sceglie subito il file
        if (chosen.kind == SoundChoice::importFile)
        {
            confirm();
            return;
        }

        preview (chosen);
        confirmButton.setEnabled (true);
        repaint();
    }

    void preview (const SoundChoice& c)
    {
        if (c.kind == SoundChoice::library)
        {
            if (auto* s = samples.load (SampleStore::libraryName (c.id)))
                engine.previewSample (s);
        }
        else if (c.kind == SoundChoice::userSample)
        {
            if (auto* s = samples.load (c.id))
                engine.previewSample (s);
        }
        else if (c.kind == SoundChoice::drumSynth)
        {
            engine.previewDrum (c.drumType, c.kit);
        }
        else if (c.kind == SoundChoice::drumForgeKit || c.kind == SoundChoice::kitChannels)
        {
            const auto& kit = df::factoryKits()[(size_t) juce::jlimit (0, df::Presets::numFactory() - 1, c.kit)];
            if (auto* s = samples.load (SampleStore::libraryName (kit.pads[0].sample)))
                engine.previewSample (s);
        }
        else if (c.kind == SoundChoice::instrument)
        {
            // Anteprima: se nel progetto c'e' gia' uno strumento di questo tipo, fa sentire quello
            for (int ch = 0; ch < engine.getNumChannels(); ++ch)
                if (engine.isSynth (ch) && engine.getChannel (ch).instrumentKind == c.id)
                {
                    engine.previewChannel (ch);
                    break;
                }
        }
    }

    void confirm()
    {
        if (chosen.kind == SoundChoice::none)
            return;

        const auto choice = chosen;
        const int channel = targetChannel;
        if (onChoose)
            onChoose (channel, choice);
    }

public:
    /** I pad di un kit che diventano canali ("Kit su canali separati"): cassa, rullante, clap, hat chiuso, hat aperto, rim, tom, crash. */
    static inline const std::array<int, 8> kitChannelPads { 0, 2, 3, 5, 7, 4, 9, 11 };

private:
    static constexpr int firstDrumSynthCategory = 3;
    SequencerEngine& engine;
    SampleStore& samples;
    int targetChannel = -1, lastTab = 0;
    std::array<int, 4> lastCategory {};
    std::array<juce::StringArray, 2> groups;      // categorie della libreria: Strumenti, Elettronica
    std::vector<SoundChoice> choices;
    SoundChoice chosen;

    SegmentedControl tabs { { "Strumenti", "Elettronica", "Sintesi", "Campioni" } };
    juce::Viewport categoryView, tileView;
    TileList categoryList, tileList;
    juce::TextButton closeButton { "Chiudi" }, confirmButton { "Aggiungi" };
    juce::Rectangle<float> titleArea, footerText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundBrowser)
};
