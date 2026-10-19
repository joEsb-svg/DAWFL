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
    static constexpr int formatVersion = 1;
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
    inline juce::String encodeRow (SequencerEngine::Pattern& p, int channel)
    {
        juce::String s;
        for (int i = 0; i < SequencerEngine::maxSteps; ++i)
            s << juce::String::toHexString ((int) p.velocity[(size_t) channel][(size_t) i].load()).paddedLeft ('0', 2);
        return s;
    }

    inline void decodeRow (SequencerEngine::Pattern& p, int channel, const juce::String& s)
    {
        for (int i = 0; i < SequencerEngine::maxSteps; ++i)
        {
            const int v = s.substring (i * 2, i * 2 + 2).getHexValue32();
            p.velocity[(size_t) channel][(size_t) i].store ((uint8_t) juce::jlimit (0, 127, v));
        }
    }

    //==============================================================================
    inline juce::var toJson (SequencerEngine& engine)
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
            channels.add (juce::var (o));
        }
        root->setProperty ("channels", channels);

        juce::Array<juce::var> patterns;
        for (int p = 0; p < SequencerEngine::numPatterns; ++p)
        {
            auto& pat = engine.getPattern (p);
            auto* o = new juce::DynamicObject();
            o->setProperty ("length", pat.length.load());

            juce::Array<juce::var> rows;
            for (int c = 0; c < SequencerEngine::numChannels; ++c)
                rows.add (encodeRow (pat, c));

            o->setProperty ("steps", rows);
            patterns.add (juce::var (o));
        }
        root->setProperty ("patterns", patterns);

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
            }
        }

        if (auto* patterns = json.getProperty ("patterns", {}).getArray())
        {
            for (int p = 0; p < juce::jmin (patterns->size(), SequencerEngine::numPatterns); ++p)
            {
                const auto& o = patterns->getReference (p);
                auto& pat = engine.getPattern (p);
                pat.length.store ((int) o.getProperty ("length", 16) > 16 ? 32 : 16);

                if (auto* rows = o.getProperty ("steps", {}).getArray())
                    for (int c = 0; c < juce::jmin (rows->size(), SequencerEngine::numChannels); ++c)
                        decodeRow (pat, c, rows->getReference (c).toString());
            }
        }

        engine.selectPattern ((int) json.getProperty ("currentPattern", 0));
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
