#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "../Engine/SequencerEngine.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PolyForge/PolyForge.h"
#include "FMForge/FMForge.h"
#include "DrumForge/DrumForge.h"
#include "FrenchRoulette/FrenchRoulette.h"

//==============================================================================
/**
    Gli strumenti (synth) che JLoop sa ospitare, e chi li possiede.

    Ogni canale "strumento" ha il suo AudioProcessor (si possono avere piu' BassForge e piu' PolyForge):
    il motore audio li usa tramite un puntatore, la UI (Host) li crea, ne salva lo stato e li distrugge.
*/
namespace Instruments
{
    struct Info
    {
        const char* id;
        const char* name;
        const char* detail;
        juce::uint32 colour;
    };

    inline const std::vector<Info>& list()
    {
        static const std::vector<Info> infos =
        {
            { "bassforge", "BassForge", "Bassi: wavetable, Performer, matrice", SequencerEngine::synthColour },
            { "polyforge", "PolyForge", "Polifonico: pad, lead, plucks, accordi", 0xffb07cff },
            { "fmforge",   "FMForge",   "FM: piani elettrici, campane, bassi FM", 0xff7dffb0 },
            { "drumforge", "DrumForge", "Drum machine: 16 pad, kit per genere", 0xffff6b5b },
            { "frenchroulette", "French Roulette", "Bassi choppati, lead distorti, step matrix", 0xffff3a3a },
        };
        return infos;
    }

    inline const Info* find (const juce::String& id)
    {
        for (auto& i : list())
            if (id == i.id)
                return &i;
        return nullptr;
    }

    /** La nota suonata dagli step del channel rack (DrumForge: il primo pad, la cassa). */
    inline int stepNoteFor (const juce::String& id)
    {
        return id == "drumforge" ? df::firstNote : SequencerEngine::synthStepNote;
    }

    inline std::unique_ptr<juce::AudioProcessor> create (const juce::String& id)
    {
        if (id == "polyforge") return std::make_unique<PolyForgeAudioProcessor>();
        if (id == "fmforge")   return std::make_unique<FMForgeAudioProcessor>();
        if (id == "drumforge") return std::make_unique<DrumForgeAudioProcessor>();
        if (id == "frenchroulette") return std::make_unique<FrenchRouletteAudioProcessor>();
        return std::make_unique<BassForgeAudioProcessor>();
    }

    //==========================================================================
    // Preset (ogni synth ha il suo sistema: qui un'interfaccia comune)

    inline juce::StringArray factoryPresets (juce::AudioProcessor* p)
    {
        juce::StringArray names;
        if (dynamic_cast<BassForgeAudioProcessor*> (p) != nullptr)
            for (int i = 0; i < bf::PresetManager::numFactory(); ++i) names.add (bf::PresetManager::factoryName (i));
        else if (dynamic_cast<PolyForgeAudioProcessor*> (p) != nullptr)
            for (int i = 0; i < pf::Presets::numFactory(); ++i) names.add (pf::Presets::factoryName (i));
        else if (dynamic_cast<FMForgeAudioProcessor*> (p) != nullptr)
            for (int i = 0; i < fm::Presets::numFactory(); ++i) names.add (fm::Presets::factoryName (i));
        else if (dynamic_cast<DrumForgeAudioProcessor*> (p) != nullptr)
            for (int i = 0; i < df::Presets::numFactory(); ++i) names.add (df::Presets::factoryName (i));
        else if (dynamic_cast<FrenchRouletteAudioProcessor*> (p) != nullptr)
            for (int i = 0; i < fr::Presets::numFactory(); ++i) names.add (fr::Presets::factoryName (i));
        return names;
    }

    inline void loadFactory (juce::AudioProcessor* p, int index)
    {
        if (auto* b = dynamic_cast<BassForgeAudioProcessor*> (p))  b->getPresets().loadFactory (index);
        if (auto* f = dynamic_cast<PolyForgeAudioProcessor*> (p))  f->getPresets().loadFactory (index);
        if (auto* m = dynamic_cast<FMForgeAudioProcessor*> (p))    m->getPresets().loadFactory (index);
        if (auto* d = dynamic_cast<DrumForgeAudioProcessor*> (p))  d->getPresets().loadFactory (index);
        if (auto* r = dynamic_cast<FrenchRouletteAudioProcessor*> (p))  r->getPresets().loadFactory (index);
    }

    inline juce::Array<juce::File> userPresets (juce::AudioProcessor* p)
    {
        if (auto* b = dynamic_cast<BassForgeAudioProcessor*> (p))  return b->getPresets().userPresets();
        if (auto* f = dynamic_cast<PolyForgeAudioProcessor*> (p))  return f->getPresets().userPresets();
        if (auto* m = dynamic_cast<FMForgeAudioProcessor*> (p))    return m->getPresets().userPresets();
        if (auto* d = dynamic_cast<DrumForgeAudioProcessor*> (p))  return d->getPresets().userPresets();
        if (auto* r = dynamic_cast<FrenchRouletteAudioProcessor*> (p))  return r->getPresets().userPresets();
        return {};
    }

    inline void loadUser (juce::AudioProcessor* p, const juce::File& file)
    {
        if (auto* b = dynamic_cast<BassForgeAudioProcessor*> (p))  b->getPresets().load (file);
        if (auto* f = dynamic_cast<PolyForgeAudioProcessor*> (p))  f->getPresets().load (file);
        if (auto* m = dynamic_cast<FMForgeAudioProcessor*> (p))    m->getPresets().load (file);
        if (auto* d = dynamic_cast<DrumForgeAudioProcessor*> (p))  d->getPresets().load (file);
        if (auto* r = dynamic_cast<FrenchRouletteAudioProcessor*> (p))  r->getPresets().load (file);
    }

    inline bool saveUser (juce::AudioProcessor* p, const juce::String& name)
    {
        if (auto* b = dynamic_cast<BassForgeAudioProcessor*> (p))  return b->getPresets().save (name);
        if (auto* f = dynamic_cast<PolyForgeAudioProcessor*> (p))  return f->getPresets().save (name);
        if (auto* m = dynamic_cast<FMForgeAudioProcessor*> (p))    return m->getPresets().save (name);
        if (auto* d = dynamic_cast<DrumForgeAudioProcessor*> (p))  return d->getPresets().save (name);
        if (auto* r = dynamic_cast<FrenchRouletteAudioProcessor*> (p))  return r->getPresets().save (name);
        return false;
    }

    inline juce::String presetName (juce::AudioProcessor* p)
    {
        if (auto* b = dynamic_cast<BassForgeAudioProcessor*> (p))  return b->getPresets().currentName();
        if (auto* f = dynamic_cast<PolyForgeAudioProcessor*> (p))  return f->getPresets().currentName();
        if (auto* m = dynamic_cast<FMForgeAudioProcessor*> (p))    return m->getPresets().currentName();
        if (auto* d = dynamic_cast<DrumForgeAudioProcessor*> (p))  return d->getPresets().currentName();
        if (auto* r = dynamic_cast<FrenchRouletteAudioProcessor*> (p))  return r->getPresets().currentName();
        return "Synth";
    }

    /** Adatta l'interfaccia dello strumento al telefono. */
    inline void prepareEditor (juce::AudioProcessorEditor* editor)
    {
        if (auto* bf = dynamic_cast<BassForgeAudioProcessorEditor*> (editor))
        {
            bf->setCompactLayout (true);
            bf->setPresetBarVisible (false);   // i preset si scelgono dal pulsante del pannello
        }
    }

    inline juce::String getState (juce::AudioProcessor* p)
    {
        juce::MemoryBlock state;
        if (p != nullptr)
            p->getStateInformation (state);
        return state.toBase64Encoding();
    }

    inline void setState (juce::AudioProcessor* p, const juce::String& base64)
    {
        juce::MemoryBlock state;
        if (p != nullptr && base64.isNotEmpty() && state.fromBase64Encoding (base64))
            p->setStateInformation (state.getData(), (int) state.getSize());
    }

    //==========================================================================
    /**
        Possiede gli strumenti dei canali di un motore.
        resolve() va chiamato dopo ogni modifica ai canali (caricamento, undo, clona, elimina...):
        crea quelli che mancano, riusa quelli che ci sono (stesso id), applica gli stati salvati
        e distrugge quelli che nessun canale usa piu'.
    */
    class Host
    {
    public:
        explicit Host (SequencerEngine& e) : engine (e) {}

        ~Host()
        {
            for (int c = 0; c < SequencerEngine::maxChannels; ++c)
                engine.getChannel (c).instrument.store (nullptr);
            engine.waitForAudioBlock();
        }

        void resolve()
        {
            std::vector<int> usedIds;

            for (int c = 0; c < SequencerEngine::maxChannels; ++c)
            {
                auto& ch = engine.getChannel (c);
                const bool wanted = c < engine.getNumChannels() && ch.type.load() == SequencerEngine::synth;

                if (! wanted)
                {
                    if (ch.instrument.load() != nullptr)
                        engine.setInstrument (c, nullptr);
                    continue;
                }

                if (ch.instrumentKind.isEmpty())
                    ch.instrumentKind = "bassforge";

                juce::AudioProcessor* proc = nullptr;
                const bool duplicateId = ch.instrumentId > 0 && std::find (usedIds.begin(), usedIds.end(), ch.instrumentId) != usedIds.end();

                if (! duplicateId)
                    if (auto* e = findOwned (ch.instrumentId); e != nullptr && e->kind == ch.instrumentKind)
                        proc = e->processor.get();

                if (proc == nullptr)
                {
                    // Nuovo strumento; se e' una copia (clona) parte dallo stato dell'originale
                    juce::String copyState;
                    if (duplicateId)
                        if (auto* original = findOwned (ch.instrumentId))
                            copyState = getState (original->processor.get());

                    Entry entry;
                    entry.id = ++lastId;
                    entry.kind = ch.instrumentKind;
                    entry.processor = create (ch.instrumentKind);
                    proc = entry.processor.get();
                    setState (proc, copyState);
                    ch.instrumentId = entry.id;
                    owned.push_back (std::move (entry));
                }
                else
                {
                    lastId = juce::jmax (lastId, ch.instrumentId);
                }

                usedIds.push_back (ch.instrumentId);

                if (ch.pendingState.isNotEmpty())
                {
                    setState (proc, ch.pendingState);
                    ch.pendingState.clear();
                }

                ch.stepNote.store (stepNoteFor (ch.instrumentKind));
                if (ch.instrument.load() != proc)
                    engine.setInstrument (c, proc);
            }

            collectGarbage();
        }

        juce::AudioProcessor* get (int channel) const    { return engine.getInstrument (channel); }

        /** Lo strumento con questo id, anche se al momento nessun canale lo usa (undo). */
        juce::AudioProcessor* findById (int id) const
        {
            for (auto& e : owned)
                if (e.id == id)
                    return e.processor.get();
            return nullptr;
        }

    private:
        struct Entry
        {
            int id = 0;
            juce::String kind;
            std::unique_ptr<juce::AudioProcessor> processor;
        };

        Entry* findOwned (int id)
        {
            for (auto& e : owned)
                if (e.id == id && id > 0)
                    return &e;
            return nullptr;
        }

        void collectGarbage()
        {
            // Tiene da parte gli ultimi strumenti scollegati (servono all'undo), distrugge gli altri
            std::vector<int> inUse;
            for (int c = 0; c < SequencerEngine::maxChannels; ++c)
                if (engine.getChannel (c).instrument.load() != nullptr)
                    inUse.push_back (engine.getChannel (c).instrumentId);

            std::vector<size_t> unused;
            for (size_t i = 0; i < owned.size(); ++i)
                if (std::find (inUse.begin(), inUse.end(), owned[i].id) == inUse.end())
                    unused.push_back (i);

            if ((int) unused.size() <= keepUnused)
                return;

            engine.waitForAudioBlock();   // il thread audio non li sta usando
            const size_t toRemove = unused.size() - (size_t) keepUnused;
            for (size_t k = 0; k < toRemove; ++k)
                owned.erase (owned.begin() + (long) (unused[k] - k));   // i piu' vecchi per primi
        }

        static constexpr int keepUnused = 4;
        SequencerEngine& engine;
        std::vector<Entry> owned;
        int lastId = 0;
    };
}
