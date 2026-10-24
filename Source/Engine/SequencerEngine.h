#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <memory>
#include <limits>
#include "DrumVoice.h"
#include "Sampler.h"
#include "Mixer.h"
#include "Mastering.h"
#include "Automation.h"
#include "Performance.h"

//==============================================================================
/**
    Il motore audio di JLoop.

    Gira sul thread audio, che e' "real-time": nel callback niente allocazioni,
    niente lock, niente file. La UI comunica con il motore solo tramite
    variabili atomiche, che si possono leggere e scrivere da thread diversi.

    Struttura (come in FL Studio):
    - i CANALI (strumento, suono, volume, pan, effetti...) sono del progetto: se ne possono
      aggiungere, clonare, spostare ed eliminare (fino a maxChannels)
    - i PATTERN (fino a 8) contengono le note: per ogni canale e step
      velocity (0 = spento), probabilita' e ratchet (colpi ripetuti nello step)
    - clock preciso al campione con swing; il cambio pattern durante la
      riproduzione avviene alla fine del giro, per restare a tempo

    Catena audio:  voce -> filtro del canale -> volume/pan -> mix stereo
                                             \-> mandate a riverbero e delay
                   mix + riverbero + delay -> limiter del master -> uscita
*/
class SequencerEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int maxSteps         = 32;
    static constexpr int maxChannels      = 32;    // canali del channel rack (strumenti)
    static constexpr int defaultChannels  = 7;     // progetto nuovo: 6 strumenti di batteria + il synth BassForge
    static constexpr int numDrumTypes     = 6;     // strumenti della batteria sintetizzata (vedi SoundLibrary)
    static constexpr int synthStepNote    = 48;    // nota suonata dagli step del synth (C3: registro da basso)
    static constexpr int numPatterns      = 99;    // pattern per progetto (01..99)
    static constexpr int closedHatType    = 3;
    static constexpr int openHatType      = 4;
    static constexpr int defaultVelocity  = 100;
    static constexpr int maxRatchet       = 4;
    static constexpr int ticksPerStep     = 12;    // risoluzione del piano roll: 1/12 di sedicesimo
    static constexpr int maxNotes         = 256;   // note per canale e per pattern
    static constexpr int rootNote         = 60;
    static constexpr int stepsPerBar      = 16;
    static constexpr int numTracks        = 12;    // tracce della playlist
    static constexpr int maxClips         = 512;   // pattern piazzati nella playlist
    static constexpr int maxBars          = 256;   // lunghezza massima della canzone (battute)    // la nota che suona lo strumento alla sua altezza originale (C5)

    /** Una nota del piano roll (tempi in tick). */
    struct NoteData
    {
        int start = 0, length = ticksPerStep, pitch = rootNote, velocity = defaultVelocity;
        bool operator== (const NoteData& o) const { return start == o.start && length == o.length && pitch == o.pitch && velocity == o.velocity; }
    };

    /** Tipo di canale: batteria sintetizzata / campione (con il campionatore), oppure uno strumento (synth: BassForge, PolyForge...). */
    enum ChannelType { drumOrSample = 0, synth = 1 };

    struct DrumTypeInfo
    {
        const char*  name;
        juce::uint32 colour;
    };

    /** Gli strumenti della batteria sintetizzata, con il nome e il colore di default. */
    static const DrumTypeInfo& getDrumTypeInfo (int drumType)
    {
        static const DrumTypeInfo infos[numDrumTypes] =
        {
            { "Kick",     0xffe8833a },
            { "Snare",    0xffd2555a },
            { "Clap",     0xffd8b14a },
            { "Hi-Hat",   0xff4fb3a9 },
            { "Open Hat", 0xff5b8fd6 },
            { "Tom",      0xff9a78d0 },
        };
        return infos[juce::jlimit (0, numDrumTypes - 1, drumType)];
    }

    static constexpr juce::uint32 synthColour = 0xff2fc6d6;

    //==============================================================================
    /** Impostazioni di un canale (valgono per tutti i pattern). */
    struct Channel
    {
        std::atomic<float> volume   { 0.8f };
        std::atomic<bool>  muted    { false };
        std::atomic<bool>  solo     { false };
        std::atomic<int>   type     { drumOrSample };
        std::atomic<int>   drumType { 0 };       // quale strumento della batteria sintetizzata (Kick, Snare...)
        std::atomic<int>   kit      { 0 };       // kit della batteria sintetizzata (vedi SoundLibrary)
        std::atomic<juce::uint32> colour { 0xff8aa0b8 };

        // Strumento (synth) del canale: un AudioProcessor posseduto dalla UI (Instruments::Host)
        std::atomic<juce::AudioProcessor*> instrument { nullptr };
        std::atomic<int> stepNote { synthStepNote };   // nota suonata dagli step (DrumForge: 36, il primo pad)
        std::atomic<int> mixerTrack { 0 };             // traccia del mixer: 0 = Master, 1..32 = insert

        // Arpeggiatore (solo canali strumento): le note tenute suonano una alla volta, a tempo
        std::atomic<bool>  arpOn      { false };
        std::atomic<int>   arpMode    { 0 };       // 0 su, 1 giu', 2 su e giu', 3 casuale, 4 accordo ripetuto
        std::atomic<int>   arpRate    { 12 };      // tick tra una nota e l'altra (12 = 1/16)
        std::atomic<int>   arpOctaves { 1 };       // 1..3
        std::atomic<float> arpGate    { 0.6f };    // durata delle note (frazione del passo)

        // Usati solo dal thread della UI
        juce::String name;
        bool customName = false;                 // rinominato dall'utente: cambiando suono il nome resta
        juce::String instrumentKind;             // "bassforge", "polyforge"...
        int instrumentId = 0;                    // identifica l'istanza (undo, clona)
        juce::String pendingState;               // stato da applicare allo strumento (base64), dopo un caricamento

        // Editor del canale
        std::atomic<float> pan      { 0.0f };    // -1 sinistra .. +1 destra
        std::atomic<float> pitch    { 0.0f };    // semitoni
        std::atomic<float> decay    { 1.0f };    // moltiplicatore
        std::atomic<float> tone     { 0.0f };    // -1 .. +1
        std::atomic<float> drive    { 0.0f };    // 0 .. 1
        std::atomic<float> filter   { 1.0f };    // 0 .. 1 (1 = filtro aperto)
        std::atomic<float> reverb   { 0.0f };    // mandata 0 .. 1
        std::atomic<float> delay    { 0.0f };    // mandata 0 .. 1
        std::atomic<float> humanize { 0.15f };   // 0 .. 1

        std::atomic<int>   soundVersion     { 0 };      // la UI lo incrementa quando cambia pitch/decay/tone/drive

        // Campione dell'utente al posto della sintesi (nullptr = sintesi). Il puntatore resta valido:
        // i campioni vivono nel SampleStore per tutta la sessione.
        std::atomic<const SampleData*> sample { nullptr };
        juce::String sampleName;                        // usato solo dal thread della UI (salvataggio); "lib:<id>" = libreria
        std::atomic<bool>  previewRequested { false };
        std::atomic<int>   previewPitch     { -1 };     // nota MIDI dell'anteprima (piano roll), -1 = suono di base
        std::atomic<float> activity         { 0.0f };   // per far "lampeggiare" la UI quando suona
        std::atomic<int>   triggerCount     { 0 };      // colpi suonati in totale (diagnostica e test)

        void touchSound()  { soundVersion.fetch_add (1); }
    };

    /** Un pattern: per canale e step velocity, probabilita' e ratchet; e la sua lunghezza. */
    struct Pattern
    {
        using Row = std::array<std::atomic<uint8_t>, maxSteps>;
        std::array<Row, maxChannels> velocity;      // 0 = spento, 1..127
        std::array<Row, maxChannels> probability;   // 0..100 %
        std::array<Row, maxChannels> ratchet;       // 1..4 colpi nello step
        std::atomic<int> length { 16 };

        // Note del piano roll, impacchettate in 32 bit (0 = slot vuoto):
        // inizio 9 bit | durata 9 bit | altezza 7 bit | velocity 7 bit
        std::array<std::array<std::atomic<uint32_t>, maxNotes>, maxChannels> notes;
        std::array<std::atomic<int>, maxChannels> noteCount;   // se > 0 il canale usa il piano roll, non gli step
    };

    static uint32_t packNote (const NoteData& n)
    {
        return (uint32_t) (juce::jlimit (0, 511, n.start)) << 23
             | (uint32_t) (juce::jlimit (1, 511, n.length)) << 14
             | (uint32_t) (juce::jlimit (0, 127, n.pitch)) << 7
             | (uint32_t) (juce::jlimit (1, 127, n.velocity));
    }

    static NoteData unpackNote (uint32_t v)
    {
        return { (int) (v >> 23) & 511, (int) (v >> 14) & 511, (int) (v >> 7) & 127, (int) v & 127 };
    }

    /** Un pattern piazzato nella playlist: traccia, battuta d'inizio, quale pattern. */
    /**
        Un pattern piazzato nella playlist.
        - posizione: battuta + 'sub' sedicesimi (il bordo sinistro si puo' spostare dentro la battuta)
        - length: quanti step suona (0 = tutto il pattern). Piu' corto: si ferma li'; piu' lungo: il pattern si ripete
        - offset: da quale step del pattern parte (bordo sinistro tagliato)
    */
    struct Clip
    {
        int track = 0, bar = 0, pattern = 0, sub = 0, length = 0, offset = 0;
        int startStep() const                   { return bar * stepsPerBar + sub; }
        bool operator== (const Clip& o) const
        {
            return track == o.track && bar == o.bar && pattern == o.pattern && sub == o.sub && length == o.length && offset == o.offset;
        }
    };

    static uint64_t packClip (const Clip& c)
    {
        return 0x80000000ull
             | (uint64_t) juce::jlimit (0, numTracks - 1, c.track) << 20
             | (uint64_t) juce::jlimit (0, maxBars - 1, c.bar) << 8
             | (uint64_t) juce::jlimit (0, numPatterns - 1, c.pattern)
             | (uint64_t) juce::jlimit (0, 8191, c.length) << 32
             | (uint64_t) juce::jlimit (0, 63, c.offset) << 45
             | (uint64_t) juce::jlimit (0, 15, c.sub) << 51;
    }

    static Clip unpackClip (uint64_t v)
    {
        Clip c;
        c.track = (int) (v >> 20) & 0x7ff;
        c.bar = (int) (v >> 8) & 0xfff;
        c.pattern = (int) v & 0xff;
        c.length = (int) (v >> 32) & 0x1fff;
        c.offset = (int) (v >> 45) & 0x3f;
        c.sub = (int) (v >> 51) & 0xf;
        return c;
    }

    /** Quanti step suona davvero una clip (la sua lunghezza, o quella del pattern). */
    int getClipLength (const Clip& c) const
    {
        return c.length > 0 ? c.length : patterns[(size_t) juce::jlimit (0, numPatterns - 1, c.pattern)].length.load();
    }

    SequencerEngine()   { resetProject(); }

    //==============================================================================
    /** Progetto nuovo: tutto vuoto e valori di default. */
    void resetProject (bool keepMixerEffects = false)
    {
        for (int p = 0; p < numPatterns; ++p)
        {
            clearPattern (p);
            patternNames[(size_t) p].clear();
        }

        // Progetto nuovo: Kick, Snare, Clap, Hi-Hat, Open Hat, Tom + BassForge
        static const float defaultVolumes[defaultChannels] = { 0.8f, 0.8f, 0.7f, 0.6f, 0.55f, 0.75f, 0.8f };
        setNumChannels (defaultChannels);

        for (int c = 0; c < maxChannels; ++c)
            resetChannel (c, c < defaultChannels ? defaultVolumes[c] : 0.8f);

        for (int c = 0; c < numDrumTypes; ++c)
            makeDrumChannel (c, c);
        makeSynthChannel (defaultChannels - 1);

        // Mixer: ogni canale del progetto nuovo ha la sua traccia insert
        for (int t = 0; t < mixer::numTracks; ++t)
            resetTrack (t, keepMixerEffects);
        for (int c = 0; c < defaultChannels; ++c)
            channels[(size_t) c].mixerTrack.store (c + 1);

        bpm.store (120.0);
        swing.store (0.0f);
        scaleRoot.store (9);     // La
        scaleType.store (0);     // nessuna scala
        masterVolume.store (1.0f);
        masteringParams.reset();
        for (int l = 0; l < automation::maxLanes; ++l)
            removeLane (l);
        currentPattern.store (0);
        setClips ({});
        songMode.store (false);
        songStartBar.store (0);
    }

    void resetChannel (int c, float vol = 0.8f)
    {
        auto& ch = channels[(size_t) c];
        ch.volume.store (vol);
        ch.muted.store (false);
        ch.solo.store (false);
        ch.type.store (drumOrSample);
        ch.drumType.store (0);
        ch.colour.store (0xff8aa0b8);
        ch.name = "Canale";
        ch.customName = false;
        ch.instrument.store (nullptr);
        ch.instrumentKind.clear();
        ch.instrumentId = 0;
        ch.pendingState.clear();
        ch.kit.store (0);
        ch.pan.store (0.0f);
        ch.pitch.store (0.0f);
        ch.decay.store (1.0f);
        ch.tone.store (0.0f);
        ch.drive.store (0.0f);
        ch.filter.store (1.0f);
        ch.reverb.store (0.0f);
        ch.delay.store (0.0f);
        ch.humanize.store (0.15f);
        ch.mixerTrack.store (0);
        ch.arpOn.store (false); ch.arpMode.store (0); ch.arpRate.store (12); ch.arpOctaves.store (1); ch.arpGate.store (0.6f);
        ch.sample.store (nullptr);
        ch.sampleName.clear();
        ch.touchSound();
    }

    /** Trasforma un canale in uno strumento della batteria sintetizzata (senza campione). */
    void makeDrumChannel (int c, int drumType)
    {
        auto& ch = channels[(size_t) c];
        ch.type.store (drumOrSample);
        ch.drumType.store (juce::jlimit (0, numDrumTypes - 1, drumType));
        ch.sample.store (nullptr);
        ch.sampleName.clear();
        ch.colour.store (getDrumTypeInfo (drumType).colour);
        if (! ch.customName)
            ch.name = getDrumTypeInfo (drumType).name;
        ch.touchSound();
    }

    void makeSynthChannel (int c)   { makeInstrumentChannel (c, "bassforge", "BassForge", synthColour); }

    /** Trasforma un canale in uno strumento (lo strumento vero lo crea la UI: Instruments::Host). */
    void makeInstrumentChannel (int c, const juce::String& kind, const juce::String& displayName, juce::uint32 colour)
    {
        auto& ch = channels[(size_t) c];
        if (ch.instrumentKind != kind)
        {
            setInstrument (c, nullptr);
            ch.instrumentId = 0;
            ch.pendingState.clear();
        }
        ch.type.store (synth);
        ch.instrumentKind = kind;
        ch.sample.store (nullptr);
        ch.sampleName.clear();
        ch.colour.store (colour);
        if (! ch.customName)
            ch.name = displayName;
        ch.touchSound();
    }

    /**
        Collega uno strumento a un canale (nullptr = scollega). Lo prepara alla frequenza del motore.
        Dopo il ritorno il thread audio non usa piu' lo strumento precedente: si puo' distruggere.
    */
    void setInstrument (int c, juce::AudioProcessor* processor)
    {
        if (c < 0 || c >= maxChannels)
            return;

        if (processor != nullptr)
            prepareInstrument (*processor);

        const juce::SpinLock::ScopedLockType lock (structureLock);
        channels[(size_t) c].instrument.store (processor);
        instOffTicks[(size_t) c].fill (0);
        instActiveNotes[(size_t) c] = 0;
        instPreviewNote[(size_t) c] = -1;
        perf::wake();
        instMidi[(size_t) c].clear();
    }

    /** Aspetta che il thread audio finisca il blocco in corso (dopo, i puntatori vecchi sono liberi). */
    void waitForAudioBlock()    { const juce::SpinLock::ScopedLockType lock (structureLock); }

    struct LiveEvent { int channel = 0, note = 0; float velocity = 0.0f; };

    /** Nota suonata dal vivo (tastiera a schermo, pad, tastiera MIDI). velocity 0 = rilascio. */
    void liveNote (int channel, int note, float velocity)
    {
        const juce::SpinLock::ScopedLockType lock (liveWriteLock);   // scrivono la UI e il thread MIDI
        int s1, n1, s2, n2;
        liveFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0)
            return;
        liveEvents[(size_t) (n1 > 0 ? s1 : s2)] = { channel, note, velocity };
        liveFifo.finishedWrite (1);
    }

    //==============================================================================
    // Canali: quanti sono, aggiungi / clona / sposta / elimina

    int getNumChannels() const                  { return numActiveChannels.load(); }
    /** Per i test: voci del campionatore ancora in corso su un canale. */
    int getSamplerVoicesForTest (int c) const   { return samplers[(size_t) c].activeVoices(); }

    /** Il primo canale con uno strumento (-1 se non c'e'). */
    int getSynthChannel() const
    {
        for (int c = 0; c < getNumChannels(); ++c)
            if (channels[(size_t) c].type.load() == synth)
                return c;
        return -1;
    }

    bool isSynth (int channel) const            { return channel >= 0 && channel < maxChannels && channels[(size_t) channel].type.load() == synth; }

    /** La nota "naturale" del canale: quella a cui suona lo step (per il piano roll). */
    int getChannelRootNote (int c) const
    {
        if (isSynth (c))
            return channels[(size_t) c].stepNote.load();
        if (auto* smp = channels[(size_t) c].sample.load())
            return smp->rootNote;
        return rootNote;
    }

    /** Cambia il numero di canali (quelli in piu' vanno impostati dal chiamante). */
    void setNumChannels (int count)
    {
        const juce::SpinLock::ScopedLockType lock (structureLock);
        numActiveChannels.store (juce::jlimit (1, maxChannels, count));
        structureChanged.store (true);
        structureVersion.fetch_add (1);
    }

    /** Cambia ogni volta che i canali vengono aggiunti, tolti o spostati (la UI ricostruisce le righe). */
    int getStructureVersion() const             { return structureVersion.load(); }

    /** Aggiunge un canale in fondo (o dopo "after"), con le impostazioni di default. Ritorna l'indice o -1. */
    int addChannel (int after = -1)
    {
        const int n = getNumChannels();
        if (n >= maxChannels)
            return -1;

        const int index = after < 0 ? n : juce::jlimit (0, n, after + 1);
        std::vector<int> order;
        for (int c = 0; c < n; ++c)
        {
            if (c == index) order.push_back (-1);
            order.push_back (c);
        }
        if (index == n) order.push_back (-1);
        applyOrder (order);
        channels[(size_t) index].mixerTrack.store (findFreeInsert());
        return index;
    }

    /** Copia un canale (suono, regolazioni, step e note di tutti i pattern) subito sotto. */
    int duplicateChannel (int c)
    {
        const int n = getNumChannels();
        if (n >= maxChannels || c < 0 || c >= n)
            return -1;

        std::vector<int> order;
        for (int i = 0; i < n; ++i)
        {
            order.push_back (i);
            if (i == c) order.push_back (c);
        }
        applyOrder (order);

        auto& copy = channels[(size_t) c + 1];
        copy.solo.store (false);
        copy.name = channels[(size_t) c].name + " 2";
        copy.customName = true;
        return c + 1;
    }

    void removeChannel (int c)
    {
        const int n = getNumChannels();
        if (n <= 1 || c < 0 || c >= n)
            return;

        std::vector<int> order;
        for (int i = 0; i < n; ++i)
            if (i != c)
                order.push_back (i);
        applyOrder (order);
    }

    void moveChannel (int from, int to)
    {
        const int n = getNumChannels();
        if (from < 0 || from >= n || to < 0 || to >= n || from == to)
            return;

        std::vector<int> order;
        for (int i = 0; i < n; ++i)
            if (i != from)
                order.push_back (i);
        order.insert (order.begin() + to, from);
        applyOrder (order);
    }

    //==============================================================================
    // Mixer (thread della UI)

    mixer::Track& getTrack (int t)                  { return tracks[(size_t) juce::jlimit (0, mixer::numTracks - 1, t)]; }
    const mixer::Track& getTrack (int t) const      { return tracks[(size_t) juce::jlimit (0, mixer::numTracks - 1, t)]; }
    int getChannelTrack (int c) const               { return channels[(size_t) c].mixerTrack.load(); }
    void setChannelTrack (int c, int t)             { channels[(size_t) c].mixerTrack.store (juce::jlimit (0, mixer::numInserts, t)); }

    /** La traccia non e' mai stata toccata (nessun effetto, volume e EQ di default, nessun nome). */
    bool isTrackDefault (int t) const
    {
        auto& tr = getTrack (t);
        for (auto& s : tr.slots)
            if (s.type.load() != mixer::none)
                return false;
        return std::abs (tr.volume.load() - 1.0f) < 0.0001f && tr.pan.load() == 0.0f && ! tr.muted.load() && ! tr.solo.load()
            && tr.eqLow.load() == 0.0f && tr.eqMid.load() == 0.0f && tr.eqHigh.load() == 0.0f && tr.name.isEmpty();
    }

    bool isTrackUsed (int t) const
    {
        if (t == 0) return true;
        for (int c = 0; c < getNumChannels(); ++c)
            if (channels[(size_t) c].mixerTrack.load() == t)
                return true;
        return ! isTrackDefault (t);
    }

    /** La prima traccia insert libera (nessun canale e nessuna regolazione); 0 = nessuna, va al Master. */
    int findFreeInsert() const
    {
        for (int t = 1; t <= mixer::numInserts; ++t)
            if (! isTrackUsed (t))
                return t;
        return 0;
    }

    juce::String getTrackName (int t) const
    {
        if (t <= 0) return "Master";
        const auto& tr = getTrack (t);
        if (tr.name.isNotEmpty()) return tr.name;
        for (int c = 0; c < getNumChannels(); ++c)
            if (channels[(size_t) c].mixerTrack.load() == t)
                return channels[(size_t) c].name;
        return "Insert " + juce::String (t);
    }

    juce::Colour getTrackColour (int t) const
    {
        if (t <= 0) return juce::Colour (0xffff9a3c);
        if (const auto col = getTrack (t).colour; col != 0) return juce::Colour (col);
        for (int c = 0; c < getNumChannels(); ++c)
            if (channels[(size_t) c].mixerTrack.load() == t)
                return juce::Colour (channels[(size_t) c].colour.load());
        return juce::Colour (0xff6c7a89);
    }

    /** Mette un effetto in uno slot (none = lo toglie). L'effetto si crea qui, fuori dal thread audio. */
    void setTrackEffect (int t, int slot, int type)
    {
        auto& s = getTrack (t).slots[(size_t) juce::jlimit (0, mixer::numSlots - 1, slot)];
        type = juce::jlimit (0, mixer::numFxTypes - 1, type);
        if (s.type.load() == type && (type == mixer::none) == (s.owned == nullptr))
        {
            resetEffectParams (s, type);
            return;
        }

        auto fx = mixer::createEffect (type);
        if (fx != nullptr)
            fx->prepare (sampleRate.load());

        std::unique_ptr<mixer::Effect> old;
        {
            const juce::SpinLock::ScopedLockType lock (structureLock);
            resetEffectParams (s, type);
            s.type.store (type);
            s.bypass.store (false);
            old = std::move (s.owned);
            s.owned = std::move (fx);
            s.effect = s.owned.get();
        }
    }   // il vecchio effetto viene distrutto qui, quando il thread audio non lo usa piu'

    /** Scambia due slot di una traccia (per spostare un effetto su o giu'). */
    void swapTrackEffects (int t, int a, int b)
    {
        if (a == b || a < 0 || b < 0 || a >= mixer::numSlots || b >= mixer::numSlots)
            return;
        auto& tr = getTrack (t);
        auto& x = tr.slots[(size_t) a];
        auto& y = tr.slots[(size_t) b];
        const juce::SpinLock::ScopedLockType lock (structureLock);
        const int tx = x.type.load(); x.type.store (y.type.load()); y.type.store (tx);
        const bool bx = x.bypass.load(); x.bypass.store (y.bypass.load()); y.bypass.store (bx);
        for (int p = 0; p < mixer::maxParams; ++p)
        {
            const float v = x.params[(size_t) p].load();
            x.params[(size_t) p].store (y.params[(size_t) p].load());
            y.params[(size_t) p].store (v);
        }
        std::swap (x.owned, y.owned);
        for (auto& l : lanes)   // le automazioni degli effetti seguono lo slot
            if (l.kind.load() == automation::effectParam && l.a.load() == t)
            {
                if (l.b.load() == a) l.b.store (b);
                else if (l.b.load() == b) l.b.store (a);
            }
        x.effect = x.owned.get();
        y.effect = y.owned.get();
    }

    void resetTrack (int t, bool keepEffects = false)
    {
        auto& tr = getTrack (t);
        tr.volume.store (1.0f); tr.pan.store (0.0f);
        tr.muted.store (false); tr.solo.store (false);
        tr.eqLow.store (0.0f); tr.eqMid.store (0.0f); tr.eqMidFreq.store (1000.0f); tr.eqHigh.store (0.0f);
        tr.name.clear();
        tr.colour = 0;
        if (! keepEffects)
            for (int s = 0; s < mixer::numSlots; ++s)
                setTrackEffect (t, s, mixer::none);
    }

    //==============================================================================
    // Scala del progetto (per il piano roll: righe evidenziate, accordi, progressioni)

    int getScaleRoot() const                { return scaleRoot.load(); }
    int getScaleType() const                { return scaleType.load(); }
    void setScale (int root, int type)      { scaleRoot.store (((root % 12) + 12) % 12); scaleType.store (juce::jmax (0, type)); }

    //==============================================================================
    // Registrazione dal vivo: le note suonate mentre il pattern gira finiscono nel pattern

    struct RecordedNote { int channel = 0, pattern = 0, start = 0, length = 0, pitch = 60, velocity = 100; };

    void setRecording (bool on)         { recording.store (on); }
    bool isRecording() const            { return recording.load(); }

    /** Le note registrate dal thread audio, da mettere nel pattern (thread della UI). */
    std::vector<RecordedNote> popRecordedNotes()
    {
        std::vector<RecordedNote> out;
        int s1, n1, s2, n2;
        recFifo.prepareToRead (recFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) out.push_back (recEvents[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) out.push_back (recEvents[(size_t) (s2 + i)]);
        recFifo.finishedRead (n1 + n2);
        return out;
    }

    //==============================================================================
    // Automazioni (thread della UI)

    automation::Lane& getLane (int i)               { return lanes[(size_t) juce::jlimit (0, automation::maxLanes - 1, i)]; }
    const automation::Lane& getLane (int i) const   { return lanes[(size_t) juce::jlimit (0, automation::maxLanes - 1, i)]; }
    bool isLaneUsed (int i) const                   { return getLane (i).kind.load() != automation::none; }

    /** Nuova automazione (o quella che c'e' gia' per lo stesso bersaglio). -1 = piene. */
    int addLane (const automation::Target& t)
    {
        for (int i = 0; i < automation::maxLanes; ++i)
            if (isLaneUsed (i) && getLane (i).target() == t)
                return i;
        for (int i = 0; i < automation::maxLanes; ++i)
            if (! isLaneUsed (i))
            {
                auto& l = lanes[(size_t) i];
                l.count.store (0);
                l.enabled.store (true);
                l.setTarget (t);
                return i;
            }
        return -1;
    }

    void removeLane (int i)
    {
        auto& l = getLane (i);
        l.kind.store (automation::none);
        l.count.store (0);
        l.enabled.store (true);
    }

    std::vector<automation::Point> getLanePoints (int i) const
    {
        std::vector<automation::Point> pts;
        const auto& l = getLane (i);
        for (int k = 0; k < juce::jmin (l.count.load(), automation::maxPoints); ++k)
            pts.push_back (automation::unpack (l.points[(size_t) k].load()));
        return pts;
    }

    /** Sostituisce i punti (vengono ordinati nel tempo; due punti sullo stesso tick: vince l'ultimo). */
    void setLanePoints (int i, std::vector<automation::Point> pts)
    {
        std::stable_sort (pts.begin(), pts.end(), [] (auto& x, auto& y) { return x.tick < y.tick; });
        std::vector<automation::Point> clean;
        for (auto& p : pts)
        {
            p.tick = juce::jlimit (0, maxBars * stepsPerBar * ticksPerStep, p.tick);
            p.value = juce::jlimit (0.0f, 1.0f, p.value);
            if (! clean.empty() && clean.back().tick == p.tick) clean.back() = p;
            else clean.push_back (p);
        }
        if ((int) clean.size() > automation::maxPoints)
            clean.resize ((size_t) automation::maxPoints);

        auto& l = getLane (i);
        l.count.store (0);   // il thread audio non legge punti a meta'
        for (size_t k = 0; k < clean.size(); ++k)
            l.points[k].store (automation::pack (clean[k]));
        l.count.store ((int) clean.size());
    }

    /** Ferma subito ogni suono: note dei synth, campioni, code di riverbero e delay (pulsante "panico"). */
    void panic()                                    { playing.store (false); panicRequested.store (true); }

    /** Il mastering (impostazioni lette dal thread audio; risultati dell'analisi per la UI). */
    mastering::Params& getMastering()               { return masteringParams; }
    const mastering::Params& getMastering() const   { return masteringParams; }

    float takeTrackPeak (int t)
    {
        auto& tr = getTrack (t);
        return juce::jmax (tr.peakL.exchange (0.0f), tr.peakR.exchange (0.0f));
    }

    //==============================================================================
    // Trasporto

    void setPlaying (bool shouldPlay)
    {
        if (shouldPlay && ! playing.load())
        {
            playingPattern.store (currentPattern.load());
            restartRequested.store (true);
        }

        playing.store (shouldPlay);
    }

    //==============================================================================
    // Playlist (modalita' canzone)

    /** PAT = ripete il pattern selezionato; SONG = suona la playlist. */
    void setSongMode (bool shouldUseSong)
    {
        if (shouldUseSong != songMode.load())
        {
            songMode.store (shouldUseSong);
            restartRequested.store (true);
        }
    }

    bool isSongMode() const                     { return songMode.load(); }

    std::vector<Clip> getClips() const
    {
        std::vector<Clip> result;
        for (auto& slot : clips)
            if (const auto v = slot.load(); v != 0)
                result.push_back (unpackClip (v));
        return result;
    }

    /** Sostituisce tutta la playlist (le clip oltre maxClips vengono ignorate). */
    void setClips (const std::vector<Clip>& newClips)
    {
        int count = 0;
        for (auto& c : newClips)
            if (count < maxClips)
                clips[(size_t) count++].store (packClip (c));
        for (int i = count; i < maxClips; ++i)
            clips[(size_t) i].store (0);
    }

    /** Lunghezza della canzone in step (fine dell'ultima clip). */
    int getSongLengthSteps() const
    {
        int end = 0;
        for (auto& slot : clips)
            if (const auto v = slot.load(); v != 0)
            {
                const auto c = unpackClip (v);
                end = juce::jmax (end, c.startStep() + getClipLength (c));
            }
        return end;
    }

    /** Da quale battuta parte la canzone (tocco sul righello della playlist). Se sta suonando, salta li'. */
    void setSongPosition (int bar)
    {
        songStartBar.store (juce::jlimit (0, maxBars - 1, bar));
        if (playing.load() && songMode.load())
            restartRequested.store (true);
    }

    int getSongStartBar() const                 { return songStartBar.load(); }

    /** Step della canzone che sta suonando (-1 se fermo o in modalita' pattern). */
    int getSongStep() const                     { return playing.load() && songMode.load() ? currentSongStep.load() : -1; }

    bool isPlaying() const                      { return playing.load(); }

    void setBpm (double newBpm)                 { bpm.store (juce::jlimit (40.0, 240.0, newBpm)); }
    double getBpm() const                       { return bpm.load(); }

    /** Swing 0..1 (0 = dritto, 1 = massimo). Ritarda gli step dispari. */
    void setSwing (float s)                     { swing.store (juce::jlimit (0.0f, 1.0f, s)); }
    float getSwing() const                      { return swing.load(); }

    void setMasterVolume (float v)              { masterVolume.store (juce::jlimit (0.0f, 1.5f, v)); }
    float getMasterVolume() const               { return masterVolume.load(); }

    void setMetronome (bool on)                 { metronome.store (on); }
    bool isMetronomeOn() const                  { return metronome.load(); }

    //==============================================================================
    // Pattern

    /** Sceglie il pattern da modificare. Se sta suonando, la riproduzione ci passa alla fine del giro. */
    void selectPattern (int index)
    {
        currentPattern.store (juce::jlimit (0, numPatterns - 1, index));
        if (! playing.load())
            playingPattern.store (currentPattern.load());
    }

    /** Modalita' live: il pattern scelto parte al prossimo step invece che a fine giro. */
    void switchPatternNow()                     { switchNow.store (true); }

    int getCurrentPattern() const               { return currentPattern.load(); }
    int getPlayingPattern() const               { return playingPattern.load(); }

    Pattern& getPattern (int index)             { return patterns[(size_t) index]; }
    Pattern& current()                          { return patterns[(size_t) currentPattern.load()]; }
    const Pattern& current() const              { return patterns[(size_t) currentPattern.load()]; }

    int getPatternLength() const                { return current().length.load(); }
    void setPatternLength (int len)             { current().length.store (len > 16 ? 32 : 16); }

    int getStepVelocity (int channel, int step) const      { return current().velocity[(size_t) channel][(size_t) step].load(); }
    void setStepVelocity (int channel, int step, int v)    { current().velocity[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (0, 127, v)); }
    bool isStepOn (int channel, int step) const            { return getStepVelocity (channel, step) > 0; }

    int getStepProbability (int channel, int step) const   { return current().probability[(size_t) channel][(size_t) step].load(); }
    void setStepProbability (int channel, int step, int p) { current().probability[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (0, 100, p)); }

    int getStepRatchet (int channel, int step) const       { return current().ratchet[(size_t) channel][(size_t) step].load(); }
    void setStepRatchet (int channel, int step, int r)     { current().ratchet[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (1, maxRatchet, r)); }

    void clearStep (Pattern& p, int c, int s)
    {
        p.velocity[(size_t) c][(size_t) s].store (0);
        p.probability[(size_t) c][(size_t) s].store (100);
        p.ratchet[(size_t) c][(size_t) s].store (1);
    }

    void clearPattern (int index)
    {
        auto& p = patterns[(size_t) index];
        for (int c = 0; c < maxChannels; ++c)
        {
            for (int s = 0; s < maxSteps; ++s)
                clearStep (p, c, s);

            for (auto& n : p.notes[(size_t) c])
                n.store (0);
            p.noteCount[(size_t) c].store (0);
        }
        p.length.store (16);
    }

    void copyStep (Pattern& src, int srcC, int srcS, Pattern& dst, int dstC, int dstS)
    {
        dst.velocity[(size_t) dstC][(size_t) dstS].store (src.velocity[(size_t) srcC][(size_t) srcS].load());
        dst.probability[(size_t) dstC][(size_t) dstS].store (src.probability[(size_t) srcC][(size_t) srcS].load());
        dst.ratchet[(size_t) dstC][(size_t) dstS].store (src.ratchet[(size_t) srcC][(size_t) srcS].load());
    }

    void copyPattern (int from, int to)
    {
        if (from == to)
            return;

        auto& src = patterns[(size_t) from];
        auto& dst = patterns[(size_t) to];

        for (int c = 0; c < maxChannels; ++c)
        {
            for (int s = 0; s < maxSteps; ++s)
                copyStep (src, c, s, dst, c, s);

            for (int i = 0; i < maxNotes; ++i)
                dst.notes[(size_t) c][(size_t) i].store (src.notes[(size_t) c][(size_t) i].load());
            dst.noteCount[(size_t) c].store (src.noteCount[(size_t) c].load());
        }

        dst.length.store (src.length.load());
    }

    /** Quanti pattern sono in uso: l'ultimo pattern non vuoto + 1 (almeno 1). */
    int getUsedPatternCount() const
    {
        for (int p = numPatterns - 1; p > 0; --p)
            if (! isPatternEmpty (p))
                return p + 1;
        return 1;
    }

    /** Il primo pattern vuoto dopo quelli usati (-1 se sono tutti pieni). */
    int getFirstFreePattern() const
    {
        for (int p = 0; p < numPatterns; ++p)
            if (isPatternEmpty (p) && patternNames[(size_t) p].isEmpty() && p != currentPattern.load())
                return p;
        return -1;
    }

    juce::String getPatternName (int index) const
    {
        const auto& n = patternNames[(size_t) juce::jlimit (0, numPatterns - 1, index)];
        return n.isNotEmpty() ? n : "Pattern " + juce::String (index + 1);
    }
    juce::String getCustomPatternName (int index) const        { return patternNames[(size_t) juce::jlimit (0, numPatterns - 1, index)]; }
    void setPatternName (int index, const juce::String& name)  { patternNames[(size_t) juce::jlimit (0, numPatterns - 1, index)] = name.trim(); }

    bool isPatternEmpty (int index) const
    {
        for (int c = 0; c < getNumChannels(); ++c)
        {
            for (auto& v : patterns[(size_t) index].velocity[(size_t) c])
                if (v.load() > 0)
                    return false;

            if (patterns[(size_t) index].noteCount[(size_t) c].load() > 0)
                return false;
        }

        return true;
    }

    //==============================================================================
    // Piano roll (pattern corrente)

    std::vector<NoteData> getNotes (int channel) const   { return getNotes (currentPattern.load(), channel); }

    std::vector<NoteData> getNotes (int pattern, int channel) const
    {
        std::vector<NoteData> result;
        for (auto& slot : patterns[(size_t) pattern].notes[(size_t) channel])
            if (const auto v = slot.load(); v != 0)
                result.push_back (unpackNote (v));
        return result;
    }

    /** Sostituisce tutte le note di un canale nel pattern corrente (quelle in piu' oltre maxNotes vengono ignorate). */
    void setNotes (int channel, const std::vector<NoteData>& newNotes)   { setNotes (currentPattern.load(), channel, newNotes); }

    void setNotes (int pattern, int channel, const std::vector<NoteData>& newNotes)
    {
        auto& p = patterns[(size_t) pattern];
        int count = 0;

        for (auto& n : newNotes)
            if (count < maxNotes && n.length > 0)
                p.notes[(size_t) channel][(size_t) count++].store (packNote (n));

        for (int i = count; i < maxNotes; ++i)
            p.notes[(size_t) channel][(size_t) i].store (0);

        p.noteCount[(size_t) channel].store (count);
    }

    bool hasNotes (int channel) const   { return current().noteCount[(size_t) channel].load() > 0; }

    /** Posizione di riproduzione in tick dall'inizio del pattern (-1 se fermo). */
    int getCurrentTick() const          { return playing.load() ? currentTick.load() : -1; }

    //==============================================================================
    // Strumenti per una riga del pattern corrente

    /** Sposta tutti gli step di una riga (con rientro dall'altro lato). */
    void shiftRow (int channel, int amount)
    {
        auto& p = current();
        const int len = p.length.load();
        Pattern::Row v, pr, r;

        for (int s = 0; s < len; ++s)
        {
            const int from = ((s - amount) % len + len) % len;
            v[(size_t) s].store (p.velocity[(size_t) channel][(size_t) from].load());
            pr[(size_t) s].store (p.probability[(size_t) channel][(size_t) from].load());
            r[(size_t) s].store (p.ratchet[(size_t) channel][(size_t) from].load());
        }

        for (int s = 0; s < len; ++s)
        {
            p.velocity[(size_t) channel][(size_t) s].store (v[(size_t) s].load());
            p.probability[(size_t) channel][(size_t) s].store (pr[(size_t) s].load());
            p.ratchet[(size_t) channel][(size_t) s].store (r[(size_t) s].load());
        }
    }

    void reverseRow (int channel)
    {
        auto& p = current();
        const int len = p.length.load();

        for (int a = 0, b = len - 1; a < b; ++a, --b)
        {
            auto swap = [&] (Pattern::Row& row)
            {
                const auto t = row[(size_t) a].load();
                row[(size_t) a].store (row[(size_t) b].load());
                row[(size_t) b].store (t);
            };

            swap (p.velocity[(size_t) channel]);
            swap (p.probability[(size_t) channel]);
            swap (p.ratchet[(size_t) channel]);
        }
    }

    void clearRow (int channel)
    {
        for (int s = 0; s < maxSteps; ++s)
            clearStep (current(), channel, s);
    }

    /** Accende uno step ogni "every" (es. 4 = un colpo per quarto), partendo da "offset". */
    void fillRow (int channel, int every, int offset = 0)
    {
        auto& p = current();
        for (int s = 0; s < p.length.load(); ++s)
            if ((s - offset) % every == 0 && s >= offset)
                p.velocity[(size_t) channel][(size_t) s].store ((uint8_t) defaultVelocity);
    }

    void copyRow (int fromChannel, int toChannel)
    {
        for (int s = 0; s < maxSteps; ++s)
            copyStep (current(), fromChannel, s, current(), toChannel, s);
    }

    //==============================================================================
    // Canali

    Channel& getChannel (int index)             { return channels[(size_t) index]; }

    /** Fa sentire lo strumento. Senza altezza: suono "di base" (per il synth la nota degli step). */
    /** Fa sentire lo strumento. midiNote < 0: suono "di base" (lo step); altrimenti quella nota (piano roll). */
    void previewChannel (int channel, int midiNote = -1)
    {
        if (channel < 0 || channel >= maxChannels)
            return;
        channels[(size_t) channel].previewPitch.store (midiNote);
        channels[(size_t) channel].previewRequested.store (true);
    }

    void setChannelKit (int channel, int kit)   { channels[(size_t) channel].kit.store (juce::jlimit (0, SoundLibrary::numKits - 1, kit)); }
    int getChannelKit (int channel) const       { return channels[(size_t) channel].kit.load(); }

    void setAllKits (int kit)
    {
        for (int c = 0; c < getNumChannels(); ++c)
            if (isDrumSynthChannel (c))
                setChannelKit (c, kit);
    }

    /** Canale della batteria sintetizzata (non synth, senza campione). */
    bool isDrumSynthChannel (int c) const
    {
        return channels[(size_t) c].type.load() == drumOrSample && channels[(size_t) c].sample.load() == nullptr;
    }

    /** Fa sentire un campione della libreria (browser dei suoni), senza effetti del canale. */
    void previewSample (const SampleData* sample)
    {
        browserSample.store (sample);
        browserPreview.store (1);
    }

    /** Fa sentire uno strumento della batteria sintetizzata (browser dei suoni). */
    void previewDrum (int drumType, int kit)
    {
        browserDrumType.store (drumType);
        browserKit.store (kit);
        browserPreview.store (2);
    }

    //==============================================================================
    // Synth BassForge (un AudioProcessor JUCE ospitato da JLoop)

    /** Lo strumento del canale (nullptr se non e' un canale strumento o non e' ancora creato). */
    juce::AudioProcessor* getInstrument (int c) const
    {
        return c >= 0 && c < maxChannels ? channels[(size_t) c].instrument.load() : nullptr;
    }

    //==============================================================================
    // Stato per la UI

    /** Lo step che sta suonando adesso (-1 se fermo). */
    int getCurrentStep() const                  { return playing.load() ? currentStep.load() : -1; }

    /** Picco d'uscita dall'ultima lettura (0..1), per il meter. */
    float takeOutputPeak()                      { return outputPeak.exchange (0.0f); }
    double getSampleRateForDisplay() const      { return sampleRate.load(); }

    /** Gli ultimi n campioni in uscita (mono), per lo spettro del display. Solo per disegnare. */
    void copyScope (float* dest, int n) const
    {
        n = juce::jmin (n, scopeSize);
        const uint32_t end = scopeWrite.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = scope[(size_t) ((end - (uint32_t) n + (uint32_t) i) & (uint32_t) (scopeSize - 1))];
    }

    /** Quanto sta lavorando il limiter (0 = niente, 1 = molto), per un indicatore. */
    float getLimiterReduction() const           { return limiterReduction.load(); }

    //==============================================================================
    // Callback audio

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        prepare (device->getCurrentSampleRate());
    }

    /** Prepara il motore a una frequenza di campionamento (audio dal vivo o esportazione). */
    void prepare (double sr)
    {
        const juce::SpinLock::ScopedLockType lock (structureLock);
        sampleRate.store (sr);
        restartRequested.store (true);

        resetChannelVoices (sr);
        browserSampler.prepare (sr);
        browserDrum.prepare (sr);

        // Preparazione degli effetti (qui si puo' allocare: non siamo ancora nel callback)
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.62f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        reverb.setParameters (rp);
        reverb.setSampleRate (sr);
        reverb.reset();

        delayBuffer[0].assign ((size_t) (sr * 2.0) + 1, 0.0f);
        delayBuffer[1].assign ((size_t) (sr * 2.0) + 1, 0.0f);
        delayWrite = 0;
        delayLp[0] = delayLp[1] = 0.0f;

        lookahead = juce::jlimit (1, (int) limiterDelay[0].size() - 1, (int) std::round (0.0015 * sr));
        for (auto& b : limiterDelay) b.fill (0.0f);
        limiterPos = 0;
        limiterGain = 1.0f;
        limiterHold = 0;
        limiterRelease = (float) std::exp (-1.0 / (0.15 * sr));
        limiterHoldLength = (int) (0.025 * sr);
        limiterAttack  = (float) std::exp (-1.0 / (0.0005 * sr));

        metroPhase = 0.0; metroEnv = 0.0f;

        masteringChain.prepare (sr);

        // Mixer
        for (int t = 0; t < mixer::numTracks; ++t)
        {
            trackEq[(size_t) t].reset();
            for (auto& s : tracks[(size_t) t].slots)
                if (s.effect != nullptr)
                    s.effect->prepare (sr);
        }

        // Strumenti: si preparano come farebbe una DAW con i plugin
        instBuffer.setSize (2, chunk);
        for (int c = 0; c < maxChannels; ++c)
        {
            instMidi[(size_t) c].ensureSize (4096);
            instMidi[(size_t) c].clear();
            instOffTicks[(size_t) c].fill (0);
            instActiveNotes[(size_t) c] = 0;
            instPreviewNote[(size_t) c] = -1;
            if (auto* p = channels[(size_t) c].instrument.load())
                prepareInstrumentAt (*p, sr);
        }
        preparedRate.store (sr);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        // Misura quanto tempo serve per ogni blocco (rapporto prestazioni)
        const auto t0 = juce::Time::getHighResolutionTicks();
        process (outputChannelData, numOutputChannels, numSamples);
        const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        const double available = (double) numSamples / juce::jmax (1.0, sampleRate.load());
        const float load = (float) (used / available);
        perfBlocks.fetch_add (1, std::memory_order_relaxed);
        perfLoadSum.store (perfLoadSum.load (std::memory_order_relaxed) + load, std::memory_order_relaxed);
        if (load > perfLoadMax.load (std::memory_order_relaxed)) perfLoadMax.store (load, std::memory_order_relaxed);
        if (load > 0.8f) perfOverruns.fetch_add (1, std::memory_order_relaxed);
        if (lockMissedThisBlock)
        {
            perfLockMisses.fetch_add (1, std::memory_order_relaxed);   // blocco saltato (silenzio = un "tic")
            lockMissedThisBlock = false;
        }
        perfBlockSize.store (numSamples, std::memory_order_relaxed);
    }

    /** Statistiche del processore audio dall'ultima lettura (per il rapporto prestazioni). */
    struct PerfStats { int blocks = 0, overruns = 0, lockMisses = 0, blockSize = 0; float avgLoad = 0.0f, maxLoad = 0.0f; };
    PerfStats takePerfStats()
    {
        PerfStats s;
        s.blocks = perfBlocks.exchange (0);
        s.avgLoad = s.blocks > 0 ? perfLoadSum.exchange (0.0f) / (float) s.blocks : 0.0f;
        s.maxLoad = perfLoadMax.exchange (0.0f);
        s.overruns = perfOverruns.exchange (0);
        s.lockMisses = perfLockMisses.exchange (0);
        s.blockSize = perfBlockSize.load();
        return s;
    }

    /** Voci in uso negli strumenti e nei campioni (per il rapporto). */
    int countActiveSamplerVoices() const
    {
        int n = 0;
        for (int c = 0; c < getNumChannels(); ++c) n += samplers[(size_t) c].activeVoices();
        return n;
    }

    /** Calcola il prossimo blocco audio (stereo: canali pari = sinistra, dispari = destra). */
    void process (float* const* outputChannelData, int numOutputChannels, int numSamples)
    {
        // Numeri "denormali" (code di riverbero e filtri che sfumano verso lo zero) rallentano moltissimo
        // alcuni processori: con questo diventano zero e il carico resta costante (niente scatti)
        juce::ScopedNoDenormals noDenormals;

        // Se la UI sta riordinando i canali (pochi microsecondi), questo blocco resta in silenzio
        const juce::SpinLock::ScopedTryLockType lock (structureLock);
        if (! lock.isLocked())
        {
            lockMissedThisBlock = true;
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    juce::FloatVectorOperations::clear (out, numSamples);
            return;
        }

        const double sr = sampleRate.load();

        if (structureChanged.exchange (false))
            resetChannelVoices (sr);

        inSong = songMode.load();
        if (inSong)
            songLength = getSongLengthSteps();

        if (restartRequested.exchange (false))
        {
            stepIndex = -1;
            songStep = songStartBar.load() * stepsPerBar - 1;
            if (songStep + 1 >= songLength) songStep = -1;   // oltre la fine: dall'inizio
            tickInStep = 0;
            samplesUntilNextTick = 0.0;   // il primo step parte subito
            numActiveClips = 0;
        }

        numActive = numActiveChannels.load();
        for (int c = 0; c < numActive; ++c)
        {
            const auto& ch = channels[(size_t) c];
            instruments[(size_t) c] = ch.type.load() == synth ? ch.instrument.load() : nullptr;
        }

        // Note dalla tastiera a schermo
        {
            int s1, n1, s2, n2;
            const int ready = liveFifo.getNumReady();
            liveFifo.prepareToRead (ready, s1, n1, s2, n2);
            auto handle = [this] (const LiveEvent& ev)
            {
                if (ev.channel < 0 || ev.channel >= numActive || ev.note < 0 || ev.note > 127)
                    return;
                recordLiveEvent (ev);
                if (instruments[(size_t) ev.channel] == nullptr)
                {
                    // Campioni e batteria: il colpo parte subito (il rilascio non serve)
                    if (ev.velocity > 0.0f && channels[(size_t) ev.channel].type.load() != synth)
                        triggerChannel (ev.channel, juce::jlimit (0.05f, 1.0f, ev.velocity), 0.0f, ev.note, -1, true);
                    return;
                }
                if (channels[(size_t) ev.channel].arpOn.load() && playing.load())
                {
                    // Dal vivo con l'arpeggiatore: tenuta finche' non si alza il dito
                    arpHeld[(size_t) ev.channel][(size_t) ev.note] = ev.velocity > 0.0f ? arpLiveHold : 0;
                    arpVelocity[(size_t) ev.channel][(size_t) ev.note] = ev.velocity;
                    return;
                }
                auto& midi = instMidi[(size_t) ev.channel];
                if (ev.velocity > 0.0f)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, ev.note, juce::jlimit (0.01f, 1.0f, ev.velocity)), 0);
                    channels[(size_t) ev.channel].activity.store (ev.velocity);
                }
                else
                    midi.addEvent (juce::MidiMessage::noteOff (1, ev.note), 0);
            };
            for (int i = 0; i < n1; ++i) handle (liveEvents[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) handle (liveEvents[(size_t) (s2 + i)]);
            liveFifo.finishedRead (n1 + n2);
        }

        updateVoiceParams (sr);

        if (panicRequested.exchange (false))
        {
            for (int c = 0; c < numActive; ++c)
            {
                if (auto* proc = instruments[(size_t) c])
                {
                    instAllNotesOff (c);
                    instMidi[(size_t) c].clear();
                    proc->reset();   // silenzio immediato (voci ed effetti interni)
                }
                samplers[(size_t) c].releaseAll();
            }
            browserSampler.stopAll();
            for (auto& tr : tracks)
                for (auto& s : tr.slots)
                    if (s.effect != nullptr)
                        s.effect->reset();
            reverb.reset();
            for (auto& d : delayBuffer) std::fill (d.begin(), d.end(), 0.0f);
            masteringChain.reset();
        }

        // Stop: spegne tutte le note degli strumenti e sfuma i campioni tenuti
        if (wasPlaying && ! playing.load())
            flushRecording();

        if (wasPlaying && ! playing.load())
        {
            for (int c = 0; c < numActive; ++c)
                if (instruments[(size_t) c] != nullptr)
                    instAllNotesOff (c);
            for (int c = 0; c < numActive; ++c)
                samplers[(size_t) c].releaseAll();
        }
        wasPlaying = playing.load();

        playHead.bpm.store (bpm.load());
        playHead.playing.store (playing.load());
        currentSample = 0;

        // Anteprime richieste dalla UI (tocco sul nome del canale, piano roll, browser dei suoni)
        for (int c = 0; c < numActive; ++c)
            if (channels[(size_t) c].previewRequested.exchange (false))
            {
                const int note = channels[(size_t) c].previewPitch.load();
                if (instruments[(size_t) c] != nullptr)
                    instPreview (c, note < 0 ? channels[(size_t) c].stepNote.load() : note);
                else
                    triggerChannel (c, 0.9f, 0.0f, note, -1, true);
            }

        if (const int what = browserPreview.exchange (0); what != 0)
        {
            browserSampler.stopAll();
            if (what == 1)
            {
                browserSampler.setSample (browserSample.load(), {});
                browserSampler.trigger (0.9f, 0.0f, -1, 0.0f, 0.0f, 0.0f);
            }
            else
            {
                browserDrum.setParams (SoundLibrary::get (browserDrumType.load(), browserKit.load()));
                browserDrum.setSample (nullptr, {});
                browserDrum.trigger (0.9f);
            }
        }

        // Automazioni (in modalita' SONG, mentre suona): muovono i controlli prima di leggerli
        if (inSong && playing.load() && songStep >= 0)
            applyAutomation ((double) songStep * ticksPerStep + tickInStep);

        // Parametri del mix letti una volta per blocco.
        bool anySolo = false;
        for (int c = 0; c < numActive; ++c)
            anySolo = anySolo || channels[(size_t) c].solo.load();

        for (int c = 0; c < numActive; ++c)
        {
            auto& ch = channels[(size_t) c];
            const bool silent = ch.muted.load() || (anySolo && ! ch.solo.load());
            const float g = silent ? 0.0f : ch.volume.load();
            const float angle = (ch.pan.load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            gainL[(size_t) c] = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
            gainR[(size_t) c] = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;
            sendRev[(size_t) c] = g * ch.reverb.load();
            sendDel[(size_t) c] = g * ch.delay.load();
            humanizeAmt[(size_t) c] = ch.humanize.load();
        }

        // Mixer: la traccia di ogni canale, e se c'e' una traccia in solo
        for (int c = 0; c < numActive; ++c)
            trackOf[(size_t) c] = juce::jlimit (0, mixer::numInserts, channels[(size_t) c].mixerTrack.load());
        anyTrackSolo = false;
        for (int t = 1; t < mixer::numTracks; ++t)
            anyTrackSolo = anyTrackSolo || tracks[(size_t) t].solo.load();

        // Elaborazione a blocchi da massimo 'chunk' campioni (gli effetti lavorano su buffer)
        int done = 0;
        while (done < numSamples)
        {
            const int n = juce::jmin (chunk, numSamples - done);
            renderChunk (sr, n);

            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    juce::FloatVectorOperations::copy (out + done, (ch % 2 == 0 ? mixL : mixR).data(), n);

            done += n;
        }
    }

private:
    static constexpr int chunk = 256;

    //==============================================================================
    /** Le impostazioni di un canale e le sue righe nei pattern, copiate per riordinare i canali. */
    struct ChannelCopy
    {
        float volume, pan, pitch, decay, tone, drive, filter, reverb, delay, humanize;
        bool muted, solo, customName;
        int type, drumType, kit;
        juce::uint32 colour;
        const SampleData* sample;
        juce::AudioProcessor* instrument;
        juce::String name, sampleName, instrumentKind, pendingState;
        int instrumentId, stepNote, mixerTrack, arpMode, arpRate, arpOctaves;
        bool arpOn;
        float arpGate;

        struct PatternRows
        {
            uint8_t velocity[maxSteps], probability[maxSteps], ratchet[maxSteps];
            uint32_t notes[maxNotes];
            int noteCount;
        };
        std::vector<PatternRows> rows;   // solo i pattern che contengono qualcosa (vedi patternDataLimit)
    };

    /** Oltre questo indice nessun canale (nemmeno quelli inattivi) ha dati nei pattern. */
    int patternDataLimit() const
    {
        for (int p = numPatterns - 1; p >= 0; --p)
        {
            const auto& pat = patterns[(size_t) p];
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                if (pat.noteCount[(size_t) ch].load() != 0)
                    return p + 1;
                for (int s = 0; s < maxSteps; ++s)
                    if (pat.velocity[(size_t) ch][(size_t) s].load() != 0 || pat.probability[(size_t) ch][(size_t) s].load() != 100
                        || pat.ratchet[(size_t) ch][(size_t) s].load() != 1)
                        return p + 1;
            }
        }
        return 1;
    }

    void readChannel (int c, ChannelCopy& out, int patternLimit) const
    {
        auto& ch = channels[(size_t) c];
        out.volume = ch.volume.load(); out.pan = ch.pan.load(); out.pitch = ch.pitch.load();
        out.decay = ch.decay.load(); out.tone = ch.tone.load(); out.drive = ch.drive.load();
        out.filter = ch.filter.load(); out.reverb = ch.reverb.load(); out.delay = ch.delay.load();
        out.humanize = ch.humanize.load(); out.muted = ch.muted.load(); out.solo = ch.solo.load();
        out.customName = ch.customName; out.type = ch.type.load(); out.drumType = ch.drumType.load();
        out.kit = ch.kit.load(); out.colour = ch.colour.load(); out.sample = ch.sample.load();
        out.name = ch.name; out.sampleName = ch.sampleName;
        out.instrument = ch.instrument.load(); out.instrumentKind = ch.instrumentKind;
        out.instrumentId = ch.instrumentId; out.pendingState = ch.pendingState;
        out.stepNote = ch.stepNote.load();
        out.mixerTrack = ch.mixerTrack.load();
        out.arpOn = ch.arpOn.load(); out.arpMode = ch.arpMode.load(); out.arpRate = ch.arpRate.load();
        out.arpOctaves = ch.arpOctaves.load(); out.arpGate = ch.arpGate.load();

        out.rows.resize ((size_t) juce::jlimit (1, numPatterns, patternLimit));
        for (int p = 0; p < (int) out.rows.size(); ++p)
        {
            auto& pat = patterns[(size_t) p];
            auto& r = out.rows[(size_t) p];
            for (int s = 0; s < maxSteps; ++s)
            {
                r.velocity[s]    = pat.velocity[(size_t) c][(size_t) s].load();
                r.probability[s] = pat.probability[(size_t) c][(size_t) s].load();
                r.ratchet[s]     = pat.ratchet[(size_t) c][(size_t) s].load();
            }
            for (int n = 0; n < maxNotes; ++n)
                r.notes[n] = pat.notes[(size_t) c][(size_t) n].load();
            r.noteCount = pat.noteCount[(size_t) c].load();
        }
    }

    void writeChannel (int c, const ChannelCopy& in)
    {
        auto& ch = channels[(size_t) c];
        ch.volume.store (in.volume); ch.pan.store (in.pan); ch.pitch.store (in.pitch);
        ch.decay.store (in.decay); ch.tone.store (in.tone); ch.drive.store (in.drive);
        ch.filter.store (in.filter); ch.reverb.store (in.reverb); ch.delay.store (in.delay);
        ch.humanize.store (in.humanize); ch.muted.store (in.muted); ch.solo.store (in.solo);
        ch.customName = in.customName; ch.type.store (in.type); ch.drumType.store (in.drumType);
        ch.kit.store (in.kit); ch.colour.store (in.colour); ch.sample.store (in.sample);
        ch.name = in.name; ch.sampleName = in.sampleName;
        ch.instrument.store (in.instrument); ch.instrumentKind = in.instrumentKind;
        ch.instrumentId = in.instrumentId; ch.pendingState = in.pendingState;
        ch.stepNote.store (in.stepNote);
        ch.mixerTrack.store (in.mixerTrack);
        ch.arpOn.store (in.arpOn); ch.arpMode.store (in.arpMode); ch.arpRate.store (in.arpRate);
        ch.arpOctaves.store (in.arpOctaves); ch.arpGate.store (in.arpGate);
        ch.touchSound();

        for (int p = 0; p < (int) in.rows.size(); ++p)
        {
            auto& pat = patterns[(size_t) p];
            auto& r = in.rows[(size_t) p];
            for (int s = 0; s < maxSteps; ++s)
            {
                pat.velocity[(size_t) c][(size_t) s].store (r.velocity[s]);
                pat.probability[(size_t) c][(size_t) s].store (r.probability[s]);
                pat.ratchet[(size_t) c][(size_t) s].store (r.ratchet[s]);
            }
            for (int n = 0; n < maxNotes; ++n)
                pat.notes[(size_t) c][(size_t) n].store (r.notes[n]);
            pat.noteCount[(size_t) c].store (r.noteCount);
        }
    }

    /** Nuovo ordine dei canali: order[nuovo] = vecchio indice, oppure -1 per un canale nuovo e vuoto. */
    void applyOrder (const std::vector<int>& order)
    {
        perf::wake();   // i canali cambiano posto: gli strumenti si ricalcolano
        // Copia fatta fuori dal lock (solo la UI scrive questi dati)
        const int n = getNumChannels();
        const int limit = patternDataLimit();   // con 99 pattern si copiano solo quelli che contengono qualcosa
        std::vector<ChannelCopy> old ((size_t) n);
        for (int c = 0; c < n; ++c)
            readChannel (c, old[(size_t) c], limit);

        ChannelCopy blank;
        {
            // Un canale "pulito" con i valori di default, usando l'ultimo slot libero come stampo
            const int spare = maxChannels - 1;
            ChannelCopy saved;
            readChannel (spare, saved, limit);
            resetChannel (spare);
            for (int pi = 0; pi < limit; ++pi)
            {
                auto& pat = patterns[(size_t) pi];
                for (int s = 0; s < maxSteps; ++s)
                    clearStep (pat, spare, s);
                for (auto& note : pat.notes[(size_t) spare]) note.store (0);
                pat.noteCount[(size_t) spare].store (0);
            }
            readChannel (spare, blank, limit);
            writeChannel (spare, saved);
        }

        const juce::SpinLock::ScopedLockType lock (structureLock);
        const int newCount = juce::jlimit (1, maxChannels, (int) order.size());
        std::vector<juce::AudioProcessor*> used;
        for (int c = 0; c < newCount; ++c)
        {
            writeChannel (c, order[(size_t) c] >= 0 ? old[(size_t) order[(size_t) c]] : blank);

            // Un canale clonato non condivide lo strumento: la UI gliene crea uno nuovo (stessa impostazione)
            if (auto* inst = channels[(size_t) c].instrument.load())
            {
                if (std::find (used.begin(), used.end(), inst) != used.end())
                    channels[(size_t) c].instrument.store (nullptr);
                else
                    used.push_back (inst);
            }
            instOffTicks[(size_t) c].fill (0);
            instActiveNotes[(size_t) c] = 0;
            instPreviewNote[(size_t) c] = -1;
            instMidi[(size_t) c].clear();
        }
        for (int c = newCount; c < maxChannels; ++c)
            channels[(size_t) c].instrument.store (nullptr);

        numActiveChannels.store (newCount);

        // Automazioni dei canali: seguono il canale nella nuova posizione (canale eliminato: via la curva)
        for (auto& l : lanes)
        {
            const int kind = l.kind.load();
            if (kind != automation::channelParam && kind != automation::instrumentParam)
                continue;
            int moved = -1;
            for (int c = 0; c < newCount && moved < 0; ++c)
                if (order[(size_t) c] == l.a.load())
                    moved = c;
            if (moved < 0) { l.kind.store (automation::none); l.count.store (0); }
            else           l.a.store (moved);
        }
        structureChanged.store (true);
        structureVersion.fetch_add (1);
    }

    static void resetEffectParams (mixer::Slot& s, int type)
    {
        const auto& inf = mixer::info (type);
        for (int p = 0; p < mixer::maxParams; ++p)
            s.params[(size_t) p].store (p < inf.numParams ? inf.params[(size_t) p].defaultValue : 0.0f);
    }

    void resetChannelVoices (double sr)
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            appliedKits[(size_t) c] = -1;           // forza il ricalcolo della ricetta
            appliedVersions[(size_t) c] = -1;
            appliedTypes[(size_t) c] = -1;
            appliedSamples[(size_t) c] = nullptr;
            appliedFilter[(size_t) c] = -1.0f;
            voices[(size_t) c].prepare (sr);
            samplers[(size_t) c].prepare (sr);
            samplers[(size_t) c].setSample (nullptr, {});
            channelFilters[(size_t) c].reset();
            ratchets[(size_t) c] = {};
        }
        numActive = numActiveChannels.load();
        updateVoiceParams (sr);
    }

    //==============================================================================
    void updateVoiceParams (double sr)
    {
        for (int c = 0; c < numActive; ++c)
        {
            auto& ch = channels[(size_t) c];
            const int kit = ch.kit.load();
            const int version = ch.soundVersion.load();
            const int type = ch.drumType.load();
            const auto* smp = ch.sample.load();

            if (kit != appliedKits[(size_t) c] || version != appliedVersions[(size_t) c]
                 || smp != appliedSamples[(size_t) c] || type != appliedTypes[(size_t) c])
            {
                appliedKits[(size_t) c] = kit;
                appliedVersions[(size_t) c] = version;
                appliedSamples[(size_t) c] = smp;
                appliedTypes[(size_t) c] = type;

                SoundShape shape { ch.pitch.load(), ch.decay.load(), ch.tone.load(), ch.drive.load() };
                voices[(size_t) c].setParams (shapeParams (SoundLibrary::get (type, kit), shape), shape.tone);
                voices[(size_t) c].setSample (nullptr, shape);
                samplers[(size_t) c].setSample (smp, shape);
            }

            const float f = ch.filter.load();
            if (std::abs (f - appliedFilter[(size_t) c]) > 0.0005f)
            {
                appliedFilter[(size_t) c] = f;
                // Da 120 Hz a 20 kHz, scala logaritmica
                channelFilters[(size_t) c].set (sr, 120.0 * std::pow (20000.0 / 120.0, (double) f), 0.75);
            }
        }
    }

    void renderChunk (double sr, int n)
    {
        const bool isOn = playing.load();

        // Durata di un sedicesimo in campioni: (60 / BPM) secondi per quarto, / 4.
        const double samplesPerStep = sr * 60.0 / bpm.load() / 4.0;
        // Swing: gli step pari si allungano e i dispari si accorciano (al massimo +-1/3: la "terzina").
        const double swingAmount = (double) swing.load() / 3.0;
        const float  masterGain  = masterVolume.load();   // niente spinta: il limiter deve intervenire solo sui picchi
        const bool   metroOn     = metronome.load();

        // Delay a tempo: 3/16 (il classico "dotted eighth"), rimbalzo tra sinistra e destra
        const int delaySamples = juce::jlimit (1, (int) delayBuffer[0].size() - 1, (int) (samplesPerStep * 3.0));
        const float delayFeedback = 0.38f;
        const float delayDamp = 0.35f;

        float peak = 0.0f, maxReduction = 0.0f;

        // Ingressi delle tracce del mixer: si azzerano quelle che ricevono qualcosa in questo blocco
        trackFed.fill (false);
        trackFed[0] = true;
        for (int c = 0; c < numActive; ++c)
            trackFed[(size_t) trackOf[(size_t) c]] = true;
        for (int t = 0; t < mixer::numTracks; ++t)
            if (trackFed[(size_t) t])
                clearTrack (t, n);

        // Tipo dei canali letto una volta per blocco (non a ogni campione: sui telefoni le letture atomiche costano)
        std::array<bool, maxChannels> sampleChannel {};
        for (int c = 0; c < numActive; ++c)
            sampleChannel[(size_t) c] = channels[(size_t) c].type.load (std::memory_order_relaxed) != synth;

        for (int s = 0; s < n; ++s)
        {
            currentSample = s;

            if (isOn)
            {
                // Clock a tick: 12 tick per step; all'inizio di ogni step suonano gli step,
                // a ogni tick le note del piano roll
                if (samplesUntilNextTick <= 0.0)
                {
                    if (tickInStep == 0)
                        advanceStep (samplesPerStep, swingAmount, metroOn);

                    processTick();
                    samplesUntilNextTick += currentStepLength / ticksPerStep;
                    tickInStep = (tickInStep + 1) % ticksPerStep;
                }

                samplesUntilNextTick -= 1.0;

                // Ratchet: colpi aggiuntivi dentro lo step
                for (int c = 0; c < numActive; ++c)
                {
                    auto& r = ratchets[(size_t) c];
                    if (r.remaining > 0 && --r.countdown <= 0)
                    {
                        triggerChannel (c, r.velocity, humanizeAmt[(size_t) c] * 0.5f, -1, juce::jmax (1, ticksPerStep / maxRatchet));
                        r.velocity *= 0.85f;
                        r.countdown = r.interval;
                        --r.remaining;
                    }
                }
            }

            float l = 0.0f, r = 0.0f, rev = 0.0f, del = 0.0f;

            for (int c = 0; c < numActive; ++c)
            {
                if (! sampleChannel[(size_t) c])
                    continue;

                auto& voice = voices[(size_t) c];
                auto& smp = samplers[(size_t) c];
                const bool drumOn = voice.isActive(), smpOn = smp.isActive();
                if (! drumOn && ! smpOn)
                    continue;

                float x = (drumOn ? voice.render() : 0.0f) + (smpOn ? smp.render() : 0.0f);
                if (appliedFilter[(size_t) c] < 0.999f)
                {
                    float lp, bp, hp;
                    channelFilters[(size_t) c].process (x, lp, bp, hp);
                    x = lp;
                }

                const int t = trackOf[(size_t) c];
                tL (t)[s]   += x * gainL[(size_t) c];
                tR (t)[s]   += x * gainR[(size_t) c];
                tRev (t)[s] += x * sendRev[(size_t) c];
                tDel (t)[s] += x * sendDel[(size_t) c];
            }

            delIn[(size_t) s] = del;

            // Anteprima del browser dei suoni: diretta, senza effetti
            if (browserSampler.isActive() || browserDrum.isActive())
            {
                const float b = (browserSampler.isActive() ? browserSampler.render() : 0.0f)
                              + (browserDrum.isActive() ? browserDrum.render() : 0.0f);
                l += b * 0.8f;
                r += b * 0.8f;
            }

            mixL[(size_t) s] = l;
            mixR[(size_t) s] = r;
            revL[(size_t) s] = rev;
            revR[(size_t) s] = rev;
        }

        // Strumenti: ognuno suona il blocco con i suoi eventi MIDI (gia' alla posizione giusta nel blocco)
        if (const auto w = perf::wakeCounter().load (std::memory_order_relaxed); w != lastWake)
        {
            lastWake = w;
            instIdleSamples.fill (0);
        }
        for (int c = 0; c < numActive; ++c)
        {
            auto* proc = instruments[(size_t) c];
            auto& midi = instMidi[(size_t) c];
            if (proc == nullptr)
            {
                midi.clear();
                continue;
            }

            // Fine dell'anteprima (tocco sul nome del canale)
            if (instPreviewNote[(size_t) c] >= 0)
            {
                if (instPreviewLeft[(size_t) c] <= n)
                {
                    midi.addEvent (juce::MidiMessage::noteOff (1, instPreviewNote[(size_t) c]), juce::jmax (0, instPreviewLeft[(size_t) c] - 1));
                    instPreviewNote[(size_t) c] = -1;
                }
                else
                    instPreviewLeft[(size_t) c] -= n;
            }

            juce::AudioBuffer<float> view (instBuffer.getArrayOfWritePointers(), 2, n);
            view.clear();

            // Strumento fermo (nessuna nota in arrivo e uscita silenziosa da un po'): non si calcola.
            // Uno strumento acceso ma muto costava quasi quanto uno che suona.
            auto& idle = instIdleSamples[(size_t) c];
            if (midi.isEmpty() && idle > (int) (sr * 0.3))
                continue;

            proc->processBlock (view, midi);
            midi.clear();
            {
                const float outPeak = juce::jmax (view.getMagnitude (0, 0, n), view.getMagnitude (1, 0, n));
                idle = outPeak > 1.0e-5f ? 0 : juce::jmin (idle + n, 1 << 28);
            }

            auto& ch = channels[(size_t) c];
            const float g   = gainL[(size_t) c] == 0.0f && gainR[(size_t) c] == 0.0f ? 0.0f : ch.volume.load();
            const float pan = ch.pan.load();
            const float gl  = g * juce::jmin (1.0f, 1.0f - pan);   // bilanciamento stereo
            const float gr  = g * juce::jmin (1.0f, 1.0f + pan);
            const float sr2 = g * ch.reverb.load() * 0.5f;
            const float sd2 = g * ch.delay.load() * 0.5f;
            const float* sl = view.getReadPointer (0);
            const float* sr_ = view.getReadPointer (1);
            const int t = trackOf[(size_t) c];
            float* outL = tL (t);
            float* outR = tR (t);
            float* outRev = tRev (t);
            float* outDel = tDel (t);

            for (int s = 0; s < n; ++s)
            {
                outL[s] += sl[s] * gl;
                outR[s] += sr_[s] * gr;
                outRev[s] += (sl[s] + sr_[s]) * sr2;
                outDel[s] += (sl[s] + sr_[s]) * sd2;
            }
        }

        // Mixer: EQ ed effetti di ogni traccia, poi tutto nel Master
        processMixer (sr, n);

        // Delay ping-pong con un po' di attenuazione degli acuti nei rimbalzi
        // (calcolato solo se arriva qualcosa o se gli echi suonano ancora)
        const float delInPeak = juce::FloatVectorOperations::findMaximum (delIn.data(), n) - juce::FloatVectorOperations::findMinimum (delIn.data(), n);
        const bool delayOn = delInPeak > 1.0e-6f || delayOutPeak > 1.0e-5f;
        if (delayOn) delayOutPeak = 0.0f;
        for (int s = 0; delayOn && s < n; ++s)
        {
            const float del = delIn[(size_t) s];
            const size_t size = delayBuffer[0].size();
            const size_t readPos = (delayWrite + size - (size_t) delaySamples) % size;
            const float dl = delayBuffer[0][readPos];
            const float dr = delayBuffer[1][readPos];

            delayLp[0] += delayDamp * (dr - delayLp[0]);
            delayLp[1] += delayDamp * (dl - delayLp[1]);
            delayBuffer[0][delayWrite] = del + delayLp[0] * delayFeedback;
            delayBuffer[1][delayWrite] = delayLp[1] * delayFeedback;
            delayWrite = (delayWrite + 1) % size;

            mixL[(size_t) s] += dl;
            mixR[(size_t) s] += dr;
            delayOutPeak = juce::jmax (delayOutPeak, std::abs (dl), std::abs (dr));
        }

        const float revInPeak = juce::FloatVectorOperations::findMaximum (revL.data(), n) - juce::FloatVectorOperations::findMinimum (revL.data(), n);
        if (revInPeak > 1.0e-6f || reverbOutPeak > 1.0e-5f)
        {
            reverb.processStereo (revL.data(), revR.data(), n);
            reverbOutPeak = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                mixL[(size_t) s] += revL[(size_t) s] * 0.6f;
                mixR[(size_t) s] += revR[(size_t) s] * 0.6f;
                reverbOutPeak = juce::jmax (reverbOutPeak, std::abs (revL[(size_t) s]), std::abs (revR[(size_t) s]));
            }
        }

        // Effetti della traccia Master
        runTrackChain (0, mixL.data(), mixR.data(), sr, n);

        juce::FloatVectorOperations::multiply (mixL.data(), masterGain, n);
        juce::FloatVectorOperations::multiply (mixR.data(), masterGain, n);

        // Mastering (se acceso): l'ultimo passaggio, prima del limiter di sicurezza
        if (masteringParams.enabled.load())
            masteringChain.process (mixL.data(), mixR.data(), n, mastering::Snapshot::from (masteringParams));

        for (int s = 0; s < n; ++s)
        {
            float l = mixL[(size_t) s];
            float r = mixR[(size_t) s];

            // Metronomo: va dritto in uscita, fuori dagli effetti e dal limiter
            float click = 0.0f;
            if (metroEnv > 0.0005f)
            {
                click = (float) std::sin (metroPhase) * metroEnv * 0.35f;
                metroPhase += juce::MathConstants<double>::twoPi * metroFreq / sr;
                metroEnv *= metroDecay;
            }

            limit (l, r, maxReduction);

            l = juce::jlimit (-1.0f, 1.0f, l + click);
            r = juce::jlimit (-1.0f, 1.0f, r + click);
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            mixL[(size_t) s] = l;
            mixR[(size_t) s] = r;
            scope[(size_t) (scopePos++ & (uint32_t) (scopeSize - 1))] = 0.5f * (l + r);
        }
        scopeWrite.store (scopePos, std::memory_order_release);

        if (peak > outputPeak.load())
            outputPeak.store (peak);
        if (peak > tracks[0].peakL.load())
        {
            tracks[0].peakL.store (peak);
            tracks[0].peakR.store (peak);
        }

        limiterReduction.store (maxReduction);
    }

    /** Tick del pattern che sta suonando, contando anche i giri (per le note tenute a cavallo della fine). */
    juce::int64 recordPosition() const noexcept
    {
        const int len = juce::jmax (1, patterns[(size_t) playingPattern.load()].length.load() * ticksPerStep);
        // Il prossimo tick, meno il tempo che manca per arrivarci
        const int next = tickInStep == 0 ? (juce::jmax (0, stepIndex) + 1) * ticksPerStep : juce::jmax (0, stepIndex) * ticksPerStep + tickInStep;
        const double tickLength = juce::jmax (1.0, currentStepLength / ticksPerStep);
        const auto pos = (juce::int64) std::llround ((double) next - samplesUntilNextTick / tickLength);
        return (juce::int64) patternLoops * len + juce::jmax ((juce::int64) 0, pos);
    }

    void recordLiveEvent (const LiveEvent& ev) noexcept
    {
        auto& start = recStart[(size_t) ev.channel][(size_t) ev.note];
        const bool active = recording.load() && playing.load() && ! inSong && stepIndex >= 0;

        if (ev.velocity > 0.0f)
        {
            if (! active)
                return;
            start = recordPosition();
            recVelocity[(size_t) ev.channel][(size_t) ev.note] = ev.velocity;
            recPattern[(size_t) ev.channel][(size_t) ev.note] = playingPattern.load();
            return;
        }

        if (start >= 0)
        {
            pushRecorded (ev.channel, ev.note, playing.load() ? recordPosition() : start + ticksPerStep);
            start = -1;
        }
    }

    void pushRecorded (int channel, int note, juce::int64 endPosition) noexcept
    {
        const int pattern = recPattern[(size_t) channel][(size_t) note];
        const int len = juce::jmax (1, patterns[(size_t) pattern].length.load() * ticksPerStep);
        const auto start = recStart[(size_t) channel][(size_t) note];
        RecordedNote r;
        r.channel = channel;
        r.pattern = pattern;
        r.start = (int) (start % len);
        r.length = (int) juce::jlimit ((juce::int64) ticksPerStep / 2, (juce::int64) len, endPosition - start);
        r.pitch = note;
        r.velocity = juce::jlimit (1, 127, juce::roundToInt (recVelocity[(size_t) channel][(size_t) note] * 127.0f));

        int s1, n1, s2, n2;
        recFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 > 0)
        {
            recEvents[(size_t) (n1 > 0 ? s1 : s2)] = r;
            recFifo.finishedWrite (1);
        }
    }

    /** Allo stop: le note ancora tenute finiscono li'. */
    void flushRecording() noexcept
    {
        for (int c = 0; c < maxChannels; ++c)
            for (int n = 0; n < 128; ++n)
                if (recStart[(size_t) c][(size_t) n] >= 0)
                {
                    pushRecorded (c, n, recordPosition());
                    recStart[(size_t) c][(size_t) n] = -1;
                }
    }

    void applyAutomation (double tick) noexcept
    {
        using namespace automation;
        for (auto& l : lanes)
        {
            const int kind = l.kind.load();
            if (kind == none || ! l.enabled.load())
                continue;
            const float v = l.valueAt (tick);
            if (v < 0.0f)
                continue;
            const int a = l.a.load(), b = l.b.load();

            if (kind == master)
                masterVolume.store (volumeGain (v, 3.5f));
            else if (kind == channelParam && a >= 0 && a < numActive)
            {
                auto& ch = channels[(size_t) a];
                switch (b)
                {
                    case chVolume: ch.volume.store (volumeGain (v, 0.0f)); break;
                    case chPan:    ch.pan.store (-1.0f + 2.0f * v); break;
                    case chFilter: ch.filter.store (v); break;
                    case chReverb: ch.reverb.store (v); break;
                    case chDelay:  ch.delay.store (v); break;
                    case chPitch:
                    {
                        const float semis = std::round ((-12.0f + 24.0f * v) * 10.0f) / 10.0f;
                        if (std::abs (semis - ch.pitch.load()) > 0.05f) { ch.pitch.store (semis); ch.touchSound(); }
                        break;
                    }
                    default: break;
                }
            }
            else if (kind == trackParam && a >= 0 && a < mixer::numTracks)
            {
                auto& tr = tracks[(size_t) a];
                switch (b)
                {
                    case trVolume: tr.volume.store (volumeGain (v, 4.0f)); break;
                    case trPan:    tr.pan.store (-1.0f + 2.0f * v); break;
                    case trEqLow:  tr.eqLow.store (-15.0f + 30.0f * v); break;
                    case trEqMid:  tr.eqMid.store (-15.0f + 30.0f * v); break;
                    case trEqHigh: tr.eqHigh.store (-15.0f + 30.0f * v); break;
                    default: break;
                }
            }
            else if (kind == effectParam && a >= 0 && a < mixer::numTracks && b >= 0 && b < mixer::numSlots)
            {
                auto& slot = tracks[(size_t) a].slots[(size_t) b];
                const auto& inf = mixer::info (slot.type.load());
                const int p = l.c.load();
                if (p >= 0 && p < inf.numParams)
                    slot.params[(size_t) p].store (mixer::fromNormalised (inf.params[(size_t) p], v));
            }
            else if (kind == instrumentParam && a >= 0 && a < numActive)
            {
                if (auto* proc = instruments[(size_t) a])
                {
                    const auto& params = proc->getParameters();
                    if (b >= 0 && b < params.size())
                        if (auto* prm = params[b]; std::abs (prm->getValue() - v) > 0.0005f)
                            prm->setValueNotifyingHost (v);
                }
            }
        }
    }

    float* trackBuffer (int t, int which) noexcept    { return trackBuffers.data() + ((size_t) t * 4 + (size_t) which) * (size_t) chunk; }
    float* tL (int t) noexcept      { return trackBuffer (t, 0); }
    float* tR (int t) noexcept      { return trackBuffer (t, 1); }
    float* tRev (int t) noexcept    { return trackBuffer (t, 2); }
    float* tDel (int t) noexcept    { return trackBuffer (t, 3); }

    void clearTrack (int t, int n) noexcept
    {
        for (int w = 0; w < 4; ++w)
            juce::FloatVectorOperations::clear (trackBuffer (t, w), n);
    }

    bool trackHasEffects (int t) const noexcept
    {
        for (auto& s : tracks[(size_t) t].slots)
            if (s.effect != nullptr && ! s.bypass.load())
                return true;
        return false;
    }

    /** EQ ed effetti di una traccia, sul posto. */
    void runTrackChain (int t, float* L, float* R, double sr, int n) noexcept
    {
        auto& tr = tracks[(size_t) t];

        // Niente in ingresso e le code degli effetti (riverbero, delay) finite: silenzio, non serve calcolare
        bool hasEffects = false;
        for (auto& s : tr.slots)
            hasEffects = hasEffects || (s.effect != nullptr && ! s.bypass.load (std::memory_order_relaxed));
        const float inPeak = juce::jmax (juce::FloatVectorOperations::findMaximum (L, n) - juce::FloatVectorOperations::findMinimum (L, n),
                                         juce::FloatVectorOperations::findMaximum (R, n) - juce::FloatVectorOperations::findMinimum (R, n));
        auto& outPeak = trackOutPeak[(size_t) t];
        if (inPeak <= 1.0e-6f && (outPeak <= 1.0e-5f || ! hasEffects))
        {
            outPeak = 0.0f;
            return;
        }

        auto& eq = trackEq[(size_t) t];
        eq.update (sr, tr.eqLow.load(), tr.eqMid.load(), tr.eqMidFreq.load(), tr.eqHigh.load());
        eq.process (L, R, n);

        mixer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = bpm.load();
        for (auto& s : tr.slots)
        {
            if (s.effect == nullptr || s.bypass.load())
                continue;
            float params[mixer::maxParams];
            for (int p = 0; p < mixer::maxParams; ++p)
                params[p] = s.params[(size_t) p].load();

            ctx.keyL = ctx.keyR = nullptr;
            if (s.type.load() == mixer::sidechain)
            {
                const int src = juce::jlimit (1, mixer::numInserts, juce::roundToInt (params[0]));
                if (src != t && keyReady[(size_t) src])
                {
                    ctx.keyL = keyBuffers.data() + (size_t) src * 2 * (size_t) chunk;
                    ctx.keyR = ctx.keyL + chunk;
                }
            }
            s.effect->process (L, R, n, params, ctx);
        }

        outPeak = juce::jmax (juce::FloatVectorOperations::findMaximum (L, n) - juce::FloatVectorOperations::findMinimum (L, n),
                              juce::FloatVectorOperations::findMaximum (R, n) - juce::FloatVectorOperations::findMinimum (R, n));
    }

    void processMixer (double sr, int n) noexcept
    {
        // Sidechain: si copia l'ingresso (prima degli effetti) delle tracce sorgente
        keyReady.fill (false);
        for (int t = 0; t < mixer::numTracks; ++t)
            for (auto& s : tracks[(size_t) t].slots)
                if (s.effect != nullptr && ! s.bypass.load() && s.type.load() == mixer::sidechain)
                {
                    const int src = juce::jlimit (1, mixer::numInserts, juce::roundToInt (s.params[0].load()));
                    if (keyReady[(size_t) src])
                        continue;
                    float* k = keyBuffers.data() + (size_t) src * 2 * (size_t) chunk;
                    if (trackFed[(size_t) src])
                    {
                        juce::FloatVectorOperations::copy (k, tL (src), n);
                        juce::FloatVectorOperations::copy (k + chunk, tR (src), n);
                    }
                    else
                    {
                        juce::FloatVectorOperations::clear (k, n);
                        juce::FloatVectorOperations::clear (k + chunk, n);
                    }
                    keyReady[(size_t) src] = true;
                }

        float* mL = tL (0);
        float* mR = tR (0);
        float* mRev = tRev (0);
        float* mDel = tDel (0);

        for (int t = 1; t < mixer::numTracks; ++t)
        {
            const bool fed = trackFed[(size_t) t];
            if (! fed && ! trackHasEffects (t))
                continue;              // traccia vuota: niente da fare
            if (! fed)
                clearTrack (t, n);     // solo le code degli effetti (riverbero, delay...)

            float* L = tL (t);
            float* R = tR (t);
            runTrackChain (t, L, R, sr, n);

            auto& tr = tracks[(size_t) t];
            const bool silent = tr.muted.load() || (anyTrackSolo && ! tr.solo.load());
            const float g = silent ? 0.0f : tr.volume.load();
            const float pan = tr.pan.load();
            const float gl = g * juce::jmin (1.0f, 1.0f - pan);
            const float gr = g * juce::jmin (1.0f, 1.0f + pan);
            float pl = 0.0f, pr = 0.0f;
            const float* rv = tRev (t);
            const float* dl = tDel (t);
            for (int s = 0; s < n; ++s)
            {
                const float l = L[s] * gl, r = R[s] * gr;
                pl = juce::jmax (pl, std::abs (l));
                pr = juce::jmax (pr, std::abs (r));
                mL[s] += l;
                mR[s] += r;
                mRev[s] += rv[s] * g;
                mDel[s] += dl[s] * g;
            }
            if (pl > tr.peakL.load()) tr.peakL.store (pl);
            if (pr > tr.peakR.load()) tr.peakR.store (pr);
        }

        // Il Master: canali collegati direttamente + tutte le tracce
        for (int s = 0; s < n; ++s)
        {
            mixL[(size_t) s] += mL[s];
            mixR[(size_t) s] += mR[s];
            revL[(size_t) s] += mRev[s];
            revR[(size_t) s] = revL[(size_t) s];
            delIn[(size_t) s] += mDel[s];
        }
    }

    /** Limiter con 1.5 ms di "anticipo": vede arrivare i picchi e abbassa il volume prima che passino. */
    void limit (float& l, float& r, float& maxReduction) noexcept
    {
        constexpr float threshold = 0.89f;   // circa -1 dB

        const float inPeak = juce::jmax (std::abs (l), std::abs (r));
        const float target = inPeak > threshold ? threshold / inPeak : 1.0f;

        if (target < holdTarget || limiterHold <= 0)
        {
            holdTarget = target;
            limiterHold = limiterHoldLength;   // il volume resta giu' 25 ms: non "insegue" ogni onda del basso (distorsione)
        }
        --limiterHold;

        const float coefToUse = holdTarget < limiterGain ? limiterAttack : limiterRelease;
        limiterGain = holdTarget + coefToUse * (limiterGain - holdTarget);

        // Uscita ritardata di 'lookahead' campioni
        const int readPos = (limiterPos + (int) limiterDelay[0].size() - lookahead) % (int) limiterDelay[0].size();
        limiterDelay[0][(size_t) limiterPos] = l;
        limiterDelay[1][(size_t) limiterPos] = r;
        l = limiterDelay[0][(size_t) readPos] * limiterGain;
        r = limiterDelay[1][(size_t) readPos] * limiterGain;
        limiterPos = (limiterPos + 1) % (int) limiterDelay[0].size();

        maxReduction = juce::jmax (maxReduction, 1.0f - limiterGain);
    }

    void advanceStep (double samplesPerStep, double swingAmount, bool metroOn)
    {
        if (inSong)
        {
            advanceSongStep (samplesPerStep, swingAmount, metroOn);
            return;
        }

        auto& playPat = patterns[(size_t) playingPattern.load()];
        const int length = playPat.length.load();
        stepIndex = (stepIndex + 1) % length;
        if (stepIndex == 0)
            ++patternLoops;   // per le note registrate tenute oltre la fine del giro

        // Fine del giro: se nel frattempo e' stato scelto un altro pattern, si passa a quello
        // (o subito, se richiesto dalla modalita' live: il ritmo continua dallo stesso punto)
        if (switchNow.exchange (false) || (stepIndex == 0 && playingPattern.load() != currentPattern.load()))
            playingPattern.store (currentPattern.load());

        auto& pattern = patterns[(size_t) playingPattern.load()];
        if (stepIndex >= pattern.length.load())
            stepIndex = 0;

        currentStep.store (stepIndex);

        const bool even = (stepIndex % 2) == 0;
        const double stepLength = samplesPerStep * (even ? 1.0 + swingAmount : 1.0 - swingAmount);

        playPatternStep (pattern, stepIndex, stepLength);
        metronomeAt (metroOn, stepIndex);
        currentStepLength = stepLength;
    }

    /** Canzone: avanza di uno step e suona tutte le clip della playlist che coprono questo step. */
    void advanceSongStep (double samplesPerStep, double swingAmount, bool metroOn)
    {
        ++songStep;
        if (songStep >= songLength)
            songStep = 0;                      // fine della canzone: ricomincia (giro)

        currentSongStep.store (songStep);

        const bool even = (songStep % 2) == 0;
        const double stepLength = samplesPerStep * (even ? 1.0 + swingAmount : 1.0 - swingAmount);

        numActiveClips = 0;
        int shownStep = -1;
        const int edited = currentPattern.load();

        if (songLength > 0)
        {
            for (auto& slot : clips)
            {
                const auto v = slot.load();
                if (v == 0)
                    continue;

                const auto clip = unpackClip (v);
                auto& pattern = patterns[(size_t) clip.pattern];
                const int rel = songStep - clip.startStep();
                const int clipLength = clip.length > 0 ? clip.length : pattern.length.load();
                if (rel < 0 || rel >= clipLength)
                    continue;
                // Step del pattern: dal punto di partenza (bordo sinistro), e se la clip e' piu' lunga si ricomincia
                const int patLen = juce::jmax (1, pattern.length.load());
                const int local = (clip.offset + rel) % patLen;

                // la stessa clip doppia (stesso pattern e battuta su due tracce) suona una volta sola
                bool duplicate = false;
                for (int i = 0; i < numActiveClips; ++i)
                    duplicate = duplicate || (activeClips[(size_t) i].pattern == clip.pattern && activeClips[(size_t) i].localStep == local);
                if (duplicate || numActiveClips >= (int) activeClips.size())
                    continue;

                activeClips[(size_t) numActiveClips++] = { clip.pattern, local, clipLength - rel };
                playPatternStep (pattern, local, stepLength);

                if (clip.pattern == edited)
                    shownStep = local;
            }
        }

        // Il channel rack mostra il playhead se il pattern che si sta modificando sta suonando
        stepIndex = shownStep;
        currentStep.store (shownStep);
        playingPattern.store (edited);
        metronomeAt (metroOn, songStep % stepsPerBar);
        currentStepLength = stepLength;
    }

    void metronomeAt (bool metroOn, int step)
    {
        if (metroOn && step % 4 == 0)
        {
            metroFreq = step == 0 ? 1760.0 : 1320.0;   // primo quarto piu' acuto
            metroEnv = 1.0f;
            metroPhase = 0.0;
            metroDecay = (float) std::exp (-1.0 / (0.012 * sampleRate.load()));
        }
    }

    /** Suona gli step di un pattern (i canali che usano il piano roll vengono saltati). */
    void playPatternStep (Pattern& pattern, int step, double stepLength)
    {
        const int stepIndex = step;   // (nasconde il membro: qui conta lo step del pattern)

        for (int c = 0; c < numActive; ++c)
        {
            if (pattern.noteCount[(size_t) c].load() > 0)   // questo canale usa il piano roll
                continue;

            const int vel = pattern.velocity[(size_t) c][(size_t) stepIndex].load();
            if (vel <= 0)
                continue;

            const int prob = pattern.probability[(size_t) c][(size_t) stepIndex].load();
            if (prob < 100 && (int) (rng.nextFloat() * 100.0f) >= prob)
                continue;

            const float v = (float) vel / 127.0f;
            const int count = juce::jlimit (1, maxRatchet, (int) pattern.ratchet[(size_t) c][(size_t) stepIndex].load());
            triggerChannel (c, v, humanizeAmt[(size_t) c], -1, ticksPerStep / count);

            auto& r = ratchets[(size_t) c];
            r.remaining = count - 1;
            r.interval = juce::jmax (1, (int) (stepLength / count));
            r.countdown = r.interval;
            r.velocity = v * 0.85f;
        }
    }

    /** Ogni tick: fa suonare le note del piano roll che iniziano adesso. */
    void processTick()
    {
        const int tick = stepIndex * ticksPerStep + tickInStep;
        currentTick.store (stepIndex >= 0 ? tick : -1);
        playHead.ppq.store ((double) ((inSong ? songStep : stepIndex) * ticksPerStep + tickInStep) / (4.0 * ticksPerStep));

        // Note degli strumenti che finiscono a questo tick (prima delle nuove: una nota ripetuta riparte pulita)
        for (int c = 0; c < numActive; ++c)
        {
            if (instActiveNotes[(size_t) c] <= 0)
                continue;
            auto& offs = instOffTicks[(size_t) c];
            for (int note = 0; note < 128; ++note)
                if (offs[(size_t) note] > 0 && --offs[(size_t) note] == 0)
                {
                    instMidi[(size_t) c].addEvent (juce::MidiMessage::noteOff (1, note), currentSample);
                    --instActiveNotes[(size_t) c];
                }
        }

        for (int c = 0; c < numActive; ++c)
            samplers[(size_t) c].tick();

        if (inSong)
        {
            for (int i = 0; i < numActiveClips; ++i)
                playPatternNotes (patterns[(size_t) activeClips[(size_t) i].pattern],
                                  activeClips[(size_t) i].localStep * ticksPerStep + tickInStep,
                                  activeClips[(size_t) i].stepsLeft * ticksPerStep - tickInStep);   // le note si fermano a fine clip
        }
        else
        {
            playPatternNotes (patterns[(size_t) playingPattern.load()], tick);
        }

        runArpeggiators();
        ++arpClock;
    }

    /** Arpeggiatori: a ogni passo suona la nota successiva tra quelle tenute. */
    void runArpeggiators() noexcept
    {
        for (int c = 0; c < numActive; ++c)
        {
            auto& ch = channels[(size_t) c];
            if (! ch.arpOn.load() || instruments[(size_t) c] == nullptr)
                continue;

            auto& held = arpHeld[(size_t) c];
            int notes[128 * 3];
            int count = 0;
            float velocity = 0.0f;
            const int octaves = juce::jlimit (1, 3, ch.arpOctaves.load());
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n] > 0)
                    velocity = juce::jmax (velocity, arpVelocity[(size_t) c][(size_t) n]);
            for (int o = 0; o < octaves; ++o)
                for (int n = 0; n < 128; ++n)
                    if (held[(size_t) n] > 0 && n + o * 12 <= 127)
                        notes[count++] = n + o * 12;

            const int rate = juce::jlimit (3, 96, ch.arpRate.load());
            if (count > 0 && arpClock % rate == 0)
            {
                const int mode = ch.arpMode.load();
                const int len = juce::jmax (1, (int) std::round ((float) rate * juce::jlimit (0.05f, 1.0f, ch.arpGate.load())));
                auto& pos = arpPosition[(size_t) c];
                if (mode == 4)
                {
                    for (int i = 0; i < count; ++i)   // accordo ripetuto
                        instNoteOnRaw (c, notes[i], velocity, len);
                }
                else
                {
                    int index = 0;
                    if (mode == 0)      index = pos % count;
                    else if (mode == 1) index = count - 1 - pos % count;
                    else if (mode == 2) { const int cycle = juce::jmax (1, 2 * count - 2); const int p = pos % cycle; index = p < count ? p : cycle - p; }
                    else                index = (int) (rng.nextFloat() * (float) count) % count;
                    instNoteOnRaw (c, notes[index], velocity, len);
                }
                ++pos;
            }

            // Le note del piano roll e degli step finiscono quando finisce la loro durata
            bool any = false;
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n] > 0)
                {
                    if (held[(size_t) n] < arpLiveHold)
                        --held[(size_t) n];
                    any = any || held[(size_t) n] > 0;
                }
            if (! any)
                arpPosition[(size_t) c] = 0;   // nuovo accordo: ricomincia dalla prima nota
        }
    }

    /** Fa partire le note del piano roll di un pattern che iniziano a questo tick. */
    void playPatternNotes (Pattern& pattern, int tick, int maxLength = 1 << 24)
    {
        for (int c = 0; c < numActive; ++c)
        {
            if (pattern.noteCount[(size_t) c].load() <= 0)
                continue;

            for (auto& slot : pattern.notes[(size_t) c])
            {
                const auto v = slot.load();
                if (v == 0 || (int) (v >> 23) != tick)
                    continue;

                const auto n = unpackNote (v);
                const int length = juce::jmax (1, juce::jmin (n.length, maxLength));
                if (instruments[(size_t) c] != nullptr)
                    instNoteOn (c, n.pitch, (float) n.velocity / 127.0f, length);   // lo strumento riceve l'altezza vera
                else
                    triggerChannel (c, (float) n.velocity / 127.0f, humanizeAmt[(size_t) c], n.pitch, length, true);
            }
        }
    }

    void instNoteOn (int c, int note, float velocity, int lengthTicks)
    {
        if (note < 0 || note > 127)
            return;

        // Arpeggiatore acceso (e il tempo scorre): la nota viene "tenuta", l'arpeggiatore la suonera' a tempo
        if (channels[(size_t) c].arpOn.load() && playing.load())
        {
            arpHeld[(size_t) c][(size_t) note] = juce::jmax (1, lengthTicks);
            arpVelocity[(size_t) c][(size_t) note] = velocity;
            channels[(size_t) c].triggerCount.fetch_add (1);
            return;
        }
        instNoteOnRaw (c, note, velocity, lengthTicks);
    }

    void instNoteOnRaw (int c, int note, float velocity, int lengthTicks)
    {
        auto& midi = instMidi[(size_t) c];
        auto& offs = instOffTicks[(size_t) c];
        if (offs[(size_t) note] > 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), currentSample);
        else
            ++instActiveNotes[(size_t) c];

        midi.addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.01f, 1.0f, velocity)), currentSample);
        offs[(size_t) note] = juce::jmax (1, lengthTicks);
        channels[(size_t) c].activity.store (velocity);
        channels[(size_t) c].triggerCount.fetch_add (1);
    }

    void instPreview (int c, int note)
    {
        auto& midi = instMidi[(size_t) c];
        if (instPreviewNote[(size_t) c] >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, instPreviewNote[(size_t) c]), 0);

        instPreviewNote[(size_t) c] = juce::jlimit (0, 127, note);
        instPreviewLeft[(size_t) c] = (int) (0.45 * sampleRate.load());
        midi.addEvent (juce::MidiMessage::noteOn (1, instPreviewNote[(size_t) c], 0.85f), 0);
        channels[(size_t) c].activity.store (0.85f);
    }

    void instAllNotesOff (int c)
    {
        arpHeld[(size_t) c].fill (0);
        arpPosition[(size_t) c] = 0;
        auto& midi = instMidi[(size_t) c];
        auto& offs = instOffTicks[(size_t) c];
        for (int note = 0; note < 128; ++note)
            if (offs[(size_t) note] > 0)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);

        offs.fill (0);
        instActiveNotes[(size_t) c] = 0;
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    void prepareInstrument (juce::AudioProcessor& p)
    {
        if (const double sr = preparedRate.load(); sr > 0.0)
            prepareInstrumentAt (p, sr);
        else
            p.setPlayHead (&playHead);
    }

    void prepareInstrumentAt (juce::AudioProcessor& p, double sr)
    {
        p.setPlayHead (&playHead);
        p.setPlayConfigDetails (0, 2, sr, chunk);
        p.prepareToPlay (sr, chunk);
    }

    /**
        Suona un canale.
        @param midiNote     nota del piano roll (-1 = lo step: suono alla sua altezza naturale)
        @param lengthTicks  durata della nota (per synth e campioni "tenuti")
        @param fromNote     true per le note del piano roll e le anteprime con altezza
    */
    void triggerChannel (int c, float velocity, float humanize, int midiNote, int lengthTicks, bool fromNote = false)
    {
        if (instruments[(size_t) c] != nullptr)
        {
            instNoteOn (c, midiNote < 0 ? channels[(size_t) c].stepNote.load() : midiNote, velocity, lengthTicks);
            return;
        }
        if (channels[(size_t) c].type.load() == synth)
            return;   // canale strumento senza strumento (ancora da creare)

        auto& ch = channels[(size_t) c];
        auto& smp = samplers[(size_t) c];

        if (const auto* sample = smp.getSample())
        {
            // Campione: le note sono relative alla nota a cui e' registrato. Gli step suonano il campione
            // intero; le note del piano roll degli strumenti "tenuti" sfumano alla loro fine.
            const float semis = midiNote < 0 ? 0.0f : (float) (midiNote - sample->rootNote);
            const int gate = (fromNote && midiNote >= 0 && sample->gated && lengthTicks > 0) ? lengthTicks : -1;
            smp.trigger (velocity, semis, gate, humanize, rng.nextFloat() * 2.0f - 1.0f, rng.nextFloat() * 2.0f - 1.0f, ! fromNote);
        }
        else
        {
            // L'hi-hat chiuso "strozza" quello aperto, come su una vera drum machine.
            if (ch.drumType.load() == closedHatType)
                for (int o = 0; o < numActive; ++o)
                    if (o != c && channels[(size_t) o].drumType.load() == openHatType && channels[(size_t) o].type.load() == drumOrSample)
                        voices[(size_t) o].choke();

            voices[(size_t) c].trigger (velocity, humanize, midiNote < 0 ? 0.0f : (float) (midiNote - rootNote));
        }

        ch.activity.store (velocity);
        ch.triggerCount.fetch_add (1);
    }

    //==============================================================================
    /** Il "playhead" che il synth legge per BPM e posizione (LFO e Performer a tempo). */
    struct EnginePlayHead : public juce::AudioPlayHead
    {
        std::atomic<double> bpm { 120.0 }, ppq { 0.0 };
        std::atomic<bool>   playing { false };

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm.load());
            info.setPpqPosition (ppq.load());
            info.setIsPlaying (playing.load());
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return info;
        }
    };

    // Stato condiviso UI <-> audio
    std::array<Channel, maxChannels> channels;
    std::unique_ptr<Pattern[]> patterns { new Pattern[(size_t) numPatterns] };   // sullo heap: sono ~3.5 MB
    std::array<juce::String, numPatterns> patternNames;                        // solo UI
    std::atomic<int>    numActiveChannels { defaultChannels };
    std::atomic<int>    structureVersion { 0 };
    std::atomic<bool>   structureChanged { true };
    juce::SpinLock      structureLock;            // tenuto dalla UI solo mentre riordina i canali
    std::atomic<int>    currentPattern   { 0 };   // quello che si vede e si modifica
    std::atomic<int>    playingPattern   { 0 };   // quello che sta suonando
    std::atomic<bool>   playing          { false };
    std::atomic<bool>   restartRequested { true };
    std::atomic<bool>   metronome        { false };
    std::atomic<double> bpm              { 120.0 };
    std::atomic<float>  swing            { 0.0f };
    std::atomic<float>  masterVolume     { 1.0f };
    std::atomic<int>    currentStep      { -1 };
    std::atomic<int>    currentTick      { -1 };
    std::array<std::atomic<uint64_t>, maxClips> clips {};
    std::atomic<bool>   songMode         { false };
    std::atomic<int>    songStartBar     { 0 };
    std::atomic<int>    currentSongStep  { -1 };
    std::atomic<float>  outputPeak       { 0.0f };
    static constexpr int scopeSize = 2048;
    std::array<float, scopeSize> scope {};
    uint32_t scopePos = 0;
    std::atomic<uint32_t> scopeWrite { 0 };
    std::atomic<float>  limiterReduction { 0.0f };
    std::atomic<double> sampleRate       { 48000.0 };

    // Anteprima del browser dei suoni
    std::atomic<const SampleData*> browserSample { nullptr };
    std::atomic<int> browserPreview { 0 }, browserDrumType { 0 }, browserKit { 0 };

    // Stato usato SOLO dal thread audio
    struct RatchetState { int remaining = 0, interval = 1, countdown = 0; float velocity = 0.0f; };

    std::array<DrumVoice, maxChannels>    voices;
    std::array<SamplerPool, maxChannels>  samplers;
    std::array<SVFilter, maxChannels>     channelFilters;
    std::array<RatchetState, maxChannels> ratchets {};
    std::array<int, maxChannels>   appliedKits {}, appliedVersions {}, appliedTypes {};
    std::array<const SampleData*, maxChannels> appliedSamples {};
    std::array<float, maxChannels> appliedFilter {}, gainL {}, gainR {}, sendRev {}, sendDel {}, humanizeAmt {};
    SamplerPool browserSampler;
    DrumVoice   browserDrum;

    std::atomic<bool> panicRequested { false };
    std::atomic<bool> switchNow { false };
    std::atomic<int> perfBlocks { 0 }, perfOverruns { 0 }, perfLockMisses { 0 }, perfBlockSize { 0 };
    std::atomic<float> perfLoadSum { 0.0f }, perfLoadMax { 0.0f };
    bool lockMissedThisBlock = false;
    std::atomic<int> scaleRoot { 9 }, scaleType { 0 };

    // Automazioni
    std::array<automation::Lane, automation::maxLanes> lanes;

    // Arpeggiatori (thread audio)
    static constexpr int arpLiveHold = 1 << 30;
    std::array<std::array<int, 128>, maxChannels> arpHeld {};
    std::array<std::array<float, 128>, maxChannels> arpVelocity {};
    std::array<int, maxChannels> arpPosition {};
    juce::int64 arpClock = 0;

    // Registrazione dal vivo
    std::atomic<bool> recording { false };
    juce::SpinLock liveWriteLock;
    juce::int64 patternLoops = 0;
    std::array<std::array<juce::int64, 128>, maxChannels> recStart = [] { std::array<std::array<juce::int64, 128>, maxChannels> a {}; for (auto& r : a) r.fill (-1); return a; }();
    std::array<std::array<float, 128>, maxChannels> recVelocity {};
    std::array<std::array<int, 128>, maxChannels> recPattern {};
    juce::AbstractFifo recFifo { 512 };
    std::array<RecordedNote, 512> recEvents {};

    // Mastering
    mastering::Params masteringParams;
    mastering::Chain masteringChain;

    // Mixer
    std::array<mixer::Track, mixer::numTracks> tracks;                 // impostazioni (UI) + effetti
    std::array<mixer::TrackEq, mixer::numTracks> trackEq;              // stato del thread audio
    std::array<int, maxChannels> trackOf {};
    std::array<bool, mixer::numTracks> trackFed {}, keyReady {};
    bool anyTrackSolo = false;
    std::vector<float> trackBuffers = std::vector<float> ((size_t) mixer::numTracks * 4 * (size_t) chunk, 0.0f);
    std::vector<float> keyBuffers   = std::vector<float> ((size_t) mixer::numTracks * 2 * (size_t) chunk, 0.0f);
    juce::Random rng;
    int    numActive            = defaultChannels;
    int    stepIndex            = -1;
    int    songStep             = -1, songLength = 0;
    bool   inSong               = false;
    struct ActiveClip { int pattern = 0, localStep = 0, stepsLeft = 1 << 20; };
    std::array<ActiveClip, 64> activeClips {};
    int    numActiveClips       = 0;
    int    tickInStep           = 0;
    int    currentSample        = 0;      // posizione nel blocco corrente (per gli eventi MIDI)
    bool   wasPlaying           = false;

    // Strumenti (un AudioProcessor per canale strumento)
    EnginePlayHead playHead;
    std::atomic<double> preparedRate { 0.0 };
    juce::AudioBuffer<float> instBuffer;
    std::array<juce::AudioProcessor*, maxChannels> instruments {};
    std::array<juce::MidiBuffer, maxChannels> instMidi;
    std::array<std::array<int, 128>, maxChannels> instOffTicks {};   // tick mancanti alla fine di ogni nota (0 = spenta)
    std::array<int, maxChannels> instActiveNotes {}, instPreviewNote {}, instPreviewLeft {};

    // Note dal vivo (tastiera a schermo): coda senza lock UI -> audio
    juce::AbstractFifo liveFifo { 256 };
    std::array<LiveEvent, 256> liveEvents {};
    double samplesUntilNextTick = 0.0;
    double currentStepLength    = 1.0;

    std::array<float, chunk> mixL {}, mixR {}, revL {}, revR {}, delIn {};
    juce::Reverb reverb;
    float reverbOutPeak = 0.0f, delayOutPeak = 0.0f;               // per saltare riverbero e delay quando tacciono
    std::array<int, maxChannels> instIdleSamples {};
    unsigned lastWake = 0;               // da quanto ogni strumento e' in silenzio
    std::array<float, mixer::numTracks> trackOutPeak {};           // uscita degli effetti di ogni traccia (code)
    std::vector<float> delayBuffer[2];
    size_t delayWrite = 0;
    float  delayLp[2] { 0.0f, 0.0f };

    std::array<std::array<float, 256>, 2> limiterDelay {};
    int   lookahead = 72, limiterPos = 0, limiterHold = 0, limiterHoldLength = 1200;
    float limiterGain = 1.0f, holdTarget = 1.0f, limiterAttack = 0.9f, limiterRelease = 0.999f;

    double metroPhase = 0.0, metroFreq = 1320.0;
    float  metroEnv = 0.0f, metroDecay = 0.99f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerEngine)
};
