#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"
#include "../Engine/MusicTheory.h"

//==============================================================================
/**
    Progetti demo: una canzone breve per genere, costruita qui nel codice (niente file da scaricare).
    Ogni demo ha batteria a step (un canale per suono: si vede e si modifica nel channel rack),
    basso, accordi e melodia con gli strumenti di JLoop, pattern con nome, una playlist con
    intro / drop / pausa / finale, sidechain e riverbero nel mixer e un'automazione del filtro.
*/
namespace demos
{
    /** Quello che serve all'app per costruire i canali (lo fa MainComponent). */
    struct Host
    {
        /** Canale con un campione della libreria; restituisce l'indice (-1 = errore). */
        std::function<int (const char* sampleId, const char* label, float tune, float levelDb, float pan)> addSample;
        /** Canale strumento ("polyforge", "bassforge", "fmforge") con il preset di fabbrica indicato. */
        std::function<int (const char* kind, const char* name, const char* preset)> addInstrument;
    };

    struct DrumRow
    {
        const char* sample;
        const char* label;
        const char* intro;     // 16 caratteri: x colpo, o colpo leggero, r rullata (3 colpi), . vuoto
        const char* full;      // 32 caratteri (2 battute)
        float levelDb = 0.0f, pan = 0.0f, tune = 0.0f;
    };

    struct Part
    {
        const char* kind;      // strumento
        const char* name;
        const char* preset;
        float volume = 0.75f;
    };

    struct Melody
    {
        int step, length, degree;   // grado della scala, 0 = tonica (ottava sopra gli accordi)
    };

    struct Demo
    {
        const char* name;
        const char* description;
        double bpm;
        float swing;
        int root, scaleType;
        std::vector<int> progression;            // 4 gradi, uno ogni mezza battuta... o battuta (vedi chordSteps)
        int chordType;                           // theory::ChordType
        const char* chordRhythm;                 // 8 caratteri per accordo: x attacco, - tenuto, . pausa
        const char* bassRhythm;                  // 8 caratteri per accordo: x tonica, o ottava sopra, - tenuto, . pausa
        std::vector<DrumRow> drums;
        Part bass, chords, lead;
        bool leadIsArp;                          // la "melodia" e' l'arpeggiatore sugli accordi
        std::vector<Melody> melody;
        bool sidechain;                          // basso e accordi "pompano" con la cassa
        bool lofi;                               // saturazione e bitcrusher leggero sugli accordi
    };

    inline const std::vector<Demo>& list()
    {
        static const std::vector<Demo> demos =
        {
            { "Techno", "128 BPM, La minore: cassa dritta, basso rolling, stab e arpeggio",
              128.0, 0.0f, 9, 2, { 0, 0, 5, 6 }, theory::triad, "..x...x.", ".xx.-xx.",
              {
                  { "techno_kick", "Cassa",       "x...x...x...x...", "x...x...x...x...x...x...x...x...", 0.0f },
                  { "techno_rumble", "Rumble",    "................", "..x...x...x...x...x...x...x...x.", -6.0f },
                  { "techno_clap", "Clap",        "................", "....x.......x.......x.......x...", -3.0f },
                  { "techno_hat", "Hi-hat",       "..x...x...x...x.", "..x...x...x...x...x...x...x...x.", -1.0f, 0.15f },
                  { "d909_ch", "Hi-hat 909",      "xoxoxoxoxoxoxoxo", "xoxoxoxoxoxoxoxoxoxoxoxoxoxoxoxo", -6.0f, -0.15f },
                  { "techno_ride", "Ride",        "................", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", -10.0f, 0.3f },
              },
              { "bassforge", "Basso", "Techno - Rumble", 0.7f },
              { "polyforge", "Stab", "Accordi Dub", 0.55f },
              { "polyforge", "Arpeggio", "Arpeggio PWM", 0.32f },
              true, {}, true, false },

            { "House", "124 BPM, Fa minore: accordi d'organo, basso in levare, pluck",
              124.0, 0.12f, 5, 2, { 0, 6, 5, 6 }, theory::seventh, "x..x..x.", "..x...o.",
              {
                  { "house_kick", "Cassa",        "x...x...x...x...", "x...x...x...x...x...x...x...x...", 0.0f },
                  { "house_clap", "Clap",         "................", "....x.......x.......x.......x...", -2.0f },
                  { "house_hat_open", "Hi-hat aperto", "..x...x...x...x.", "..x...x...x...x...x...x...x...x.", -5.0f, 0.15f },
                  { "house_shaker", "Shaker",     "xoxoxoxoxoxoxoxo", "xoxoxoxoxoxoxoxoxoxoxoxoxoxoxoxo", -9.0f, -0.2f },
                  { "conga_high", "Conga",        "................", "...x.....x.x.......x.....x..x...", -8.0f, -0.3f },
              },
              { "bassforge", "Basso", "Progressive - Basso Rolling", 0.7f },
              { "polyforge", "Organo", "Stab Organo House", 0.34f },
              { "polyforge", "Pluck", "Pluck House", 0.5f },
              false,
              { { 0, 2, 4 }, { 3, 2, 2 }, { 6, 2, 4 }, { 10, 4, 0 }, { 16, 2, 4 }, { 19, 2, 5 }, { 22, 2, 4 }, { 26, 4, 2 } },
              true, false },

            { "Trap", "140 BPM, Do minore armonica: 808, hi-hat con rullate, campane",
              140.0, 0.0f, 0, 3, { 0, 5, 3, 4 }, theory::triad, "x-------", "x--.-x--",
              {
                  { "trap_kick", "Cassa",         "x.........x.....", "x.........x.....x.....x...x.....", 0.0f },
                  { "trap_snare", "Rullante",     "................", "........x...............x.......", -1.0f },
                  { "trap_clap", "Clap",          "................", "........x...............x.......", -6.0f },
                  { "trap_hat", "Hi-hat",         "x.x.x.x.x.x.x.x.", "x.x.x.x.x.x.r.x.x.x.x.x.x.r.r.x.", -2.0f, 0.1f },
                  { "trap_hat_open", "Hi-hat aperto", "................", "..............x...............x.", -8.0f, -0.15f },
              },
              { "polyforge", "808", "Sub Morbido", 0.8f },
              { "polyforge", "Pad", "Pad Scuro", 0.4f },
              { "fmforge", "Campane", "Campana", 0.3f },
              false,
              { { 0, 3, 7 }, { 3, 3, 6 }, { 6, 2, 4 }, { 8, 6, 2 }, { 16, 3, 7 }, { 19, 3, 9 }, { 22, 2, 7 }, { 24, 8, 4 } },
              false, false },

            { "Drum and Bass", "174 BPM, Re minore: two-step, Reese, pad e piano elettrico",
              174.0, 0.0f, 2, 2, { 0, 5, 2, 6 }, theory::seventh, "x-------", "x---x-o-",
              {
                  { "dnb_kick", "Cassa",          "x.........x.....", "x.........x.....x.........x..x..", 0.0f },
                  { "dnb_snare", "Rullante",      "................", "....x.......x.......x.......x..o", -1.0f },
                  { "dnb_hat", "Hi-hat",          "x.x.x.x.x.x.x.x.", "x.x.x.x.x.x.x.xox.x.x.x.x.x.x.xo", -2.0f, 0.15f },
                  { "shaker", "Shaker",           "................", ".x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x", -10.0f, -0.25f },
                  { "dnb_ride", "Ride",           "................", "x...x...x...x...x...x...x...x...", -11.0f, 0.3f },
              },
              { "bassforge", "Reese", "DnB - Reese", 0.45f },
              { "polyforge", "Pad", "Pad Caldo", 0.3f },
              { "fmforge", "Piano", "Piano Elettrico", 0.5f },
              false,
              { { 0, 4, 4 }, { 4, 2, 6 }, { 6, 2, 7 }, { 8, 8, 9 }, { 16, 4, 7 }, { 20, 4, 6 }, { 24, 8, 4 } },
              true, false },

            { "Lo-Fi", "82 BPM, Fa maggiore: accordi jazz con la settima, swing, vinile",
              82.0, 0.35f, 5, 1, { 1, 4, 0, 0 }, theory::seventh, "x-----x-", "x---.-o-",
              {
                  { "lofi_kick", "Cassa",         "x.........x.....", "x.........x..x..x.......x.......", 0.0f },
                  { "lofi_snare", "Rullante",     "................", "....x.......x.......x.......x...", -2.0f },
                  { "lofi_hat", "Hi-hat",         "x.x.x.x.x.x.x.x.", "x.xox.x.x.xox.x.x.xox.x.x.xox.x.", -3.0f, 0.15f },
                  { "lofi_shaker", "Shaker",      "................", "..x...x...x...x...x...x...x...x.", -9.0f, -0.15f },
                  { "fx_vinyl", "Vinile",         "x...............", "x...............x...............", -12.0f },
              },
              { "fmforge", "Basso", "Basso FM", 0.6f },
              { "fmforge", "Piano", "Piano Elettrico Morbido", 0.2f },
              { "polyforge", "Melodia", "Lead Morbido", 0.4f },
              false,
              { { 2, 2, 4 }, { 4, 2, 5 }, { 6, 4, 4 }, { 12, 4, 2 }, { 18, 2, 4 }, { 20, 2, 2 }, { 22, 4, 1 }, { 28, 4, 0 } },
              false, true },
        };
        return demos;
    }

    //==========================================================================
    namespace detail
    {
        inline void writeSteps (SequencerEngine& engine, int pattern, int channel, const char* text)
        {
            engine.selectPattern (pattern);
            const int len = (int) std::strlen (text);
            for (int s = 0; s < len && s < SequencerEngine::maxSteps; ++s)
            {
                const char c = text[s];
                if (c == '.')
                    continue;
                engine.setStepVelocity (channel, s, c == 'o' ? 62 : 100);
                engine.setStepProbability (channel, s, 100);
                engine.setStepRatchet (channel, s, c == 'r' ? 3 : 1);
            }
        }

        /** Tonica degli accordi intorno al Do centrale (La3..Sol#4). */
        inline int chordOctave (int root)       { return root >= 6 ? 4 : 5; }

        /** Le note dell'accordo sul grado indicato, vicino alla tonica (senza salti grandi). */
        inline std::vector<int> chordNotes (const Demo& d, int degree)
        {
            const int oct = chordOctave (d.root);
            const int tonic = d.root + 12 * oct;
            int base = theory::pitchOfDegree (d.root, d.scaleType, degree, oct);
            const int shift = base - tonic > 7 ? -12 : 0;
            std::vector<int> notes;
            for (int k : { 0, 2, 4 })
                notes.push_back (theory::pitchOfDegree (d.root, d.scaleType, degree + k, oct) + shift);
            if (d.chordType == theory::seventh)
                notes.push_back (theory::pitchOfDegree (d.root, d.scaleType, degree + 6, oct) + shift);
            return notes;
        }

        /** Note da un ritmo di 8 caratteri ripetuto per ogni accordo (x attacco, - tenuto, . pausa). */
        template <typename PitchFn>
        inline std::vector<SequencerEngine::NoteData> rhythmNotes (const Demo& d, const char* rhythm, PitchFn pitchesFor, int velocity)
        {
            std::vector<SequencerEngine::NoteData> out;
            constexpr int tps = SequencerEngine::ticksPerStep;
            for (int slot = 0; slot < (int) d.progression.size(); ++slot)
            {
                for (int s = 0; s < 8; ++s)
                {
                    const char c = rhythm[s];
                    if (c != 'x' && c != 'o')
                        continue;
                    int len = 1;
                    while (s + len < 8 && rhythm[s + len] == '-')
                        ++len;
                    for (int pitch : pitchesFor (d.progression[(size_t) slot], c == 'o'))
                    {
                        SequencerEngine::NoteData n;
                        n.start = (slot * 8 + s) * tps;
                        n.length = len * tps - (len > 1 ? 0 : tps / 4);
                        n.pitch = juce::jlimit (0, 127, pitch);
                        n.velocity = velocity;
                        out.push_back (n);
                    }
                }
            }
            return out;
        }
    }

    //==========================================================================
    /**
        Costruisce la demo nel progetto (gia' vuoto: niente canali). Pattern:
          1 Intro batteria, 2 Batteria, 3 Basso, 4 Accordi, 5 Melodia
        Playlist (battute): intro 0-3, drop 4-11, pausa 12-15, finale 16-23.
    */
    inline bool build (SequencerEngine& engine, int index, const Host& host)
    {
        if (index < 0 || index >= (int) list().size())
            return false;
        const auto& d = list()[(size_t) index];
        using namespace detail;
        constexpr int tps = SequencerEngine::ticksPerStep;

        engine.setBpm (d.bpm);
        engine.setSwing (d.swing);
        engine.setScale (d.root, d.scaleType);
        engine.setMasterVolume (0.8f);     // un po' di margine: il limiter non deve schiacciare

        enum { pIntro = 0, pDrums, pBass, pChords, pLead, numDemoPatterns };
        static const char* patternNames[] = { "Intro batteria", "Batteria", "Basso", "Accordi", "Melodia" };
        for (int p = 0; p < numDemoPatterns; ++p)
        {
            engine.selectPattern (p);
            engine.setPatternLength (32);
            engine.setPatternName (p, patternNames[p]);
        }

        // Batteria: un canale per suono, a step
        int kickChannel = -1;
        for (auto& row : d.drums)
        {
            const int c = host.addSample (row.sample, row.label, row.tune, row.levelDb, row.pan);
            if (c < 0)
                continue;
            if (kickChannel < 0)
                kickChannel = c;
            juce::String intro (row.intro);
            writeSteps (engine, pIntro, c, (intro + intro).toRawUTF8());
            writeSteps (engine, pDrums, c, row.full);
        }

        auto addPart = [&] (const Part& part)
        {
            const int c = host.addInstrument (part.kind, part.name, part.preset);
            if (c >= 0)
                engine.getChannel (c).volume.store (part.volume);
            return c;
        };

        const int bass = addPart (d.bass);
        const int chords = addPart (d.chords);
        const int lead = addPart (d.lead);

        // Basso: la fondamentale dell'accordo, due ottave sotto
        if (bass >= 0)
            engine.setNotes (pBass, bass, rhythmNotes (d, d.bassRhythm, [&d] (int degree, bool up)
            {
                const int r = chordNotes (d, degree).front() - 24 + (up ? 12 : 0);
                return std::vector<int> { r };
            }, 105));

        if (chords >= 0)
            engine.setNotes (pChords, chords, rhythmNotes (d, d.chordRhythm, [&d] (int degree, bool) { return chordNotes (d, degree); }, 88));

        if (lead >= 0)
        {
            std::vector<SequencerEngine::NoteData> notes;
            if (d.leadIsArp)
            {
                // Arpeggiatore: l'accordo tenuto, un'ottava sopra, suonato una nota alla volta
                notes = rhythmNotes (d, "x-------", [&d] (int degree, bool)
                {
                    auto n = chordNotes (d, degree);
                    for (auto& p : n) p += 12;
                    return n;
                }, 92);
                auto& ch = engine.getChannel (lead);
                ch.arpOn.store (true);
                ch.arpMode.store (2);
                ch.arpRate.store (12);
                ch.arpOctaves.store (2);
                ch.arpGate.store (0.5f);
            }
            else
            {
                const int oct = chordOctave (d.root) + 1;
                for (auto& m : d.melody)
                {
                    SequencerEngine::NoteData n;
                    n.start = m.step * tps;
                    n.length = m.length * tps - tps / 4;
                    n.pitch = juce::jlimit (0, 127, theory::pitchOfDegree (d.root, d.scaleType, m.degree, oct));
                    n.velocity = 96;
                    notes.push_back (n);
                }
            }
            engine.setNotes (pLead, lead, notes);
        }

        // Mixer: sidechain con la cassa, riverbero e delay
        const int kickTrack = kickChannel >= 0 ? engine.getChannelTrack (kickChannel) : 0;
        int chordsFilterSlot = -1;
        auto fx = [&engine] (int channel, int slot, int type, std::initializer_list<std::pair<int, float>> params)
        {
            if (channel < 0) return;
            const int t = engine.getChannelTrack (channel);
            if (t <= 0) return;
            engine.setTrackEffect (t, slot, type);
            for (auto& p : params)
                engine.getTrack (t).slots[(size_t) slot].params[(size_t) p.first].store (p.second);
        };

        if (d.sidechain && kickTrack > 0)
        {
            fx (bass, 0, mixer::sidechain, { { 0, (float) kickTrack }, { 1, 0.75f } });
            fx (chords, 0, mixer::sidechain, { { 0, (float) kickTrack }, { 1, 0.6f } });
        }
        if (chords >= 0)
        {
            chordsFilterSlot = 1;
            fx (chords, 1, mixer::filter, { { 1, 20000.0f }, { 2, 0.25f } });
            if (d.lofi)
            {
                fx (chords, 2, mixer::saturation, { { 0, 0.3f }, { 1, 5000.0f } });
                fx (chords, 3, mixer::bitcrusher, { { 0, 12.0f }, { 1, 2.0f }, { 2, 0.5f } });
                fx (chords, 4, mixer::reverb, { { 0, 0.7f }, { 3, 0.3f } });
            }
            else
                fx (chords, 2, mixer::reverb, { { 0, 0.65f }, { 3, 0.25f } });
        }
        fx (lead, 0, mixer::delay, { { 0, 4.0f }, { 1, 0.35f }, { 2, 0.22f } });
        fx (lead, 1, mixer::reverb, { { 0, 0.7f }, { 3, 0.25f } });

        // Playlist: 4 tracce (batteria, basso, accordi, melodia), clip da 2 battute
        std::vector<SequencerEngine::Clip> clips;
        auto clip = [&clips] (int track, int fromBar, int toBar, int pattern)
        {
            for (int b = fromBar; b < toBar; b += 2)
                clips.push_back ({ track, b, pattern });
        };
        clip (0, 0, 4, pIntro);   clip (2, 0, 4, pChords);
        clip (0, 4, 12, pDrums);  clip (1, 4, 12, pBass);  clip (2, 4, 12, pChords);  clip (3, 8, 12, pLead);
        clip (2, 12, 16, pChords); clip (3, 12, 16, pLead);
        clip (0, 16, 24, pDrums); clip (1, 16, 24, pBass); clip (2, 16, 24, pChords); clip (3, 16, 24, pLead);
        engine.setClips (clips);

        // Automazione: il filtro degli accordi si apre nell'intro e si chiude nella pausa
        if (chords >= 0 && chordsFilterSlot >= 0)
        {
            const int t = engine.getChannelTrack (chords);
            const int lane = engine.addLane ({ automation::effectParam, t, chordsFilterSlot, 1 });
            if (lane >= 0)
            {
                const int bar = SequencerEngine::ticksPerStep * 16;
                engine.setLanePoints (lane, { { 0, 0.25f }, { 4 * bar, 1.0f }, { 12 * bar, 1.0f },
                                              { 14 * bar, 0.45f }, { 16 * bar, 1.0f }, { 24 * bar, 1.0f } });
            }
        }

        engine.selectPattern (pDrums);
        engine.setSongPosition (0);
        engine.setSongMode (true);
        return true;
    }
}
