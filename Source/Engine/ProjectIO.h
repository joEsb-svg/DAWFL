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
    static constexpr int formatVersion = 5;   // 3: canali variabili; 4: uno strumento (synth) per canale; 5: mixer
    static constexpr const char* extension = ".dawfl";

    inline juce::File getRootFolder()
    {
        // La cartella si chiama ancora "DAWFL" (il vecchio nome dell'app): cosi' chi aggiorna ritrova tutto
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
        root->setProperty ("app", "JLoop");
        root->setProperty ("version", formatVersion);
        root->setProperty ("bpm", engine.getBpm());
        root->setProperty ("swing", engine.getSwing());
        if (engine.getScaleType() > 0)
            root->setProperty ("scale", juce::Array<juce::var> { engine.getScaleRoot(), engine.getScaleType() });
        root->setProperty ("master", engine.getMasterVolume());
        root->setProperty ("currentPattern", engine.getCurrentPattern());

        const int numChannels = engine.getNumChannels();
        juce::Array<juce::var> channels;
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = engine.getChannel (c);
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", ch.name);
            if (ch.customName)
                o->setProperty ("customName", true);
            o->setProperty ("type", ch.type.load() == SequencerEngine::synth ? "synth" : "drum");
            if (ch.type.load() == SequencerEngine::synth)
            {
                o->setProperty ("instrument", ch.instrumentKind.isEmpty() ? juce::String ("bassforge") : ch.instrumentKind);
                o->setProperty ("instrumentId", ch.instrumentId);

                // Stato completo dello strumento (parametri, preset...), in base64
                if (includeSynthState)
                {
                    if (auto* inst = ch.instrument.load())
                    {
                        juce::MemoryBlock state;
                        inst->getStateInformation (state);
                        o->setProperty ("state", state.toBase64Encoding());
                    }
                    else if (ch.pendingState.isNotEmpty())
                        o->setProperty ("state", ch.pendingState);
                }
            }
            o->setProperty ("drumType", ch.drumType.load());
            o->setProperty ("colour", juce::String::toHexString ((juce::int64) ch.colour.load()));
            if (ch.solo.load())
                o->setProperty ("solo", true);
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
            o->setProperty ("mixer", ch.mixerTrack.load());
            if (ch.arpOn.load() || ch.arpMode.load() != 0 || ch.arpRate.load() != 12)
                o->setProperty ("arp", juce::Array<juce::var> { ch.arpOn.load(), ch.arpMode.load(), ch.arpRate.load(), ch.arpOctaves.load(), ch.arpGate.load() });
            if (ch.sampleName.isNotEmpty())
                o->setProperty ("sample", ch.sampleName);
            channels.add (juce::var (o));
        }
        root->setProperty ("channels", channels);

        // Solo i pattern in uso (e quello aperto): i progetti restano piccoli anche con 99 pattern
        int patternCount = juce::jmax (engine.getUsedPatternCount(), engine.getCurrentPattern() + 1);
        for (const auto& clip : engine.getClips())
            patternCount = juce::jmax (patternCount, clip.pattern + 1);
        for (int p = 0; p < SequencerEngine::numPatterns; ++p)
            if (engine.getCustomPatternName (p).isNotEmpty())
                patternCount = juce::jmax (patternCount, p + 1);

        juce::Array<juce::var> patterns;
        for (int p = 0; p < patternCount; ++p)
        {
            auto& pat = engine.getPattern (p);
            auto* o = new juce::DynamicObject();
            o->setProperty ("length", pat.length.load());
            if (engine.getCustomPatternName (p).isNotEmpty())
                o->setProperty ("name", engine.getCustomPatternName (p));

            if (engine.isPatternEmpty (p))   // pattern vuoto: basta la lunghezza (e il nome)
            {
                patterns.add (juce::var (o));
                continue;
            }

            juce::Array<juce::var> rows, probs, ratchets;
            for (int c = 0; c < numChannels; ++c)
            {
                rows.add (encodeRow (pat.velocity[(size_t) c]));
                probs.add (encodeRow (pat.probability[(size_t) c]));
                ratchets.add (encodeRow (pat.ratchet[(size_t) c]));
            }

            // Note del piano roll: per canale una lista di [inizio, durata, altezza, velocity] in tick
            juce::Array<juce::var> notes;
            bool anyNotes = false;
            for (int c = 0; c < numChannels; ++c)
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

        // Playlist: [traccia, battuta, pattern] per ogni clip
        juce::Array<juce::var> song;
        for (auto& c : engine.getClips())
        {
            juce::Array<juce::var> entry { c.track, c.bar, c.pattern };
            if (c.length > 0 || c.offset > 0 || c.sub > 0)   // clip accorciata/allungata/tagliata
            {
                entry.add (c.length); entry.add (c.offset); entry.add (c.sub);
            }
            song.add (entry);
        }
        if (! song.isEmpty())
            root->setProperty ("song", song);
        if (engine.isSongMode())
            root->setProperty ("songMode", true);

        // Mixer: solo le tracce modificate
        juce::Array<juce::var> tracks;
        for (int t = 0; t < mixer::numTracks; ++t)
        {
            if (engine.isTrackDefault (t))
                continue;
            const auto& tr = engine.getTrack (t);
            auto* o = new juce::DynamicObject();
            o->setProperty ("track", t);
            if (tr.name.isNotEmpty()) o->setProperty ("name", tr.name);
            if (tr.colour != 0)       o->setProperty ("colour", juce::String::toHexString ((juce::int64) tr.colour));
            o->setProperty ("volume", tr.volume.load());
            o->setProperty ("pan", tr.pan.load());
            if (tr.muted.load()) o->setProperty ("muted", true);
            if (tr.solo.load())  o->setProperty ("solo", true);
            o->setProperty ("eq", juce::Array<juce::var> { tr.eqLow.load(), tr.eqMid.load(), tr.eqMidFreq.load(), tr.eqHigh.load() });

            juce::Array<juce::var> fx;
            for (int s = 0; s < mixer::numSlots; ++s)
            {
                const auto& slot = tr.slots[(size_t) s];
                const int type = slot.type.load();
                if (type == mixer::none)
                    continue;
                auto* f = new juce::DynamicObject();
                f->setProperty ("slot", s);
                f->setProperty ("type", mixer::info (type).id);
                if (slot.bypass.load()) f->setProperty ("bypass", true);
                juce::Array<juce::var> values;
                for (int k = 0; k < mixer::info (type).numParams; ++k)
                    values.add (slot.params[(size_t) k].load());
                f->setProperty ("params", values);
                fx.add (juce::var (f));
            }
            if (! fx.isEmpty())
                o->setProperty ("fx", fx);
            tracks.add (juce::var (o));
        }
        if (! tracks.isEmpty())
            root->setProperty ("mixer", tracks);

        // Automazioni
        {
            juce::Array<juce::var> lanes;
            for (int i = 0; i < automation::maxLanes; ++i)
            {
                if (! engine.isLaneUsed (i))
                    continue;
                const auto& l = engine.getLane (i);
                auto* o = new juce::DynamicObject();
                o->setProperty ("lane", i);
                o->setProperty ("target", juce::Array<juce::var> { l.kind.load(), l.a.load(), l.b.load(), l.c.load() });
                if (! l.enabled.load()) o->setProperty ("enabled", false);
                juce::Array<juce::var> pts;
                for (auto& p : engine.getLanePoints (i))
                    pts.add (juce::Array<juce::var> { p.tick, juce::roundToInt (p.value * 10000.0f) });
                o->setProperty ("points", pts);
                lanes.add (juce::var (o));
            }
            if (! lanes.isEmpty())
                root->setProperty ("automation", lanes);
        }

        // Mastering
        {
            const auto& m = engine.getMastering();
            auto* o = new juce::DynamicObject();
            o->setProperty ("enabled", m.enabled.load());
            o->setProperty ("style", m.styleIndex.load());
            o->setProperty ("loudness", m.loudness.load());
            o->setProperty ("gain", m.gainDb.load());
            o->setProperty ("low", m.lowDb.load());
            o->setProperty ("presence", m.presenceDb.load());
            o->setProperty ("air", m.airDb.load());
            o->setProperty ("glue", m.glue.load());
            o->setProperty ("warmth", m.warmth.load());
            o->setProperty ("width", m.width.load());
            o->setProperty ("ceiling", m.ceiling.load());
            if (m.analysed)
                o->setProperty ("analysis", juce::Array<juce::var> { m.measuredLufs, m.measuredPeakDb, m.resultLufs, m.analysedLoudness });
            root->setProperty ("mastering", juce::var (o));
        }

        return juce::var (root);
    }

    /** Carica un progetto nel motore. Ritorna false se il file non e' valido (il motore resta com'era). */
    inline bool fromJson (SequencerEngine& engine, const juce::var& json)
    {
        const auto app = json.getProperty ("app", {}).toString();
        if (app != "JLoop" && app != "DAWFL")   // accetta anche i progetti salvati col vecchio nome
            return false;

        engine.resetProject (true);   // gli effetti del mixer restano (si riusano se sono dello stesso tipo)
        engine.setBpm ((double) json.getProperty ("bpm", 120.0));
        engine.setSwing ((float) (double) json.getProperty ("swing", 0.0));
        if (auto* sc = json.getProperty ("scale", {}).getArray(); sc != nullptr && sc->size() >= 2)
            engine.setScale ((int) (*sc)[0], (int) (*sc)[1]);
        engine.setMasterVolume ((float) (double) json.getProperty ("master", 1.0));

        const int version = (int) json.getProperty ("version", 1);
        int numChannels = SequencerEngine::defaultChannels;

        if (auto* channels = json.getProperty ("channels", {}).getArray())
        {
            numChannels = juce::jlimit (1, SequencerEngine::maxChannels, channels->size());
            if (version < 3)
                numChannels = SequencerEngine::defaultChannels;   // i progetti vecchi avevano sempre 6 batterie + BassForge
            engine.setNumChannels (numChannels);

            for (int c = 0; c < numChannels; ++c)
            {
                const auto o = c < channels->size() ? channels->getReference (c) : juce::var();
                auto& ch = engine.getChannel (c);

                // Tipo di canale. Nei progetti vecchi (v1-v2) i canali erano fissi: 6 di batteria + BassForge.
                const bool isSynthChannel = version >= 3 ? o.getProperty ("type", "drum").toString() == "synth" : c == 6;
                const int drumType = version >= 3 ? (int) o.getProperty ("drumType", 0) : juce::jmin (c, SequencerEngine::numDrumTypes - 1);
                ch.customName = false;
                if (isSynthChannel)
                {
                    const auto kind = o.getProperty ("instrument", "bassforge").toString();
                    engine.makeInstrumentChannel (c, kind, kind == "polyforge" ? "PolyForge" : kind == "fmforge" ? "FMForge"
                                                         : kind == "drumforge" ? "DrumForge" : kind == "frenchroulette" ? "French Roulette" : "BassForge",
                                                  kind == "polyforge" ? 0xffb07cffu : kind == "fmforge" ? 0xff7dffb0u
                                                         : kind == "drumforge" ? 0xffff6b5bu : kind == "frenchroulette" ? 0xffff3a3au : SequencerEngine::synthColour);
                    ch.stepNote.store (kind == "drumforge" ? 36 : SequencerEngine::synthStepNote);
                    ch.instrumentId = (int) o.getProperty ("instrumentId", 0);
                    ch.pendingState = o.getProperty ("state", {}).toString();   // lo applica la UI (Instruments::Host)
                }
                else
                    engine.makeDrumChannel (c, drumType);

                if (version >= 3)
                {
                    ch.name = o.getProperty ("name", ch.name).toString();
                    ch.customName = (bool) o.getProperty ("customName", false);
                    ch.colour.store ((juce::uint32) o.getProperty ("colour", "ff8aa0b8").toString().getHexValue64());
                    ch.solo.store ((bool) o.getProperty ("solo", false));
                }

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
                engine.setPatternName (p, o.getProperty ("name", {}).toString());

                auto readRows = [&o, numChannels] (const char* key, std::array<SequencerEngine::Pattern::Row, SequencerEngine::maxChannels>& target,
                                                   int lo, int hi)
                {
                    if (auto* rows = o.getProperty (key, {}).getArray())
                        for (int c = 0; c < juce::jmin (rows->size(), numChannels); ++c)
                            decodeRow (target[(size_t) c], rows->getReference (c).toString(), lo, hi);
                };

                readRows ("steps",       pat.velocity,    0, 127);
                readRows ("probability", pat.probability, 0, 100);   // assenti nei file vecchi: restano al 100%
                readRows ("ratchet",     pat.ratchet,     1, SequencerEngine::maxRatchet);

                if (auto* notes = o.getProperty ("notes", {}).getArray())
                {
                    for (int c = 0; c < juce::jmin (notes->size(), numChannels); ++c)
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

        if (auto* song = json.getProperty ("song", {}).getArray())
        {
            std::vector<SequencerEngine::Clip> clips;
            for (auto& c : *song)
                if (auto* f = c.getArray(); f != nullptr && f->size() >= 3)
                {
                    SequencerEngine::Clip clip;
                    clip.track = juce::jlimit (0, SequencerEngine::numTracks - 1, (int) (*f)[0]);
                    clip.bar = juce::jlimit (0, SequencerEngine::maxBars - 1, (int) (*f)[1]);
                    clip.pattern = juce::jlimit (0, SequencerEngine::numPatterns - 1, (int) (*f)[2]);
                    if (f->size() >= 6)
                    {
                        clip.length = juce::jlimit (0, 8191, (int) (*f)[3]);
                        clip.offset = juce::jlimit (0, 63, (int) (*f)[4]);
                        clip.sub = juce::jlimit (0, 15, (int) (*f)[5]);
                    }
                    clips.push_back (clip);
                }
            engine.setClips (clips);
        }
        engine.setSongMode ((bool) json.getProperty ("songMode", false));

        // Automazioni
        if (auto* lanes = json.getProperty ("automation", {}).getArray())
            for (auto& o : *lanes)
            {
                const int i = (int) o.getProperty ("lane", -1);
                auto* t = o.getProperty ("target", {}).getArray();
                if (i < 0 || i >= automation::maxLanes || t == nullptr || t->size() < 4)
                    continue;
                auto& l = engine.getLane (i);
                l.count.store (0);
                l.setTarget ({ (int) (*t)[0], (int) (*t)[1], (int) (*t)[2], (int) (*t)[3] });
                l.enabled.store ((bool) o.getProperty ("enabled", true));
                std::vector<automation::Point> pts;
                if (auto* arr = o.getProperty ("points", {}).getArray())
                    for (auto& p : *arr)
                        if (auto* f = p.getArray(); f != nullptr && f->size() >= 2)
                            pts.push_back ({ (int) (*f)[0], (float) (int) (*f)[1] / 10000.0f });
                engine.setLanePoints (i, pts);
            }

        // Mastering
        if (const auto o = json.getProperty ("mastering", {}); o.isObject())
        {
            auto& m = engine.getMastering();
            auto get = [&o] (const char* key, float def, float lo, float hi) { return juce::jlimit (lo, hi, (float) (double) o.getProperty (key, def)); };
            m.enabled.store ((bool) o.getProperty ("enabled", false));
            m.styleIndex.store (juce::jlimit (0, mastering::numStyles - 1, (int) o.getProperty ("style", 0)));
            m.loudness.store (get ("loudness", -12.0f, -20.0f, -6.0f));
            m.gainDb.store (get ("gain", 0.0f, -12.0f, 30.0f));
            m.lowDb.store (get ("low", 0.0f, -6.0f, 6.0f));
            m.presenceDb.store (get ("presence", 0.0f, -6.0f, 6.0f));
            m.airDb.store (get ("air", 0.0f, -6.0f, 6.0f));
            m.glue.store (get ("glue", 0.4f, 0.0f, 1.0f));
            m.warmth.store (get ("warmth", 0.2f, 0.0f, 1.0f));
            m.width.store (get ("width", 1.0f, 0.0f, 1.5f));
            m.ceiling.store (get ("ceiling", -1.0f, -6.0f, -0.3f));
            if (auto* a = o.getProperty ("analysis", {}).getArray(); a != nullptr && a->size() >= 4)
            {
                m.analysed = true;
                m.measuredLufs = (float) (*a)[0]; m.measuredPeakDb = (float) (*a)[1];
                m.resultLufs = (float) (*a)[2]; m.analysedLoudness = (float) (*a)[3];
            }
        }

        // Mixer. Nei progetti senza mixer (v1-v4) ogni canale va nella sua traccia insert.
        for (int c = 0; c < engine.getNumChannels(); ++c)
        {
            int track = c + 1;
            if (version >= 5)
                if (auto* chs = json.getProperty ("channels", {}).getArray(); chs != nullptr && c < chs->size())
                    track = (int) chs->getReference (c).getProperty ("mixer", 0);
            engine.setChannelTrack (c, track);
            if (auto* chs = json.getProperty ("channels", {}).getArray(); chs != nullptr && c < chs->size())
                if (auto* arp = chs->getReference (c).getProperty ("arp", {}).getArray(); arp != nullptr && arp->size() >= 5)
                {
                    auto& ch = engine.getChannel (c);
                    ch.arpOn.store ((bool) (*arp)[0]);
                    ch.arpMode.store (juce::jlimit (0, 4, (int) (*arp)[1]));
                    ch.arpRate.store (juce::jlimit (3, 96, (int) (*arp)[2]));
                    ch.arpOctaves.store (juce::jlimit (1, 3, (int) (*arp)[3]));
                    ch.arpGate.store (juce::jlimit (0.05f, 1.0f, (float) (double) (*arp)[4]));
                }
        }

        std::array<std::array<int, mixer::numSlots>, mixer::numTracks> wanted {};   // tipo di effetto voluto per ogni slot
        std::array<std::array<const juce::var*, mixer::numSlots>, mixer::numTracks> wantedFx {};
        if (auto* tracks = json.getProperty ("mixer", {}).getArray())
            for (auto& o : *tracks)
            {
                const int t = (int) o.getProperty ("track", -1);
                if (t < 0 || t >= mixer::numTracks)
                    continue;
                auto& tr = engine.getTrack (t);
                tr.name = o.getProperty ("name", {}).toString();
                tr.colour = (juce::uint32) o.getProperty ("colour", "0").toString().getHexValue64();
                tr.volume.store (juce::jlimit (0.0f, 2.0f, (float) o.getProperty ("volume", 1.0)));
                tr.pan.store (juce::jlimit (-1.0f, 1.0f, (float) o.getProperty ("pan", 0.0)));
                tr.muted.store ((bool) o.getProperty ("muted", false));
                tr.solo.store ((bool) o.getProperty ("solo", false));
                if (auto* eq = o.getProperty ("eq", {}).getArray(); eq != nullptr && eq->size() >= 4)
                {
                    tr.eqLow.store (juce::jlimit (-18.0f, 18.0f, (float) (*eq)[0]));
                    tr.eqMid.store (juce::jlimit (-18.0f, 18.0f, (float) (*eq)[1]));
                    tr.eqMidFreq.store (juce::jlimit (100.0f, 10000.0f, (float) (*eq)[2]));
                    tr.eqHigh.store (juce::jlimit (-18.0f, 18.0f, (float) (*eq)[3]));
                }
                if (auto* fx = o.getProperty ("fx", {}).getArray())
                    for (auto& f : *fx)
                    {
                        const int s = (int) f.getProperty ("slot", -1);
                        if (s >= 0 && s < mixer::numSlots)
                        {
                            wanted[(size_t) t][(size_t) s] = mixer::typeFromId (f.getProperty ("type", {}).toString());
                            wantedFx[(size_t) t][(size_t) s] = &f;
                        }
                    }
            }

        for (int t = 0; t < mixer::numTracks; ++t)
            for (int s = 0; s < mixer::numSlots; ++s)
            {
                const int type = wanted[(size_t) t][(size_t) s];
                engine.setTrackEffect (t, s, type);   // stesso tipo di prima: l'effetto resta, si azzerano i parametri
                auto& slot = engine.getTrack (t).slots[(size_t) s];
                if (auto* f = wantedFx[(size_t) t][(size_t) s]; f != nullptr && type != mixer::none)
                {
                    slot.bypass.store ((bool) f->getProperty ("bypass", false));
                    if (auto* values = f->getProperty ("params", {}).getArray())
                        for (int k = 0; k < juce::jmin (values->size(), mixer::info (type).numParams); ++k)
                        {
                            const auto& def = mixer::info (type).params[(size_t) k];
                            slot.params[(size_t) k].store (juce::jlimit (def.minValue, def.maxValue, (float) (*values)[k]));
                        }
                }
            }

        // Progetti v1-v3: un solo BassForge, con lo stato nella radice del file
        if (const auto legacy = json.getProperty ("bassforge", {}).toString(); legacy.isNotEmpty())
            for (int c = 0; c < engine.getNumChannels(); ++c)
                if (engine.isSynth (c))
                {
                    engine.getChannel (c).pendingState = legacy;
                    break;
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
