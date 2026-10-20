#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "../Instruments/Instruments.h"
#include "SoundBrowser.h"   // TileList
#include "Theme.h"

//==============================================================================
/**
    Scelta dei preset di uno strumento (BassForge, PolyForge, FMForge, kit di DrumForge), a tutto schermo:
    a sinistra le categorie, a destra i preset come piastrelle grandi da toccare.

    - Un tocco carica il preset e lo fa sentire subito (si possono provare uno dopo l'altro)
    - "Fatto" tiene quello scelto, "Annulla" torna al suono di prima
    - In fondo: "Salva come mio preset"
*/
class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser (SequencerEngine& e) : engine (e)
    {
        for (auto* b : { &doneButton, &cancelButton, &saveButton, &editButton })
            addAndMakeVisible (b);

        doneButton.onClick   = [this] { finish (true); };
        cancelButton.onClick = [this] { finish (false); };
        saveButton.onClick   = [this] { if (onSaveRequest) onSaveRequest (channel); };
        editButton.onClick   = [this] { finish (true); if (onOpenInstrument) onOpenInstrument (channel); };

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
        tileList.onTap     = [this] (int i) { choose (i); };
    }

    std::function<void()> onClose, onChanged;                 // onChanged: un preset e' stato caricato
    std::function<void (int channel)> onSaveRequest, onOpenInstrument;

    /** Apre il browser per lo strumento del canale (lo stato attuale si ricorda per "Annulla"). */
    void openFor (int channelIndex)
    {
        channel = channelIndex;
        auto* proc = engine.getInstrument (channel);
        if (proc == nullptr)
            return;

        savedState = Instruments::getState (proc);
        const bool drums = dynamic_cast<DrumForgeAudioProcessor*> (proc) != nullptr;
        editButton.setButtonText (drums ? "Apri i pad..." : "Apri il synth...");
        saveButton.setButtonText (drums ? "Salva come mio kit..." : "Salva come mio preset...");
        rebuild();
        repaint();
    }

    int getChannel() const      { return channel; }

    /** Dopo un salvataggio: ricarica la lista (il nuovo preset compare tra i "Miei preset"). */
    void rebuild()
    {
        auto* proc = engine.getInstrument (channel);
        entries.clear();
        if (proc == nullptr)
            return;

        const auto factory = Instruments::factoryPresets (proc);
        for (int i = 0; i < factory.size(); ++i)
            entries.push_back ({ factory[i], categoryFor (proc, factory[i]), i, {} });
        for (auto& f : Instruments::userPresets (proc))
            entries.push_back ({ f.getFileNameWithoutExtension(), myCategory, -1, f });

        categories.clear();
        categories.add ("Tutti");
        for (auto& en : entries)
            if (en.category != myCategory && ! categories.contains (en.category))
                categories.add (en.category);
        categories.add (myCategory);

        std::vector<TileList::Item> cats;
        for (auto& c : categories)
            cats.push_back ({ c, {}, c == myCategory ? Theme::accent : Theme::textDim, true });
        categoryList.setItems (cats, -1);

        // Parte dalla categoria del preset attuale
        const auto current = Instruments::presetName (proc);
        int start = 0;
        for (auto& en : entries)
            if (en.name == current && en.category != "Base")
                start = juce::jmax (0, categories.indexOf (en.category));
        showCategory (start);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), Theme::corner, 1.0f);

        auto title = titleArea;
        const auto colour = channel >= 0 ? juce::Colour (engine.getChannel (channel).colour.load()) : Theme::accent;
        g.setColour (colour);
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 6.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (17.0f, true));
        const auto* proc = channel >= 0 ? engine.getInstrument (channel) : nullptr;
        const bool drums = dynamic_cast<const DrumForgeAudioProcessor*> (proc) != nullptr;
        g.drawFittedText ((drums ? "Kit di \"" : "Preset di \"") + (channel >= 0 ? engine.getChannel (channel).name : juce::String()) + "\"",
                          title.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (Theme::text);
        g.setFont (Theme::font (13.0f, true));
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaa  ")) + Instruments::presetName (engine.getInstrument (channel))
                              + juce::String ("   -   tocca per ascoltare"),
                          footerText.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto top = r.removeFromTop (36);
        cancelButton.setBounds (top.removeFromRight (90).reduced (0, 3));
        top.removeFromRight (6);
        doneButton.setBounds (top.removeFromRight (90).reduced (0, 3));
        titleArea = top.toFloat();
        r.removeFromTop (6);

        const bool narrow = r.getWidth() < 520;
        auto footer = r.removeFromBottom (narrow ? 84 : 40);
        if (narrow)
        {
            footerText = footer.removeFromTop (40).toFloat();
            saveButton.setBounds (footer.removeFromLeft (footer.getWidth() / 2).reduced (0, 4).withTrimmedRight (4));
            editButton.setBounds (footer.reduced (0, 4).withTrimmedLeft (4));
        }
        else
        {
            editButton.setBounds (footer.removeFromRight (150).reduced (0, 3));
            footer.removeFromRight (6);
            saveButton.setBounds (footer.removeFromRight (190).reduced (0, 3));
            footerText = footer.toFloat().withTrimmedRight (8.0f);
        }
        r.removeFromBottom (4);

        auto left = r.removeFromLeft (juce::jlimit (110, 190, (int) (r.getWidth() * (narrow ? 0.36f : 0.24f))));
        r.removeFromLeft (8);
        categoryView.setBounds (left);
        categoryList.setLayout (1, narrow ? 44 : 40);
        categoryList.setVisibleWidth (left.getWidth() - 8);

        tileView.setBounds (r);
        tileList.setLayout (juce::jmax (1, (r.getWidth() - 8) / (narrow ? 180 : 190)), 54);
        tileList.setVisibleWidth (r.getWidth() - 8);
    }

    /** Solo per i test: sceglie una categoria. */
    void showCategoryForTest (int i)    { showCategory (i); }

private:
    struct Entry
    {
        juce::String name, category;
        int factoryIndex = -1;
        juce::File file;
    };

    inline static const juce::String myCategory { "I miei preset" };

    /** La categoria di un preset: "Genere - Nome" (BassForge), oppure dalle parole nel nome. */
    static juce::String categoryFor (juce::AudioProcessor* proc, const juce::String& name)
    {
        if (dynamic_cast<DrumForgeAudioProcessor*> (proc) != nullptr)
        {
            const auto n = name.toLowerCase();
            if (n.contains ("acustic") || n.contains ("percuss")) return "Acustici";
            if (n == "808" || n == "909" || n.contains ("lo-fi")) return "Drum machine";
            return "Generi";
        }
        if (name.contains (" - "))
            return name.upToFirstOccurrenceOf (" - ", false, false).trim();

        const auto n = name.toLowerCase();
        auto has = [&n] (std::initializer_list<const char*> words)
        {
            for (auto* w : words) if (n.contains (w)) return true;
            return false;
        };
        if (n == "init")                                                         return "Base";
        if (has ({ "pad" }))                                                     return "Pad";
        if (has ({ "lead" }))                                                    return "Lead";
        if (has ({ "bass", "basso", "sub", "reese", "wobble", "growl" }))       return "Bassi";
        if (has ({ "pluck", "arpeggio", "arp", "mallet" }))                     return "Pluck e arpeggi";
        if (has ({ "piano", "organ", "clav", "rhodes", "wurli", "ep " }))      return "Tastiere";
        if (has ({ "campan", "bell", "glock", "marimba", "vibra", "kalimba", "steel", "metall" })) return "Campane e mallet";
        if (has ({ "brass", "stab", "accordi", "chord", "poly", "organo" }))   return "Stab e accordi";
        return "Altro";
    }

    void showCategory (int index)
    {
        index = juce::jlimit (0, juce::jmax (0, categories.size() - 1), index);
        shownCategory = index;
        categoryList.setSelected (index);

        shown.clear();
        std::vector<TileList::Item> tiles;
        const auto cat = categories[index];
        const auto current = Instruments::presetName (engine.getInstrument (channel));
        int selected = -1;
        for (int i = 0; i < (int) entries.size(); ++i)
        {
            const auto& en = entries[(size_t) i];
            if (cat != "Tutti" && en.category != cat)
                continue;
            if (en.name == current)
                selected = (int) shown.size();
            shown.push_back (i);
            const auto colour = en.factoryIndex < 0 ? Theme::accent : juce::Colour (engine.getChannel (channel).colour.load());
            tiles.push_back ({ en.name.fromFirstOccurrenceOf (" - ", false, false).isNotEmpty() ? en.name.fromFirstOccurrenceOf (" - ", false, false) : en.name,
                               cat == "Tutti" ? en.category : juce::String(), colour, true });
        }
        if (tiles.empty())
            tiles.push_back ({ cat == myCategory ? juce::String ("Nessun preset salvato: usa \"Salva come mio preset\"") : juce::String ("(vuoto)"),
                               {}, Theme::textDim, false });

        tileList.setItems (tiles, selected);
        tileView.setViewPosition (0, selected >= 0 ? juce::jmax (0, tileList.getItemBounds (selected).getY() - 40) : 0);
        repaint();
    }

    void choose (int tile)
    {
        if (tile < 0 || tile >= (int) shown.size())
            return;
        const auto& en = entries[(size_t) shown[(size_t) tile]];
        auto* proc = engine.getInstrument (channel);
        if (proc == nullptr)
            return;

        if (en.factoryIndex >= 0) Instruments::loadFactory (proc, en.factoryIndex);
        else                      Instruments::loadUser (proc, en.file);

        tileList.setSelected (tile);
        // Anteprima: una nota adatta (bassi in basso, il resto al centro; i kit con la cassa)
        const bool drums = dynamic_cast<DrumForgeAudioProcessor*> (proc) != nullptr;
        const bool bass = dynamic_cast<BassForgeAudioProcessor*> (proc) != nullptr || en.category == "Bassi";
        engine.previewChannel (channel, drums ? df::firstNote : bass ? 36 : 60);
        if (drums)
            if (auto* d = dynamic_cast<DrumForgeAudioProcessor*> (proc))
                d->hitPad (2, 0.8f);   // anche il rullante, per sentire il carattere del kit
        if (onChanged) onChanged();
        repaint();
    }

    void finish (bool keep)
    {
        if (! keep && savedState.isNotEmpty())
            if (auto* proc = engine.getInstrument (channel))
            {
                Instruments::setState (proc, savedState);
                if (onChanged) onChanged();
            }
        savedState.clear();
        if (onClose) onClose();
    }

    SequencerEngine& engine;
    int channel = -1, shownCategory = 0;
    juce::String savedState;
    std::vector<Entry> entries;
    std::vector<int> shown;
    juce::StringArray categories;

    juce::Viewport categoryView, tileView;
    TileList categoryList, tileList;
    juce::TextButton doneButton { "Fatto" }, cancelButton { "Annulla" }, saveButton, editButton;
    juce::Rectangle<float> titleArea, footerText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
