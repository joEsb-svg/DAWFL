#pragma once

#include <JuceHeader.h>
#include "SequencerEngine.h"

//==============================================================================
/**
    Salvataggio e caricamento dei progetti in formato JSON (leggibile anche a occhio).

    I progetti stanno nella cartella privata dell'app:  .../DAWFL/Projects/<nome>.dawfl
    L'ultimo stato viene salvato anche automaticamente in  .../DAWFL/autosave.dawfl
*/
namespace ProjectIO
{
    static constexpr int formatVersion = 2;
    static constexpr const char* extension = ".dawfl";

    inline juce::File getRootFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DAWFL");
    }

    inline juce::File getProjectsFolder()   { return getRootFolder().getChildFile ("Projects"); }
    inline juce::File getAutosaveFile()     { return getRootFolder().getChildFile (juce::String ("autosave") + extension); }

    inline juce::File getProjectFile (const juce::String& name)
    {
        return getProjectsFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + extension);
    }

    /** Nomi dei progetti salvati, dal piu' recente. */
    inline juce::StringArray listProjects()
    {
        auto files = getProjectsFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);

        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

        juce::StringArray names;
        for (auto& f : files)
            names.add (f.getFileNameWithoutExtension());
        return names;
    }

    //==============================================================================
    /** Una riga di step come stringa compatta: 2 cifre esadecimali per step ("00" = spento). */
    inline juce::String encodeRow (const SequencerEngine::Pattern::Row& row)
    {
        juce::String s;
        for (int i = 0; i < SequencerEngine::maxSteps; ++i)
            s << juce::String::toHexString ((int) row[(size_t) i].load()).paddedLeft ('0', 2);
        return s;
    }

    inline void decodeRow (SequencerEngine::Pattern::Row& row, const juce::String& s, int minValue, int maxValue)
    {
        for (int i = 0; i < SequencerEngine::maxSteps && i * 2 + 2 <= s.length(); ++i)
        {
            const int v = s.substring (i * 2, i * 2 + 2).getHexValue32();
            row[(size_t) i].store ((uint8_t) juce::jlimit (minValue, maxValue, v));
        }
    }

    //==============================================================================
    /** @param includeSynthState  false per le istantanee dell'undo (lo stato del synth e' grande e ha i suoi preset) */
    inline juce::var toJson (SequencerEngine& engine, bool includeSynthState = true)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("app", "DAWFL");
        root->setProperty ("version", formatVersion);
        root->setProperty ("bpm", engine.getBpm());
        root->setProperty ("swing", engine.getSwing());
        root->setProperty ("master", engine.getMasterVolume());
        root->setProperty ("currentPattern", engine.getCurrentPattern());

        juce::Array<juce::var> channels;
        for (int c = 0; c < SequencerEngine::numChannels; ++c)
        {
            auto& ch = engine.getChannel (c);
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", SequencerEngine::getChannelInfo (c).name);
            o->setProperty ("kit", ch.kit.load());
            o->setProperty ("volume", ch.volume.load());
            o->setProperty ("muted", ch.muted.load());
            o->setProperty ("pan", ch.pan.load());
            o->setProperty ("pitch", ch.pitch.load());
            o->setProperty ("decay", ch.decay.load());
            o->setProperty ("tone", ch.tone.load());
            o->setProperty ("drive", ch.drive.load());
            o->setProperty ("filter", ch.filter.load());
            o->setProperty ("reverb", ch.reverb.load());
            o->setProperty ("delay", ch.delay.load());
            o->setProperty ("humanize", ch.humanize.load());
            if (ch.sampleName.isNotEmpty())
                o->setProperty ("sample", ch.sampleName);
            channels.add (juce::var (o));
        }
        root->setProperty ("channels", channels);

        juce::Array<juce::var> patterns;
        for (int p = 0; p < SequencerEngine::numPatterns; ++p)
        {
            auto& pat = engine.getPattern (p);
            auto* o = new juce::DynamicObject();
            o->setProperty ("length", pat.length.load());

            juce::Array<juce::var> rows, probs, ratchets;
            for (int c = 0; c < SequencerEngine::numChannels; ++c)
            {
                rows.add (encodeRow (pat.velocity[(size_t) c]));
                probs.add (encodeRow (pat.probability[(size_t) c]));
                ratchets.add (encodeRow (pat.ratchet[(size_t) c]));
            }

            // Note del piano roll: per canale una lista di [inizio, durata, altezza, velocity] in tick
            juce::Array<juce::var> notes;
            bool anyNotes = false;
            for (int c = 0; c < SequencerEngine::numChannels; ++c)
            {
                juce::Array<juce::var> list;
                for (auto& n : engine.getNotes (p, c))
                    list.add (juce::Array<juce::var> { n.start, n.length, n.pitch, n.velocity });
                anyNotes = anyNotes || ! list.isEmpty();
                notes.add (list);
            }
            if (anyNotes)
                o->setProperty ("notes", notes);

            o->setProperty ("steps", rows);
            o->setProperty ("probability", probs);
            o->setProperty ("ratchet", ratchets);
            patterns.add (juce::var (o));
        }
        root->setProperty ("patterns", patterns);

        // Stato completo di BassForge (tutti i parametri + pattern dei Performer), in base64
        if (includeSynthState)
            if (auto* synth = engine.getSynth())
            {
                juce::MemoryBlock state;
                synth->getStateInformation (state);
                root->setProperty ("bassforge", state.toBase64Encoding());
            }

        return juce::var (root);
    }

    /** Carica un progetto nel motore. Ritorna false se il file non e' valido (il motore resta com'era). */
    inline bool fromJson (SequencerEngine& engine, const juce::var& json)
    {
        if (json.getProperty ("app", {}).toString() != "DAWFL")
            return false;

        engine.resetProject();
        engine.setBpm ((double) json.getProperty ("bpm", 120.0));
        engine.setSwing ((float) (double) json.getProperty ("swing", 0.0));
        engine.setMasterVolume ((float) (double) json.getProperty ("master", 1.0));

        if (auto* channels = json.getProperty ("channels", {}).getArray())
        {
            for (int c = 0; c < juce::jmin (channels->size(), SequencerEngine::numChannels); ++c)
            {
                const auto& o = channels->getReference (c);
                auto& ch = engine.getChannel (c);
                engine.setChannelKit (c, (int) o.getProperty ("kit", 0));
                ch.volume.store (juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("volume", 0.8)));
                ch.muted.store ((bool) o.getProperty ("muted", false));

                auto get = [&o] (const char* key, float def, float lo, float hi)
                {
                    return juce::jlimit (lo, hi, (float) (double) o.getProperty (key, (double) def));
                };

                ch.pan.store      (get ("pan",      0.0f, -1.0f, 1.0f));
                ch.pitch.store    (get ("pitch",    0.0f, -12.0f, 12.0f));
                ch.decay.store    (get ("decay",    1.0f, 0.25f, 3.0f));
                ch.tone.store     (get ("tone",     0.0f, -1.0f, 1.0f));
                ch.drive.store    (get ("drive",    0.0f, 0.0f, 1.0f));
                ch.filter.store   (get ("filter",   1.0f, 0.0f, 1.0f));
                ch.reverb.store   (get ("reverb",   0.0f, 0.0f, 1.0f));
                ch.delay.store    (get ("delay",    0.0f, 0.0f, 1.0f));
                ch.humanize.store (get ("humanize", 0.15f, 0.0f, 1.0f));
                ch.sampleName = o.getProperty ("sample", {}).toString();   // il campione lo carica la UI (SampleStore)
                ch.touchSound();
            }
        }

        if (auto* patterns = json.getProperty ("patterns", {}).getArray())
        {
            for (int p = 0; p < juce::jmin (patterns->size(), SequencerEngine::numPatterns); ++p)
            {
                const auto& o = patterns->getReference (p);
                auto& pat = engine.getPattern (p);
                pat.length.store ((int) o.getProperty ("length", 16) > 16 ? 32 : 16);

                auto readRows = [&o] (const char* key, std::array<SequencerEngine::Pattern::Row, SequencerEngine::numChannels>& target,
                                      int lo, int hi)
                {
                    if (auto* rows = o.getProperty (key, {}).getArray())
                        for (int c = 0; c < juce::jmin (rows->size(), SequencerEngine::numChannels); ++c)
                            decodeRow (target[(size_t) c], rows->getReference (c).toString(), lo, hi);
                };

                readRows ("steps",       pat.velocity,    0, 127);
                readRows ("probability", pat.probability, 0, 100);   // assenti nei file vecchi: restano al 100%
                readRows ("ratchet",     pat.ratchet,     1, SequencerEngine::maxRatchet);

                if (auto* notes = o.getProperty ("notes", {}).getArray())
                {
                    for (int c = 0; c < juce::jmin (notes->size(), SequencerEngine::numChannels); ++c)
                    {
                        std::vector<SequencerEngine::NoteData> list;
                        if (auto* arr = notes->getReference (c).getArray())
                            for (auto& n : *arr)
                                if (auto* f = n.getArray(); f != nullptr && f->size() >= 4)
                                    list.push_back ({ (int) (*f)[0], (int) (*f)[1], (int) (*f)[2], (int) (*f)[3] });
                        engine.setNotes (p, c, list);
                    }
                }
            }
        }

        engine.selectPattern ((int) json.getProperty ("currentPattern", 0));

        if (auto* synth = engine.getSynth())
        {
            const auto encoded = json.getProperty ("bassforge", {}).toString();
            juce::MemoryBlock state;
            if (encoded.isNotEmpty() && state.fromBase64Encoding (encoded))
                synth->setStateInformation (state.getData(), (int) state.getSize());
        }

        return true;
    }

    //==============================================================================
    inline bool saveToFile (SequencerEngine& engine, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        return file.replaceWithText (juce::JSON::toString (toJson (engine)));
    }

    inline bool loadFromFile (SequencerEngine& engine, const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        return fromJson (engine, juce::JSON::parse (file.loadFileAsString()));
    }
}
